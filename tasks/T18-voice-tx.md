# T18 — `voice_pipe`: framing + jitter + concealment (pure C)

*(v3: was "voice TX chain"; now the transport-agnostic pure-C voice brain.
Filename kept for queue stability.)*

**Depends:** T02 · **Phase:** M5 (pure C — host-tested)
**Required reading:** `docs/04-voice.md` §voice frame + §Jitter buffer +
§PTT state machine; `docs/03` §Sequence-number comparison

## Goal

The transport-agnostic heart of voice: build `vf_hdr_t` frames from captured
PCM (per-frame ADPCM seed), and on RX run a seq-ordered jitter buffer with
talker-lock and loss concealment. Pure C so all the fiddly logic is
host-tested; the ESP transports (T19, T22) just move the bytes.

## Deliverables

- `firmware/components/voice_pipe/include/voice_proto.h` — the `vf_hdr_t`
  wire layout + `VF_*` constants (the contract in `docs/04`), with
  `_Static_assert`s. This is to voice what `convoy_proto.h` is to beacons.
- `firmware/components/voice_pipe/include/voice_pipe.h` + `voice_pipe.c`
- `firmware/components/voice_pipe/CMakeLists.txt` (REQUIRES adpcm)
- `test/host/test_voice_pipe.c` (Makefile wires `SRCS_test_voice_pipe`)

## Interface contract

```c
#include "adpcm.h"
#include "voice_proto.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- TX: PCM -> frames ---- */
typedef struct { /* adpcm state, seq, uid, accumulator; <=256 B, no ptrs */ } vp_tx_t;
void vp_tx_init(vp_tx_t *t, uint8_t sender_uid);
void vp_tx_begin(vp_tx_t *t);   /* next emitted frame carries VF_F_START */
/* Feed captured PCM; emit each complete CL_VOICE_FRAME_SAMPLES frame via cb.
 * Snapshots adpcm state into the frame BEFORE encoding it (reseed property). */
void vp_tx_feed(vp_tx_t *t, const int16_t *pcm, size_t n,
                void (*emit)(const uint8_t *frame, size_t len, void *), void *ctx);
/* Flush the partial tail (n_samples < 160 ok) with VF_F_END; a burst with
 * no pending samples still emits one START|END silent frame. */
void vp_tx_end(vp_tx_t *t,
               void (*emit)(const uint8_t *frame, size_t len, void *), void *ctx);

/* ---- RX: frames -> PCM ---- */
typedef struct { /* ring of CL_JITTER_SLOTS, play cursor, talker, conceal ct */ } vp_rx_t;
void vp_rx_init(vp_rx_t *r);
/* Validate (magic/codec/sender/len) then insert by seq. Talker lock: first
 * sender of a burst owns it until END/starvation; others dropped. */
bool vp_rx_offer(vp_rx_t *r, const uint8_t *frame, size_t len, uint32_t now_ms);
/* Pull next 20 ms PCM once prefilled (CL_JITTER_PREFILL). Per-frame seed
 * decode; conceal (prev frame x½, up to CL_CONCEAL_MAX) then silence.
 * Returns 1 = frame out, 0 = prefilling/not ready, -1 = burst ended. */
int  vp_rx_pull(vp_rx_t *r, int16_t pcm[CL_VOICE_FRAME_SAMPLES], uint32_t now_ms);
int8_t vp_rx_talker(const vp_rx_t *r);   /* -1 when idle */
```

## Test requirements

1. Framing: 3×160 samples → 3 frames, seqs consecutive, frame 0 `VF_F_START`,
   none `VF_F_END`; frame codec state matches an independent chunked
   `adpcm_encode` (reseed property).
2. Tail: 100 leftover samples + `vp_tx_end` → one END frame, n_bytes for 100
   samples; zero-sample burst → one `START|END` silent frame.
3. Seq continues monotonically across two bursts; second burst re-flags START.
4. Every emitted frame passes the `voice_proto` validity check.
5. **TX→RX loop, clean**: frames fed in order → `vp_rx_pull` PCM bit-identical
   to a chunked `adpcm_decode` reference.
6. **Drop frame k**: pulls conceal (prev ×½, ≤ CL_CONCEAL_MAX), then frames
   k+1… decode bit-identical to loss-free (per-frame seed proof).
7. Reordered (k+1 before k within prefill) plays in order; duplicate ignored;
   frame older than play cursor dropped.
8. Competing talker mid-burst rejected until END; starvation 300 ms → pull
   returns −1, next START opens a fresh burst.

## Acceptance — CI

`make -C test/host test` green.

## Out of scope

I²S (T12), ESP-NOW / radio transports (T19/T22), NVS, UI — pure logic only.
