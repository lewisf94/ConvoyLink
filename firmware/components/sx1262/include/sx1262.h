/**
 * sx1262 — the LoRa data radio (EBYTE E22-900M22S) behind a small
 * single-owner API. Programs the full `docs/03-radio-protocol.md` radio
 * table and drives the E22's TXEN/RXEN RF switch around every mode change.
 *
 * This wrapper is the **only** SX1262 API the rest of the firmware may
 * use; the register-level work lives in `vendor/ra01s.c` (MIT, see
 * `vendor/NOTICE` for origin and our patches).
 *
 * Concurrency contract:
 *   - `sx1262_init` exactly once, from startup, before any other call.
 *   - Afterwards **only `radio_task`** (or a bring-up app's main loop) may
 *     call send/receive/channel_active — they are not safe to call from
 *     two tasks at once by design, not by accident.
 *   - The DIO1 ISR does no SPI: it gives a semaphore, and a small internal
 *     task drains the modem FIFO into the packet queue. That internal task
 *     and the caller are serialised on an internal SPI mutex, so a
 *     `sx1262_send` never interleaves with a FIFO read.
 */
#ifndef SX1262_H
#define SX1262_H

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/** Every on-air ConvoyLink packet is exactly this size (docs/03). */
#define SX1262_PAYLOAD_LEN 32

/**
 * Configures SPI + control pins from convoy_pins.h and programs the full
 * docs/03 table: freq_hz (region-selected by the caller from convoy_cfg),
 * SF7/BW125/CR4:5, preamble 8, private sync word, +22 dBm, CRC on, fixed
 * 32-byte payloads. Ends in continuous-RX mode.
 *
 * Fails soft — a missing chip or a stuck BUSY line returns an error rather
 * than hanging or aborting, so the app can retry every 5 s and show
 * `RADIO?` (docs/01). Safe to call again after a failure.
 */
esp_err_t sx1262_init(uint32_t freq_hz);

/**
 * Blocking send of one 32-byte payload (~61 ms on air, 200 ms timeout).
 * Returns to continuous RX afterwards. ESP_ERR_TIMEOUT if the modem never
 * reported TX-done.
 */
esp_err_t sx1262_send(const uint8_t payload[SX1262_PAYLOAD_LEN]);

/**
 * Pop one received packet from the internal queue (depth 8, drop-oldest),
 * filled by the DIO1 interrupt path. `rssi_dbm`/`snr_db` are the
 * per-packet values from the modem; either pointer may be NULL.
 * Returns false if nothing arrived within wait_ms.
 */
bool sx1262_receive(uint8_t out[SX1262_PAYLOAD_LEN], int16_t *rssi_dbm,
                    int8_t *snr_db, uint32_t wait_ms);

/**
 * Cheap listen-before-talk hint: true if a reception is in progress
 * (preamble or valid header detected, or the modem is busy in RX).
 * Advisory only — it races with the air by nature.
 */
bool sx1262_channel_active(void);

/** ESP_LOGI: chip mode/status, frequency, and the drop counters. */
void sx1262_dump_status(void);

#endif /* SX1262_H */
