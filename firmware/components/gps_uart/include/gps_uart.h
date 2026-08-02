/**
 * gps_uart — UART plumbing from the GY-NEO6MV2 into the pure-C `nmea`
 * parser, exposing a thread-safe "latest fix + freshness" snapshot
 * (docs/05-gps-geo.md §GPS module, docs/02 §GPS wiring).
 *
 * The module runs at its factory defaults — 9600 8N1, NMEA at 1 Hz, no
 * UBX configuration (docs/05). Only RMC and GGA are consumed; everything
 * else the module emits is parsed and ignored.
 *
 * Concurrency: `gps_uart_start` once from startup. Afterwards the getters
 * are safe from any task — the reader task owns the parser and publishes
 * snapshots under a mutex.
 */
#ifndef GPS_UART_H
#define GPS_UART_H

#include "esp_err.h"
#include "nmea.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * Starts the GPS UART (9600 8N1 on the CONVOY_PIN_GPS_* pins) and the
 * internal reader task (priority 6, pinned to core 0) that feeds every
 * received byte to the nmea parser. Idempotent: a second call returns
 * ESP_OK without starting a second reader.
 */
esp_err_t gps_uart_start(void);

/**
 * Copy the latest merged fix. `*age_ms` is the time since the last RMC
 * with status 'A', or UINT32_MAX if there has never been one — note that
 * is the age of the last *valid* fix, so it keeps growing if the module
 * drops to no-fix while still emitting sentences. Either pointer may be
 * NULL. Returns the copied fix's `valid` flag for convenience.
 */
bool gps_uart_get_fix(nmea_fix_t *out, uint32_t *age_ms);

/**
 * Optional raw tap for the bring-up app: `cb` receives each complete
 * NMEA line, NUL-terminated and stripped of CR/LF. NULL disables it.
 * Called from the reader task — keep it fast and non-blocking.
 */
void gps_uart_set_raw_cb(void (*cb)(const char *line));

/** Sentence counters: recognised-and-merged, and failed-checksum. */
void gps_uart_stats(uint32_t *ok, uint32_t *bad);

/**
 * Milliseconds since the last byte of any kind arrived on the GPS UART
 * (not just a complete or valid sentence), or UINT32_MAX if never. This is
 * "is the module talking at all" — distinct from `*age_ms` out of
 * gps_uart_get_fix, which is "when did we last have a *valid* fix" and
 * can grow for entirely normal reasons (driving into a tunnel). A large
 * idle time here means the module or wiring itself has gone away (T20).
 */
uint32_t gps_uart_idle_ms(void);

#endif /* GPS_UART_H */
