/* ui_task — snapshot -> rr_scene_t -> radar_render -> LCD strips
 * (docs/01: 200 ms tick, never blocks on anything but its tick).
 *
 * This is the one stub that is already the real thing: the scene is built
 * from live state and flushed through the same rr_screen_draw the
 * simulator renders, so what T16/T17 add is data, not drawing.
 *
 * Button events arrive on ui_q, which carries only the AUX events meant
 * for this task. docs/01 draws one ctrl_q feeding both voice_task and
 * ui_task, but a FreeRTOS queue delivers each item to exactly one reader,
 * so ctrl_task routes instead: PTT to ctrl_q, AUX here (T19).
 */
#include "app_queues.h"
#include "app_state.h"
#include "app_tasks.h"

#include "convoy_geo.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ili9341_disp.h"
#include "radar_scene.h"

#include <string.h>

static const char *TAG = "ui_task";

#define UI_TICK_MS 200 /* 5 Hz, docs/01 */
#define STRIP_H 20     /* docs/06 rendering contract */
#define STRIPS (RR_H / STRIP_H)

/* docs/06: auto-zoom changes at most every 2 s. rr_pick_zoom already
 * encodes the 90 %-of-scale rule, so this only rate-limits it. */
#define ZOOM_MIN_INTERVAL_MS 2000

/* docs/05 §Own course: below 1.5 m/s hold the last valid course for the
 * compass arrow, then hide it after 60 s stationary. */
#define COURSE_HOLD_MS 60000

/* AUX hold cycles these (docs/06). */
static const uint8_t BACKLIGHT_STEPS[] = {100, 60, 25};

/* One strip buffer: disp_flush is synchronous (T14's contract), so a
 * second buffer would never be filled while the first is in flight. */
static uint16_t s_strip[RR_W * STRIP_H];

/* Applied auto-zoom scale and when it last changed (hysteresis state). */
static uint16_t s_auto_scale;
static uint32_t s_auto_changed_ms;

/* Last course we considered valid, for the hold-then-hide rule. */
static uint16_t s_held_course = CL_COURSE_INVALID;
static uint32_t s_held_course_ms;

static uint8_t s_backlight_idx;

/*
 * docs/05: the arrow follows live course above 1.5 m/s; below that it
 * holds the last valid one for 60 s and then disappears, so a car waiting
 * at lights keeps a sensible heading instead of spinning.
 */
static uint16_t resolve_own_course(const nmea_fix_t *fix, uint32_t now)
{
    if (fix->valid && fix->speed_dm_s >= CL_COURSE_VALID_DM_S &&
        fix->course_cdeg != CL_COURSE_INVALID) {
        s_held_course = fix->course_cdeg;
        s_held_course_ms = now;
        return s_held_course;
    }
    if (s_held_course != CL_COURSE_INVALID &&
        (uint32_t)(int32_t)(now - s_held_course_ms) < COURSE_HOLD_MS) {
        return s_held_course;
    }
    s_held_course = CL_COURSE_INVALID;
    return CL_COURSE_INVALID;
}

/*
 * Auto-zoom with docs/06 hysteresis: adopt rr_pick_zoom's answer only if
 * it differs and at least 2 s has passed since the last change. Note this
 * is exactly the two rules the doc gives (90 % crossing, 2 s apart) and
 * nothing more — a car parked on a boundary can still oscillate at 0.5 Hz.
 * Adding a deadband would mean inventing a threshold; that belongs to
 * T20's polish pass if the field shows it matters.
 */
static uint16_t resolve_auto_zoom(float max_dist_m, uint32_t now)
{
    uint16_t ideal = rr_pick_zoom(max_dist_m);
    if (s_auto_scale == 0) {
        s_auto_scale = ideal;
        s_auto_changed_ms = now;
    } else if (ideal != s_auto_scale &&
               (uint32_t)(int32_t)(now - s_auto_changed_ms) >=
                   ZOOM_MIN_INTERVAL_MS) {
        s_auto_scale = ideal;
        s_auto_changed_ms = now;
    }
    return s_auto_scale;
}

static void build_scene(const convoy_state_t *st, uint32_t now,
                        rr_scene_t *sc)
{
    memset(sc, 0, sizeof *sc);

    sc->provisioned = st->provisioned;
    sc->self_uid = st->cfg.unit_id;
    sc->self_initials[0] = st->cfg.initials[0];
    sc->self_initials[1] = st->cfg.initials[1];

    sc->own_fix = st->own_fix.valid;
    sc->own_lat_e7 = st->own_fix.lat_e7;
    sc->own_lon_e7 = st->own_fix.lon_e7;
    sc->own_course_cdeg = resolve_own_course(&st->own_fix, now);
    sc->own_sats = st->own_fix.sats;

    sc->now_ms = now;
    sc->n_neighbors = nt_snapshot(&st->neighbors, sc->neighbors, now);

    sc->ptt_tx = (st->voice_status == VOICE_TX);
    sc->ptt_busy = (st->voice_status == VOICE_BUSY);
    sc->rx_talker_uid =
        (st->voice_status == VOICE_RX) ? st->voice_talker_uid : -1;

    sc->zoom_mode = st->zoom_mode;
    if (st->zoom_mode == RR_ZOOM_AUTO) {
        float max_dist = 0.0f;
        if (sc->own_fix) {
            for (int i = 0; i < sc->n_neighbors; i++) {
                nt_tier_t tier = nt_tier(&sc->neighbors[i], now);
                if ((tier == NT_LIVE || tier == NT_STALE) &&
                    sc->neighbors[i].last.fix_quality != 0) {
                    float d = geo_dist_m(sc->own_lat_e7, sc->own_lon_e7,
                                         sc->neighbors[i].last.lat_e7,
                                         sc->neighbors[i].last.lon_e7);
                    if (d > max_dist) {
                        max_dist = d;
                    }
                }
            }
        }
        sc->zoom_scale_m = resolve_auto_zoom(max_dist, now);
    } else {
        static const uint16_t fixed[] = {0, 250, 500, 1000, 2000, 4000};
        sc->zoom_scale_m = fixed[st->zoom_mode];
        /* Re-entering auto should re-evaluate immediately, not inherit a
         * stale applied scale from before the manual excursion. */
        s_auto_scale = 0;
    }
}

static void drain_ctrl_events(void)
{
    ctrl_event_t ev;
    while (ui_q_recv(&ev, 0)) {
        switch (ev.type) {
        case BTN_AUX_PRESS: {
            /* auto -> 250 -> 500 -> 1k -> 2k -> 4k -> auto (docs/06) */
            state_lock();
            convoy_state_t *st = state_get();
            st->zoom_mode = (rr_zoom_t)((st->zoom_mode + 1) % 6);
            rr_zoom_t z = st->zoom_mode;
            state_unlock();
            ESP_LOGD(TAG, "zoom mode -> %d", (int)z);
            break;
        }
        case BTN_AUX_HOLD: {
            s_backlight_idx = (uint8_t)((s_backlight_idx + 1) %
                                        (sizeof BACKLIGHT_STEPS /
                                         sizeof BACKLIGHT_STEPS[0]));
            uint8_t pct = BACKLIGHT_STEPS[s_backlight_idx];
            (void)disp_backlight_pct(pct);
            ESP_LOGD(TAG, "backlight -> %u%%", pct);
            break;
        }
        default: /* PTT is routed to voice_task, never here */
            break;
        }
    }
}

void ui_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t last_beat = 0;
    uint32_t frames = 0;

    for (;;) {
        drain_ctrl_events();

        convoy_state_t st;
        state_snapshot(&st);

        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        rr_scene_t sc;
        build_scene(&st, now, &sc);

        for (int i = 0; i < STRIPS; i++) {
            rr_fb_t fb = {s_strip, i * STRIP_H, STRIP_H};
            rr_screen_draw(&fb, &sc);
            (void)disp_flush(fb.y0, fb.h, s_strip);
        }
        frames++;

        if ((uint32_t)(int32_t)(now - last_beat) >= TASK_HEARTBEAT_MS) {
            ESP_LOGD(TAG, "heartbeat frames=%lu neighbors=%d zoom=%um",
                     (unsigned long)frames, sc.n_neighbors,
                     (unsigned)sc.zoom_scale_m);
            last_beat = now;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(UI_TICK_MS));
    }
}
