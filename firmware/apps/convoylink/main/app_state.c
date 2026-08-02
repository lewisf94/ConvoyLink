/* app_state.c — see app_state.h */
#include "app_state.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static convoy_state_t s_state;
static SemaphoreHandle_t s_lock;

esp_err_t state_init(const unit_cfg_t *cfg, bool provisioned)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    memset(&s_state, 0, sizeof s_state);
    s_state.cfg = *cfg;
    s_state.provisioned = provisioned;
    s_state.own_fix_age_ms = UINT32_MAX;
    s_state.gps_idle_ms = UINT32_MAX;
    s_state.voice_status = VOICE_IDLE;
    s_state.voice_talker_uid = -1;
    s_state.zoom_mode = RR_ZOOM_AUTO;
    nt_init(&s_state.neighbors, cfg->unit_id);
    return ESP_OK;
}

void state_lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

void state_unlock(void)
{
    xSemaphoreGive(s_lock);
}

convoy_state_t *state_get(void)
{
    return &s_state;
}

void state_snapshot(convoy_state_t *out)
{
    state_lock();
    *out = s_state;
    state_unlock();
}
