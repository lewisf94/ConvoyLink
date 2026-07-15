# T09 — `sx1262` LoRa driver + `convoy_pins`

*(v2: this task originally targeted the NRF24L01+; the SX1262 replaced it —
`docs/00` decision log. Filename kept for queue stability.)*

**Depends:** none · **Phase:** M3 (ESP-IDF)
**Required reading:** `docs/03-radio-protocol.md` §Radio configuration +
§Driver notes; `docs/02-hardware.md` §Pin map + §SX1262 wiring

## Goal

The LoRa data radio behind a small single-owner API, plus the shared
pin-map header every app uses.

## Deliverables

- `firmware/components/convoy_pins/include/convoy_pins.h` — header-only
  component: `#define CONVOY_PIN_<NET> <gpio>` for **every** row of the
  docs/02 pin table, plus `CMakeLists.txt`
  (`idf_component_register(INCLUDE_DIRS "include")`).
- `firmware/components/sx1262/` — the driver component:
  - `vendor/` — the MIT-licensed core of
    <https://github.com/nopnop2002/esp-idf-sx126x> (its `ra01s.c/.h`
    driver files), copied verbatim with licence header intact and a
    `NOTICE` file recording origin + commit. This is the task's one
    allowed external dependency.
  - `include/sx1262.h` + `sx1262.c` — our wrapper (below), the only API
    the rest of the firmware may use.
  - `CMakeLists.txt` (REQUIRES driver, convoy_pins)

## Interface contract (`sx1262.h`)

```c
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/* Configures SPI + control pins from convoy_pins.h and programs the full
 * docs/03 table: freq_hz (region-selected by the caller from convoy_cfg),
 * SF7/BW125/CR4:5, preamble 8, private sync word, +22 dBm, CRC on,
 * fixed 32-byte payloads. Drives the E22's TXEN/RXEN switch pins around
 * every mode change. Ends in continuous-RX mode. */
esp_err_t sx1262_init(uint32_t freq_hz);

/* Blocking send of one 32-byte payload (~61 ms + margin, 200 ms timeout).
 * Returns to continuous RX afterwards. */
esp_err_t sx1262_send(const uint8_t payload[32]);

/* Pop one received packet from the driver's internal queue (depth 8,
 * drop-oldest), filled by the DIO1 interrupt path. rssi/snr are the
 * per-packet values from the modem; either pointer may be NULL. */
bool sx1262_receive(uint8_t out[32], int16_t *rssi_dbm, int8_t *snr_db,
                    uint32_t wait_ms);

/* Cheap listen-before-talk hint: true if a reception is in progress
 * (preamble/header detected or modem busy in RX). */
bool sx1262_channel_active(void);

void sx1262_dump_status(void); /* ESP_LOGI: chip mode, errors, freq */
```

Concurrency contract (document in the header): `sx1262_init` once from
startup; afterwards **only `radio_task`** (or the bring-up app's main
loop) may call send/receive. The DIO1 ISR does no SPI — it releases a
semaphore; a small internal task reads the FIFO and pushes to the queue.

## Implementation notes

- Wrapper only translates; keep all register-level logic in the vendored
  files. If the vendored driver needs a patch, make it, and record it in
  the NOTICE file.
- Packets shorter/longer than 32 bytes on air: drop at the wrapper, count
  (`sx1262_dump_status` prints the counter).
- Init failure (chip absent, BUSY stuck) must return an error, not hang —
  the app retries every 5 s and shows `RADIO?` (docs/01).

## Acceptance — CI

`./tools/ci_build_apps.sh` green (component builds as part of T10's app;
until T10 exists it must compile standalone in the container).

## Acceptance — hardware

Deferred to T10's checklist.

## Out of scope

Protocol logic (validate/relay/queues — T16), FSK mode, LoRaWAN, duty-cycle
accounting (the beacon design already guarantees it — docs/03).
