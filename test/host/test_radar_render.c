/* Host tests for radar_render primitives + screen composition (docs/06).
 * T05 covers the primitives/font; T06 (below) covers rr_screen_draw. */
#include "radar_render.h"
#include "radar_scene.h"
#include "tinytest.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

/* A scene deliberately touching several strip boundaries: rects (one
 * partly off-screen), odd-angle lines corner to corner, filled + outline
 * circles crossing y=28/100/300, text at a strip edge and near the
 * bottom, and a filled triangle. Must be a pure function of (y0, h). */
static void draw_scene(rr_fb_t *fb)
{
    rr_clear(fb, RR_BG);
    rr_fill_rect(fb, 10, 5, 50, 30, RR_RED);
    rr_fill_rect(fb, -20, 300, 60, 40, RR_GREEN);
    rr_hline(fb, 5, 235, 100, RR_WHITE);
    rr_vline(fb, 120, 10, 310, RR_GRID);
    rr_line(fb, 0, 0, 239, 319, RR_YELLOW);
    rr_line(fb, 239, 0, 0, 200, RR_TEXT);
    rr_circle(fb, 120, 28, 20, RR_RING, false);
    rr_circle(fb, 60, 100, 15, RR_UNIT_COLOR[0], true);
    rr_circle(fb, 200, 300, 25, RR_GHOST, true);
    rr_text(fb, 2, 18, "STRIP EDGE", RR_WHITE, 1);
    rr_text(fb, 100, 300, "Bottom!?", RR_TEXT, 2);
    rr_tri_filled(fb, 120, 140, 100, 170, 140, 170, RR_WHITE);
}

TT_TEST(strip_invariance)
{
    static uint16_t full[RR_W * RR_H];
    rr_fb_t full_fb = {full, 0, RR_H};
    draw_scene(&full_fb);

    static uint16_t stitched[RR_W * RR_H];
    for (int y0 = 0; y0 < RR_H; y0 += 20) {
        uint16_t strip[RR_W * 20];
        rr_fb_t strip_fb = {strip, y0, 20};
        draw_scene(&strip_fb);
        memcpy(stitched + y0 * RR_W, strip, sizeof(strip));
    }

    TT_ASSERT_MEMEQ(full, stitched, sizeof(full));
}

TT_TEST(fully_offscreen_primitives_touch_nothing)
{
    uint16_t buf[RR_W * 20];
    rr_fb_t fb = {buf, 100, 20}; /* screen rows 100..119 */
    rr_clear(&fb, RR_BG);

    rr_fill_rect(&fb, -50, -50, 20, 20, RR_RED);
    rr_fill_rect(&fb, 300, 500, 20, 20, RR_RED);
    rr_fill_rect(&fb, 10, 10, -5, -5, RR_RED); /* negative dims */
    rr_line(&fb, -10000, -10000, -9000, -9000, RR_WHITE);
    rr_circle(&fb, -10000, -10000, 50, RR_WHITE, true);
    rr_circle(&fb, 10000, 10000, 50, RR_WHITE, false);
    rr_tri_filled(&fb, -10000, -10000, -9000, -9000, -9500, -8000, RR_WHITE);
    rr_text(&fb, -10000, -10000, "nope", RR_WHITE, 2);

    for (int i = 0; i < RR_W * 20; i++) {
        TT_ASSERT_EQ(buf[i], RR_BG);
    }
}

TT_TEST(partial_and_extreme_clipping_correct)
{
    uint16_t buf[RR_W * 20];
    rr_fb_t fb = {buf, 100, 20};
    rr_clear(&fb, RR_BG);

    /* x in [-10,10) clips to [0,10); y in [90,105) clips to rows 0..4 */
    rr_fill_rect(&fb, -10, 90, 20, 15, RR_GREEN);
    for (int y = 0; y < 20; y++) {
        for (int x = 0; x < RR_W; x++) {
            uint16_t want = (y < 5 && x < 10) ? RR_GREEN : RR_BG;
            TT_ASSERT_EQ(buf[y * RR_W + x], want);
        }
    }

    /* huge-but-partially-visible horizontal line at screen y=105 (row 5
     * of this strip): must fill the entire visible row without crashing */
    rr_clear(&fb, RR_BG);
    rr_line(&fb, -1000, 105, 1000, 105, RR_WHITE);
    for (int x = 0; x < RR_W; x++) {
        TT_ASSERT_EQ(buf[5 * RR_W + x], RR_WHITE);
    }
    TT_ASSERT_EQ(buf[4 * RR_W + 0], RR_BG);
    TT_ASSERT_EQ(buf[6 * RR_W + 0], RR_BG);
}

TT_TEST(line_exact_pixel_sets)
{
    static uint16_t buf[RR_W * RR_H];
    rr_fb_t fb = {buf, 0, RR_H};

    rr_clear(&fb, RR_BG);
    rr_line(&fb, 10, 50, 20, 50, RR_WHITE); /* horizontal, 11 px inclusive */
    for (int x = 10; x <= 20; x++) {
        TT_ASSERT_EQ(buf[50 * RR_W + x], RR_WHITE);
    }
    TT_ASSERT_EQ(buf[50 * RR_W + 9], RR_BG);
    TT_ASSERT_EQ(buf[50 * RR_W + 21], RR_BG);

    rr_clear(&fb, RR_BG);
    rr_line(&fb, 30, 10, 30, 20, RR_WHITE); /* vertical, 11 px inclusive */
    for (int y = 10; y <= 20; y++) {
        TT_ASSERT_EQ(buf[y * RR_W + 30], RR_WHITE);
    }
    TT_ASSERT_EQ(buf[9 * RR_W + 30], RR_BG);
    TT_ASSERT_EQ(buf[21 * RR_W + 30], RR_BG);

    rr_clear(&fb, RR_BG);
    rr_line(&fb, 0, 0, 4, 4, RR_WHITE); /* exact 45-degree diagonal */
    for (int i = 0; i <= 4; i++) {
        TT_ASSERT_EQ(buf[i * RR_W + i], RR_WHITE);
    }
    TT_ASSERT_EQ(buf[0 * RR_W + 1], RR_BG);
    TT_ASSERT_EQ(buf[4 * RR_W + 3], RR_BG);
}

TT_TEST(filled_circle_symmetric)
{
    static uint16_t buf[RR_W * RR_H];
    rr_fb_t fb = {buf, 0, RR_H};
    rr_clear(&fb, RR_BG);

    int cx = 120, cy = 150, r = 4;
    rr_circle(&fb, cx, cy, r, RR_WHITE, true);

    for (int dx = -r; dx <= r; dx++) {
        for (int dy = -r; dy <= r; dy++) {
            uint16_t a = buf[(cy + dy) * RR_W + (cx + dx)];
            uint16_t b = buf[(cy - dy) * RR_W + (cx - dx)];
            TT_ASSERT_EQ(a, b);
        }
    }
}

TT_TEST(text_scale_and_advance)
{
    static uint16_t buf[RR_W * RR_H];
    rr_fb_t fb = {buf, 0, RR_H};
    rr_clear(&fb, RR_BG);

    int adv = rr_text(&fb, 50, 50, "A", RR_WHITE, 1);
    TT_ASSERT_EQ(adv, 8);
    bool any_set = false;
    for (int y = 0; y < RR_H; y++) {
        for (int x = 0; x < RR_W; x++) {
            if (buf[y * RR_W + x] == RR_WHITE) {
                any_set = true;
                TT_ASSERT(x >= 50 && x < 58 && y >= 50 && y < 58);
            }
        }
    }
    TT_ASSERT(any_set);

    /* scale 2: each glyph bit becomes an exact 2x2 block */
    rr_clear(&fb, RR_BG);
    int adv2 = rr_text(&fb, 0, 0, "A", RR_WHITE, 2);
    TT_ASSERT_EQ(adv2, 16);
    const uint8_t *rows = rr_font8x8['A' - 32];
    for (int ry = 0; ry < 8; ry++) {
        for (int rx = 0; rx < 8; rx++) {
            bool bit = (rows[ry] & (uint8_t)(1u << rx)) != 0;
            uint16_t want = bit ? RR_WHITE : RR_BG;
            for (int sy = 0; sy < 2; sy++) {
                for (int sx = 0; sx < 2; sx++) {
                    uint16_t got = buf[(ry * 2 + sy) * RR_W + (rx * 2 + sx)];
                    TT_ASSERT_EQ(got, want);
                }
            }
        }
    }

    rr_clear(&fb, RR_BG);
    TT_ASSERT_EQ(rr_text(&fb, 0, 0, "HELLO", RR_WHITE, 1), 8 * 1 * 5);
    TT_ASSERT_EQ(rr_text(&fb, 0, 0, "HELLO", RR_WHITE, 2), 8 * 2 * 5);
}

static uint32_t fnv1a(const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

TT_TEST(font_hash_pinned)
{
    uint16_t buf[RR_W * 16];
    rr_fb_t fb = {buf, 0, 16};
    rr_clear(&fb, RR_BG);
    rr_text(&fb, 0, 0, "AB 12", RR_WHITE, 2);

    uint32_t h = fnv1a(buf, sizeof(buf));
    /* FNV-1a of the rendered "AB 12" bitmap (240x16 RGB565, this platform's
     * native byte order) at scale 2 — computed once, pinned here to catch
     * accidental future edits to the font table or rr_text (docs/06, T05). */
    TT_ASSERT_EQ(h, 3553019093u);
}

/* ==================== T06: screen composition (docs/06) ================= */

static nt_entry_t mk_entry(uint8_t uid, const char *initials, int32_t lat_e7,
                          int32_t lon_e7, uint8_t fix_quality,
                          uint32_t last_heard_ms)
{
    nt_entry_t e;
    memset(&e, 0, sizeof(e));
    e.in_use = true;
    e.uid = uid;
    e.initials[0] = initials[0];
    e.initials[1] = initials[1];
    cl_make_beacon(&e.last, uid, 1, initials, lat_e7, lon_e7, 0,
                   CL_COURSE_INVALID, fix_quality, 5, 0);
    e.last_seq = 1;
    e.last_heard_ms = last_heard_ms;
    return e;
}

static void build_busy_scene(rr_scene_t *sc)
{
    memset(sc, 0, sizeof(*sc));
    sc->provisioned = true;
    sc->self_uid = 0;
    sc->self_initials[0] = 'L';
    sc->self_initials[1] = 'F';
    sc->own_fix = true;
    sc->own_lat_e7 = 515000000;
    sc->own_lon_e7 = 0;
    sc->own_course_cdeg = 9000; /* 90.00 deg, arbitrary valid course */
    sc->own_sats = 9;
    sc->now_ms = 100000;
    sc->ptt_tx = true;
    sc->rx_talker_uid = -1;
    sc->zoom_mode = RR_ZOOM_500;
    sc->zoom_scale_m = 500;

    sc->neighbors[0] = mk_entry(1, "AM", 515000000 + 9000, 0, 3,
                               sc->now_ms - 1000); /* ~100m N, LIVE */
    sc->neighbors[1] =
        mk_entry(2, "RK", 515000000, 9000, 3, sc->now_ms - 20000); /* STALE */
    sc->neighbors[2] = mk_entry(3, "JS", 515000000 - 9000, -9000, 3,
                               sc->now_ms - 300000); /* GHOST */
    sc->neighbors[3] = mk_entry(4, "TP", 515000000, 180000, 3,
                               sc->now_ms - 500); /* ~2km E, off-scale @500m */
    sc->n_neighbors = 4;
}

TT_TEST(screen_strip_invariance_busy_scene)
{
    rr_scene_t sc;
    build_busy_scene(&sc);

    static uint16_t full[RR_W * RR_H];
    rr_fb_t full_fb = {full, 0, RR_H};
    rr_screen_draw(&full_fb, &sc);

    static uint16_t stitched[RR_W * RR_H];
    for (int y0 = 0; y0 < RR_H; y0 += 20) {
        uint16_t strip[RR_W * 20];
        rr_fb_t strip_fb = {strip, y0, 20};
        rr_screen_draw(&strip_fb, &sc);
        memcpy(stitched + y0 * RR_W, strip, sizeof(strip));
    }

    TT_ASSERT_MEMEQ(full, stitched, sizeof(full));
}

TT_TEST(map_to_px_fixtures)
{
    int x, y;
    rr_map_to_px(0.0f, 100.0f, 250, &x, &y);
    TT_ASSERT_EQ(x, 120);
    TT_ASSERT_EQ(y, 105);

    rr_map_to_px(176.8f, -176.8f, 1000, &x, &y);
    TT_ASSERT_EQ(x, 139);
    TT_ASSERT_EQ(y, 167);
}

TT_TEST(live_neighbor_dot_and_background)
{
    rr_scene_t sc;
    memset(&sc, 0, sizeof(sc));
    sc.provisioned = true;
    sc.self_uid = 0;
    sc.self_initials[0] = 'L';
    sc.self_initials[1] = 'F';
    sc.own_fix = true;
    sc.own_lat_e7 = 515000000;
    sc.own_lon_e7 = 0;
    sc.own_course_cdeg = CL_COURSE_INVALID;
    sc.now_ms = 10000;
    sc.rx_talker_uid = -1;
    sc.zoom_scale_m = 250;
    sc.neighbors[0] =
        mk_entry(1, "AM", 515000000 + 9000, 0, 3, sc.now_ms - 1000);
    sc.n_neighbors = 1;

    static uint16_t buf[RR_W * RR_H];
    rr_fb_t fb = {buf, 0, RR_H};
    rr_screen_draw(&fb, &sc);

    TT_ASSERT_EQ(buf[105 * RR_W + 120], RR_UNIT_COLOR[1]);
    TT_ASSERT_EQ(buf[191 * RR_W + 120], RR_BG);
}

TT_TEST(offscale_arrowhead_no_dot_beyond_ring)
{
    rr_scene_t sc;
    memset(&sc, 0, sizeof(sc));
    sc.provisioned = true;
    sc.own_fix = true;
    sc.own_lat_e7 = 0;
    sc.own_lon_e7 = 0;
    sc.own_course_cdeg = CL_COURSE_INVALID;
    sc.now_ms = 10000;
    sc.rx_talker_uid = -1;
    sc.zoom_scale_m = 500;

    /* due-east 2km at the equator -> bearing exactly 90 deg */
    int32_t dlon_e7 = (int32_t)(2000.0 /
                                (6371000.0 * (3.14159265358979323846 / 180.0)) *
                                1e7);
    sc.neighbors[0] = mk_entry(2, "ZZ", 0, dlon_e7, 3, sc.now_ms - 1000);
    sc.n_neighbors = 1;

    static uint16_t buf[RR_W * RR_H];
    rr_fb_t fb = {buf, 0, RR_H};
    rr_screen_draw(&fb, &sc);

    uint16_t color = RR_UNIT_COLOR[2];

    /* radar area only (28..267) - the neighbour strip below it legitimately
     * shows this same unit colour in its chip text, far from centre */
    for (int y = 28; y < 268; y++) {
        for (int x = 0; x < RR_W; x++) {
            if (buf[y * RR_W + x] == color) {
                float ddx = (float)(x - 120), ddy = (float)(y - 148);
                TT_ASSERT(sqrtf(ddx * ddx + ddy * ddy) <= 110.0f);
            }
        }
    }

    bool found = false;
    for (int y = 145; y <= 151 && !found; y++) {
        for (int x = 224; x <= 232 && !found; x++) {
            if (buf[y * RR_W + x] == color) {
                found = true;
            }
        }
    }
    TT_ASSERT(found);
}

TT_TEST(pick_zoom_fixtures)
{
    TT_ASSERT_EQ(rr_pick_zoom(0.0f), 250);
    TT_ASSERT_EQ(rr_pick_zoom(220.0f), 250);
    TT_ASSERT_EQ(rr_pick_zoom(226.0f), 500);
    TT_ASSERT_EQ(rr_pick_zoom(3400.0f), 4000);
    TT_ASSERT_EQ(rr_pick_zoom(99999.0f), 4000);
}

TT_TEST(no_fix_scene_hash_pinned)
{
    rr_scene_t sc;
    memset(&sc, 0, sizeof(sc));
    sc.provisioned = true;
    sc.self_uid = 0;
    sc.self_initials[0] = 'L';
    sc.self_initials[1] = 'F';
    sc.own_fix = false; /* the point of this test */
    sc.now_ms = 12345;  /* fixed: deterministic NO FIX blink phase */
    sc.rx_talker_uid = -1;
    sc.zoom_scale_m = 250;
    sc.neighbors[0] = mk_entry(1, "AM", 515000000, 0, 3, sc.now_ms - 1000);
    sc.n_neighbors = 1; /* strip must still show it; radar must not */

    static uint16_t buf[RR_W * RR_H];
    rr_fb_t fb = {buf, 0, RR_H};
    rr_screen_draw(&fb, &sc);

    for (int y = 28; y < 268; y++) {
        for (int x = 0; x < RR_W; x++) {
            TT_ASSERT(buf[y * RR_W + x] != RR_UNIT_COLOR[1]);
        }
    }

    uint32_t h = fnv1a(&buf[28 * RR_W], (size_t)(268 - 28) * RR_W * sizeof(uint16_t));
    /* FNV-1a of the radar area (rows 28..267) for this exact NO-FIX scene
     * (own_fix=false, one tracked-but-unplotted neighbour, now_ms=12345) —
     * computed once, pinned to catch accidental banner/layout regressions. */
    TT_ASSERT_EQ(h, 4281412827u);
}

TT_TEST(waiting_scene_hash_pinned)
{
    rr_scene_t sc;
    memset(&sc, 0, sizeof(sc));
    sc.provisioned = true;
    sc.self_uid = 2;
    sc.self_initials[0] = 'J';
    sc.self_initials[1] = 'S';
    sc.own_fix = true;
    sc.own_lat_e7 = 515000000;
    sc.own_lon_e7 = 0;
    sc.own_course_cdeg = CL_COURSE_INVALID;
    sc.own_sats = 7;
    sc.now_ms = 54321;
    sc.rx_talker_uid = -1;
    sc.zoom_scale_m = 250;
    sc.n_neighbors = 0; /* the point of this test */

    static uint16_t buf[RR_W * RR_H];
    rr_fb_t fb = {buf, 0, RR_H};
    rr_screen_draw(&fb, &sc);

    uint32_t h = fnv1a(&buf[28 * RR_W], (size_t)(268 - 28) * RR_W * sizeof(uint16_t));
    /* FNV-1a of the radar area for this exact "waiting for convoy" scene —
     * computed once, pinned (docs/06, T06). */
    TT_ASSERT_EQ(h, 3903215319u);
}

int main(void)
{
    TT_RUN(strip_invariance);
    TT_RUN(fully_offscreen_primitives_touch_nothing);
    TT_RUN(partial_and_extreme_clipping_correct);
    TT_RUN(line_exact_pixel_sets);
    TT_RUN(filled_circle_symmetric);
    TT_RUN(text_scale_and_advance);
    TT_RUN(font_hash_pinned);
    TT_RUN(screen_strip_invariance_busy_scene);
    TT_RUN(map_to_px_fixtures);
    TT_RUN(live_neighbor_dot_and_background);
    TT_RUN(offscale_arrowhead_no_dot_beyond_ring);
    TT_RUN(pick_zoom_fixtures);
    TT_RUN(no_fix_scene_hash_pinned);
    TT_RUN(waiting_scene_hash_pinned);
    return tt_summary("radar_render");
}
