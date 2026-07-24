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
| T06 | radar screen composition | T01,T04,T05 | todo | | |
| T07 | simulator runner (SDL2) | T06 | todo | | |
| T08 | simulator scenarios + range model | T07 | todo | | |
| T09 | sx1262 LoRa driver + convoy_pins | — | todo | | |
| T10 | bringup_radio app (LoRa ping/RSSI) | T09 | todo | | |
| T11 | gps_uart + bringup_gps app | T03 | todo | | |
| T12 | audio_io: I²S mic + speaker | — | todo | | v3: was sa818 |
| T13 | bringup_audio: I²S/codec bench | T12,T02 | todo | | v3: was bringup_voice |
| T14 | ili9341_disp + bringup_display app | T09* | todo | | *needs convoy_pins only |
| T15 | convoylink app skeleton | T09,T12,T14 | todo | | |
| T16 | radio task: beacons + relay | T04,T10,T15 | todo | | |
| T17 | radar integration (v0.1 field gate) | T06,T11,T16 | todo | | |
| T18 | voice_pipe: framing+jitter (pure C) | T02 | todo | | v3: transport-agnostic |
| T19 | ESP-NOW voice (M5 gate) | T12,T13,T18,T17 | todo | | v3: digital voice ships |
| T20 | UI + robustness polish | T17,T19 | todo | | |
| T21 | field guide + tuning pass | T20 | todo | | |
| T22 | SX1262/Codec2 voice transport | T16,T19 | todo | | M6/experimental — range upgrade, A/B vs ESP-NOW |
