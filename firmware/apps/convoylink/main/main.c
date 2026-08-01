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

#include "audio_io.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gps_uart.h"
#include "ili9341_disp.h"
#include "nvs_flash.h"
#include "radar_scene.h"
#include "sx1262.h"
#include "unit_cfg.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "convoylink";

/* Splash: draw the provision/waiting screen once before the tasks start,
 * so a slow radio or GPS init never looks like a dead device. */
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
    uint32_t tx_dropped, ctrl_dropped;
    queues_dropped(&tx_dropped, &ctrl_dropped);

    printf("heap free=%u min_free=%u largest_block=%u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    printf("queues dropped: tx=%u ctrl=%u\n", (unsigned)tx_dropped,
           (unsigned)ctrl_dropped);
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

    static const esp_console_cmd_t cmds[] = {
        {.command = "free",
         .help = "Heap free/min-free/largest block and queue drop counts",
         .func = cmd_free},
        {.command = "status",
         .help = "Identity, peripheral health and current fix",
         .func = cmd_status},
    };
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

void app_main(void)
{
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
    /* The voice transport itself (ESP-NOW or SX1262/Codec2) is T18/T19;
     * cfg.voice is already carried in state for voice_task to act on. */

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
