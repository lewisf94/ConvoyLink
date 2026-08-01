/**
 * ili9341_disp — the 2.8" ILI9341 panel behind a strip-flush API shaped
 * to match `radar_render`'s strip model, plus backlight PWM
 * (docs/06-ui.md §Refresh & rendering contract, docs/02 §ILI9341 wiring).
 *
 * Owns SPI2_HOST (FSPI): the panel is its only device — touch is
 * descoped (docs/02).
 *
 * Concurrency: `disp_init` once from startup; afterwards a single owner
 * task drives `disp_flush`. Flushes are synchronous, so a returned call
 * means the pixels are on the glass and the caller's buffer is reusable.
 */
#ifndef ILI9341_DISP_H
#define ILI9341_DISP_H

#include "esp_err.h"

#include <stdint.h>

/** Brings up SPI, the panel and the backlight (starts at 100 %). */
esp_err_t disp_init(void);

/**
 * Blocking flush of one strip of RR_W-wide RGB565 pixels to rows
 * [y0, y0+h). `px` holds h*RR_W pixels, row-major — exactly what
 * `rr_screen_draw` writes into an `rr_fb_t`.
 *
 * The panel's byte order is handled internally, so callers pass
 * radar_render's native little-endian buffers untouched.
 */
esp_err_t disp_flush(int y0, int h, const uint16_t *px);

/** Backlight duty, 0..100 (clamped). LEDC PWM on CONVOY_PIN_TFT_BL. */
esp_err_t disp_backlight_pct(uint8_t pct);

#endif /* ILI9341_DISP_H */
