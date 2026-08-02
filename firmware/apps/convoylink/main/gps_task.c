/* gps_task — publishes fixes into shared state and queues the own-beacon
 * every CL_BEACON_PERIOD_MS (docs/01, docs/05 §Beacon cadence).
 *
 * The +/-200 ms jitter matters: without it five units powered up together
 * would phase-lock and collide on every beacon slot forever.
 *
 * A unit with no fix still beacons, with fix_quality 0, so the convoy sees
 * it as "online, no GPS" rather than absent (docs/03).
 */
#include "app_queues.h"
#include "app_state.h"
#include "app_tasks.h"
#include "radio_stats.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gps_uart.h"

#include <string.h>

static const char *TAG = "gps_task";

#define GPS_POLL_MS 200

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* CL_BEACON_PERIOD_MS +/- CL_BEACON_JITTER_MS */
static uint32_t beacon_interval(void)
{
    uint32_t span = 2u * CL_BEACON_JITTER_MS + 1u;
    return CL_BEACON_PERIOD_MS - CL_BEACON_JITTER_MS + (esp_random() % span);
}

static void queue_beacon(const convoy_state_t *st, const nmea_fix_t *fix,
                         uint32_t fix_age_ms, uint16_t seq)
{
    /* Saturate the fix age into the uint16_t the wire format carries. */
    uint16_t age = (fix_age_ms > 0xFFFFu) ? 0xFFFFu : (uint16_t)fix_age_ms;

    cl_beacon_t b;
    cl_make_beacon(&b, st->cfg.unit_id, seq, st->cfg.initials,
                   fix->valid ? fix->lat_e7 : 0,
                   fix->valid ? fix->lon_e7 : 0,
                   fix->valid ? fix->speed_dm_s : 0,
                   fix->valid ? fix->course_cdeg : CL_COURSE_INVALID,
                   fix->valid ? fix->fix_quality : 0,
                   fix->valid ? fix->sats : 0, age);

    tx_item_t item;
    memcpy(item.payload, &b, sizeof b);
    tx_q_send(&item);
    radio_stats_inc_beacon_tx();
}

void gps_task(void *arg)
{
    (void)arg;
    uint32_t last_beat = 0;
    uint32_t next_beacon = now_ms() + beacon_interval();
    uint16_t seq = 0;

    for (;;) {
        nmea_fix_t fix;
        uint32_t age_ms;
        gps_uart_get_fix(&fix, &age_ms);

        state_lock();
        convoy_state_t *stp = state_get();
        stp->own_fix = fix;
        stp->own_fix_age_ms = age_ms;
        stp->gps_idle_ms = gps_uart_idle_ms();
        state_unlock();

        convoy_state_t st;
        state_snapshot(&st);
        uint32_t now = now_ms();

        if ((int32_t)(now - next_beacon) >= 0) {
            next_beacon = now + beacon_interval();
            /* Unprovisioned units transmit nothing at all (docs/07). */
            if (st.provisioned) {
                queue_beacon(&st, &fix, age_ms, seq++);
            }
        }

        if ((uint32_t)(int32_t)(now - last_beat) >= TASK_HEARTBEAT_MS) {
            uint32_t ok, bad;
            gps_uart_stats(&ok, &bad);
            ESP_LOGD(TAG, "heartbeat fix=%d sats=%u seq=%u ok=%lu bad=%lu",
                     (int)fix.valid, fix.sats, seq, (unsigned long)ok,
                     (unsigned long)bad);
            last_beat = now;
        }
        vTaskDelay(pdMS_TO_TICKS(GPS_POLL_MS));
    }
}
