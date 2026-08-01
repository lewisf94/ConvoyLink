/**
 * app_queues — the two inter-task queues from docs/01 §Shared state &
 * queues. Both are fixed-depth and **drop-oldest** on overflow: a stale
 * position or a stale button press is worth less than a fresh one, and a
 * blocking send would stall the producer.
 *
 * Each queue keeps a dropped-counter so a backlog shows up in diagnostics
 * instead of silently vanishing.
 */
#ifndef APP_QUEUES_H
#define APP_QUEUES_H

#include "convoy_proto.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define TX_Q_DEPTH 8
#define CTRL_Q_DEPTH 8

/** One ready-to-send 32-byte LoRa payload (beacon or relay copy). */
typedef struct {
    uint8_t payload[CL_PKT_SIZE];
} tx_item_t;

typedef enum {
    BTN_PTT_DOWN = 0,
    BTN_PTT_UP,
    BTN_AUX_PRESS, /* short press -> cycle zoom mode      */
    BTN_AUX_HOLD,  /* >= 2 s hold -> cycle backlight level */
} ctrl_event_type_t;

typedef struct {
    ctrl_event_type_t type;
    uint32_t t_ms;
} ctrl_event_t;

/** Creates both queues. Call once, before any task starts. */
esp_err_t queues_init(void);

/** Non-blocking; evicts the oldest item if full. Always succeeds. */
void tx_q_send(const tx_item_t *item);
bool tx_q_recv(tx_item_t *out, uint32_t wait_ms);

void ctrl_q_send(const ctrl_event_t *ev);
bool ctrl_q_recv(ctrl_event_t *out, uint32_t wait_ms);

/** How many items each queue has evicted since boot. */
void queues_dropped(uint32_t *tx_dropped, uint32_t *ctrl_dropped);

#endif /* APP_QUEUES_H */
