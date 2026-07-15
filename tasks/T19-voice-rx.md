# T19 — ~~Voice RX chain (jitter buffer + playback)~~ **SUPERSEDED**

**Status: superseded by the v2 radio architecture (2026-07-15).**

Analog FM reception is handled entirely inside the SA818 module — received
speech goes SA818 `AF_OUT` → PAM8403 → speaker in hardware, so there is no
jitter buffer, no decoder and no playback code (`docs/04-voice-sa818.md`).

The remaining RX-side firmware work (carrier detect → `RX` indicator on the
UI) is part of **T18 — voice integration**. Do not implement anything from
this task.
