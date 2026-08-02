/* app_queues.c — see app_queues.h */
#include "app_queues.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static QueueHandle_t s_tx_q;
static QueueHandle_t s_ctrl_q;
static QueueHandle_t s_ui_q;
static uint32_t s_tx_dropped;
static uint32_t s_ctrl_dropped;
static uint32_t s_ui_dropped;

esp_err_t queues_init(void)
{
    if (s_tx_q == NULL) {
        s_tx_q = xQueueCreate(TX_Q_DEPTH, sizeof(tx_item_t));
    }
    if (s_ctrl_q == NULL) {
        s_ctrl_q = xQueueCreate(CTRL_Q_DEPTH, sizeof(ctrl_event_t));
    }
    if (s_ui_q == NULL) {
        s_ui_q = xQueueCreate(CTRL_Q_DEPTH, sizeof(ctrl_event_t));
    }
    if (s_tx_q == NULL || s_ctrl_q == NULL || s_ui_q == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* Drop-oldest: evict one, then retry. The retry cannot fail in practice —
 * we just freed a slot — but a concurrent producer could take it, so the
 * result is still discarded rather than asserted on. */
static void send_drop_oldest(QueueHandle_t q, const void *item, size_t size,
                             uint32_t *dropped)
{
    if (xQueueSend(q, item, 0) == pdTRUE) {
        return;
    }
    uint8_t scratch[sizeof(tx_item_t) > sizeof(ctrl_event_t)
                        ? sizeof(tx_item_t)
                        : sizeof(ctrl_event_t)];
    (void)size;
    if (xQueueReceive(q, scratch, 0) == pdTRUE) {
        (*dropped)++;
    }
    (void)xQueueSend(q, item, 0);
}

void tx_q_send(const tx_item_t *item)
{
    send_drop_oldest(s_tx_q, item, sizeof *item, &s_tx_dropped);
}

bool tx_q_recv(tx_item_t *out, uint32_t wait_ms)
{
    return xQueueReceive(s_tx_q, out, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
}

void ctrl_q_send(const ctrl_event_t *ev)
{
    send_drop_oldest(s_ctrl_q, ev, sizeof *ev, &s_ctrl_dropped);
}

bool ctrl_q_recv(ctrl_event_t *out, uint32_t wait_ms)
{
    return xQueueReceive(s_ctrl_q, out, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
}

void ui_q_send(const ctrl_event_t *ev)
{
    send_drop_oldest(s_ui_q, ev, sizeof *ev, &s_ui_dropped);
}

bool ui_q_recv(ctrl_event_t *out, uint32_t wait_ms)
{
    return xQueueReceive(s_ui_q, out, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
}

void queues_dropped(uint32_t *tx_dropped, uint32_t *ctrl_dropped,
                    uint32_t *ui_dropped)
{
    if (tx_dropped != NULL) {
        *tx_dropped = s_tx_dropped;
    }
    if (ctrl_dropped != NULL) {
        *ctrl_dropped = s_ctrl_dropped;
    }
    if (ui_dropped != NULL) {
        *ui_dropped = s_ui_dropped;
    }
}
