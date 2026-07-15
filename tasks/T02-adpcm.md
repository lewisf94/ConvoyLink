# T02 — ~~`adpcm`: IMA ADPCM codec component~~ **SUPERSEDED**

**Status: superseded by the v2 radio architecture (2026-07-15).**

Voice moved from digital ADPCM-over-NRF24 to an analog SA818 UHF module
(`docs/00` decision log, `docs/04-voice-sa818.md`). Firmware never touches
audio samples, so no codec exists in the project.

Do not implement anything from this task. The replacement voice work is:

- **T12** — `sa818` control component (UART/PTT/RSSI)
- **T13** — `bringup_voice` app
- **T18** — voice integration into the main firmware

Packet type 2 (`CL_TYPE_VOICE`) is reserved in `convoy_proto.h` and must
never be reused.
