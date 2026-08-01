/* gps_task — publishes fixes into shared state (docs/01).
 *
 * T15 stub: the fix publishing is real (gps_uart already does the parsing
 * from T11), which is what lets the radar show a live NO FIX -> fix
 * transition. Queueing the 5 s own-beacon onto tx_q is T16, since the
 * beacon contents are that task's contract. */
#include "app_state.h"
#include "app_tasks.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gps_uart.h"

static const char *TAG = "gps_task";

#define GPS_POLL_MS 200

void gps_task(void *arg)
{
    (void)arg;
    uint32_t last_beat = 0;

    for (;;) {
        nmea_fix_t fix;
        uint32_t age_ms;
        gps_uart_get_fix(&fix, &age_ms);

        state_lock();
        convoy_state_t *st = state_get();
        st->own_fix = fix;
        st->own_fix_age_ms = age_ms;
        state_unlock();

        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if ((uint32_t)(int32_t)(now - last_beat) >= TASK_HEARTBEAT_MS) {
            uint32_t ok, bad;
            gps_uart_stats(&ok, &bad);
            ESP_LOGD(TAG, "heartbeat fix=%d sats=%u ok=%lu bad=%lu",
                     (int)fix.valid, fix.sats, (unsigned long)ok,
                     (unsigned long)bad);
            last_beat = now;
        }
        vTaskDelay(pdMS_TO_TICKS(GPS_POLL_MS));
    }
}
