/* convoylink — the real firmware's load-bearing frame (T15).
 *
 * Init order is docs/01's: NVS -> unit_cfg -> display -> splash -> radio
 * -> audio -> GPS -> state/queues -> tasks. Every peripheral is allowed
 * to fail: docs/01 §Error-handling requires the firmware to run forever
 * with any of them absent, so nothing here aborts on a peripheral error.
 */
#include "app_queues.h"
#include "app_state.h"
#include "app_tasks.h"
#include "radio_stats.h"

#include "audio_io.h"
#include "esp_app_desc.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gps_uart.h"
#include "ili9341_disp.h"
#include "nvs_flash.h"
#include "radar_scene.h"
#include "sx1262.h"
#include "unit_cfg.h"
#include "voice_transport.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "convoylink";

#define BOOT_SPLASH_MS 1500 /* T20: project name/id/version, then radar */

/*
 * If the last boot ended in a task/interrupt watchdog reset, make that the
 * first thing visible in the log - it means some task stopped responding,
 * which is worth the owner's attention before anything else prints
 * (T20 acceptance: "reset-reason banner appears after a deliberate WDT
 * trip"). Ordinary resets (power-on, esp_restart, panic) print nothing
 * extra here.
 */
static void log_reset_reason_banner(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    const char *name;
    switch (reason) {
    case ESP_RST_TASK_WDT:
        name = "TASK watchdog";
        break;
    case ESP_RST_INT_WDT:
        name = "INTERRUPT watchdog";
        break;
    case ESP_RST_WDT:
        name = "watchdog (other)";
        break;
    default:
        return;
    }
    ESP_LOGW(TAG, "############################################");
    ESP_LOGW(TAG, "# LAST RESET WAS A WATCHDOG TIMEOUT: %s", name);
    ESP_LOGW(TAG, "# a task stopped responding to its watchdog - check the");
    ESP_LOGW(TAG, "# logs from just before this boot for which one (T20)");
    ESP_LOGW(TAG, "############################################");
}

/* Splash 1/2: project name, identity, firmware version (`git describe`,
 * via IDF's PROJECT_VER machinery — esp_app_desc.h needs no extra CMake
 * work). Held a fixed 1.5 s so it's readable, then splash 2/2 below takes
 * over. */
static void draw_boot_splash(const unit_cfg_t *cfg, bool provisioned)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    char id_buf[20], ver_buf[40];
    if (provisioned) {
        snprintf(id_buf, sizeof id_buf, "U%u %c%c", (unsigned)cfg->unit_id,
                 cfg->initials[0], cfg->initials[1]);
    } else {
        snprintf(id_buf, sizeof id_buf, "UNPROVISIONED");
    }
    snprintf(ver_buf, sizeof ver_buf, "fw %s", desc->version);

    const char *title = "ConvoyLink";
    static uint16_t strip[RR_W * 20];
    for (int y0 = 0; y0 < RR_H; y0 += 20) {
        rr_fb_t fb = {strip, y0, 20};
        rr_clear(&fb, RR_BG);
        rr_text(&fb, (RR_W - 16 * (int)strlen(title)) / 2, 130, title,
                RR_WHITE, 2);
        rr_text(&fb, (RR_W - 16 * (int)strlen(id_buf)) / 2, 160, id_buf,
                RR_TEXT, 2);
        rr_text(&fb, (RR_W - 8 * (int)strlen(ver_buf)) / 2, 290, ver_buf,
                RR_TEXT, 1);
        (void)disp_flush(fb.y0, fb.h, strip);
    }
    vTaskDelay(pdMS_TO_TICKS(BOOT_SPLASH_MS));
}

/* Splash 2/2: draw the provision/waiting screen once before the tasks
 * start, so a slow radio or GPS init never looks like a dead device. */
static uint16_t s_splash_strip[RR_W * 20];

static void draw_splash(const unit_cfg_t *cfg, bool provisioned)
{
    rr_scene_t sc;
    memset(&sc, 0, sizeof sc);
    sc.provisioned = provisioned;
    sc.self_uid = cfg->unit_id;
    sc.self_initials[0] = cfg->initials[0];
    sc.self_initials[1] = cfg->initials[1];
    sc.own_course_cdeg = CL_COURSE_INVALID;
    sc.rx_talker_uid = -1;
    sc.zoom_mode = RR_ZOOM_AUTO;
    sc.zoom_scale_m = rr_pick_zoom(0.0f);

    for (int y = 0; y < RR_H; y += 20) {
        rr_fb_t fb = {s_splash_strip, y, 20};
        rr_screen_draw(&fb, &sc);
        (void)disp_flush(y, 20, s_splash_strip);
    }
}

static int cmd_free(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t tx_dropped, ctrl_dropped, ui_dropped;
    queues_dropped(&tx_dropped, &ctrl_dropped, &ui_dropped);

    printf("heap free=%u min_free=%u largest_block=%u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    printf("queues dropped: tx=%u ctrl=%u ui=%u\n", (unsigned)tx_dropped,
           (unsigned)ctrl_dropped, (unsigned)ui_dropped);
    return 0;
}

/* Declared in voice_task.c. */
void voice_task_stats(const char **transport, int *state,
                      uint32_t *tx_seconds, uint32_t *frame_invalid);

static int cmd_voice(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const char *transport;
    int ptt_state;
    uint32_t tx_seconds, frame_invalid, sent, recvd, dropped;

    voice_task_stats(&transport, &ptt_state, &tx_seconds, &frame_invalid);
    voice_transport_stats(&sent, &recvd, &dropped);

    static const char *PTT_NAMES[] = {"IDLE", "ARMED_WAIT", "TX", "TX_DRAIN"};
    convoy_state_t st;
    state_snapshot(&st);

    printf("transport=%s up=%d ptt=%s talker=%d\n", transport,
           (int)st.voice_ok,
           (ptt_state >= 0 && ptt_state < 4) ? PTT_NAMES[ptt_state] : "?",
           (int)st.voice_talker_uid);
    printf("tx_seconds=%u frames out=%u in=%u dropped=%u invalid=%u\n",
           (unsigned)tx_seconds, (unsigned)sent, (unsigned)recvd,
           (unsigned)dropped, (unsigned)frame_invalid);
    return 0;
}

/* Declared in ui_task.c. Debug-only aid so the WDT/reset-reason path can
 * be exercised on real hardware without a genuine fault (T20 acceptance:
 * "add a hidden crash console cmd"). REMOVE-ME if this firmware ever
 * needs to leave the owner's hands with debug commands still live. */
void ui_task_trigger_crash(void);

static int cmd_crash(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("triggering a deliberate ui_task hang - expect a task-WDT reset "
           "within ~10 s and a banner on the next boot\n");
    ui_task_trigger_crash();
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    convoy_state_t st;
    state_snapshot(&st);
    printf("U%u %c%c region=%s voice=%s %s\n", st.cfg.unit_id,
           st.cfg.initials[0], st.cfg.initials[1],
           unit_cfg_region_name(st.cfg.region),
           unit_cfg_voice_name(st.cfg.voice),
           st.provisioned ? "" : "(UNPROVISIONED)");
    printf("radio_ok=%d voice_ok=%d fix=%d sats=%u\n", (int)st.radio_ok,
           (int)st.voice_ok, (int)st.own_fix.valid, st.own_fix.sats);
    return 0;
}

static void start_console(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "convoy>";
    repl_cfg.max_cmdline_length = 128;

    esp_console_dev_uart_config_t uart_cfg =
        ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));
    ESP_ERROR_CHECK(esp_console_register_help_command());
    ESP_ERROR_CHECK(unit_cfg_register_console());
    ESP_ERROR_CHECK(radio_stats_register_console());

    static const esp_console_cmd_t cmds[] = {
        {.command = "free",
         .help = "Heap free/min-free/largest block and queue drop counts",
         .func = cmd_free},
        {.command = "status",
         .help = "Identity, peripheral health and current fix",
         .func = cmd_status},
        {.command = "voice",
         .help = "Voice transport, PTT state, tx-seconds, frame counters",
         .func = cmd_voice},
        {.command = "crash",
         .help = "Debug only: deliberately hang ui_task to test the "
                 "watchdog/reset-reason path",
         .func = cmd_crash},
    };
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

void app_main(void)
{
    log_reset_reason_banner(); /* first thing printed, before anything else */

    /* --- NVS + identity ------------------------------------------------- */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err); /* NVS is not a peripheral — it must work */
    ESP_ERROR_CHECK(unit_cfg_init());

    unit_cfg_t cfg;
    bool provisioned = unit_cfg_get(&cfg);
    if (provisioned) {
        ESP_LOGI(TAG, "U%u %c%c region=%s voice=%s", cfg.unit_id,
                 cfg.initials[0], cfg.initials[1],
                 unit_cfg_region_name(cfg.region),
                 unit_cfg_voice_name(cfg.voice));
    } else {
        ESP_LOGW(TAG, "UNPROVISIONED — run: unitcfg set <0-4> <AA>");
    }

    /* --- state + queues, before any task can touch them ------------------ */
    ESP_ERROR_CHECK(state_init(&cfg, provisioned));
    ESP_ERROR_CHECK(queues_init());

    /* --- display + splash ------------------------------------------------ */
    if (disp_init() == ESP_OK) {
        draw_boot_splash(&cfg, provisioned);
        draw_splash(&cfg, provisioned);
    } else {
        ESP_LOGE(TAG, "display init failed — running headless");
    }

    /* --- radio ----------------------------------------------------------- */
    bool radio_ok = sx1262_init(unit_cfg_region_freq_hz(cfg.region)) == ESP_OK;
    if (!radio_ok) {
        ESP_LOGE(TAG, "RADIO? sx1262 init failed — retrying every 5 s");
    }

    /* --- audio ----------------------------------------------------------- */
    bool voice_ok = aio_init() == ESP_OK;
    if (!voice_ok) {
        ESP_LOGE(TAG, "VOICE? audio_io init failed");
    }
    /* The transport itself is brought up by voice_task, which owns it and
     * retries every 5 s; aio_init here just proves the I2S side. */

    /* --- GPS ------------------------------------------------------------- */
    if (gps_uart_start() != ESP_OK) {
        ESP_LOGE(TAG, "GPS UART init failed — radar will show NO FIX");
    }

    state_lock();
    state_get()->radio_ok = radio_ok;
    state_get()->voice_ok = voice_ok;
    state_unlock();

    /* --- tasks, exactly per the docs/01 table ---------------------------- */
    xTaskCreatePinnedToCore(radio_task, "radio_task", RADIO_TASK_STACK, NULL,
                            RADIO_TASK_PRIO, NULL, RADIO_TASK_CORE);
    xTaskCreatePinnedToCore(voice_task, "voice_task", VOICE_TASK_STACK, NULL,
                            VOICE_TASK_PRIO, NULL, VOICE_TASK_CORE);
    xTaskCreatePinnedToCore(gps_task, "gps_task", GPS_TASK_STACK, NULL,
                            GPS_TASK_PRIO, NULL, GPS_TASK_CORE);
    xTaskCreatePinnedToCore(ctrl_task, "ctrl_task", CTRL_TASK_STACK, NULL,
                            CTRL_TASK_PRIO, NULL, CTRL_TASK_CORE);
    xTaskCreatePinnedToCore(ui_task, "ui_task", UI_TASK_STACK, NULL,
                            UI_TASK_PRIO, NULL, UI_TASK_CORE);

    ESP_LOGI(TAG, "boot complete, heap free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));

    start_console();
}
