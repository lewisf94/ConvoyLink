# bringup_radio — LoRa ping + range logger

First hardware contact. Proves the SX1262 driver, wiring and power, and
doubles as the walk-around range tester whose numbers calibrate the
"2–5 km" estimate in `docs/00`.

Needs **two** units to be useful: one pinging, one monitoring.

## Build & flash

```sh
. ~/esp-idf/export.sh
idf.py -C firmware/apps/bringup_radio set-target esp32s3 build
idf.py -C firmware/apps/bringup_radio -p /dev/ttyUSB0 flash monitor
```

Console is the IDF `esp_console` REPL on UART0 at 115200. `help` lists
everything; exit the monitor with `Ctrl-]`.

## Commands

| Command | Action |
|---|---|
| `status` | Radio status/frequency, identity, and the RX counters — run this first |
| `region <EU\|US\|AU>` | Set frequency per the `docs/03` table and re-init the radio (RAM only) |
| `id <0-4>` | Set this bench unit's sender id (RAM only) |
| `ping [rate_hz]` | Start sending `cl_ping_t`, seq incrementing (default 2/s) |
| `stop` | Stop pinging |
| `mon` | Toggle the per-packet monitor: `seq=… rssi=…dBm snr=…dB from=U… loss%=…` |

Identity and region are deliberately RAM-only here — NVS provisioning is
T15's job. Both reset on reboot.

### Duty budget

A 32-byte packet at SF7/125 kHz/4:5 is ~61 ms on air, and the EU
869.40–869.65 sub-band allows 10 % duty (`docs/03`), so the fastest
compliant sustained rate is one packet per 610 ms (~1.64/s). In region
`EU` a faster `ping` rate is clamped to that and the clamp is printed;
`US`/`AU` sit in 902–928 MHz ISM / LIPD with no duty limit in the same
table, so they run at the rate you ask for.

### Loss and invalid counters

`loss%` is a 30 s sliding window per sender: the wrap-safe sequence span
between the oldest and newest packet still inside the window, minus what
actually arrived. It shows `--` until at least two packets are in the
window.

Every packet goes through `cl_validate` first. Failures are counted
separately as `invalid_rx` in `status` (and printed while `mon` is on) —
they mean interference or someone else's LoRa gear colliding with our
private sync word, *not* convoy packet loss. `snr` is whole dB: the
vendored driver reports it pre-scaled.

## Acceptance — hardware checklist (project owner)

- [ ] `status` shows the configured frequency and no chip errors on both
      units. A stuck BUSY or SPI error points at wiring or power — see
      `docs/07` §Troubleshooting.
- [ ] Unit A `ping`, unit B `mon`: same room, loss ≈ 0 %, rssi roughly
      −40 to −60 dBm depending on distance.
- [ ] Walk test: note the distance at the first sustained loss > 10 %, and
      the rssi/snr there. **Record it in `tasks/STATUS.md`** — this is what
      calibrates the range estimate in `docs/00` against your actual
      antennas and terrain.
- [ ] Antennas vertical vs lying flat: observe the rssi difference.
- [ ] Confirm both units agree on `region` before any further bring-up —
      a frequency mismatch looks exactly like "no range".
