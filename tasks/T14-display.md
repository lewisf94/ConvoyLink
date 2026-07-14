# T14 — `ili9341_disp` component + `bringup_display` app

**Depends:** T09 (convoy_pins only) · **Phase:** M3
**Required reading:** `docs/06-ui.md` §Refresh & rendering contract;
`docs/02` §ILI9341 wiring

## Goal

Bring the panel up behind a strip-flush API that matches `radar_render`'s
strip model, plus backlight PWM.

## Deliverables

- `firmware/components/ili9341_disp/include/ili9341_disp.h`
- `firmware/components/ili9341_disp/ili9341_disp.c`
- `firmware/components/ili9341_disp/CMakeLists.txt`
- `firmware/components/ili9341_disp/idf_component.yml` — the **one**
  allowed managed dependency: `espressif/esp_lcd_ili9341: "^1"`
- `firmware/apps/bringup_display/…`

## Interface contract

```c
#include "esp_err.h"
#include <stdint.h>

/* VSPI (SPI3_HOST) @ 40 MHz on CONVOY_PIN_TFT_*, portrait 240x320,
 * USB-down orientation per docs/06. Initialises the SPI bus (touch will
 * share it later — bus config must leave room for a second device). */
esp_err_t disp_init(void);

/* Blocking flush of one strip of RR_W-wide RGB565 pixels to rows
 * [y0, y0+h). Handles the panel's byte order internally — callers pass
 * radar_render's native little-endian buffers untouched. */
esp_err_t disp_flush(int y0, int h, const uint16_t *px);

esp_err_t disp_backlight_pct(uint8_t pct);   /* LEDC PWM on TFT_BL */
```

Implementation notes: `esp_lcd_new_panel_io_spi` + the managed ili9341
panel driver; colour byte-swap — verify empirically with the test pattern
(a red screen that shows cyan means you got it backwards); invert/gamma
defaults are fine; `disp_flush` may use `esp_lcd_panel_draw_bitmap` with a
completion semaphore (keep it synchronous for v1 simplicity).

## `bringup_display` behaviour

Cycles automatically every 3 s: (1) solid **red** full-screen with white
`RED` label — the byte-order check; (2) RGB colour bars + 8×8 font sample
line; (3) 16-strip stress: full-screen redraw loop at max rate, printing
achieved FPS 1×/s (must exceed 5); (4) moving white box on black (tearing
eyeball check). `bl <pct>` console command for backlight.

## Acceptance — CI

`./tools/ci_build_apps.sh` green (component manager pulls the panel driver
in the IDF container).

## Acceptance — hardware (owner checklist)

- [ ] Pattern 1 is RED (not cyan/blue) with readable label
- [ ] Colour bars ordered R-G-B left→right; text crisp
- [ ] Stress FPS ≥ 5 printed (expect ~15–20 at 40 MHz)
- [ ] No white-screen boots across 5 power cycles (reset timing OK)
- [ ] `bl 25` visibly dims (night mode plumbing works)

## Out of scope

Touch (XPT2046 — stretch), radar_render integration (T17), sleep/dimming
logic (T20).
