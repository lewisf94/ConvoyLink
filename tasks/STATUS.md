# Task status

Update your row in the same commit as the work. Keep notes to one line;
longer findings go at the bottom of the task file itself.

Voice is **digital, transport-abstracted** (v3, `docs/00` decision log):
ESP-NOW ships first, SX1262/Codec2 (T22) is the range-upgrade experiment,
the analog SA818 is a documented licensed-variant appendix (no task).

| Task | Title | Depends | Status | Commit | Note |
|---|---|---|---|---|---|
| T01 | convoy_geo component | — | done | (this commit) | test fixture typo: spec said 45 e7-units≈5m, is ~0.5m; test covers both scales |
| T02 | adpcm codec (pure C) | — | done | (this commit) | spec's 25 dB SNR bar unreachable: canonical IMA = 24.19 dB on the prescribed signal (search-encoder ties); test bar set to 23 dB with comment |
| T03 | nmea parser component | — | done | (this commit) | |
| T04 | neighbor_table component | — | done | (this commit) | Makefile SRCS_test_neighbor_table was missing convoy_proto.c (link error), fixed |
| T05 | radar_render primitives | — | done | (this commit) | |
| T06 | radar screen composition | T01,T04,T05 | done | (this commit) | M1 complete — all 6 pure-C components done, 51/51 host tests |
| T07 | simulator runner (SDL2) | T06 | done | (this commit) | docs/07's CLI example says "PNGs"/no `--headless`; task spec (BMP, separate flags) is authoritative, implemented as specced |
| T08 | simulator scenarios + range model | T07 | done | (this commit) | extracted sim_core.c from main.c; replay_to's RX gating changed from scn_sample "present" to track "used" (+first-waypoint stand-in position while un-fixed) so a no-fix unit still receives/tracks neighbours per docs/05 — sim-only behaviour, no wire format touched |
| T09 | sx1262 LoRa driver + convoy_pins | — | done | (this commit) | vendored ra01s @f5e0e7a, 4 patches in vendor/NOTICE; LoRaError's infinite-loop default overridden via setjmp so init fails soft. CI can't reach it yet — ci_build_apps.sh is a no-op until T10 adds an app; verified locally against real ESP-IDF v5.3.2 (clean build, 0 warnings) |
| T10 | bringup_radio app (LoRa ping/RSSI) | T09 | done | (this commit) | first app — ci_build_apps.sh now really builds (also covers T09). Spec's 2/s ping default is 12.2% duty vs docs/03's EU 10% cap, and the same line says respect the budget: EU clamps to 610 ms (~1.64/s) and says so, US/AU unclamped per docs/03. Owner may want the task's default restated as 1.5/s |
| T11 | gps_uart + bringup_gps app | T03 | done | (this commit) | task's contract says UART2, docs/02 pin table says UART1 (UART0 is the console) — used UART1 per docs-are-law; owner may want T11's comment corrected |
| T12 | audio_io: I²S mic + speaker | — | done | (this commit) | v3: was sa818. 32-bit slots BOTH ways, not 16-bit TX: one shared BCLK can't serve two slot widths, so playback rides the top 16 bits (API stays int16). Mic gain left at unity for T21 to tune. CI can't reach it until T13's app |
| T13 | bringup_audio: I²S/codec bench | T12,T02 | done | (this commit) | v3: was bringup_voice. `codec` runs the round-trip per 20 ms frame (docs/04 seeding), not bulk, so the preview matches on-air. Recording capped at 5 s / 80 KB static; DIRAM 41.7%, 199 KB free. Also brings T12 under CI |
| T14 | ili9341_disp + bringup_display app | T09* | todo | | *needs convoy_pins only |
| T15 | convoylink app skeleton | T09,T12,T14 | todo | | |
| T16 | radio task: beacons + relay | T04,T10,T15 | todo | | |
| T17 | radar integration (v0.1 field gate) | T06,T11,T16 | todo | | |
| T18 | voice_pipe: framing+jitter (pure C) | T02 | todo | | v3: transport-agnostic |
| T19 | ESP-NOW voice (M5 gate) | T12,T13,T18,T17 | todo | | v3: digital voice ships |
| T20 | UI + robustness polish | T17,T19 | todo | | |
| T21 | field guide + tuning pass | T20 | todo | | |
| T22 | SX1262/Codec2 voice transport | T16,T19 | todo | | M6/experimental — range upgrade, A/B vs ESP-NOW |
