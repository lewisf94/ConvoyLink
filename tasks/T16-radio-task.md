# T16 — radio task: beacons, RX dispatch, relay

**Depends:** T04, T10, T15 · **Phase:** M4
**Required reading:** `docs/03-radio-protocol.md` §Channel access +
§Beacon relay (binding); `docs/01` §Task layout

## Goal

The data plane goes live: own beacons out every 5 s, received beacons into
the neighbour table, single-hop relay with suppression — per the protocol
doc, exactly.

## Deliverables (within `firmware/apps/convoylink/main/`)

- `radio_task.c` — real implementation:
  - drain `sx1262_receive` → `cl_validate` → dispatch: BEACON →
    `nt_update_from_beacon` (+ relay decision below) under state lock;
    PING → counter (bring-up interop). Log per-packet RSSI/SNR at DEBUG
  - service `tx_q` with the cheap listen-before-talk from docs/03: if
    `sx1262_channel_active()`, defer up to 200 ms in 20 ms steps, then
    send anyway
  - relay scheduler: `esp_timer` one-shots at `uniform(150..450 ms)`;
    fire → re-check suppression + LBT → `cl_beacon_to_relay` → `tx_q`.
    Track pending relays in a small static array (≥ 4 slots, drop when full)
- `gps_task.c` — real implementation: poll `gps_uart_get_fix` at 1 Hz into
  state; every `CL_BEACON_PERIOD_MS ± jitter` build `cl_make_beacon` from
  the freshest fix (fix_quality 0 when invalid) and queue it. Skip all TX
  while unprovisioned
- `main/radio_stats.c` — console `radiostat` (tx/rx/invalid/relayed/
  suppressed/dropped counters) and `nt` (neighbour table dump with ages
  and tiers)

## Acceptance — CI

`./tools/ci_build_apps.sh` green.

## Acceptance — hardware (owner checklist, needs 2–3 units)

- [ ] Two provisioned units: each sees the other in `nt` within 10 s of
      boot, age resets ~every 5 s
- [ ] Kill unit B: A's `nt` walks B through STALE at 15 s, GHOST at 60 s
- [ ] `radiostat` beacon counters advance; invalid stays ≈ 0
- [ ] Three units: put A and C out of mutual range (opposite ends of the
      street, or unscrew one antenna as a crude attenuator), B in the
      middle — A's `nt` shows C `via_relay`, and `radiostat` on B shows
      `relayed` counting; on A+C `suppressed` stays low (usually one
      relayer wins)
- [ ] GPS-less bench unit (no antenna sky view) still appears in others'
      `nt` as online/no-GPS

## Out of scope

Voice anything (separate radio + task entirely — T18), UI changes
(ui_task already renders whatever the table says).
