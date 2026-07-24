/* Host tests for radar_render primitives (docs/06). T06 extends this
 * suite with the screen-composition tests. */
#include "radar_render.h"
#include "tinytest.h"

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

int main(void)
{
    TT_RUN(strip_invariance);
    TT_RUN(fully_offscreen_primitives_touch_nothing);
    TT_RUN(partial_and_extreme_clipping_correct);
    TT_RUN(line_exact_pixel_sets);
    TT_RUN(filled_circle_symmetric);
    TT_RUN(text_scale_and_advance);
    TT_RUN(font_hash_pinned);
    return tt_summary("radar_render");
}
