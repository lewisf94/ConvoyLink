# T19 — Voice RX chain (jitter buffer + playback) — closes M5

**Depends:** T18 · **Phase:** M5
**Required reading:** `docs/04-audio.md` §RX path & jitter buffer (binding)

## Goal

Hear the convoy: received voice frames → seq-ordered jitter buffer →
per-packet-seeded ADPCM decode with concealment → DAC. Talker lock, busy
hangover, UI talker indicator.

## Deliverables

- `voice_pipe` RX half (same pure-C component, extends T18's files):

```c
typedef struct { /* slot ring of CL_JITTER_SLOTS frames, play cursor,
                    talker uid, conceal counter, last PCM frame */ } vp_rx_t;

void vp_rx_init(vp_rx_t *r);
/* Offer a received (already cl_validate'd) frame. Handles talker lock
 * (first sender wins until burst end), dup/late drop, START/END flags.
 * Returns whether accepted. */
bool vp_rx_offer(vp_rx_t *r, const cl_voice_t *f, uint32_t now_ms);
/* Pull the next 44-sample PCM frame for playback once prefilled
 * (CL_JITTER_PREFILL). Applies per-packet state seeding and the
 * concealment policy (repeat-at-half-amplitude x CL_CONCEAL_MAX, then
 * silence). Returns: 1 = frame out, 0 = not ready/prefilling,
 * -1 = burst ended (drained after END or 300 ms starvation). */
int  vp_rx_pull(vp_rx_t *r, int16_t pcm[CL_FRAME_SAMPLES], uint32_t now_ms);
int8_t vp_rx_talker(const vp_rx_t *r);   /* -1 when idle */
```

- `test/host/test_voice_pipe.c` — extend with RX cases (below)
- `audio_task.c` — RX half: when idle and `voice_rx_q` yields a frame,
  `aio_set_mode(PLAYBACK)`, pump `vp_rx_pull` → `aio_write`; on burst end
  return to AIO_OFF; publish talker uid to state (UI + chip highlight);
  keep the busy-hangover timestamp radio_task uses
- `ui_task.c`: RX talker indicator per docs/06 (status bar + chip border)

## Host-test requirements (drive vp_rx with synthetic frame streams)

1. Clean burst TX→RX loop: frames from `vp_tx_*` fed in order →
   pulled PCM bit-identical to chunked `adpcm_decode` reference.
2. Drop frame k: pulls for k conceal (previous frame ×½, count ≤ 3);
   frames k+1… decode **bit-identical** to loss-free (per-packet seeding
   proof, mirrors T02 test 3 at pipeline level).
3. Reordered arrival (k+1 before k within prefill window) plays in order.
4. Duplicate frame ignored; frame older than play cursor dropped.
5. Competing talker mid-burst rejected by lock; accepted after END.
6. Starvation: no frames for 300 ms → pull returns −1 (burst closed);
   next START opens a fresh burst.
7. Zero-sample START|END burst (T18 case 3) plays silence, closes cleanly.

## Acceptance — CI

Host tests + firmware build green.

## Acceptance — hardware (owner checklist == M5 gate, 2 units)

- [ ] Bench, two units 5 m apart: PTT conversation both directions,
      intelligible, no lockups across 20 exchanges
- [ ] Latency clap test: speaker-to-ear delay subjectively instant
      (< ~¼ s); note impression in STATUS.md
- [ ] Talker initials show on the receiver's status bar while speaking
- [ ] Both-press-at-once: second presser sees BUSY, gets the channel when
      first releases (arbitration behaves)
- [ ] Radar dots keep updating during a 30 s monologue
- [ ] Field: intelligible at ≥ 300 m between cars (record actual)

## Out of scope

Roger beep, per-unit volume memory, voice relay (never), recording.
