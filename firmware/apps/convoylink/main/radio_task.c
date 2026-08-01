/* radio_task — owns the SX1262 (docs/01: the ONLY task that touches it).
 *
 * T15 stub: drains tx_q so producers never wedge, and drains the driver's
 * RX queue so it cannot silently back up. Validation, the neighbour table
 * and the relay scheduler are T16. */
#include "app_queues.h"
#include "app_state.h"
#include "app_tasks.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sx1262.h"

static const char *TAG = "radio_task";

#define RADIO_RETRY_MS 5000 /* docs/01 §Error-handling */

void radio_task(void *arg)
{
    (void)arg;
    uint32_t last_beat = 0, last_retry = 0;
    uint32_t rx_seen = 0, tx_sent = 0;

    for (;;) {
        convoy_state_t st;
        state_snapshot(&st);

        /* Retry a failed init every 5 s rather than giving up: the radio
         * may simply have been slow to power up (docs/01). */
        if (!st.radio_ok) {
            uint32_t t = (uint32_t)(esp_timer_get_time() / 1000);
            if (last_retry == 0 ||
                (uint32_t)(int32_t)(t - last_retry) >= RADIO_RETRY_MS) {
                last_retry = t;
                uint32_t freq = unit_cfg_region_freq_hz(st.cfg.region);
                if (sx1262_init(freq) == ESP_OK) {
                    ESP_LOGI(TAG, "radio came up on retry");
                    state_lock();
                    state_get()->radio_ok = true;
                    state_unlock();
                    st.radio_ok = true;
                }
            }
        }

        /* TX: never transmit while unprovisioned or radio-down (docs/07). */
        tx_item_t item;
        while (tx_q_recv(&item, 0)) {
            if (!st.provisioned || !st.radio_ok) {
                continue; /* discard: the queue must not build up */
            }
            if (sx1262_send(item.payload) == ESP_OK) {
                tx_sent++;
            }
        }

        /* RX: T16 will validate and feed the neighbour table. */
        uint8_t buf[CL_PKT_SIZE];
        int16_t rssi;
        int8_t snr;
        while (st.radio_ok && sx1262_receive(buf, &rssi, &snr, 0)) {
            rx_seen++;
        }

        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if ((uint32_t)(int32_t)(now - last_beat) >= TASK_HEARTBEAT_MS) {
            ESP_LOGD(TAG, "heartbeat rx=%lu tx=%lu radio_ok=%d",
                     (unsigned long)rx_seen, (unsigned long)tx_sent,
                     (int)st.radio_ok);
            last_beat = now;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
