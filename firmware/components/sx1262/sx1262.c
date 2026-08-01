/* sx1262.c — see include/sx1262.h, docs/03-radio-protocol.md.
 *
 * Translation layer only: the radio table from docs/03 is pushed into the
 * vendored ra01s driver, and the DIO1 interrupt path the vendored driver
 * does not provide is built here (ISR -> semaphore -> task -> queue).
 */
#include "sx1262.h"

#include "convoy_pins.h"
#include "ra01s.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <setjmp.h>
#include <string.h>

static const char *TAG = "sx1262";

/* docs/03 §Radio configuration - the whole table lives here, once. */
#define SX_SF 7
#define SX_BW SX126X_LORA_BW_125_0
#define SX_CR SX126X_LORA_CR_4_5
#define SX_PREAMBLE 8
#define SX_TX_DBM 22
#define SX_CRC_ON true
#define SX_INVERT_IRQ false
#define SX_TCXO_V 0.0f  /* E22-900M22S has no DIO3-controlled TCXO */
#define SX_USE_LDO false /* module uses the DC-DC regulator */

#define RX_QUEUE_DEPTH 8
#define RX_TASK_STACK 3072
#define RX_TASK_PRIO 10
#define SPI_MUTEX_WAIT_MS 500

typedef struct {
    uint8_t payload[SX1262_PAYLOAD_LEN];
    int16_t rssi_dbm;
    int8_t snr_db;
} rx_packet_t;

static QueueHandle_t s_rx_queue;
static SemaphoreHandle_t s_dio1_sem;
static SemaphoreHandle_t s_spi_mutex;
static TaskHandle_t s_rx_task;
static bool s_inited;
static uint32_t s_freq_hz;

/* Diagnostics for sx1262_dump_status(). */
static uint32_t s_bad_len_drops; /* on-air length != 32                  */
static uint32_t s_queue_drops;   /* queue full, oldest discarded          */

/*
 * The vendored driver signals unrecoverable states by calling LoRaError(),
 * whose default implementation loops forever. That is exactly the hang
 * docs/01 forbids, so we override the weak symbol and longjmp back to
 * whoever armed s_fault_jmp. Only init arms it: a fault outside init is
 * logged and the driver's own retry/timeout paths take over.
 */
static jmp_buf s_fault_jmp;
static volatile bool s_fault_armed;
static volatile int s_fault_code;

void LoRaError(int error)
{
    s_fault_code = error;
    if (s_fault_armed) {
        s_fault_armed = false;
        longjmp(s_fault_jmp, 1);
    }
    ESP_LOGE(TAG, "radio fault %d outside init", error);
}

static inline bool spi_lock(void)
{
    return xSemaphoreTake(s_spi_mutex, pdMS_TO_TICKS(SPI_MUTEX_WAIT_MS)) ==
           pdTRUE;
}

static inline void spi_unlock(void)
{
    xSemaphoreGive(s_spi_mutex);
}

static void IRAM_ATTR dio1_isr(void *arg)
{
    (void)arg;
    BaseType_t hp_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_dio1_sem, &hp_woken);
    if (hp_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

/* Drains one modem FIFO read into the queue, dropping the oldest entry if
 * the consumer has fallen behind (fresh positions beat stale ones). */
static void push_packet(const uint8_t *buf, int16_t rssi, int8_t snr)
{
    rx_packet_t pkt;
    memcpy(pkt.payload, buf, SX1262_PAYLOAD_LEN);
    pkt.rssi_dbm = rssi;
    pkt.snr_db = snr;

    if (xQueueSend(s_rx_queue, &pkt, 0) != pdTRUE) {
        rx_packet_t discarded;
        (void)xQueueReceive(s_rx_queue, &discarded, 0);
        s_queue_drops++;
        (void)xQueueSend(s_rx_queue, &pkt, 0);
    }
}

static void rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[SX1262_PAYLOAD_LEN];

    for (;;) {
        if (xSemaphoreTake(s_dio1_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!spi_lock()) {
            ESP_LOGW(TAG, "rx: SPI mutex timeout");
            continue;
        }

        uint8_t len = LoRaReceive(buf, (int16_t)sizeof buf);
        if (len == SX1262_PAYLOAD_LEN) {
            int8_t rssi = 0, snr = 0;
            GetPacketStatus(&rssi, &snr);
            spi_unlock();
            push_packet(buf, (int16_t)rssi, snr);
        } else {
            if (len != 0) {
                s_bad_len_drops++; /* not one of ours - docs/03 is 32B */
            }
            spi_unlock();
        }
    }
}

static esp_err_t attach_dio1(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONVOY_PIN_LORA_DIO1,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err; /* INVALID_STATE just means someone installed it first */
    }
    return gpio_isr_handler_add(CONVOY_PIN_LORA_DIO1, dio1_isr, NULL);
}

esp_err_t sx1262_init(uint32_t freq_hz)
{
    esp_err_t err;

    if (s_rx_queue == NULL) {
        s_rx_queue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(rx_packet_t));
        s_dio1_sem = xSemaphoreCreateBinary();
        s_spi_mutex = xSemaphoreCreateMutex();
        if (s_rx_queue == NULL || s_dio1_sem == NULL || s_spi_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!s_inited) {
        err = LoRaInit();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LoRaInit failed: %s", esp_err_to_name(err));
            return err;
        }
        s_inited = true; /* SPI bus is up; keep it across retries */
    }

    /* Hold the SPI mutex across all chip programming: on a retry after a
     * failed init the rx_task may already exist, and it must not drive the
     * bus while we are reprogramming the modem. Taken before setjmp so the
     * fault path below can release it. */
    if (!spi_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    /* Everything below can trip the vendored driver's fatal path. */
    s_fault_code = 0;
    if (setjmp(s_fault_jmp) != 0) {
        s_fault_armed = false;
        spi_unlock();
        ESP_LOGE(TAG, "radio fault %d during init (chip absent or BUSY stuck)",
                 s_fault_code);
        return ESP_ERR_INVALID_RESPONSE;
    }
    s_fault_armed = true;

    if (LoRaBegin(freq_hz, SX_TX_DBM, SX_TCXO_V, SX_USE_LDO) != 0) {
        s_fault_armed = false;
        spi_unlock();
        ESP_LOGE(TAG, "LoRaBegin failed (no SPI answer from the SX1262?)");
        return ESP_ERR_NOT_FOUND;
    }

    LoRaConfig(SX_SF, SX_BW, SX_CR, SX_PREAMBLE, SX1262_PAYLOAD_LEN, SX_CRC_ON,
               SX_INVERT_IRQ);
    /* Private sync word keeps LoRaWAN/Meshtastic traffic out of our RX. */
    SetSyncWord(SX126X_SYNC_WORD_PRIVATE);

    /* Route RX-done (and the errors we want to see) to DIO1. */
    SetDioIrqParams(SX126X_IRQ_ALL,
                    SX126X_IRQ_RX_DONE | SX126X_IRQ_TX_DONE |
                        SX126X_IRQ_TIMEOUT | SX126X_IRQ_CRC_ERR,
                    SX126X_IRQ_NONE, SX126X_IRQ_NONE);

    SetRx(0xFFFFFF); /* continuous RX is the resting state */

    s_fault_armed = false;
    s_freq_hz = freq_hz;
    spi_unlock();

    err = attach_dio1();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DIO1 attach failed: %s", esp_err_to_name(err));
        return err;
    }

    if (s_rx_task == NULL &&
        xTaskCreate(rx_task, "sx1262_rx", RX_TASK_STACK, NULL, RX_TASK_PRIO,
                    &s_rx_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    /* Pick up anything that landed while we were setting up. */
    xSemaphoreGive(s_dio1_sem);

    ESP_LOGI(TAG, "ready: %" PRIu32 " Hz SF%d BW125 CR4:5 +%d dBm", freq_hz,
             SX_SF, SX_TX_DBM);
    return ESP_OK;
}

esp_err_t sx1262_send(const uint8_t payload[SX1262_PAYLOAD_LEN])
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!spi_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    /* LoRaSend takes a non-const pointer but only reads the buffer. */
    uint8_t tx[SX1262_PAYLOAD_LEN];
    memcpy(tx, payload, sizeof tx);

    /* SYNC: blocks until TX-done or the modem's 200 ms timeout, then the
     * vendored driver puts the chip back into continuous RX for us. */
    bool ok = LoRaSend(tx, (int16_t)sizeof tx, SX126x_TXMODE_SYNC);
    spi_unlock();

    if (!ok) {
        ESP_LOGW(TAG, "send timed out");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

bool sx1262_receive(uint8_t out[SX1262_PAYLOAD_LEN], int16_t *rssi_dbm,
                    int8_t *snr_db, uint32_t wait_ms)
{
    if (s_rx_queue == NULL) {
        return false;
    }
    rx_packet_t pkt;
    if (xQueueReceive(s_rx_queue, &pkt, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        return false;
    }
    memcpy(out, pkt.payload, SX1262_PAYLOAD_LEN);
    if (rssi_dbm != NULL) {
        *rssi_dbm = pkt.rssi_dbm;
    }
    if (snr_db != NULL) {
        *snr_db = pkt.snr_db;
    }
    return true;
}

bool sx1262_channel_active(void)
{
    if (!s_inited || !spi_lock()) {
        return false;
    }
    uint16_t irq = GetIrqStatus();
    uint8_t status = GetStatus();
    spi_unlock();

    if (irq & (SX126X_IRQ_PREAMBLE_DETECTED | SX126X_IRQ_HEADER_VALID)) {
        return true;
    }
    /* Status bits 6:4 == 0b101 -> chip mode RX. */
    return (status & 0x70) == 0x50;
}

void sx1262_dump_status(void)
{
    if (!s_inited) {
        ESP_LOGI(TAG, "not initialised");
        return;
    }
    if (!spi_lock()) {
        ESP_LOGW(TAG, "status: SPI mutex timeout");
        return;
    }
    uint8_t status = GetStatus();
    uint16_t irq = GetIrqStatus();
    spi_unlock();

    ESP_LOGI(TAG,
             "freq=%" PRIu32 " Hz status=0x%02x (mode=%d) irq=0x%03x "
             "tx_lost=%d bad_len=%" PRIu32 " qdrop=%" PRIu32,
             s_freq_hz, status, (status >> 4) & 0x7, irq, GetPacketLost(),
             s_bad_len_drops, s_queue_drops);
}
