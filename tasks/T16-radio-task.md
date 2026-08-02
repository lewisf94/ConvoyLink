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

---

## Finding — beacon `seq` does not survive a reboot (owner decision needed)

Found while implementing T16; **not fixed here**, because every fix
touches a contract this task does not own.

`seq` starts at 0 each boot, and `nt_update_from_beacon` only accepts a
beacon when `cl_seq_newer(b.seq, entry.last_seq)`. So a unit that reboots
is ignored by any peer that is still running and still remembers it,
until its fresh `seq` climbs past the remembered one.

Measured with a host harness against the real `convoy_proto` +
`neighbor_table` sources:

```
before reboot: last_seq=500
after reboot:  0 of 20 beacons accepted, last_seq=500
first seq accepted again: 501  (= ~2505 s ≈ 42 min at one beacon per 5 s)
```

The remembered entry does not age out of the way either: `NT_GONE` at
15 min hides an entry from `nt_snapshot` but leaves `in_use` set, so the
sequence check still applies.

Field symptom: power-cycle one car mid-convoy and it vanishes from
everyone else's radar for up to ~40 minutes, while its own radar looks
perfectly healthy. Two units booted together are unaffected, which is why
the T16 hardware checklist would not catch it.

Options, all needing an owner call:

1. **Wire-format epoch** — add a boot counter (NVS, incremented once per
   boot) to `cl_beacon_t` and treat a changed epoch as "accept anything".
   Correct and explicit, but changes `docs/03` and `convoy_proto`, and
   the beacon has 8 reserved bytes to spend.
2. **Accept-after-silence in `neighbor_table`** — if an entry has not
   been heard from in longer than some threshold, accept the next beacon
   regardless of `seq`. No wire change; needs a threshold picked in
   `docs/05` and new T04 tests.
3. **Persist `seq` in NVS** — simplest conceptually, but a flash write
   every 5 s is unacceptable wear; only viable if written coarsely (e.g.
   +1000 per boot), which is really option 1 in disguise.

Recommendation: option 2. It is the smallest change, needs no wire-format
edit, and the staleness tiers already give a natural threshold to reuse.

### Resolution (owner decision)

Option 2, **accept-after-silence**, was chosen and is implemented in
`neighbor_table` — see `docs/05` §Sequence resync for the rule and
`NT_RESYNC_MS` in `convoy_cfg.h` for the threshold (15 s, tied to
`NT_STALE_MS`). Relay bookkeeping resets alongside the entry, which the
original write-up above did not call out but which matters just as much:
it is `seq`-based too, so without the reset a rebooted unit would be
tracked but never relayed for again.

Blackout after a reboot is now bounded at ~15 s instead of ~42 min. Three
host tests cover it (`reboot_resync_after_silence`,
`no_resync_while_still_live`, `resync_restarts_relay_bookkeeping`), and
the original standalone reproduction goes from 0/20 to 20/20 beacons
accepted.
