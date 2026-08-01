/* ili9341_disp.c — see include/ili9341_disp.h, docs/06, docs/02. */
#include "ili9341_disp.h"

#include "convoy_pins.h"
#include "radar_render.h" /* RR_W / RR_H: the panel geometry is the UI's */

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ili9341.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

static const char *TAG = "ili9341";

#define DISP_SPI_HOST SPI2_HOST /* FSPI, docs/02 */
#define DISP_CLK_HZ (40 * 1000 * 1000)
#define DISP_CMD_BITS 8
#define DISP_PARAM_BITS 8

#define BL_TIMER LEDC_TIMER_0
#define BL_CHANNEL LEDC_CHANNEL_0
#define BL_MODE LEDC_LOW_SPEED_MODE
#define BL_RES LEDC_TIMER_10_BIT
#define BL_MAX_DUTY ((1u << 10) - 1u)
#define BL_FREQ_HZ 5000

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static bool s_inited;

static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = BL_MODE,
        .timer_num = BL_TIMER,
        .duty_resolution = BL_RES,
        .freq_hz = BL_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }

    const ledc_channel_config_t ch = {
        .gpio_num = CONVOY_PIN_TFT_BL,
        .speed_mode = BL_MODE,
        .channel = BL_CHANNEL,
        .timer_sel = BL_TIMER,
        .duty = BL_MAX_DUTY,
        .hpoint = 0,
    };
    return ledc_channel_config(&ch);
}

esp_err_t disp_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    const spi_bus_config_t bus = {
        .sclk_io_num = CONVOY_PIN_TFT_SCK,
        .mosi_io_num = CONVOY_PIN_TFT_MOSI,
        .miso_io_num = CONVOY_PIN_TFT_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        /* One full-width strip is the largest transfer we ever issue. */
        .max_transfer_sz = RR_W * RR_H * (int)sizeof(uint16_t),
    };
    esp_err_t err = spi_bus_initialize(DISP_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = CONVOY_PIN_TFT_DC,
        .cs_gpio_num = CONVOY_PIN_TFT_CS,
        .pclk_hz = DISP_CLK_HZ,
        .lcd_cmd_bits = DISP_CMD_BITS,
        .lcd_param_bits = DISP_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISP_SPI_HOST,
                                   &io_cfg, &s_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi: %s", esp_err_to_name(err));
        goto fail_bus;
    }

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = CONVOY_PIN_TFT_RST,
        /* These panels are BGR-ordered; getting it wrong shows a red test
         * pattern as cyan, which is exactly what bringup_display checks. */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_ili9341(s_io, &panel_cfg, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_ili9341: %s", esp_err_to_name(err));
        goto fail_io;
    }

    err = esp_lcd_panel_reset(s_panel);
    if (err == ESP_OK) {
        err = esp_lcd_panel_init(s_panel);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel reset/init: %s", esp_err_to_name(err));
        goto fail_panel;
    }

    /* Portrait 240x320, USB-down (docs/06): no swap, no mirror. */
    (void)esp_lcd_panel_swap_xy(s_panel, false);
    (void)esp_lcd_panel_mirror(s_panel, false, false);
    (void)esp_lcd_panel_disp_on_off(s_panel, true);

    err = backlight_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "backlight: %s", esp_err_to_name(err));
        goto fail_panel;
    }

    s_inited = true;
    ESP_LOGI(TAG, "ILI9341 %dx%d on SPI%d @ %d MHz", RR_W, RR_H, DISP_SPI_HOST,
             DISP_CLK_HZ / 1000000);
    return ESP_OK;

fail_panel:
    esp_lcd_panel_del(s_panel);
    s_panel = NULL;
fail_io:
    esp_lcd_panel_io_del(s_io);
    s_io = NULL;
fail_bus:
    spi_bus_free(DISP_SPI_HOST);
    return err;
}

esp_err_t disp_flush(int y0, int h, const uint16_t *px)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (px == NULL || h <= 0 || y0 < 0 || y0 + h > RR_H) {
        return ESP_ERR_INVALID_ARG;
    }
    /* draw_bitmap's end coordinates are exclusive; it blocks until the
     * transfer completes, which is the synchronous contract we want. */
    return esp_lcd_panel_draw_bitmap(s_panel, 0, y0, RR_W, y0 + h, px);
}

esp_err_t disp_backlight_pct(uint8_t pct)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pct > 100) {
        pct = 100;
    }
    uint32_t duty = (BL_MAX_DUTY * pct) / 100u;
    esp_err_t err = ledc_set_duty(BL_MODE, BL_CHANNEL, duty);
    if (err != ESP_OK) {
        return err;
    }
    return ledc_update_duty(BL_MODE, BL_CHANNEL);
}
