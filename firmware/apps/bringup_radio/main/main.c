/* bringup_radio — first firmware app: prove the SX1262 driver, wiring and
 * power on real hardware, and give the owner a walk-around range tester
 * with real RSSI/SNR (tasks/T10-bringup-radio.md, docs/03 §CL_TYPE_PING).
 *
 * esp_console REPL on UART0. Identity (`id`) and `region` live in RAM
 * only — NVS provisioning is T15's job, not this bench tool's.
 */
#include "convoy_cfg.h"
#include "convoy_proto.h"
#include "sx1262.h"

#include "esp_console.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */

static const char *TAG = "bringup_radio";

/*
 * Duty budget (docs/03 §Airtime & duty budget + §Regulatory quick
 * reference). One 32-byte packet at SF7/125k/4:5 is ~61 ms on air, and the
 * EU 869.40-869.65 sub-band allows 10 % duty, so the fastest compliant
 * sustained rate is 61/0.10 = 610 ms between transmissions (~1.64/s).
 *
 * T10 asks for a 2/s default, which is 12.2 % — over that limit — while
 * also requiring the duty budget be respected in test mode. The doc wins
 * (docs are law), so in region EU the rate is clamped to the compliant
 * maximum and the clamp is printed. US/AU are 902-928 MHz ISM / LIPD with
 * no duty limit in the same table, so no clamp is applied there.
 */
#define PING_AIRTIME_MS 61
#define EU_DUTY_PERCENT 10
#define EU_MIN_TX_INTERVAL_MS ((PING_AIRTIME_MS * 100) / EU_DUTY_PERCENT)

#define PING_DEFAULT_RATE_HZ 2.0f
#define PING_MAX_RATE_HZ 10.0f

#define LOSS_WINDOW_MS 30000u
#define RX_RING 256 /* 30 s at the EU max rate needs ~50; ample headroom */

/* ---- state (bench tool: single console task mutates, tasks read) ------- */

static uint8_t s_id;
static uint32_t s_freq_hz = CL_LORA_FREQ_EU_HZ;
static char s_region[4] = "EU";
static bool s_duty_limited = true;

static volatile bool s_pinging;
static volatile bool s_mon;
static volatile uint32_t s_tx_interval_ms = 500;

static uint16_t s_tx_seq;
static uint32_t s_tx_count;
static uint32_t s_invalid_count; /* failed cl_validate — interference or
                                  * someone else's LoRa gear on our sync word */
static uint32_t s_nonping_count; /* valid, but not a CL_TYPE_PING */

/* Per-sender arrival history, for the 30 s sliding-window loss estimate. */
typedef struct {
    uint16_t seq[RX_RING];
    uint32_t t_ms[RX_RING];
    int head;
    int count;
} rx_hist_t;

static rx_hist_t s_hist[CL_MAX_UNITS];

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void hist_push(uint8_t uid, uint16_t seq, uint32_t t)
{
    if (uid >= CL_MAX_UNITS) {
        return;
    }
    rx_hist_t *h = &s_hist[uid];
    h->seq[h->head] = seq;
    h->t_ms[h->head] = t;
    h->head = (h->head + 1) % RX_RING;
    if (h->count < RX_RING) {
        h->count++;
    }
}

/*
 * Loss over the last LOSS_WINDOW_MS: expected = the wrap-safe seq span
 * between the oldest and newest arrival still inside the window, and
 * anything in that span we never saw is loss. Returns -1 with fewer than
 * two samples, where a rate is meaningless.
 */
static int hist_loss_pct(uint8_t uid, uint32_t t_now)
{
    if (uid >= CL_MAX_UNITS) {
        return -1;
    }
    const rx_hist_t *h = &s_hist[uid];
    uint16_t oldest = 0, newest = 0;
    uint32_t in_window = 0;

    for (int i = 0; i < h->count; i++) {
        int idx = (h->head - 1 - i + 2 * RX_RING) % RX_RING;
        if ((uint32_t)(int32_t)(t_now - h->t_ms[idx]) > LOSS_WINDOW_MS) {
            break; /* walking backwards: everything older is out too */
        }
        if (in_window == 0) {
            newest = h->seq[idx];
        }
        oldest = h->seq[idx];
        in_window++;
    }

    if (in_window < 2) {
        return -1;
    }
    uint32_t expected = (uint32_t)(uint16_t)(newest - oldest) + 1u;
    if (expected <= in_window) {
        return 0; /* duplicates/reordering — never report negative loss */
    }
    return (int)(((expected - in_window) * 100u) / expected);
}

/* ---- radio tasks -------------------------------------------------------- */

static void ping_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!s_pinging) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        cl_ping_t p;
        cl_make_ping(&p, s_id, s_tx_seq++, now_ms());

        esp_err_t err = sx1262_send((const uint8_t *)&p);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "tx seq=%u failed: %s", (unsigned)(s_tx_seq - 1),
                     esp_err_to_name(err));
        } else {
            s_tx_count++;
        }
        vTaskDelay(pdMS_TO_TICKS(s_tx_interval_ms));
    }
}

static void rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[CL_PKT_SIZE];
    int16_t rssi;
    int8_t snr;

    for (;;) {
        if (!sx1262_receive(buf, &rssi, &snr, 500)) {
            continue;
        }

        int type = cl_validate(buf, sizeof buf);
        if (type < 0) {
            s_invalid_count++;
            if (s_mon) {
                printf("invalid packet (cl_validate=%d) rssi=%ddBm — "
                       "interference or a sync-word clash\n",
                       type, rssi);
            }
            continue;
        }
        if (type != CL_TYPE_PING) {
            s_nonping_count++;
            continue;
        }

        const cl_ping_t *p = (const cl_ping_t *)buf;
        uint32_t t = now_ms();
        hist_push(p->hdr.sender, p->seq, t);

        if (s_mon) {
            int loss = hist_loss_pct(p->hdr.sender, t);
            printf("seq=%u rssi=%ddBm snr=%ddB from=U%u loss%%=",
                   (unsigned)p->seq, rssi, snr, (unsigned)p->hdr.sender);
            if (loss < 0) {
                printf("--\n");
            } else {
                printf("%d\n", loss);
            }
        }
    }
}

/* ---- console commands --------------------------------------------------- */

static int cmd_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sx1262_dump_status();
    printf("id=U%u region=%s freq=%" PRIu32 " Hz pinging=%s mon=%s\n", s_id,
           s_region, s_freq_hz, s_pinging ? "yes" : "no",
           s_mon ? "on" : "off");
    printf("tx=%" PRIu32 " interval=%" PRIu32 " ms  invalid_rx=%" PRIu32
           " non_ping_rx=%" PRIu32 "\n",
           s_tx_count, s_tx_interval_ms, s_invalid_count, s_nonping_count);
    return 0;
}

static int cmd_region(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: region <EU|US|AU>\n");
        return 1;
    }
    uint32_t freq;
    bool duty_limited;
    if (strcasecmp(argv[1], "EU") == 0) {
        freq = CL_LORA_FREQ_EU_HZ;
        duty_limited = true;
    } else if (strcasecmp(argv[1], "US") == 0 ||
               strcasecmp(argv[1], "AU") == 0) {
        freq = CL_LORA_FREQ_US_HZ;
        duty_limited = false;
    } else {
        printf("unknown region '%s' (EU|US|AU)\n", argv[1]);
        return 1;
    }

    esp_err_t err = sx1262_init(freq);
    if (err != ESP_OK) {
        printf("region change failed: %s (radio left as it was)\n",
               esp_err_to_name(err));
        return 1;
    }
    s_freq_hz = freq;
    s_duty_limited = duty_limited;
    snprintf(s_region, sizeof s_region, "%s", argv[1]);
    printf("region=%s freq=%" PRIu32 " Hz duty_limit=%s\n", s_region, s_freq_hz,
           s_duty_limited ? "10% (EU)" : "none");
    printf("both units must agree on region — a mismatch looks exactly like "
           "'no range'\n");
    return 0;
}

static int cmd_id(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: id <0-%d>\n", CL_MAX_UNITS - 1);
        return 1;
    }
    int v = atoi(argv[1]);
    if (v < 0 || v >= CL_MAX_UNITS) {
        printf("id must be 0..%d\n", CL_MAX_UNITS - 1);
        return 1;
    }
    s_id = (uint8_t)v;
    printf("id=U%u\n", s_id);
    return 0;
}

static int cmd_ping(int argc, char **argv)
{
    float rate = PING_DEFAULT_RATE_HZ;
    if (argc == 2) {
        rate = strtof(argv[1], NULL);
    } else if (argc > 2) {
        printf("usage: ping [rate_hz]\n");
        return 1;
    }
    if (!(rate > 0.0f) || rate > PING_MAX_RATE_HZ) {
        printf("rate_hz must be >0 and <=%.0f\n", (double)PING_MAX_RATE_HZ);
        return 1;
    }

    uint32_t interval = (uint32_t)(1000.0f / rate + 0.5f);
    if (s_duty_limited && interval < EU_MIN_TX_INTERVAL_MS) {
        printf("duty budget: %.2f/s is %.1f%% duty at %d ms airtime; EU allows "
               "%d%% — clamping to %d ms (%.2f/s). docs/03.\n",
               (double)rate, (double)(rate * PING_AIRTIME_MS / 10.0f),
               PING_AIRTIME_MS, EU_DUTY_PERCENT, EU_MIN_TX_INTERVAL_MS,
               1000.0 / EU_MIN_TX_INTERVAL_MS);
        interval = EU_MIN_TX_INTERVAL_MS;
    }

    s_tx_interval_ms = interval;
    s_pinging = true;
    printf("pinging as U%u every %" PRIu32 " ms\n", s_id, s_tx_interval_ms);
    return 0;
}

static int cmd_stop(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    s_pinging = false;
    printf("stopped after %" PRIu32 " packets\n", s_tx_count);
    return 0;
}

static int cmd_mon(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    s_mon = !s_mon;
    printf("monitor %s\n", s_mon ? "on" : "off");
    return 0;
}

static void register_commands(void)
{
    static const esp_console_cmd_t cmds[] = {
        {.command = "status",
         .help = "Radio status, identity and counters",
         .func = cmd_status},
        {.command = "region",
         .help = "region <EU|US|AU> — set frequency (RAM only)",
         .func = cmd_region},
        {.command = "id",
         .help = "id <0-4> — set this unit's sender id (RAM only)",
         .func = cmd_id},
        {.command = "ping",
         .help = "ping [rate_hz] — start sending pings (default 2/s, "
                 "duty-clamped in EU)",
         .func = cmd_ping},
        {.command = "stop", .help = "Stop pinging", .func = cmd_stop},
        {.command = "mon",
         .help = "Toggle per-packet RX monitor (seq/rssi/snr/loss)",
         .func = cmd_mon},
    };
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}

void app_main(void)
{
    esp_err_t err = sx1262_init(s_freq_hz);
    if (err != ESP_OK) {
        /* docs/01: fail soft and keep the console usable so the owner can
         * run `status` and see why. */
        ESP_LOGE(TAG, "RADIO? sx1262_init failed: %s — check wiring/power "
                      "(docs/07 §Troubleshooting)",
                 esp_err_to_name(err));
    }

    xTaskCreate(ping_task, "ping", 4096, NULL, 5, NULL);
    xTaskCreate(rx_task, "rx", 4096, NULL, 6, NULL);

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "convoy>";
    repl_cfg.max_cmdline_length = 128;

    esp_console_dev_uart_config_t uart_cfg =
        ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(
        esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));

    ESP_ERROR_CHECK(esp_console_register_help_command());
    register_commands();

    printf("\nConvoyLink bringup_radio — 'help' for commands, "
           "'status' first.\n");
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
