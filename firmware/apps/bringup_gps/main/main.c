/* bringup_gps — proves the NEO-6M module, its wiring and the patch antenna
 * (tasks/T11-gps-uart.md, docs/05-gps-geo.md §GPS module).
 *
 * esp_console REPL on UART0 plus an automatic 1 Hz status line. Every
 * printed number comes from the parser's integer fields — lat/lon are
 * formatted straight out of the e7 integers, so no float ever touches a
 * coordinate (docs/05).
 */
#include "gps_uart.h"

#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "bringup_gps";

#define STATUS_PERIOD_MS 1000

static volatile bool s_raw;

/* degrees*1e7 -> "-0.1278456", sign handled separately so the fractional
 * part never loses it for |value| < 1 degree. */
static void fmt_e7(int32_t v, char *out, size_t n)
{
    uint32_t a = (v < 0) ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
    snprintf(out, n, "%s%" PRIu32 ".%07" PRIu32, (v < 0) ? "-" : "",
             a / 10000000u, a % 10000000u);
}

static void raw_cb(const char *line)
{
    if (s_raw) {
        printf("%s\n", line);
    }
}

/* One status line in the exact T11 format. */
static void print_status(void)
{
    nmea_fix_t f;
    uint32_t age_ms, ok, bad;
    gps_uart_get_fix(&f, &age_ms);
    gps_uart_stats(&ok, &bad);

    char lat[16], lon[16];
    fmt_e7(f.lat_e7, lat, sizeof lat);
    fmt_e7(f.lon_e7, lon, sizeof lon);

    char crs[12];
    if (f.course_cdeg == 0xFFFFu) {
        snprintf(crs, sizeof crs, "---");
    } else {
        snprintf(crs, sizeof crs, "%u.%u", f.course_cdeg / 100u,
                 (f.course_cdeg % 100u) / 10u);
    }

    char age[16];
    if (age_ms == UINT32_MAX) {
        snprintf(age, sizeof age, "--");
    } else {
        snprintf(age, sizeof age, "%" PRIu32 ".%" PRIu32, age_ms / 1000u,
                 (age_ms % 1000u) / 100u);
    }

    printf("fix=%c sats=%u hdop=%u.%u lat=%s lon=%s spd=%u.%um/s crs=%s "
           "age=%ss ok=%" PRIu32 " bad=%" PRIu32 "\n",
           f.valid ? 'Y' : 'N', f.sats, f.hdop_x10 / 10u, f.hdop_x10 % 10u,
           lat, lon, f.speed_dm_s / 10u, f.speed_dm_s % 10u, crs, age, ok,
           bad);
}

static void status_task(void *arg)
{
    (void)arg;
    for (;;) {
        print_status();
        vTaskDelay(pdMS_TO_TICKS(STATUS_PERIOD_MS));
    }
}

static int cmd_raw(int argc, char **argv)
{
    if (argc != 2 ||
        (strcmp(argv[1], "on") != 0 && strcmp(argv[1], "off") != 0)) {
        printf("usage: raw on|off\n");
        return 1;
    }
    s_raw = (strcmp(argv[1], "on") == 0);
    printf("raw %s\n", s_raw ? "on" : "off");
    return 0;
}

static int cmd_fix(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    print_status();
    return 0;
}

static void register_commands(void)
{
    static const esp_console_cmd_t cmds[] = {
        {.command = "raw",
         .help = "raw on|off — echo raw NMEA sentences",
         .func = cmd_raw},
        {.command = "fix",
         .help = "Print the current fix once",
         .func = cmd_fix},
    };
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}

void app_main(void)
{
    esp_err_t err = gps_uart_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gps_uart_start failed: %s — check wiring/power "
                      "(docs/07 §Troubleshooting)",
                 esp_err_to_name(err));
    }
    gps_uart_set_raw_cb(raw_cb);

    xTaskCreate(status_task, "gps_status", 4096, NULL, 4, NULL);

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "gps>";
    repl_cfg.max_cmdline_length = 128;

    esp_console_dev_uart_config_t uart_cfg =
        ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));

    ESP_ERROR_CHECK(esp_console_register_help_command());
    register_commands();

    printf("\nConvoyLink bringup_gps — 'help' for commands, 'raw on' to see "
           "sentences.\n");
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
