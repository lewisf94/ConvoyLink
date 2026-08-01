/**
 * radio_stats — the data plane's counters and the `radiostat` / `nt`
 * console commands (T16). Counters are plain uint32_t incremented only
 * from radio_task and gps_task; readers tolerate a torn value, which is
 * fine for diagnostics.
 */
#ifndef RADIO_STATS_H
#define RADIO_STATS_H

#include "esp_err.h"

#include <stdint.h>

void radio_stats_inc_tx(void);
void radio_stats_inc_tx_fail(void);
void radio_stats_inc_beacon_tx(void);
void radio_stats_inc_beacon_rx(void);
void radio_stats_inc_ping(void);
void radio_stats_inc_invalid(void);
void radio_stats_inc_relayed(void);
void radio_stats_inc_suppressed(void);
void radio_stats_inc_lbt_forced(void);

/** Registers `radiostat` and `nt`. */
esp_err_t radio_stats_register_console(void);

#endif /* RADIO_STATS_H */
