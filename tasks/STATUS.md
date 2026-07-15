# Task status

Update your row in the same commit as the work. Keep notes to one line;
longer findings go at the bottom of the task file itself.

`superseded` rows are closed permanently by the v2 radio architecture
(2026-07-15, `docs/00` decision log) — do not pick them up.

| Task | Title | Depends | Status | Commit | Note |
|---|---|---|---|---|---|
| T01 | convoy_geo component | — | todo | | |
| T02 | adpcm codec | — | superseded | | v2: voice is analog (SA818), no codec exists |
| T03 | nmea parser component | — | todo | | |
| T04 | neighbor_table component | — | todo | | |
| T05 | radar_render primitives | — | todo | | |
| T06 | radar screen composition | T01,T04,T05 | todo | | |
| T07 | simulator runner (SDL2) | T06 | todo | | |
| T08 | simulator scenarios + range model | T07 | todo | | |
| T09 | sx1262 LoRa driver + convoy_pins | — | todo | | |
| T10 | bringup_radio app (LoRa ping/RSSI) | T09 | todo | | |
| T11 | gps_uart + bringup_gps app | T03 | todo | | |
| T12 | sa818 voice module component | T09* | todo | | *needs convoy_pins only |
| T13 | bringup_voice app | T12 | todo | | |
| T14 | ili9341_disp + bringup_display app | T09* | todo | | *needs convoy_pins only |
| T15 | convoylink app skeleton | T09,T12,T14 | todo | | |
| T16 | radio task: beacons + relay | T04,T10,T15 | todo | | |
| T17 | radar integration (v0.1 field gate) | T06,T11,T16 | todo | | |
| T18 | voice integration (M5 gate) | T13,T17 | todo | | |
| T19 | voice RX chain | — | superseded | | v2: absorbed into T18; no jitter/playback code exists |
| T20 | UI + robustness polish | T17,T18 | todo | | |
| T21 | field guide + tuning pass | T20 | todo | | |
