# T18 — Voice TX chain (`voice_pipe` TX half + PTT wiring)

**Depends:** T02, T13, T16 · **Phase:** M5
**Required reading:** `docs/04-audio.md` §PTT state machine (binding);
`docs/03` §CL_TYPE_VOICE + §Channel arbitration

## Goal

Hold PTT, talk: mic capture → ADPCM frames with per-packet codec state →
radio, under the documented arbitration rules. Includes the pure-C TX
framer so the tricky logic is host-tested.

## Deliverables

- `firmware/components/voice_pipe/include/voice_pipe.h` (+ `.c`,
  `CMakeLists.txt`) — **pure C** (host-testable, no ESP includes), TX half:

```c
#include "adpcm.h"
#include "convoy_proto.h"

typedef struct { /* adpcm state, seq counter, sample accumulator,
                    burst-active flag; <= 256 bytes, no pointers */ } vp_tx_t;

void vp_tx_init(vp_tx_t *t, uint8_t sender_uid);
void vp_tx_begin_burst(vp_tx_t *t);
/* Feed captured PCM; emits 0..k complete cl_voice_t frames via cb.
 * First frame after begin_burst carries CL_VOICE_F_START. */
void vp_tx_feed(vp_tx_t *t, const int16_t *pcm, size_t n,
                void (*emit)(const cl_voice_t *, void *), void *ctx);
/* Flush the partial tail frame (n_samples < 44 allowed) with
 * CL_VOICE_F_END; a burst with no pending samples emits a 1-sample
 * silent END frame so receivers always see END. */
void vp_tx_end_burst(vp_tx_t *t,
                     void (*emit)(const cl_voice_t *, void *), void *ctx);
```

- `test/host/test_voice_pipe.c` (wire `SRCS_test_voice_pipe` in the
  Makefile: voice_pipe + adpcm + convoy_proto)
- `firmware/apps/convoylink/main/audio_task.c` — TX half: consume PTT
  events (ARMED_WAIT/busy logic per docs/04), `aio_set_mode(CAPTURE)`,
  `aio_read` → `vp_tx_feed` → frames to `tx_q`; 60 s cap; UI state
  (`ptt_tx`/`ptt_busy`) into shared state
- `radio_task.c`: voice frames from `tx_q` get priority over pending
  beacons/relays; drop RX voice frames while own burst active

## Host-test requirements (the framer is where voice bugs would live)

1. Feeding 3 × 44 samples emits 3 frames, seqs consecutive, frame 0 has
   START, none have END; states in frames match an independent chunked
   `adpcm_encode` of the same PCM (snapshot-before property).
2. Odd totals: 100 samples → 2 full frames + END frame with n_samples=12
   after `vp_tx_end_burst`.
3. Instant press-release (0 samples) → exactly one silent END|START frame? 
   No: START on first emitted frame — a zero-sample burst emits one frame
   flagged START|END, n_samples=1, silence. Assert that.
4. Two consecutive bursts: seq continues monotonically across bursts
   (docs/03), second burst re-flags START.
5. Every emitted frame passes `cl_validate`.

## Acceptance — CI

Host tests + firmware build green.

## Acceptance — hardware

Joint with T19 (needs a receiver). Interim single-unit check:
- [ ] PTT down: status bar TX tile lights; `radiostat` voice-tx counter
      runs at ~182/s; release: counter stops, END frame logged
- [ ] Radar keeps updating while PTT held (beacons still flowing)

## Out of scope

RX/jitter/playback (T19), squelch tones, VOX.
