# bringup_audio — I²S mic / speaker / codec bench

Proves the digital audio chain end to end: the INMP441 captures, the
MAX98357A plays, and an ADPCM round-trip through RAM stays intelligible.
This is where you hear what the on-air voice will actually sound like.

Single unit — no radio involved.

## Build & flash

```sh
. ~/esp-idf/export.sh
idf.py -C firmware/apps/bringup_audio set-target esp32s3 build
idf.py -C firmware/apps/bringup_audio -p /dev/ttyUSB0 flash monitor
```

## Commands

| Command | Action |
|---|---|
| `tone [hz] [s]` | Play a synthesised sine (default 440 Hz, 2 s) |
| `meter` | Live mic RMS/peak bar at 10 Hz; press **Enter** to stop |
| `loop [s]` | Record N s of mic to RAM and play it straight back (default 3 s) |
| `codec [s]` | Record, ADPCM round-trip, play back — the voice-quality preview |
| `vol <0-100>` | Playback volume |

Recording is capped at 5 s: the buffer is statically allocated (80 KB),
because nothing on the audio path may allocate at runtime.

`codec` deliberately runs the round-trip **one 20 ms frame at a time**,
each frame decoded from the state snapshotted before its own encode. That
is exactly how frames travel on air (`docs/04` §Codecs) — a bulk encode
would sound better than the real thing and mislead you.

## Acceptance — hardware checklist (project owner)

- [ ] `tone` — a clean tone from the speaker. `tone 3000` and `tone 200`
      should both be audible (speaker band sanity).
- [ ] `meter` — a quiet room reads rms < 300; talking at arm's length
      pushes peak > 5000 **without** pegging at 32767. Pegging means the
      mic is too hot or too close.
- [ ] `loop` — your voice plays back clearly, confirming the raw path.
- [ ] `codec` — still intelligible after ADPCM. Expect 8 kHz and a bit
      crunchy: walkie-talkie grade is the target, not hi-fi.
- [ ] No I²S underruns or overruns logged over a 2-minute `meter` run.

If the mic reads consistently quiet even close up, that is the fixed
capture gain sitting at unity in `audio_io.c` — raising it is part of
T21's tuning pass, once the enclosure and mic placement are final.
