# T05 — `radar_render` part 1: framebuffer primitives + font

**Depends:** none · **Phase:** M1 (pure C)
**Required reading:** `docs/06-ui.md` §Refresh & rendering contract

## Goal

Strip-clipped RGB565 drawing primitives and an embedded 8×8 font — the
device renders through 240×20 strips, the simulator through one full-height
strip; both must produce identical pixels.

## Deliverables

- `firmware/components/radar_render/include/radar_render.h` (primitives half)
- `firmware/components/radar_render/rr_prims.c`
- `firmware/components/radar_render/rr_font8x8.c` (embedded font data)
- `firmware/components/radar_render/CMakeLists.txt`
  (`SRCS "rr_prims.c" "rr_font8x8.c"` — T06 appends its file)
- `test/host/test_radar_render.c` (T06 extends this suite)

## Interface contract

```c
#include <stdbool.h>
#include <stdint.h>

#define RR_W 240
#define RR_H 320

/* Colour constants from docs/06 §Colours: RR_BG, RR_GRID, RR_RING,
 * RR_TEXT, RR_WHITE, RR_RED, RR_GREEN, RR_YELLOW, RR_GHOST and
 * extern const uint16_t RR_UNIT_COLOR[5]; — define all here. */

/* A horizontal strip window of the 240-wide screen. px has w*h entries,
 * row-major, row 0 = screen row y0. All primitives take FULL-SCREEN
 * coordinates and clip everything outside [0,RR_W) x [y0, y0+h). */
typedef struct { uint16_t *px; int y0; int h; } rr_fb_t;

void rr_clear(rr_fb_t *fb, uint16_t c);
void rr_fill_rect(rr_fb_t *fb, int x, int y, int w, int h, uint16_t c);
void rr_hline(rr_fb_t *fb, int x0, int x1, int y, uint16_t c);
void rr_vline(rr_fb_t *fb, int x, int y0, int y1, uint16_t c);
void rr_line(rr_fb_t *fb, int x0, int y0, int x1, int y1, uint16_t c);   /* Bresenham */
void rr_circle(rr_fb_t *fb, int cx, int cy, int r, uint16_t c, bool filled); /* midpoint */
void rr_tri_filled(rr_fb_t *fb, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);
/* 8x8 bitmap font, ASCII 32..126, integer scale 1 or 2; (x,y) = top-left;
 * unknown chars render as '?'. Returns x advance (8*scale per char). */
int  rr_text(rr_fb_t *fb, int x, int y, const char *s, uint16_t c, int scale);
```

Font data: embed a standard public-domain 8×8 ASCII bitmap set
(the classic `font8x8_basic` layout: one `uint8_t[95][8]`, LSB = leftmost
pixel — document the bit order you choose in the header and keep the tests
consistent with it).

## Test requirements

1. **Strip invariance (the load-bearing test):** draw a fixed scene (rects,
   lines at odd angles, filled+outline circles crossing strip boundaries,
   text at strip edges) twice — once into a single 240×320 buffer, once
   into sixteen 240×20 strips — and `memcmp` the assembled results equal.
2. Clipping: primitives wholly/partially off-screen (negative coords,
   > RR_W/RR_H) write nothing out of bounds (ASan) and correct pixels in.
3. Determinism fixtures: horizontal/vertical/diagonal `rr_line` exact pixel
   sets; filled circle r=4 symmetric (px(cx+dx,cy+dy) == px(cx−dx,cy−dy)).
4. Text: 'A' at scale 1 fills only its 8×8 box; scale 2 exactly doubles
   each pixel; string advance = 8·scale·len. Pin an FNV-1a hash of the
   rendered "AB 12" bitmap in the test to freeze the font against
   accidental edits (compute once, assert thereafter — comment the hash).

## Acceptance — CI

`make -C test/host test` green (Makefile already wires
`SRCS_test_radar_render` to `radar_render/*.c` + geo + neighbor_table; geo
and neighbor_table stubs aren't needed until T06 links them — if T01/T04
are not yet done, trim the wired list in your commit and T06 restores it).

## Out of scope

The radar screen itself (T06), ILI9341 flushing (T14), sweep animation.
