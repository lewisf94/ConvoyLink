/* ctrl_task — PTT/AUX buttons into ctrl_q (docs/01, 50 ms debounce).
 *
 * Polled rather than interrupt-driven: at a 50 ms debounce window an ISR
 * buys nothing, and polling keeps the debounce logic in one readable
 * place. Buttons are active-low with internal pull-ups (docs/02). */
#include "app_queues.h"
#include "app_tasks.h"

#include "convoy_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ctrl_task";

#define DEBOUNCE_MS 50
#define POLL_MS 10

typedef struct {
    int pin;
    bool stable;      /* debounced level: true = pressed */
    bool last_raw;
    uint32_t changed_ms;
} button_t;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void button_init(button_t *b, int pin)
{
    b->pin = pin;
    b->stable = false;
    b->last_raw = false;
    b->changed_ms = 0;

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

/* Returns true when the debounced level changed, with the new level in
 * *pressed. */
static bool button_poll(button_t *b, uint32_t now, bool *pressed)
{
    bool raw = (gpio_get_level(b->pin) == 0); /* active low */
    if (raw != b->last_raw) {
        b->last_raw = raw;
        b->changed_ms = now;
        return false;
    }
    if (raw != b->stable &&
        (uint32_t)(int32_t)(now - b->changed_ms) >= DEBOUNCE_MS) {
        b->stable = raw;
        *pressed = raw;
        return true;
    }
    return false;
}

void ctrl_task(void *arg)
{
    (void)arg;
    button_t ptt, aux;
    button_init(&ptt, CONVOY_PIN_BTN_PTT);
    button_init(&aux, CONVOY_PIN_BTN_AUX);

    uint32_t last_beat = 0;

    for (;;) {
        uint32_t now = now_ms();
        bool pressed;

        if (button_poll(&ptt, now, &pressed)) {
            ctrl_event_t ev = {
                .type = pressed ? BTN_PTT_DOWN : BTN_PTT_UP,
                .t_ms = now,
            };
            ctrl_q_send(&ev);
            ESP_LOGD(TAG, "PTT %s", pressed ? "down" : "up");
        }
        if (button_poll(&aux, now, &pressed) && pressed) {
            ctrl_event_t ev = {.type = BTN_AUX_PRESS, .t_ms = now};
            ctrl_q_send(&ev);
            ESP_LOGD(TAG, "AUX press");
        }

        if ((uint32_t)(int32_t)(now - last_beat) >= TASK_HEARTBEAT_MS) {
            ESP_LOGD(TAG, "heartbeat ptt=%d aux=%d", (int)ptt.stable,
                     (int)aux.stable);
            last_beat = now;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
