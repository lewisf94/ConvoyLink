/* ui_task — snapshot -> rr_scene_t -> radar_render -> LCD strips
 * (docs/01: 200 ms tick, never blocks on anything but its tick).
 *
 * This is the one stub that is already the real thing: the scene is built
 * from live state and flushed through the same rr_screen_draw the
 * simulator renders, so what T16/T17 add is data, not drawing.
 *
 * ctrl_q is consumed here for now. docs/01 lists both voice_task and
 * ui_task as consumers of that one queue, which cannot work as written
 * (a single queue delivers each event to exactly one reader) — T15's
 * scope says voice_task ignores control events until T18, so ui_task
 * drains it and handles AUX. Resolving the fan-out is T18's problem and
 * is flagged in STATUS.md.
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

/* One strip buffer: disp_flush is synchronous (T14's contract), so a
 * second buffer would never be filled while the first is in flight. */
static uint16_t s_strip[RR_W * STRIP_H];

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
    sc->own_course_cdeg = st->own_fix.course_cdeg;
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
        sc->zoom_scale_m = rr_pick_zoom(max_dist);
    } else {
        static const uint16_t fixed[] = {0, 250, 500, 1000, 2000, 4000};
        sc->zoom_scale_m = fixed[st->zoom_mode];
    }
}

static void drain_ctrl_events(void)
{
    ctrl_event_t ev;
    while (ctrl_q_recv(&ev, 0)) {
        if (ev.type != BTN_AUX_PRESS) {
            continue; /* PTT belongs to voice_task from T18 */
        }
        state_lock();
        convoy_state_t *st = state_get();
        st->zoom_mode = (rr_zoom_t)((st->zoom_mode + 1) % 6);
        rr_zoom_t z = st->zoom_mode;
        state_unlock();
        ESP_LOGD(TAG, "zoom -> %d", (int)z);
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
