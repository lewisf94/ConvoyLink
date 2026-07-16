# CLAUDE.md — ConvoyLink agent guide

ESP32-S3 (DevKitC-1, N16R8) device: GPS radar + digital PTT voice for
5-car convoys. Positions ride an SX1262 LoRa radio; voice is digital
(I²S mic/amp → codec → jitter buffer) over a swappable transport —
ESP-NOW by default, SX1262/Codec2 as a range upgrade (`docs/04`). ESP-IDF
v5.3 native. **The design docs in `docs/` are law**; implementation happens
through the task queue in `tasks/`.

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

- LoRa beacon payloads are **exactly 32 bytes**; layouts live only in
  `convoy_proto.h` and `docs/03` — never define beacon bytes elsewhere.
  Voice frames are a **separate** format in `voice_proto.h` + `docs/04`
  (magic `0xC8`), carried over the voice transport, not the LoRa link.
- Compare sequence numbers only with `cl_seq_newer()` (beacons and voice).
- Pin assignments come from `convoy_pins.h` (created in T09) / `docs/02` —
  never hard-code a GPIO number anywhere else.
- Pure-C components (`convoy_proto`, `convoy_geo`, `adpcm`, `voice_pipe`,
  `nmea`, `neighbor_table`, `radar_render`): **no ESP-IDF/FreeRTOS
  includes, no malloc after init, must build with plain gcc** via
  `test/host`.
- Firmware radio use is **constrained, not banned**: Wi-Fi may be brought
  up **only** for ESP-NOW (connectionless, STA, fixed channel — never
  associate/host/scan); Bluetooth never. No heap allocation after startup
  on the radio/audio paths; no float math in ISRs. Only `radio_task`
  touches the SX1262; only `voice_task` touches `audio_io` (I²S) and the
  ESP-NOW radio (the SX1262 voice transport goes *through* `radio_task`).
- Voice is **digital** (`docs/04`): I²S in/out, per-frame codec state for
  loss resilience, transport-abstracted. The analog SA818 is a retired
  licensed-variant appendix — do not wire audio to it in the core build.
- Timestamps are `uint32_t` ms; age math wrap-safe: `(int32_t)(now - then)`.
- All five units run one identical binary; identity comes from NVS.

## Code style

- C11, 4-space indent, `snake_case`; public API prefixed per component
  (`cl_`, `geo_`, `adpcm_`, `vp_`, `nmea_`, `nt_`, `rr_`, `sx1262_`,
  `aio_`, `vt_`).
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

Build one app: `idf.py -C firmware/apps/<app> set-target esp32s3 build`
(more in `docs/07-dev-guide.md`).
