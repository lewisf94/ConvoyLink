/* radio_stats.c — see radio_stats.h */
#include "radio_stats.h"

#include "app_queues.h"
#include "app_state.h"

#include "convoy_geo.h"
#include "esp_console.h"
#include "esp_timer.h"

#include <inttypes.h>
#include <stdio.h>

static uint32_t s_tx, s_tx_fail, s_beacon_tx, s_beacon_rx, s_ping, s_invalid;
static uint32_t s_relayed, s_suppressed, s_lbt_forced;

void radio_stats_inc_tx(void) { s_tx++; }
void radio_stats_inc_tx_fail(void) { s_tx_fail++; }
void radio_stats_inc_beacon_tx(void) { s_beacon_tx++; }
void radio_stats_inc_beacon_rx(void) { s_beacon_rx++; }
void radio_stats_inc_ping(void) { s_ping++; }
void radio_stats_inc_invalid(void) { s_invalid++; }
void radio_stats_inc_relayed(void) { s_relayed++; }
void radio_stats_inc_suppressed(void) { s_suppressed++; }
void radio_stats_inc_lbt_forced(void) { s_lbt_forced++; }

static int cmd_radiostat(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t tx_dropped, ctrl_dropped;
    queues_dropped(&tx_dropped, &ctrl_dropped);

    printf("tx=%" PRIu32 " tx_fail=%" PRIu32 " beacon_tx=%" PRIu32
           " beacon_rx=%" PRIu32 "\n",
           s_tx, s_tx_fail, s_beacon_tx, s_beacon_rx);
    printf("ping_rx=%" PRIu32 " invalid=%" PRIu32 "\n", s_ping, s_invalid);
    printf("relayed=%" PRIu32 " suppressed=%" PRIu32 " lbt_forced=%" PRIu32
           "\n",
           s_relayed, s_suppressed, s_lbt_forced);
    printf("tx_q dropped=%" PRIu32 "\n", tx_dropped);
    return 0;
}

static const char *tier_name(nt_tier_t t)
{
    switch (t) {
    case NT_LIVE:
        return "LIVE";
    case NT_STALE:
        return "STALE";
    case NT_GHOST:
        return "GHOST";
    default:
        return "GONE";
    }
}

static int cmd_nt(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    convoy_state_t st;
    state_snapshot(&st);
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    nt_entry_t list[CL_MAX_UNITS];
    int n = nt_snapshot(&st.neighbors, list, now);
    if (n == 0) {
        printf("no neighbours\n");
        return 0;
    }

    for (int i = 0; i < n; i++) {
        const nt_entry_t *e = &list[i];
        uint32_t age = (uint32_t)(int32_t)(now - e->last_heard_ms);

        char dist[8] = "?";
        if (st.own_fix.valid && e->last.fix_quality != 0) {
            geo_fmt_dist(geo_dist_m(st.own_fix.lat_e7, st.own_fix.lon_e7,
                                    e->last.lat_e7, e->last.lon_e7),
                         dist);
        } else if (e->last.fix_quality == 0) {
            snprintf(dist, sizeof dist, "no-gps");
        }

        printf("U%u %c%c seq=%-5u age=%-6" PRIu32 "ms %-5s dist=%-7s %s\n",
               e->uid, e->initials[0], e->initials[1], e->last_seq, age,
               tier_name(nt_tier(e, now)), dist,
               e->via_relay ? "via_relay" : "direct");
    }
    return 0;
}

esp_err_t radio_stats_register_console(void)
{
    static const esp_console_cmd_t cmds[] = {
        {.command = "radiostat",
         .help = "LoRa counters: tx/rx/invalid/relayed/suppressed/dropped",
         .func = cmd_radiostat},
        {.command = "nt",
         .help = "Neighbour table dump: seq, age, tier, distance, path",
         .func = cmd_nt},
    };
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        esp_err_t err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
