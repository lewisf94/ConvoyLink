/* vt_espnow.c — the default voice transport (docs/04 §Transport A).
 *
 * The softened "no Wi-Fi" invariant (docs/04, CLAUDE.md) is the thing to
 * be careful about here: Wi-Fi is brought up ONLY for ESP-NOW, in STA
 * mode on a fixed channel. This file never calls esp_wifi_connect, never
 * starts an AP, and never scans. If you are tempted to add any of those,
 * the invariant says don't.
 */
#include "voice_transport.h"

#include "convoy_cfg.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "voice_proto.h"

#include <string.h>

static const char *TAG = "vt_espnow";

/* Enough to cover the jitter buffer without hoarding: voice frames are
 * worthless once they are older than the burst they belong to. */
#define RX_QUEUE_DEPTH CL_JITTER_SLOTS

typedef struct {
    uint8_t data[VF_FRAME_MAX];
    uint8_t len;
} rx_frame_t;

static const uint8_t BROADCAST[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF,
                                                    0xFF, 0xFF, 0xFF};

static QueueHandle_t s_rx_q;
static bool s_inited;
static volatile uint32_t s_last_rx_ms;
static uint32_t s_sent, s_recvd, s_dropped;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ESP-NOW RX callback: runs in the Wi-Fi task, so it does no work beyond
 * a bounded copy and a non-blocking queue push. */
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data,
                    int len)
{
    (void)info;
    if (len <= 0 || (size_t)len > VF_FRAME_MAX) {
        return;
    }
    rx_frame_t f;
    memcpy(f.data, data, (size_t)len);
    f.len = (uint8_t)len;

    if (xQueueSend(s_rx_q, &f, 0) != pdTRUE) {
        /* Drop-oldest: a stale voice frame is worth less than a fresh one
         * and blocking here would stall the Wi-Fi task. */
        rx_frame_t discard;
        if (xQueueReceive(s_rx_q, &discard, 0) == pdTRUE) {
            s_dropped++;
        }
        (void)xQueueSend(s_rx_q, &f, 0);
    }
    s_last_rx_ms = now_ms();
    s_recvd++;
}

static esp_err_t espnow_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    if (s_rx_q == NULL) {
        s_rx_q = xQueueCreate(RX_QUEUE_DEPTH, sizeof(rx_frame_t));
        if (s_rx_q == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    /* esp_netif/event loop are prerequisites of esp_wifi_init even though
     * we never attach an interface to anything. */
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
        return err;
    }
    /* No NVS-backed config, no persistence: we never join anything. */
    (void)esp_wifi_set_storage(WIFI_STORAGE_RAM);

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi start: %s", esp_err_to_name(err));
        esp_wifi_deinit();
        return err;
    }

    /* Fixed channel — no scanning, so every unit must agree (docs/04). */
    err = esp_wifi_set_channel(CL_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_channel: %s", esp_err_to_name(err));
    }

    err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init: %s", esp_err_to_name(err));
        esp_wifi_stop();
        esp_wifi_deinit();
        return err;
    }
    err = esp_now_register_recv_cb(on_recv);
    if (err != ESP_OK) {
        esp_now_deinit();
        esp_wifi_stop();
        esp_wifi_deinit();
        return err;
    }

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, BROADCAST, ESP_NOW_ETH_ALEN);
    peer.channel = CL_ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG, "add_peer: %s", esp_err_to_name(err));
        esp_now_deinit();
        esp_wifi_stop();
        esp_wifi_deinit();
        return err;
    }

    s_inited = true;
    ESP_LOGI(TAG, "ESP-NOW voice up on channel %d (STA, broadcast, never "
                  "associates)",
             CL_ESPNOW_CHANNEL);
    return ESP_OK;
}

static esp_err_t espnow_deinit(void)
{
    if (!s_inited) {
        return ESP_OK;
    }
    esp_now_del_peer(BROADCAST);
    esp_now_deinit();
    esp_wifi_stop();
    esp_wifi_deinit();
    s_inited = false;
    return ESP_OK;
}

static esp_err_t espnow_send(const uint8_t *frame, size_t len)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len == 0 || len > ESP_NOW_MAX_DATA_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = esp_now_send(BROADCAST, frame, len);
    if (err == ESP_OK) {
        s_sent++;
    }
    return err;
}

static int espnow_recv(uint8_t *frame, size_t max, uint32_t wait_ms)
{
    if (!s_inited || s_rx_q == NULL) {
        return 0;
    }
    rx_frame_t f;
    if (xQueueReceive(s_rx_q, &f, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        return 0;
    }
    if (f.len > max) {
        return 0;
    }
    memcpy(frame, f.data, f.len);
    return f.len;
}

static bool espnow_busy(void)
{
    if (!s_inited || s_last_rx_ms == 0) {
        return false;
    }
    return (uint32_t)(int32_t)(now_ms() - s_last_rx_ms) < CL_VOICE_HANGOVER_MS;
}

static const voice_transport_t s_espnow = {
    .name = "espnow",
    .init = espnow_init,
    .deinit = espnow_deinit,
    .send = espnow_send,
    .recv = espnow_recv,
    .busy = espnow_busy,
};

const voice_transport_t *voice_transport_get(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    if (strcmp(name, "espnow") == 0) {
        return &s_espnow;
    }
    if (strcmp(name, "sx1262") == 0) {
        /* T22. Selecting it must not silently fall back to ESP-NOW: the
         * two cannot hear each other, so a quiet downgrade would look
         * exactly like a broken radio to the owner. */
        return NULL;
    }
    return NULL;
}

void voice_transport_stats(uint32_t *sent, uint32_t *recvd, uint32_t *dropped)
{
    if (sent != NULL) {
        *sent = s_sent;
    }
    if (recvd != NULL) {
        *recvd = s_recvd;
    }
    if (dropped != NULL) {
        *dropped = s_dropped;
    }
}
