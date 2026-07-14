# Task status

Update your row in the same commit as the work. Keep notes to one line;
longer findings go at the bottom of the task file itself.

| Task | Title | Depends | Status | Commit | Note |
|---|---|---|---|---|---|
| T01 | convoy_geo component | — | todo | | |
| T02 | adpcm codec component | — | todo | | |
| T03 | nmea parser component | — | todo | | |
| T04 | neighbor_table component | — | todo | | |
| T05 | radar_render primitives | — | todo | | |
| T06 | radar screen composition | T01,T04,T05 | todo | | |
| T07 | simulator runner (SDL2) | T06 | todo | | |
| T08 | simulator scenarios + range model | T07 | todo | | |
| T09 | nrf24 driver + convoy_pins | — | todo | | |
| T10 | bringup_radio app | T09 | todo | | |
| T11 | gps_uart + bringup_gps app | T03 | todo | | |
| T12 | audio_io component | — | todo | | |
| T13 | bringup_audio app | T12 | todo | | |
| T14 | ili9341_disp + bringup_display app | T09* | todo | | *needs convoy_pins only |
| T15 | convoylink app skeleton | T09,T14 | todo | | |
| T16 | radio task: beacons + relay | T04,T10,T15 | todo | | |
| T17 | radar integration (v0.1 field gate) | T06,T11,T16 | todo | | |
| T18 | voice TX chain (voice_pipe TX) | T02,T13,T16 | todo | | |
| T19 | voice RX chain (jitter + playback) | T18 | todo | | |
| T20 | UI + robustness polish | T17,T19 | todo | | |
| T21 | field guide + tuning pass | T20 | todo | | |
