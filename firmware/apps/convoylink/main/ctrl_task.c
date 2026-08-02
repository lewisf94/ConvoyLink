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
#define AUX_HOLD_MS 2000 /* docs/06: AUX hold = backlight cycle */

typedef struct {
    int pin;
    bool stable;      /* debounced level: true = pressed */
    bool last_raw;
    uint32_t changed_ms;
    uint32_t pressed_ms; /* when the debounced press began   */
    bool hold_fired;     /* hold already reported this press  */
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
    b->pressed_ms = 0;
    b->hold_fired = false;

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
        if (raw) {
            b->pressed_ms = now;
            b->hold_fired = false;
        }
        *pressed = raw;
        return true;
    }
    return false;
}

/* True once, the moment a still-held button passes AUX_HOLD_MS. */
static bool button_hold_elapsed(button_t *b, uint32_t now)
{
    if (!b->stable || b->hold_fired) {
        return false;
    }
    if ((uint32_t)(int32_t)(now - b->pressed_ms) >= AUX_HOLD_MS) {
        b->hold_fired = true;
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
            ctrl_q_send(&ev); /* PTT -> voice_task */
            ESP_LOGD(TAG, "PTT %s", pressed ? "down" : "up");
        }
        /* AUX: short press fires on RELEASE, so a long hold can claim the
         * press instead (docs/06: short = zoom, 2 s hold = backlight). */
        if (button_poll(&aux, now, &pressed)) {
            if (!pressed && !aux.hold_fired) {
                ctrl_event_t ev = {.type = BTN_AUX_PRESS, .t_ms = now};
                ui_q_send(&ev); /* AUX -> ui_task */
                ESP_LOGD(TAG, "AUX short press");
            }
        }
        if (button_hold_elapsed(&aux, now)) {
            ctrl_event_t ev = {.type = BTN_AUX_HOLD, .t_ms = now};
            ui_q_send(&ev); /* AUX -> ui_task */
            ESP_LOGD(TAG, "AUX hold");
        }

        if ((uint32_t)(int32_t)(now - last_beat) >= TASK_HEARTBEAT_MS) {
            ESP_LOGD(TAG, "heartbeat ptt=%d aux=%d", (int)ptt.stable,
                     (int)aux.stable);
            last_beat = now;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
