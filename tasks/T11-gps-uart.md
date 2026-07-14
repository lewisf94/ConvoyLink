# T11 — `gps_uart` component + `bringup_gps` app

**Depends:** T03 · **Phase:** M3
**Required reading:** `docs/05-gps-geo.md` §GPS module; `docs/02` §GPS wiring

## Goal

UART plumbing from the NEO-6M into the `nmea` parser, exposing a
thread-safe "latest fix + freshness" API, plus the bring-up app that
proves the module and antenna.

## Deliverables

- `firmware/components/gps_uart/include/gps_uart.h`
- `firmware/components/gps_uart/gps_uart.c`
- `firmware/components/gps_uart/CMakeLists.txt` (REQUIRES driver, nmea, convoy_pins)
- `firmware/apps/bringup_gps/…` (copy bringup_radio's project template)

## Interface contract

```c
#include "esp_err.h"
#include "nmea.h"

/* Starts UART2 @ 9600 8N1 on CONVOY_PIN_GPS_* and an internal reader task
 * (prio 6, core 0) that feeds every byte to the nmea parser. */
esp_err_t gps_uart_start(void);

/* Copy the latest merged fix. *age_ms = ms since the last RMC with
 * status 'A' (UINT32_MAX if never). Returns fix->valid for convenience.
 * Thread-safe (mutex-guarded copy). */
bool gps_uart_get_fix(nmea_fix_t *out, uint32_t *age_ms);

/* Optional raw tap for the bring-up app: NULL disables. Called from the
 * reader task — keep it fast. */
void gps_uart_set_raw_cb(void (*cb)(const char *line));

/* Counters for diagnostics: sentences ok / bad checksum. */
void gps_uart_stats(uint32_t *ok, uint32_t *bad);
```

## `bringup_gps` behaviour

Console commands: `raw on|off` (echo NMEA lines), `fix` (one-shot print),
plus an automatic 1 Hz status line:
`fix=Y sats=9 hdop=1.2 lat=51.5074123 lon=-0.1278456 spd=0.0m/s crs=--- age=0.3s ok=142 bad=0`
(lat/lon printed from the e7 integers — 7 decimals, no float parsing).

## Acceptance — CI

`./tools/ci_build_apps.sh` green.

## Acceptance — hardware (owner checklist)

- [ ] `raw on` shows `$G…` sentences within 2 s of boot (silence = wiring;
      garbage chars = baud/level problem)
- [ ] At a window/outdoors: fix acquired (cold start may take minutes —
      leave it 5 min before suspecting hardware)
- [ ] lat/lon match your phone's position to ~4 decimal places
- [ ] `bad` counter stays ≈ 0 over 10 min (rising = wiring noise)
- [ ] Drive test on the bench supply optional; note time-to-fix after a
      power cycle (warm start) in STATUS.md

## Out of scope

Beaconing (T16), UBX rate configuration, other sentence types.
