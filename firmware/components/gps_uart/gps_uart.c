/* gps_uart.c — see include/gps_uart.h, docs/05-gps-geo.md, docs/02. */
#include "gps_uart.h"

#include "convoy_pins.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "gps_uart";

/*
 * docs/02's pin table assigns the GPS to UART1 (T11's interface comment
 * says UART2; the doc wins, and UART0 is the console). The net names are
 * from the ESP32's point of view: GPS_RX is the pin the NEO-6M's TX
 * drives, GPS_TX is the pin driving the NEO-6M's RX.
 */
#define GPS_UART_NUM UART_NUM_1
#define GPS_BAUD 9600
#define GPS_RX_BUF 1024
#define READ_CHUNK 128
#define READ_TIMEOUT_MS 100

#define RAW_LINE_MAX 96 /* NMEA caps at 82; headroom, then discard */

#define READER_PRIO 6
#define READER_CORE 0
#define READER_STACK 4096

static nmea_parser_t s_parser; /* reader task only */
static nmea_fix_t s_fix;       /* published snapshot, mutex-guarded */
static uint32_t s_last_valid_ms;
static bool s_ever_valid;
static uint32_t s_ok, s_bad;

/* Single-writer (reader_task), diagnostic-only: tolerant of a torn read,
 * same convention as radio_stats' counters. Updated on ANY byte, not just
 * a complete sentence - this is "is the module alive", not "is the fix
 * good" (docs/01 GPS-silent handling, T20). */
static volatile uint32_t s_last_byte_ms;
static volatile bool s_ever_byte;

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_reader;
static void (*volatile s_raw_cb)(const char *line);

static char s_raw_line[RAW_LINE_MAX];
static size_t s_raw_len;
static bool s_raw_overflow;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Reassembles complete lines purely for the raw tap — the parser itself is
 * byte-feed and needs no line framing. */
static void raw_feed(char c)
{
    void (*cb)(const char *) = s_raw_cb;
    if (cb == NULL) {
        s_raw_len = 0;
        s_raw_overflow = false;
        return;
    }

    if (c == '\r' || c == '\n') {
        if (s_raw_len > 0 && !s_raw_overflow) {
            s_raw_line[s_raw_len] = '\0';
            cb(s_raw_line);
        }
        s_raw_len = 0;
        s_raw_overflow = false;
        return;
    }
    if (s_raw_len + 1 >= sizeof s_raw_line) {
        s_raw_overflow = true; /* drop the whole overlong line */
        return;
    }
    s_raw_line[s_raw_len++] = c;
}

static void publish(nmea_event_t ev)
{
    const nmea_fix_t *fix = nmea_fix(&s_parser);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_fix = *fix;
    if (ev == NMEA_EV_RMC && fix->valid) {
        s_last_valid_ms = now_ms();
        s_ever_valid = true;
    }
    s_ok++;
    xSemaphoreGive(s_lock);
}

static void reader_task(void *arg)
{
    (void)arg;
    uint8_t buf[READ_CHUNK];

    for (;;) {
        int n = uart_read_bytes(GPS_UART_NUM, buf, sizeof buf,
                                pdMS_TO_TICKS(READ_TIMEOUT_MS));
        if (n <= 0) {
            continue;
        }
        s_last_byte_ms = now_ms();
        s_ever_byte = true;
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            raw_feed(c);

            nmea_event_t ev = nmea_feed(&s_parser, c);
            if (ev == NMEA_EV_RMC || ev == NMEA_EV_GGA) {
                publish(ev);
            } else if (ev == NMEA_EV_BAD) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_bad++;
                xSemaphoreGive(s_lock);
            }
        }
    }
}

esp_err_t gps_uart_start(void)
{
    if (s_reader != NULL) {
        return ESP_OK; /* already running */
    }

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    nmea_init(&s_parser);
    memset(&s_fix, 0, sizeof s_fix);
    s_ever_valid = false;
    s_ever_byte = false;
    s_ok = s_bad = 0;

    const uart_config_t cfg = {
        .baud_rate = GPS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err =
        uart_driver_install(GPS_UART_NUM, GPS_RX_BUF, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install: %s", esp_err_to_name(err));
        return err;
    }
    err = uart_param_config(GPS_UART_NUM, &cfg);
    if (err == ESP_OK) {
        err = uart_set_pin(GPS_UART_NUM, CONVOY_PIN_GPS_TX, CONVOY_PIN_GPS_RX,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart config: %s", esp_err_to_name(err));
        uart_driver_delete(GPS_UART_NUM);
        return err;
    }

    if (xTaskCreatePinnedToCore(reader_task, "gps_rx", READER_STACK, NULL,
                                READER_PRIO, &s_reader,
                                READER_CORE) != pdPASS) {
        uart_driver_delete(GPS_UART_NUM);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UART%d @ %d 8N1, rx=%d tx=%d", GPS_UART_NUM, GPS_BAUD,
             CONVOY_PIN_GPS_RX, CONVOY_PIN_GPS_TX);
    return ESP_OK;
}

bool gps_uart_get_fix(nmea_fix_t *out, uint32_t *age_ms)
{
    if (s_lock == NULL) {
        if (out != NULL) {
            memset(out, 0, sizeof *out);
        }
        if (age_ms != NULL) {
            *age_ms = UINT32_MAX;
        }
        return false;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    nmea_fix_t copy = s_fix;
    bool ever = s_ever_valid;
    uint32_t last = s_last_valid_ms;
    xSemaphoreGive(s_lock);

    if (out != NULL) {
        *out = copy;
    }
    if (age_ms != NULL) {
        /* wrap-safe age (CLAUDE.md): never compare timestamps directly */
        *age_ms = ever ? (uint32_t)(int32_t)(now_ms() - last) : UINT32_MAX;
    }
    return copy.valid;
}

void gps_uart_set_raw_cb(void (*cb)(const char *line))
{
    s_raw_cb = cb;
}

uint32_t gps_uart_idle_ms(void)
{
    if (!s_ever_byte) {
        return UINT32_MAX;
    }
    return (uint32_t)(int32_t)(now_ms() - s_last_byte_ms);
}

void gps_uart_stats(uint32_t *ok, uint32_t *bad)
{
    if (s_lock == NULL) {
        if (ok != NULL) {
            *ok = 0;
        }
        if (bad != NULL) {
            *bad = 0;
        }
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (ok != NULL) {
        *ok = s_ok;
    }
    if (bad != NULL) {
        *bad = s_bad;
    }
    xSemaphoreGive(s_lock);
}
