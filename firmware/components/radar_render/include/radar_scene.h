/**
 * radar_scene — composes the complete ConvoyLink screen (status bar,
 * radar, neighbour strip, banners) from a plain scene description into
 * any rr_fb_t strip (docs/06-ui.md). This function IS the product's UI:
 * firmware and the desktop simulator both call it.
 *
 * Pure C: no ESP-IDF headers, no allocation, host-testable.
 *
 * ASCII-font substitutions (radar_render's font is ASCII 32..126 only,
 * docs/06 uses two non-ASCII glyphs): the satellite-count marker "<n>◦"
 * is rendered "<n>o"; the "waiting for convoy…" ellipsis is rendered as
 * three literal periods. Documented once here, not repeated per call site.
 */
#ifndef RADAR_SCENE_H
#define RADAR_SCENE_H

#include <stdbool.h>
#include <stdint.h>

#include "neighbor_table.h"
#include "radar_render.h"

typedef enum {
    RR_ZOOM_AUTO = 0,
    RR_ZOOM_250,
    RR_ZOOM_500,
    RR_ZOOM_1K,
    RR_ZOOM_2K,
    RR_ZOOM_4K,
} rr_zoom_t;

typedef struct {
    /* own unit */
    bool provisioned; /* false -> PROVISION ME banner                  */
    uint8_t self_uid;
    char self_initials[2];
    bool own_fix; /* false -> NO FIX state                             */
    int32_t own_lat_e7, own_lon_e7;
    uint16_t own_course_cdeg; /* CL_COURSE_INVALID -> circle marker     */
    uint8_t own_sats;
    /* convoy */
    nt_entry_t neighbors[CL_MAX_UNITS];
    int n_neighbors; /* from nt_snapshot                                */
    uint32_t now_ms;
    /* radio/UI state */
    bool ptt_tx, ptt_busy; /* TX tile / BUSY banner                     */
    int8_t rx_talker_uid;  /* -1 none; else digital voice sender uid    */
    rr_zoom_t zoom_mode;
    uint16_t zoom_scale_m; /* resolved scale actually in use            */
} rr_scene_t;

/** Render the full screen state into one strip. Pure function of scene. */
void rr_screen_draw(rr_fb_t *fb, const rr_scene_t *sc);

/**
 * Auto-zoom helper (pure, unit-testable): smallest scale from
 * {250,500,1000,2000,4000} keeping max_dist_m inside 0.9 * scale.
 * Hysteresis/timing is the caller's job (T17).
 */
uint16_t rr_pick_zoom(float max_live_dist_m);

/**
 * Metres offset (dx east, dy north) -> radar pixels, round-half-away-
 * from-zero: x = 120 + round(dx*108/scale), y = 148 - round(dy*108/scale).
 */
void rr_map_to_px(float dx_m, float dy_m, uint16_t scale_m, int *x, int *y);

#endif /* RADAR_SCENE_H */
