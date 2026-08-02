/* voice_task — owns audio_io, voice_pipe and the selected transport
 * (docs/01: the only task touching I2S and the ESP-NOW radio).
 *
 * Implements docs/04's PTT state machine:
 *
 *   IDLE --PTT down, !busy--------------> TX
 *   IDLE --PTT down, busy--> ARMED_WAIT --busy clears--> TX
 *   ARMED_WAIT --PTT up--> IDLE
 *   TX --PTT up--> TX_DRAIN --END sent--> IDLE
 *   TX --CL_VOICE_TX_MAX_MS--> TX_DRAIN            (stuck-button guard)
 *   RX driven by transport.recv
 *
 * Half-duplex: while transmitting we do not play RX frames.
 */
#include "app_queues.h"
#include "app_state.h"
#include "app_tasks.h"

#include "audio_io.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unit_cfg.h"
#include "voice_pipe.h"
#include "voice_transport.h"

#include <string.h>

static const char *TAG = "voice_task";

#define VOICE_RETRY_MS 5000
#define PUMP_MS 5 /* well under the 20 ms frame cadence */

typedef enum {
    PTT_IDLE = 0,
    PTT_ARMED_WAIT,
    PTT_TX,
    PTT_TX_DRAIN,
} ptt_state_t;

static const voice_transport_t *s_tp;
static vp_tx_t s_tx;
static vp_rx_t s_rx;
static ptt_state_t s_ptt;
static uint32_t s_tx_started_ms;
static uint32_t s_tx_seconds;
static uint32_t s_frame_invalid; /* corrupt/foreign RX frames (T20) */
static bool s_ptt_held;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void publish(voice_status_t status, int8_t talker)
{
    state_lock();
    convoy_state_t *st = state_get();
    st->voice_status = status;
    st->voice_talker_uid = talker;
    state_unlock();
}

static void emit_cb(const uint8_t *frame, size_t len, void *ctx)
{
    (void)ctx;
    if (s_tp != NULL && s_tp->send != NULL) {
        (void)s_tp->send(frame, len);
    }
}

/* ---- transitions --------------------------------------------------------- */

static void enter_tx(void)
{
    aio_set_mode(AIO_OFF); /* stop any playback: half duplex */
    if (aio_set_mode(AIO_CAPTURE) != ESP_OK) {
        ESP_LOGE(TAG, "cannot enter capture");
        s_ptt = PTT_IDLE;
        publish(VOICE_IDLE, -1);
        return;
    }
    vp_tx_begin(&s_tx);
    s_ptt = PTT_TX;
    s_tx_started_ms = now_ms();
    publish(VOICE_TX, -1);
    ESP_LOGD(TAG, "TX start");
}

static void enter_drain(void)
{
    /* Flush the partial tail and the END frame, then release the mic. */
    vp_tx_end(&s_tx, emit_cb, NULL);
    aio_set_mode(AIO_OFF);
    s_tx_seconds += (uint32_t)(int32_t)(now_ms() - s_tx_started_ms) / 1000u;
    s_ptt = PTT_IDLE;
    publish(VOICE_IDLE, -1);
    ESP_LOGD(TAG, "TX end");
}

static void handle_ctrl(void)
{
    ctrl_event_t ev;
    while (ctrl_q_recv(&ev, 0)) {
        switch (ev.type) {
        case BTN_PTT_DOWN:
            s_ptt_held = true;
            if (s_ptt != PTT_IDLE) {
                break;
            }
            if (s_tp != NULL && s_tp->busy != NULL && s_tp->busy()) {
                s_ptt = PTT_ARMED_WAIT; /* someone else is talking */
                publish(VOICE_BUSY, vp_rx_talker(&s_rx));
                ESP_LOGD(TAG, "ARMED_WAIT (channel busy)");
            } else {
                enter_tx();
            }
            break;

        case BTN_PTT_UP:
            s_ptt_held = false;
            if (s_ptt == PTT_TX) {
                enter_drain();
            } else if (s_ptt == PTT_ARMED_WAIT) {
                s_ptt = PTT_IDLE;
                publish(VOICE_IDLE, -1);
            }
            break;

        default:
            /* AUX belongs to ui_task; see the ctrl_q fan-out note below. */
            break;
        }
    }
}

/* ---- pumps --------------------------------------------------------------- */

static void pump_tx(void)
{
    /* Stuck-PTT guard (docs/04): cap a single transmission. */
    if ((uint32_t)(int32_t)(now_ms() - s_tx_started_ms) >= CL_VOICE_TX_MAX_MS) {
        ESP_LOGW(TAG, "TX cap reached, draining");
        enter_drain();
        return;
    }

    int16_t pcm[CL_VOICE_FRAME_SAMPLES];
    int n = aio_read(pcm, CL_VOICE_FRAME_SAMPLES, 20);
    if (n > 0) {
        vp_tx_feed(&s_tx, pcm, (size_t)n, emit_cb, NULL);
    }
}

static void pump_rx(bool transmitting)
{
    /* Always drain the transport, even while transmitting: dropping
     * frames on the floor is better than letting them pile up and play
     * out late once we release PTT. */
    uint8_t frame[VF_FRAME_MAX];
    int len;
    while ((len = s_tp->recv(frame, sizeof frame, 0)) > 0) {
        /* Counted separately from vp_rx_offer's other false-return causes
         * (talker lock, duplicate, too-late) - this specifically means a
         * corrupt or foreign frame, the voice-side analogue of radio_stats'
         * `invalid` counter (T20 §Defensive sweeps, per-source counting). */
        if (vf_validate(frame, (size_t)len) != VF_OK) {
            s_frame_invalid++;
        }
        if (!transmitting) {
            (void)vp_rx_offer(&s_rx, frame, (size_t)len, now_ms());
        }
    }
    if (transmitting) {
        return; /* half duplex */
    }

    int16_t pcm[CL_VOICE_FRAME_SAMPLES];
    int r = vp_rx_pull(&s_rx, pcm, now_ms());
    if (r == 1) {
        if (aio_mode() != AIO_PLAYBACK) {
            aio_set_mode(AIO_PLAYBACK);
            publish(VOICE_RX, vp_rx_talker(&s_rx));
        }
        (void)aio_write(pcm, CL_VOICE_FRAME_SAMPLES, 40);
    } else if (r == -1 && aio_mode() == AIO_PLAYBACK) {
        aio_set_mode(AIO_OFF); /* burst ended: amp mutes */
        publish(VOICE_IDLE, -1);
    }
}

/* ---- task ---------------------------------------------------------------- */

void voice_task(void *arg)
{
    (void)arg;
    convoy_state_t boot;
    state_snapshot(&boot);

    vp_tx_init(&s_tx, boot.cfg.unit_id);
    vp_rx_init(&s_rx);

    s_tp = voice_transport_get(unit_cfg_voice_name(boot.cfg.voice));
    if (s_tp == NULL) {
        ESP_LOGE(TAG, "VOICE? transport '%s' unavailable in this build",
                 unit_cfg_voice_name(boot.cfg.voice));
    }

    esp_task_wdt_add(NULL); /* 10 s window from sdkconfig.defaults (T20) */
    uint32_t last_beat = 0, last_retry = 0;
    bool up = false;

    for (;;) {
        esp_task_wdt_reset();

        convoy_state_t st;
        state_snapshot(&st);

        /* Bring the transport up, retrying every 5 s. A voice fault must
         * never disturb the radar (docs/01). */
        if (!up) {
            uint32_t t = now_ms();
            if (s_tp != NULL &&
                (last_retry == 0 ||
                 (uint32_t)(int32_t)(t - last_retry) >= VOICE_RETRY_MS)) {
                last_retry = t;
                if (s_tp->init() == ESP_OK) {
                    up = true;
                    state_lock();
                    state_get()->voice_ok = true;
                    state_unlock();
                    ESP_LOGI(TAG, "voice transport '%s' up", s_tp->name);
                }
            }
            if (!up) {
                state_lock();
                state_get()->voice_ok = false;
                state_unlock();
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
        }

        handle_ctrl();

        /* ARMED_WAIT: take the channel the moment it frees up, provided
         * the button is still held. */
        if (s_ptt == PTT_ARMED_WAIT && s_ptt_held && !s_tp->busy()) {
            enter_tx();
        }

        if (s_ptt == PTT_TX) {
            pump_tx();
        }
        pump_rx(s_ptt == PTT_TX);

        uint32_t now = now_ms();
        if ((uint32_t)(int32_t)(now - last_beat) >= TASK_HEARTBEAT_MS) {
            ESP_LOGD(TAG, "heartbeat ptt=%d talker=%d transport=%s",
                     (int)s_ptt, (int)vp_rx_talker(&s_rx),
                     s_tp ? s_tp->name : "none");
            last_beat = now;
        }
        vTaskDelay(pdMS_TO_TICKS(PUMP_MS));
    }
}

/* ---- console diagnostic --------------------------------------------------- */

void voice_task_stats(const char **transport, int *state, uint32_t *tx_seconds,
                      uint32_t *frame_invalid)
{
    if (transport != NULL) {
        *transport = (s_tp != NULL) ? s_tp->name : "none";
    }
    if (state != NULL) {
        *state = (int)s_ptt;
    }
    if (tx_seconds != NULL) {
        *tx_seconds = s_tx_seconds;
    }
    if (frame_invalid != NULL) {
        *frame_invalid = s_frame_invalid;
    }
}
