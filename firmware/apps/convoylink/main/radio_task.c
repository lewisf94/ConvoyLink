/* radio_task — owns the SX1262 (docs/01: the ONLY task that touches it)
 * and implements docs/03 §Channel access + §Beacon relay exactly.
 *
 * The relay scheduler is the interesting part. A received hop-0 beacon is
 * not rebroadcast immediately: it is scheduled for now + uniform(150..450
 * ms), and cancelled if we hear anyone else's hop-1 copy of the same
 * (uid, seq) first. That suppression is what keeps the duty cycle near
 * 2 % with five cars — usually exactly one car relays each beacon.
 *
 * Pending relays live in a fixed array; nothing here allocates.
 */
#include "app_queues.h"
#include "app_state.h"
#include "app_tasks.h"
#include "radio_stats.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sx1262.h"
#include "unit_cfg.h"

#include <string.h>

static const char *TAG = "radio_task";

#define RADIO_RETRY_MS 5000 /* docs/01 §Error-handling */

/* docs/03 §Channel access rule 2: defer up to 200 ms in 20 ms steps. */
#define LBT_STEP_MS 20
#define LBT_MAX_MS 200

/* One slot per possible (sender) — a unit can only have one beacon in
 * flight at a time, since a newer seq supersedes an older one. */
#define RELAY_SLOTS CL_MAX_UNITS

typedef struct {
    bool pending;
    uint8_t uid;
    uint16_t seq;
    uint32_t due_ms;
    cl_beacon_t beacon;
} relay_slot_t;

static relay_slot_t s_relays[RELAY_SLOTS];

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* uniform(CL_RELAY_DELAY_MIN_MS .. CL_RELAY_DELAY_MAX_MS) */
static uint32_t relay_delay(void)
{
    uint32_t span = CL_RELAY_DELAY_MAX_MS - CL_RELAY_DELAY_MIN_MS + 1;
    return CL_RELAY_DELAY_MIN_MS + (esp_random() % span);
}

/* Cancels any pending relay of (uid, seq) — someone else got there
 * first, or a newer beacon superseded it (docs/03 step 3). */
static void relay_suppress(uint8_t uid, uint16_t seq)
{
    if (uid >= RELAY_SLOTS) {
        return;
    }
    relay_slot_t *s = &s_relays[uid];
    if (s->pending && s->seq == seq) {
        s->pending = false;
        radio_stats_inc_suppressed();
    }
}

static void relay_schedule(const cl_beacon_t *b)
{
    uint8_t uid = b->hdr.sender;
    if (uid >= RELAY_SLOTS) {
        return;
    }
    relay_slot_t *s = &s_relays[uid];

    /* A newer beacon from the same unit makes an older pending relay
     * pointless — replace rather than drop the fresh one. */
    if (s->pending && !cl_seq_newer(b->seq, s->seq)) {
        return;
    }
    s->pending = true;
    s->uid = uid;
    s->seq = b->seq;
    s->beacon = *b;
    s->due_ms = now_ms() + relay_delay();
}

/* Fires any relay whose delay has elapsed and that nobody suppressed. */
static void relay_service(void)
{
    uint32_t now = now_ms();
    for (int i = 0; i < RELAY_SLOTS; i++) {
        relay_slot_t *s = &s_relays[i];
        if (!s->pending || (int32_t)(now - s->due_ms) < 0) {
            continue;
        }
        s->pending = false;

        cl_beacon_t relay = s->beacon;
        cl_beacon_to_relay(&relay);

        tx_item_t item;
        memcpy(item.payload, &relay, sizeof relay);
        tx_q_send(&item);
        radio_stats_inc_relayed();
    }
}

/* docs/03 rule 2: cheap listen-before-talk. Advisory — after the window
 * we transmit anyway rather than starving the beacon. */
static void listen_before_talk(void)
{
    for (int waited = 0; waited < LBT_MAX_MS; waited += LBT_STEP_MS) {
        if (!sx1262_channel_active()) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(LBT_STEP_MS));
    }
    radio_stats_inc_lbt_forced();
}

static void handle_rx(const uint8_t *buf, int16_t rssi, int8_t snr,
                      uint8_t self_uid)
{
    int type = cl_validate(buf, CL_PKT_SIZE);
    if (type < 0) {
        radio_stats_inc_invalid();
        ESP_LOGD(TAG, "invalid packet (%d) rssi=%d", type, rssi);
        return;
    }

    if (type == CL_TYPE_PING) {
        radio_stats_inc_ping(); /* bring-up interop only */
        ESP_LOGD(TAG, "ping rssi=%d snr=%d", rssi, snr);
        return;
    }
    if (type != CL_TYPE_BEACON) {
        return;
    }

    const cl_beacon_t *b = (const cl_beacon_t *)buf;
    if (b->hdr.sender == self_uid) {
        return; /* our own relayed copy coming back */
    }
    radio_stats_inc_beacon_rx();
    ESP_LOGD(TAG, "beacon U%u seq=%u hop=%u rssi=%d snr=%d", b->hdr.sender,
             b->seq, b->hdr.meta, rssi, snr);

    bool is_relay_copy = (b->hdr.meta != 0);

    state_lock();
    nt_update_from_beacon(&state_get()->neighbors, b, now_ms());
    state_unlock();

    if (is_relay_copy) {
        /* docs/03 step 3 + 4: hearing someone else's hop-1 copy suppresses
         * our own pending relay, and hop-1 is never relayed again. */
        relay_suppress(b->hdr.sender, b->seq);
        return;
    }

    /* docs/03 step 2: relay only if we haven't already relayed this seq.
     * nt_should_relay marks it, so it is true at most once per (uid, seq). */
    bool should;
    state_lock();
    should = nt_should_relay(&state_get()->neighbors, b);
    state_unlock();

    if (should) {
        relay_schedule(b);
    }
}

void radio_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL); /* 10 s window from sdkconfig.defaults (T20) */
    uint32_t last_beat = 0, last_retry = 0;

    for (;;) {
        esp_task_wdt_reset();

        convoy_state_t st;
        state_snapshot(&st);

        /* Retry a failed init every 5 s: the radio may simply have been
         * slow to power up (docs/01). */
        if (!st.radio_ok) {
            uint32_t t = now_ms();
            if (last_retry == 0 ||
                (uint32_t)(int32_t)(t - last_retry) >= RADIO_RETRY_MS) {
                last_retry = t;
                if (sx1262_init(unit_cfg_region_freq_hz(st.cfg.region)) ==
                    ESP_OK) {
                    ESP_LOGI(TAG, "radio came up on retry");
                    state_lock();
                    state_get()->radio_ok = true;
                    state_unlock();
                    st.radio_ok = true;
                }
            }
        }

        /* RX first: a fresh hop-1 copy may suppress a relay we are about
         * to send, so draining RX before servicing relays is what makes
         * suppression actually work. */
        if (st.radio_ok) {
            uint8_t buf[CL_PKT_SIZE];
            int16_t rssi;
            int8_t snr;
            while (sx1262_receive(buf, &rssi, &snr, 0)) {
                handle_rx(buf, rssi, snr, st.cfg.unit_id);
            }
        }

        relay_service();

        /* TX: never transmit while unprovisioned or radio-down. */
        tx_item_t item;
        while (tx_q_recv(&item, 0)) {
            if (!st.provisioned || !st.radio_ok) {
                continue; /* discard so the queue cannot build up */
            }
            listen_before_talk();
            if (sx1262_send(item.payload) == ESP_OK) {
                radio_stats_inc_tx();
            } else {
                radio_stats_inc_tx_fail();
            }
        }

        uint32_t now = now_ms();
        if ((uint32_t)(int32_t)(now - last_beat) >= TASK_HEARTBEAT_MS) {
            ESP_LOGD(TAG, "heartbeat radio_ok=%d", (int)st.radio_ok);
            last_beat = now;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
