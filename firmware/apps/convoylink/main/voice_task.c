/* voice_task — owns audio_io and the voice transport (docs/01).
 *
 * T15 stub: idles only. It deliberately does NOT consume ctrl_q — PTT is
 * wired up in T18/T19, and T15's scope says voice_task ignores control
 * events until then. Leaving them for ctrl_task's own logging keeps the
 * button path observable without pretending voice works. */
#include "app_state.h"
#include "app_tasks.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "voice_task";

void voice_task(void *arg)
{
    (void)arg;
    for (;;) {
        convoy_state_t st;
        state_snapshot(&st);
        ESP_LOGD(TAG, "heartbeat status=%d voice_ok=%d transport=%s",
                 (int)st.voice_status, (int)st.voice_ok,
                 unit_cfg_voice_name(st.cfg.voice));
        vTaskDelay(pdMS_TO_TICKS(TASK_HEARTBEAT_MS));
    }
}
