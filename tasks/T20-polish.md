# T20 — UI + robustness polish — closes toward v1.0

**Depends:** T17, T18 · **Phase:** M6
**Required reading:** `docs/06-ui.md`; `docs/00` §Success criteria;
notes accumulated in `tasks/STATUS.md` from field runs

## Goal

Turn the working prototype into the dependable road-trip unit: boot/identity
niceties, night usability, defensive robustness, and whatever small fixes
the M4/M5 field notes flagged.

## Deliverables

- Boot splash (project name, unit id/initials, firmware version from git
  describe via build system) — 1.5 s, then radar
- `PROVISION ME` banner rendered on-screen (T15 introduced the state;
  make it pretty + show the exact console command to run)
- Night mode: AUX 2 s hold already cycles backlight (T17); persist the
  chosen level in NVS; dim automatically to the stored night level when
  backlight-relevant? **No auto-dim in v1** — persist choice only
- Watchdog coverage: task WDT subscribed on radio/voice/ui tasks with
  10 s window; any WDT reset visible in logs with reset-reason banner on
  next boot (owner debugging aid)
- Defensive sweeps (audit + fix): every queue-full path counts + drops
  oldest; `cl_validate` failures counted per source; SX1262 re-init retry
  (5 s backoff) if init failed at boot — red `RADIO?` status-bar tile
  meanwhile; SA818 unresponsive → `VOICE?` tile + config re-apply retries
  (T18 started this; make it bulletproof); GPS silent > 10 s → sats
  display goes red
- Any open UI fixes from field notes (dot jitter smoothing: if noted,
  apply a 2-sample position average behind a cfg flag)
- Update `README.md` status table to v1.0-rc state

## Acceptance — CI

All jobs green; any renderer change comes with host-test coverage
(strip invariance must keep passing).

## Acceptance — hardware (owner checklist)

- [ ] Cold boot to usable radar < 5 s (excluding GPS lock)
- [ ] Splash shows correct version; reset-reason banner appears after a
      deliberate WDT trip (add a hidden `crash` console cmd, remove-me
      comment) 
- [ ] Disconnect the SX1262 mid-run: `RADIO?` tile appears, unit keeps
      rendering, reconnect + `retry` recovers without reboot; same drill
      for the SA818 → `VOICE?`
- [ ] Night level survives power cycle
- [ ] 2-hour bench soak with a second unit beaconing + periodic PTT:
      zero resets, `radiostat` drop counters ≈ 0, heap stable

## Out of scope

New features (touch, sweep, heading-up stay stretch), transport upgrades.
