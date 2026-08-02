/**
 * app_state — the one piece of shared mutable state in the firmware
 * (docs/01 §Shared state & queues: "no ad-hoc globals").
 *
 * Readers take a snapshot rather than holding the lock while they work,
 * so `ui_task` never blocks on a writer mid-render.
 */
#ifndef APP_STATE_H
#define APP_STATE_H

#include "esp_err.h"
#include "neighbor_table.h"
#include "nmea.h"
#include "radar_scene.h"
#include "unit_cfg.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    VOICE_IDLE = 0,
    VOICE_TX,
    VOICE_RX,
    VOICE_BUSY,
} voice_status_t;

typedef struct {
    /* identity (read-only after boot) */
    unit_cfg_t cfg;
    bool provisioned;

    /* own position */
    nmea_fix_t own_fix;
    uint32_t own_fix_age_ms; /* UINT32_MAX = never fixed */
    uint32_t gps_idle_ms;    /* since any byte at all; UINT32_MAX = never */

    /* convoy */
    nt_t neighbors;

    /* voice */
    voice_status_t voice_status;
    int8_t voice_talker_uid; /* -1 = nobody                            */

    /* peripheral health -> RADIO?/VOICE? tiles (docs/01) */
    bool radio_ok;
    bool voice_ok;

    /* UI */
    rr_zoom_t zoom_mode;
} convoy_state_t;

/** Creates the mutex and zeroes the state. Call once, before any task. */
esp_err_t state_init(const unit_cfg_t *cfg, bool provisioned);

/** Lock/unlock around any mutation. Keep the critical section short. */
void state_lock(void);
void state_unlock(void);

/**
 * Direct access to the state — only valid between state_lock() and
 * state_unlock().
 */
convoy_state_t *state_get(void);

/** Copy the whole state under the lock; the caller then works lock-free. */
void state_snapshot(convoy_state_t *out);

#endif /* APP_STATE_H */
