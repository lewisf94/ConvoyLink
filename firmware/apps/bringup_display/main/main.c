/* bringup_display — proves the ILI9341 panel, its wiring and the reset
 * timing, and measures the strip-flush rate the radar will actually get
 * (tasks/T14-display.md, docs/06-ui.md §Refresh & rendering contract).
 *
 * Cycles four patterns every 3 s. Everything is drawn through
 * radar_render's primitives into the same strip buffers the real UI uses,
 * so this measures the real path rather than a synthetic one.
 */
#include "ili9341_disp.h"
#include "radar_render.h"

#include "esp_console.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "bringup_display";

#define STRIP_H 20 /* docs/06: the device renders in 240x20 strips */
#define STRIPS (RR_H / STRIP_H)
#define PATTERN_MS 3000
#define MIN_FPS 5

static uint16_t s_strip[RR_W * STRIP_H];

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Renders one strip via `draw` and pushes it. */
typedef void (*strip_fn)(rr_fb_t *fb, void *ctx);

static void flush_screen(strip_fn draw, void *ctx)
{
    for (int i = 0; i < STRIPS; i++) {
        rr_fb_t fb = {s_strip, i * STRIP_H, STRIP_H};
        draw(&fb, ctx);
        esp_err_t err = disp_flush(fb.y0, fb.h, s_strip);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "flush row %d: %s", fb.y0, esp_err_to_name(err));
            return;
        }
    }
}

/* Pattern 1 — solid red with a white label. If this shows cyan the
 * panel's colour element order is backwards (docs/02 / T14). */
static void draw_red(rr_fb_t *fb, void *ctx)
{
    (void)ctx;
    rr_clear(fb, RR_RED);
    rr_text(fb, 8, 8, "RED", RR_WHITE, 2);
    rr_text(fb, 8, 32, "cyan here = byte order wrong", RR_WHITE, 1);
}

/* Pattern 2 — R/G/B bars plus a font sample line. */
static void draw_bars(rr_fb_t *fb, void *ctx)
{
    (void)ctx;
    rr_clear(fb, RR_BG);
    const int w = RR_W / 3;
    rr_fill_rect(fb, 0, 0, w, RR_H, RR_RED);
    rr_fill_rect(fb, w, 0, w, RR_H, RR_GREEN);
    rr_fill_rect(fb, 2 * w, 0, RR_W - 2 * w, RR_H, 0x001Fu /* blue */);
    rr_text(fb, 4, RR_H / 2 - 8, "ABCdef 0123 !?.,", RR_WHITE, 1);
    rr_text(fb, 4, RR_H / 2 + 4, "R  G  B", RR_WHITE, 2);
}

/* Pattern 4 — a moving white box, for the tearing eyeball check. */
typedef struct {
    int box_y;
} box_ctx_t;

static void draw_box(rr_fb_t *fb, void *ctx)
{
    const box_ctx_t *c = (const box_ctx_t *)ctx;
    rr_clear(fb, RR_BG);
    rr_fill_rect(fb, 90, c->box_y, 60, 60, RR_WHITE);
}

/* Pattern 3 — redraw as fast as possible, printing achieved FPS 1x/s. */
static void run_stress(uint32_t duration_ms)
{
    printf("stress: full-screen redraws, need >= %d FPS\n", MIN_FPS);
    uint32_t start = now_ms();
    uint32_t window_start = start;
    int frames = 0, window_frames = 0;
    box_ctx_t ctx = {0};

    while ((uint32_t)(int32_t)(now_ms() - start) < duration_ms) {
        ctx.box_y = (frames * 7) % (RR_H - 60);
        flush_screen(draw_box, &ctx);
        frames++;
        window_frames++;

        uint32_t elapsed = (uint32_t)(int32_t)(now_ms() - window_start);
        if (elapsed >= 1000u) {
            int fps = (int)((window_frames * 1000u) / elapsed);
            printf("stress: %d FPS %s\n", fps,
                   (fps >= MIN_FPS) ? "ok" : "*** BELOW MINIMUM ***");
            window_frames = 0;
            window_start = now_ms();
        }
    }
}

static void pattern_task(void *arg)
{
    (void)arg;
    for (;;) {
        printf("pattern 1/4: solid red + label (byte-order check)\n");
        flush_screen(draw_red, NULL);
        vTaskDelay(pdMS_TO_TICKS(PATTERN_MS));

        printf("pattern 2/4: colour bars + font sample\n");
        flush_screen(draw_bars, NULL);
        vTaskDelay(pdMS_TO_TICKS(PATTERN_MS));

        printf("pattern 3/4: 16-strip stress\n");
        run_stress(PATTERN_MS);

        printf("pattern 4/4: moving box (tearing check)\n");
        uint32_t start = now_ms();
        box_ctx_t ctx = {0};
        while ((uint32_t)(int32_t)(now_ms() - start) < PATTERN_MS) {
            ctx.box_y = (ctx.box_y + 4) % (RR_H - 60);
            flush_screen(draw_box, &ctx);
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }
}

static int cmd_bl(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: bl <0-100>\n");
        return 1;
    }
    int v = atoi(argv[1]);
    if (v < 0 || v > 100) {
        printf("backlight must be 0..100\n");
        return 1;
    }
    esp_err_t err = disp_backlight_pct((uint8_t)v);
    if (err != ESP_OK) {
        printf("backlight failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("backlight %d%%\n", v);
    return 0;
}

void app_main(void)
{
    esp_err_t err = disp_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "disp_init failed: %s — check wiring/power "
                      "(docs/07 §Troubleshooting)",
                 esp_err_to_name(err));
    } else {
        xTaskCreate(pattern_task, "patterns", 4096, NULL, 5, NULL);
    }

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "disp>";
    repl_cfg.max_cmdline_length = 128;

    esp_console_dev_uart_config_t uart_cfg =
        ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));

    ESP_ERROR_CHECK(esp_console_register_help_command());
    static const esp_console_cmd_t bl_cmd = {
        .command = "bl",
        .help = "bl <0-100> — backlight brightness",
        .func = cmd_bl,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&bl_cmd));

    printf("\nConvoyLink bringup_display — patterns cycle every 3 s; "
           "'bl <pct>' for backlight.\n");
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
