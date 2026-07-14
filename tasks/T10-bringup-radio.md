# T10 — `bringup_radio` app: ping-pong + range test

**Depends:** T09 · **Phase:** M3
**Required reading:** `docs/03` §CL_TYPE_PING; `docs/07-dev-guide.md`
§Build + §Troubleshooting

## Goal

First firmware app: prove the radio driver, wiring and power on real
hardware, and give the owner a walk-around range-testing tool.

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
| `regs` | `nrf24_dump_regs()` — first thing the owner runs |
| `id <0-4>` | set this bench unit's sender id (RAM only) |
| `ping [rate_hz]` | start sending `cl_ping_t` at rate (default 10/s), seq++ |
| `stop` | stop pinging |
| `mon` | toggle monitor mode: for each RX ping log nothing, but print a rolling 1 s line: `rx/s=… loss%=… rpd%=… from=U<id>` (loss from seq gaps within the last 5 s window; rpd% = share of packets with RPD set) |
| `ch <0-125>` | temporarily move both TX/RX channel (interference hunting; boots back to `CL_RF_CHANNEL`) |

Every received packet goes through `cl_validate` first; count and print
invalid packets separately (they indicate noise/supply problems).

## Acceptance — CI

`./tools/ci_build_apps.sh` green (this app now exists, so the firmware job
starts really building).

## Acceptance — hardware (owner checklist — ship this list in the app's README)

- [ ] `regs` shows RF_CH=0x4C(76), RF_SETUP=0x26, RX_ADDR_P0=43:4C:4E:4B:31
      on both units (anything 0x00/0xFF → wiring/power, see docs/07)
- [ ] Unit A `ping`, unit B `mon`: same room loss < 1 %, rpd% ≈ 100
- [ ] Opposite ends of house/garden: loss < 2 %
- [ ] Antennas vertical vs lying flat: observe the loss% difference
- [ ] Walk test to first sustained loss > 10 % — note the distance in
      `tasks/STATUS.md` (this calibrates expectations for `docs/00` range
      reality; expect 150–400 m urban on stock antennas)
- [ ] Power radio from devkit 3V3 instead of buck B for one minute:
      observe degradation (validates why buck B exists), then put it back

## Out of scope

Beacons/voice/relay, display output, NVS identity (RAM `id` is fine here).
