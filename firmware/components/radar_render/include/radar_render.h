/**
 * radar_render — strip-clipped RGB565 drawing primitives + embedded 8x8
 * font (docs/06-ui.md §Refresh & rendering contract). The device renders
 * through 240x20 strips; the simulator through one full-height strip;
 * both call the same primitives and must produce identical pixels.
 *
 * Pure C component: no ESP-IDF headers, no allocation, host-testable.
 */
#ifndef RADAR_RENDER_H
#define RADAR_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#define RR_W 240
#define RR_H 320

/* Colours (RGB565), docs/06 §Colours. */
#define RR_BG 0x0000u
#define RR_GRID 0x18E3u
#define RR_RING 0x39E7u
#define RR_TEXT 0xC618u
#define RR_WHITE 0xFFFFu
#define RR_RED 0xF800u
#define RR_GREEN 0x07E0u
#define RR_YELLOW 0xFFE0u
#define RR_GHOST 0x4208u

/* Unit palette, id 0..4: cyan, yellow, magenta, green, orange. */
extern const uint16_t RR_UNIT_COLOR[5];

/**
 * A horizontal strip window of the 240-wide screen. px has w*h entries,
 * row-major, row 0 = screen row y0. All primitives below take FULL-SCREEN
 * coordinates and clip everything outside [0,RR_W) x [y0, y0+h) — they
 * never write outside the given px buffer and never allocate.
 */
typedef struct {
    uint16_t *px;
    int y0;
    int h;
} rr_fb_t;

void rr_clear(rr_fb_t *fb, uint16_t c);
void rr_fill_rect(rr_fb_t *fb, int x, int y, int w, int h, uint16_t c);
void rr_hline(rr_fb_t *fb, int x0, int x1, int y, uint16_t c);
void rr_vline(rr_fb_t *fb, int x, int y0, int y1, uint16_t c);
/** Bresenham. */
void rr_line(rr_fb_t *fb, int x0, int y0, int x1, int y1, uint16_t c);
/** Midpoint circle, outline or filled. */
void rr_circle(rr_fb_t *fb, int cx, int cy, int r, uint16_t c, bool filled);
void rr_tri_filled(rr_fb_t *fb, int x0, int y0, int x1, int y1, int x2,
                   int y2, uint16_t c);

/* Embedded 8x8 ASCII font, glyphs for codes 32..126 (95 total), one row
 * per byte, bit 0 (LSB) = leftmost pixel of that row, row 0 = glyph top
 * (see rr_font8x8.c). */
#define RR_FONT_GLYPHS 95
extern const uint8_t rr_font8x8[RR_FONT_GLYPHS][8];

/**
 * Draws s at integer scale 1 or 2, (x,y) = top-left of the first glyph.
 * Codes outside 32..126 render as '?'. Returns the total x advance
 * (8*scale per character).
 */
int rr_text(rr_fb_t *fb, int x, int y, const char *s, uint16_t c, int scale);

#endif /* RADAR_RENDER_H */
