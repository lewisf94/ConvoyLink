# T06 — `radar_render` part 2: radar screen composition

**Depends:** T01, T04, T05 · **Phase:** M1 (pure C)
**Required reading:** `docs/06-ui.md` (all of it — the layout is binding);
`docs/05-gps-geo.md` §Staleness + §Own-position edge cases

## Goal

Compose the complete ConvoyLink screen (status bar, radar, neighbour strip,
banners) from a plain scene description into any `rr_fb_t` strip. This
function IS the product's UI — firmware and simulator both call it.

## Deliverables

- `firmware/components/radar_render/include/radar_scene.h`
- `firmware/components/radar_render/rr_screen.c` (add to component SRCS)
- extend `test/host/test_radar_render.c`

## Interface contract

```c
#include "neighbor_table.h"
#include "radar_render.h"

typedef enum { RR_ZOOM_AUTO = 0, RR_ZOOM_250, RR_ZOOM_500, RR_ZOOM_1K,
               RR_ZOOM_2K, RR_ZOOM_4K } rr_zoom_t;

typedef struct {
    /* own unit */
    bool     provisioned;      /* false → PROVISION ME banner            */
    uint8_t  self_uid;
    char     self_initials[2];
    bool     own_fix;          /* false → NO FIX state                   */
    int32_t  own_lat_e7, own_lon_e7;
    uint16_t own_course_cdeg;  /* CL_COURSE_INVALID → circle marker      */
    uint8_t  own_sats;
    /* convoy */
    nt_entry_t neighbors[CL_MAX_UNITS];
    int      n_neighbors;      /* from nt_snapshot                       */
    uint32_t now_ms;
    /* radio/UI state */
    bool     ptt_tx, ptt_busy; /* TX tile / BUSY banner                  */
    int8_t   rx_talker_uid;    /* -1 = none                              */
    rr_zoom_t zoom_mode;
    uint16_t zoom_scale_m;     /* resolved scale actually in use         */
} rr_scene_t;

/* Render the full screen state into one strip. Pure function of scene. */
void rr_screen_draw(rr_fb_t *fb, const rr_scene_t *sc);

/* Auto-zoom helper (pure, unit-testable): pick the smallest scale from
 * {250,500,1000,2000,4000} that keeps max_dist_m inside 0.9 * scale
 * (hysteresis handled by caller per docs/06). */
uint16_t rr_pick_zoom(float max_live_dist_m);

/* Screen mapping helper used by tests: metres offset -> radar pixels.
 * (dx east, dy north) -> x = 120 + round(dx * 108 / scale),
 *                        y = 148 - round(dy * 108 / scale). */
void rr_map_to_px(float dx_m, float dy_m, uint16_t scale_m, int *x, int *y);
```

Layout, colours, tier styling, off-scale arrowheads, chip strip, banners:
implement exactly `docs/06-ui.md`. Neighbour chips sort by distance
(no-fix/unknown distance last, then by uid for stability).

## Test requirements

1. **Strip invariance** for a busy scene (4 neighbours across all tiers,
   TX on, off-scale car) — single buffer vs 16 strips byte-identical.
2. `rr_map_to_px` fixtures: (0,100 m) @250 m → (120,105); (176.8, −176.8)
   @1000 m → (139, 167) (round-half-away-from-zero: document and match).
3. A LIVE neighbour 100 m due north @250 m zoom paints `RR_UNIT_COLOR[uid]`
   at (120,105) and background stays RR_BG at (120,191).
4. Off-scale neighbour (2 km @500 m zoom, bearing 90°) → no dot pixel
   beyond ring radius +2 px; arrowhead pixels present at (≈228,148).
5. `rr_pick_zoom`: 0→250, 220→250, 226→500 (0.9 boundary), 3400→4000,
   99999→4000.
6. NO FIX scene: no neighbour dots anywhere in radar area; strip chips
   still rendered; banner text present (assert via pinned FNV hash of the
   radar area, comment what it encodes).
7. `n_neighbors == 0` + provisioned → "waiting for convoy…" hash-pinned.

## Acceptance — CI

`make -C test/host test` green. Restore the full
`SRCS_test_radar_render` wiring in `test/host/Makefile` if T05 trimmed it.

## Out of scope

Zoom hysteresis timing (caller logic, T17), touch, sweep animation, night
brightness (T20).
