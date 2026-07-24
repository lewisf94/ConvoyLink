/* rr_prims.c — see include/radar_render.h and docs/06-ui.md */
#include "radar_render.h"

#include <stdlib.h>

const uint16_t RR_UNIT_COLOR[5] = {
    0x07FF, /* id0 cyan    */
    0xFFE0, /* id1 yellow  */
    0xF81F, /* id2 magenta */
    0x07E0, /* id3 green   */
    0xFD20, /* id4 orange  */
};

static inline void put_px(rr_fb_t *fb, int x, int y, uint16_t c)
{
    if (x < 0 || x >= RR_W) {
        return;
    }
    if (y < fb->y0 || y >= fb->y0 + fb->h) {
        return;
    }
    fb->px[(y - fb->y0) * RR_W + x] = c;
}

void rr_clear(rr_fb_t *fb, uint16_t c)
{
    int n = RR_W * fb->h;
    for (int i = 0; i < n; i++) {
        fb->px[i] = c;
    }
}

void rr_fill_rect(rr_fb_t *fb, int x, int y, int w, int h, uint16_t c)
{
    int x0 = x < 0 ? 0 : x;
    int x1 = x + w;
    if (x1 > RR_W) {
        x1 = RR_W;
    }
    int y0 = y < fb->y0 ? fb->y0 : y;
    int y1 = y + h;
    if (y1 > fb->y0 + fb->h) {
        y1 = fb->y0 + fb->h;
    }
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = fb->px + (yy - fb->y0) * RR_W;
        for (int xx = x0; xx < x1; xx++) {
            row[xx] = c;
        }
    }
}

void rr_hline(rr_fb_t *fb, int x0, int x1, int y, uint16_t c)
{
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    rr_fill_rect(fb, x0, y, x1 - x0 + 1, 1, c);
}

void rr_vline(rr_fb_t *fb, int x, int y0, int y1, uint16_t c)
{
    if (y0 > y1) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    rr_fill_rect(fb, x, y0, 1, y1 - y0 + 1, c);
}

void rr_line(rr_fb_t *fb, int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        put_px(fb, x0, y0, c);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void rr_circle(rr_fb_t *fb, int cx, int cy, int r, uint16_t c, bool filled)
{
    int x = r, y = 0, err = 0;
    while (x >= y) {
        if (filled) {
            rr_hline(fb, cx - x, cx + x, cy + y, c);
            rr_hline(fb, cx - x, cx + x, cy - y, c);
            rr_hline(fb, cx - y, cx + y, cy + x, c);
            rr_hline(fb, cx - y, cx + y, cy - x, c);
        } else {
            put_px(fb, cx + x, cy + y, c);
            put_px(fb, cx + y, cy + x, c);
            put_px(fb, cx - y, cy + x, c);
            put_px(fb, cx - x, cy + y, c);
            put_px(fb, cx - x, cy - y, c);
            put_px(fb, cx - y, cy - x, c);
            put_px(fb, cx + y, cy - x, c);
            put_px(fb, cx + x, cy - y, c);
        }
        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        }
        if (err > 0) {
            x--;
            err -= 2 * x + 1;
        }
    }
}

static void swap_int(int *a, int *b)
{
    int t = *a;
    *a = *b;
    *b = t;
}

/* x at the given y along the line (x0,y0)-(x1,y1). Truncating division is
 * fine here: callers always scan y strictly between y0 and y1 inclusive,
 * so numerator/denominator share sign and truncation is stable/monotonic
 * (no gaps between the two half-triangle scans in rr_tri_filled). */
static int edge_x_at_y(int x0, int y0, int x1, int y1, int y)
{
    if (y1 == y0) {
        return x0;
    }
    return x0 + (int)(((long)(x1 - x0) * (y - y0)) / (y1 - y0));
}

void rr_tri_filled(rr_fb_t *fb, int x0, int y0, int x1, int y1, int x2,
                   int y2, uint16_t c)
{
    if (y0 > y1) {
        swap_int(&x0, &x1);
        swap_int(&y0, &y1);
    }
    if (y1 > y2) {
        swap_int(&x1, &x2);
        swap_int(&y1, &y2);
    }
    if (y0 > y1) {
        swap_int(&x0, &x1);
        swap_int(&y0, &y1);
    }

    for (int y = y0; y <= y2; y++) {
        int xa = edge_x_at_y(x0, y0, x2, y2, y); /* long edge, top->bottom */
        int xb = (y <= y1) ? edge_x_at_y(x0, y0, x1, y1, y)
                            : edge_x_at_y(x1, y1, x2, y2, y);
        rr_hline(fb, xa, xb, y, c);
    }
}

int rr_text(rr_fb_t *fb, int x, int y, const char *s, uint16_t c, int scale)
{
    int start_x = x;
    for (const char *p = s; *p != '\0'; p++) {
        int code = (unsigned char)*p;
        int idx = (code >= 32 && code <= 126) ? (code - 32) : ('?' - 32);
        const uint8_t *rows = rr_font8x8[idx];
        for (int ry = 0; ry < 8; ry++) {
            uint8_t bits = rows[ry];
            for (int rx = 0; rx < 8; rx++) {
                if (bits & (uint8_t)(1u << rx)) {
                    rr_fill_rect(fb, x + rx * scale, y + ry * scale, scale,
                                scale, c);
                }
            }
        }
        x += 8 * scale;
    }
    return x - start_x;
}
