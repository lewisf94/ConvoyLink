# CLAUDE.md — ConvoyLink agent guide

ESP32 (classic WROOM-32) device: GPS radar + PTT voice for 5-car convoys
over NRF24L01+ radio. ESP-IDF v5.3 native. **The design docs in `docs/` are
law**; implementation happens through the task queue in `tasks/`.

## Session protocol (follow exactly)

1. Read `tasks/STATUS.md`, pick the first `todo` task whose dependencies are
   `done` (or the task you were told to do).
2. Read the task file fully, then its **Required reading** doc sections.
   Do not start coding before both.
3. Implement **only** what the task's Deliverables list — nothing
   speculative, no drive-by refactors, no extra features.
4. Verify (see Verification below), update your task's row in
   `tasks/STATUS.md` (status, commit hash, one-line note), commit.
5. One task = one commit: `T0x: <imperative summary>`.

If the task conflicts with a doc, or something is genuinely ambiguous:
**stop, set the STATUS row to `blocked` with a note, and say so** — do not
guess wire formats, pins, or timing values, and do not edit docs to match
code. Docs change only by explicit owner decision.

## Verification (definition of done)

```sh
make -C test/host test        # must print ALL HOST TESTS PASSED
./tools/ci_build_apps.sh      # after `. ~/esp-idf/export.sh`; builds every firmware app
make -C sim && make -C sim smoke   # once sim exists (T07+)
```

Pure-C components ship **with tests in the same commit** — a component task
without new `test/host/test_<name>.c` coverage is not done. Hardware tasks
(T09–T21) are CI-verified for compilation only; their "Acceptance —
hardware" checklist is executed by the project owner, not by you.

## Hard invariants (violating these = the task is wrong)

- Radio payloads are **exactly 32 bytes**; layouts live only in
  `convoy_proto.h` and `docs/03` — never define packet bytes elsewhere.
- Compare sequence numbers only with `cl_seq_newer()`.
- Pin assignments come from `convoy_pins.h` (created in T09) / `docs/02` —
  never hard-code a GPIO number anywhere else.
- Pure-C components (`convoy_proto`, `convoy_geo`, `adpcm`, `nmea`,
  `neighbor_table`, `radar_render`): **no ESP-IDF/FreeRTOS includes, no
  malloc after init, must build with plain gcc** via `test/host`.
- Firmware: no Wi-Fi/BT init ever; no heap allocation after startup on
  audio/radio paths; no float math in ISRs; only `radio_task` touches the
  NRF24 and only `audio_task` touches `audio_io`.
- Timestamps are `uint32_t` ms; age math wrap-safe: `(int32_t)(now - then)`.
- All five units run one identical binary; identity comes from NVS.

## Code style

- C11, 4-space indent, `snake_case`; public API prefixed per component
  (`cl_`, `geo_`, `nmea_`, `nt_`, `rr_`, `nrf24_`, `aio_`).
- Errors: pure-C returns `int` (0 OK / negative enum from the component
  header); ESP components return `esp_err_t` and log via `ESP_LOGx(TAG,…)`.
- Comment style: brief `/** … */` on public API declarations; inline
  comments only for non-obvious constraints. Match `convoy_proto` —
  it is the exemplar component (code, header, CMake, tests).
- No new third-party dependencies unless the task says so.

## Do not touch

- `.github/workflows/ci.yml`, `tools/ci_build_apps.sh`, `LICENSE`
- `docs/*` (flag doc problems in STATUS notes instead)
- Other tasks' deliverable files, except shared files a task explicitly
  lists (e.g. `test/host/Makefile` per-test variables, `tasks/STATUS.md`).

## Layout

```
docs/            design docs (law)         tasks/       queue + STATUS.md
firmware/components/   shared code         firmware/apps/   one IDF project per app
test/host/       gcc unit tests           sim/         SDL2 desktop radar sim
tools/           CI scripts
```

Build one app: `idf.py -C firmware/apps/<app> set-target esp32 build`
(more in `docs/07-dev-guide.md`).
