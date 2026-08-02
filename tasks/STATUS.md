# Task status

Update your row in the same commit as the work. Keep notes to one line;
longer findings go at the bottom of the task file itself.

Voice is **digital, transport-abstracted** (v3, `docs/00` decision log):
ESP-NOW ships first, SX1262/Codec2 (T22) is the range-upgrade experiment,
the analog SA818 is a documented licensed-variant appendix (no task).

| Task | Title | Depends | Status | Commit | Note |
|---|---|---|---|---|---|
| T01 | convoy_geo component | — | done | (this commit) | fixture arithmetic corrected in the task file (45→450 e7-units for ~5 m); test covers both scales |
| T02 | adpcm codec (pure C) | — | done | (this commit) | 25 dB SNR bar was unreachable (canonical IMA = 24.19 dB, search-encoder ties); task file corrected to 23 dB |
| T03 | nmea parser component | — | done | (this commit) | |
| T04 | neighbor_table component | — | done | (this commit) | Makefile SRCS_test_neighbor_table was missing convoy_proto.c (link error), fixed. **Reopened after T16:** added accept-after-silence (NT_RESYNC_MS) per owner decision, fixing the reboot-seq blackout; relay bookkeeping resets with it; docs/05 §Sequence resync added; 10/10 tests (63 total) |
| T05 | radar_render primitives | — | done | (this commit) | |
| T06 | radar screen composition | T01,T04,T05 | done | (this commit) | M1 complete — all 6 pure-C components done, 51/51 host tests |
| T07 | simulator runner (SDL2) | T06 | done | (this commit) | docs/07's CLI example says "PNGs"/no `--headless`; task spec (BMP, separate flags) is authoritative, implemented as specced |
| T08 | simulator scenarios + range model | T07 | done | (this commit) | extracted sim_core.c from main.c; replay_to's RX gating changed from scn_sample "present" to track "used" (+first-waypoint stand-in position while un-fixed) so a no-fix unit still receives/tracks neighbours per docs/05 — sim-only behaviour, no wire format touched |
| T09 | sx1262 LoRa driver + convoy_pins | — | done | (this commit) | vendored ra01s @f5e0e7a, 4 patches in vendor/NOTICE; LoRaError's infinite-loop default overridden via setjmp so init fails soft. CI can't reach it yet — ci_build_apps.sh is a no-op until T10 adds an app; verified locally against real ESP-IDF v5.3.2 (clean build, 0 warnings) |
| T10 | bringup_radio app (LoRa ping/RSSI) | T09 | done | (this commit) | first app — ci_build_apps.sh now really builds (also covers T09). task file's 2/s default was 12.2% duty vs docs/03's EU 10% cap; corrected to 1.5/s (9.2%) and the code default follows it. EU still clamps an explicitly faster rate to 610 ms (~1.64/s); US/AU unclamped per docs/03 |
| T11 | gps_uart + bringup_gps app | T03 | done | (this commit) | task's contract says UART2, docs/02 pin table says UART1 (UART0 is the console) — used UART1 per docs-are-law; task file corrected to UART1 |
| T12 | audio_io: I²S mic + speaker | — | done | (this commit) | v3: was sa818. 32-bit slots BOTH ways, not 16-bit TX: one shared BCLK can't serve two slot widths, so playback rides the top 16 bits (API stays int16). Mic gain left at unity for T21 to tune. CI can't reach it until T13's app |
| T13 | bringup_audio: I²S/codec bench | T12,T02 | done | (this commit) | v3: was bringup_voice. `codec` runs the round-trip per 20 ms frame (docs/04 seeding), not bulk, so the preview matches on-air. Recording capped at 5 s / 80 KB static; DIRAM 41.7%, 199 KB free. Also brings T12 under CI |
| T14 | ili9341_disp + bringup_display app | T09* | done | (this commit) | *needs convoy_pins only. M3 complete. NOTE: shared components dir means every app now compiles ili9341_disp, so all 4 apps need the component registry at build time. This sandbox's proxy blocks components.espressif.com, so the managed dep was verified against a signature-matched local stub (all 4 apps clean); the real fetch is CI-gated |
| T15 | convoylink app skeleton | T09,T12,T14 | done | (this commit) | 2 doc gaps for later tasks: (a) docs/01 lists ctrl_q with TWO consumers (voice_task+ui_task) which one queue can't do — ui_task drains it for now, T18 must split it; (b) RADIO?/VOICE? tiles need an rr_scene_t field radar_render has none of, so health is tracked in state + logged, tile deferred to T17 |
| T16 | radio task: beacons + relay | T04,T10,T15 | done | (this commit) | found the reboot-seq protocol gap (write-up at the bottom of the task file); **RESOLVED** by the owner-chosen accept-after-silence rule — see the T04 row |
| T17 | radar integration (v0.1 field gate) | T06,T11,T16 | done | (this commit) | M4 code-complete. 3 deferrals, all needing an rr_scene_t field radar_render lacks (T06 owns it): docs/06's transient zoom-mode indicator, docs/01's RADIO?/VOICE? tiles, and T17's "double-buffered" strips (pointless while disp_flush is synchronous — single buffer used). Zoom hysteresis implements exactly docs/06's two rules, no invented deadband |
| T18 | voice_pipe: framing+jitter (pure C) | T02 | done | (this commit) | v3: transport-agnostic. 9/9 new tests (60 total). **docs/04 miscount:** its vf_hdr_t fields sum to 10 B, not the "9 B header" its comment claims (so ESP-NOW frame is 90 B, not ~89) — fields kept as the contract, VF_HDR_SIZE=10; docs/04 comment corrected. Own test caught a real hang: a burst delivering < CL_JITTER_PREFILL frames then going quiet sat in prefill forever holding the talker lock; starved bursts now flush |
| T19 | ESP-NOW voice (M5 gate) | T12,T13,T18,T17 | done | (this commit) | v3: digital voice ships. M5 code-complete. Resolved T15's ctrl_q fan-out: one queue can't feed two consumers, so ctrl_task now routes PTT->ctrl_q (voice_task), AUX->ui_q (ui_task). `voice sx1262` returns NULL -> VOICE? rather than silently falling back to espnow (they can't hear each other). Wi-Fi costs +51 KB measured vs docs/01's ~50 KB estimate; DIRAM 42%, 198 KB free |
| T20 | UI + robustness polish | T17,T19 | todo | | |
| T21 | field guide + tuning pass | T20 | todo | | |
| T22 | SX1262/Codec2 voice transport | T16,T19 | todo | | M6/experimental — range upgrade, A/B vs ESP-NOW |
