# T15 — `convoylink` app skeleton: tasks, state, provisioning

**Depends:** T09, T14 · **Phase:** M4
**Required reading:** `docs/01-architecture.md` (all — the task table and
state/queue rules are binding); `docs/07` §Provisioning

## Goal

The real firmware's load-bearing frame: FreeRTOS task layout, shared state,
queues, NVS identity + console — with stub task bodies that later tasks
(T16–T19) fill in. Boots, breathes, and shows something on screen.

## Deliverables

- `firmware/apps/convoylink/…` (project template; `sdkconfig.defaults`
  additionally enables the task watchdog on both idle tasks)
- `firmware/apps/convoylink/main/`:
  - `main.c` — init order: NVS → unit_cfg → disp_init → splash →
    nrf24_init → gps_uart_start → queues/state → task creation per the
    docs/01 table (names, cores, prios exactly)
  - `app_state.h/.c` — `convoy_state_t` (own_fix + age, nt_t, voice_status,
    zoom mode) behind `state_lock()/unlock()/snapshot()`
  - `app_queues.h/.c` — `tx_q` (depth 8, 32-byte items), `voice_rx_q`
    (depth `CL_JITTER_SLOTS`), both drop-oldest on overflow with a
    dropped-counter
  - task stubs: `radio_task.c`, `audio_task.c`, `gps_task.c`, `ui_task.c`,
    `ctrl_task.c` — each logs a heartbeat at DEBUG every 5 s and, where
    trivial, does the obvious minimal thing (ui_task: renders an
    `rr_screen_draw` scene from the snapshot even if empty; ctrl_task:
    debounces buttons into events; others: idle loop)
- `firmware/components/unit_cfg/` — NVS-backed identity:
  `unit_cfg_get(uint8_t *uid, char initials[2])` (false if unprovisioned),
  esp_console REPL with `unitcfg set <0-4> <AA>` / `unitcfg show`, input
  validation per docs/07.

Unprovisioned boot: UI shows the `PROVISION ME` banner state
(`rr_scene_t.provisioned = false`), radio TX paths disabled, console works.

## Acceptance — CI

`./tools/ci_build_apps.sh` green.

## Acceptance — hardware (owner checklist)

- [ ] Fresh flash boots to PROVISION ME screen; `unitcfg set 0 LF`,
      reboot → status bar shows `U0 LF`
- [ ] All five task heartbeats appear with `log_level` DEBUG
- [ ] Radar area renders the "waiting for convoy…" state at 5 Hz
      (watch for flicker — strip flushing working)
- [ ] 10-minute idle soak: no watchdog resets, heap high-water stable
      (`free` console cmd — include one)

## Out of scope

Real radio/gps/audio behaviour (T16–T19) — stubs only. Don't wire PTT to
anything yet.
