# T10 — `bringup_radio` app: LoRa ping + range logger

*(v2: retargeted from NRF24 ping-pong to the SX1262 — `docs/00` decision log.)*

**Depends:** T09 · **Phase:** M3
**Required reading:** `docs/03` §CL_TYPE_PING + §Regulatory quick reference;
`docs/07-dev-guide.md` §Build + §Troubleshooting

## Goal

First firmware app: prove the LoRa driver, wiring and power on real
hardware, and give the owner a walk-around range-testing tool with real
RSSI/SNR numbers (a genuine upgrade over v1's 1-bit NRF24 RPD).

## Deliverables

- `firmware/apps/bringup_radio/CMakeLists.txt` + `main/CMakeLists.txt` +
  `sdkconfig.defaults` + `main/main.c`
  (this is the **first app** — follow `firmware/apps/README.md`'s template
  exactly; later apps copy yours)
- Uses `esp_console` REPL over UART0 (IDF built-in; the same pattern T15
  reuses).

## Behaviour (console commands)

| Command | Action |
|---|---|
| `status` | `sx1262_dump_status()` — first thing the owner runs |
| `region <EU\|US\|AU>` | set frequency per `docs/03` table (RAM only) |
| `id <0-4>` | set this bench unit's sender id (RAM only) |
| `ping [rate_hz]` | start sending `cl_ping_t` at rate (default 2/s — respect the duty budget even in test mode), seq++ |
| `stop` | stop pinging |
| `mon` | toggle monitor mode: for each RX ping print `seq=… rssi=-NNdBm snr=N.NdB from=U<id> loss%=…` (loss from seq gaps within the last 30 s window) |

Every received packet goes through `cl_validate` first; count and print
invalid packets separately (they indicate interference or a sync-word
mismatch with someone else's LoRa gear).

## Acceptance — CI

`./tools/ci_build_apps.sh` green (this app now exists, so the firmware job
starts really building).

## Acceptance — hardware (owner checklist — ship this list in the app's README)

- [ ] `status` shows the configured frequency and no chip errors on both
      units (BUSY stuck / SPI errors → wiring/power, see docs/07)
- [ ] Unit A `ping`, unit B `mon`: same room loss ≈ 0 %, rssi around
      −40 to −60 dBm depending on distance
- [ ] Walk test: note distance at first sustained loss > 10 %, and the
      rssi/snr at that point — record in `tasks/STATUS.md`. This
      calibrates the "2–5 km" estimate in `docs/00` against your actual
      antennas/terrain
- [ ] Antennas vertical vs lying flat: observe the rssi difference
- [ ] Confirm both units agree on `region` before any further bring-up —
      mismatched frequency looks exactly like "no range"

## Out of scope

Beacons/relay/neighbour table (T16), voice (T12/T13), display output, NVS
identity (RAM `id`/`region` are fine here).
