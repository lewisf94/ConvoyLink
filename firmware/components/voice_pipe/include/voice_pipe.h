/**
 * voice_pipe — the transport-agnostic heart of digital voice: PCM into
 * frames on TX, and a seq-ordered jitter buffer with talker lock and loss
 * concealment on RX (docs/04-voice.md §Jitter buffer & concealment).
 *
 * The transports (ESP-NOW in T19, SX1262/Codec2 in T22) only move the
 * bytes these functions produce and consume, which is what keeps all the
 * fiddly ordering logic host-testable.
 *
 * Pure C: no ESP-IDF headers, no allocation, host-testable.
 *
 * Per-frame reseeding is the property everything else rests on: each
 * frame carries the ADPCM state as it was *before* that frame was
 * encoded, so a lost frame costs only its own 20 ms — the next frame
 * decodes bit-identically whether or not its predecessor arrived.
 */
#ifndef VOICE_PIPE_H
#define VOICE_PIPE_H

#include "adpcm.h"
#include "voice_proto.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Called for each complete frame; `frame` is only valid during the call. */
typedef void (*vp_emit_fn)(const uint8_t *frame, size_t len, void *ctx);

/* ---- TX: PCM -> frames -------------------------------------------------- */

typedef struct {
    adpcm_state_t enc;
    uint16_t seq;
    uint8_t sender_uid;
    bool pending_start; /* next emitted frame carries VF_F_START */
    int16_t acc[CL_VOICE_FRAME_SAMPLES];
    size_t acc_n;
} vp_tx_t;

void vp_tx_init(vp_tx_t *t, uint8_t sender_uid);

/** Marks the next emitted frame with VF_F_START (start of a burst). */
void vp_tx_begin(vp_tx_t *t);

/**
 * Feed captured PCM. Every time a full CL_VOICE_FRAME_SAMPLES has
 * accumulated, one frame is emitted through `emit`. Partial tails stay
 * buffered until the next feed or vp_tx_end.
 */
void vp_tx_feed(vp_tx_t *t, const int16_t *pcm, size_t n, vp_emit_fn emit,
                void *ctx);

/**
 * End the burst: flushes any partial tail with VF_F_END. A burst with no
 * pending samples still emits one silent START|END frame, so the receiver
 * always sees a clean burst boundary.
 */
void vp_tx_end(vp_tx_t *t, vp_emit_fn emit, void *ctx);

/* ---- RX: frames -> PCM -------------------------------------------------- */

typedef struct {
    bool used;
    uint16_t seq;
    uint8_t flags;
    uint8_t n_bytes;
    uint8_t seed[3];
    uint8_t payload[VF_ADPCM_MAX_BYTES];
} vp_slot_t;

typedef struct {
    vp_slot_t ring[CL_JITTER_SLOTS];
    bool active;         /* a burst is in progress                     */
    bool playing;        /* prefill satisfied, playback started        */
    int8_t talker;       /* -1 when idle                               */
    uint16_t next_seq;   /* seq we want to play next                   */
    bool have_next_seq;
    uint32_t last_rx_ms; /* for the starvation timeout                 */
    uint8_t conceal_run; /* consecutive concealed frames               */
    bool saw_end;        /* END frame is buffered or already played    */
    int16_t prev_pcm[CL_VOICE_FRAME_SAMPLES]; /* for concealment       */
    bool have_prev;
} vp_rx_t;

void vp_rx_init(vp_rx_t *r);

/**
 * Validate and insert a received frame. Talker lock: the first sender of
 * a burst owns it until END or starvation, and frames from anyone else
 * are dropped meanwhile. Frames older than the play cursor, duplicates,
 * and invalid frames are rejected.
 *
 * Returns true if the frame was accepted into the buffer.
 */
bool vp_rx_offer(vp_rx_t *r, const uint8_t *frame, size_t len,
                 uint32_t now_ms);

/**
 * Pull the next 20 ms of PCM.
 *   1  = a frame was written to `pcm`
 *   0  = still prefilling, or nothing to play yet
 *  -1  = the burst ended (END played, or starvation) — the pipe is idle
 *
 * A missing frame is concealed by repeating the previous one at half
 * amplitude up to CL_CONCEAL_MAX times, then silence.
 */
int vp_rx_pull(vp_rx_t *r, int16_t pcm[CL_VOICE_FRAME_SAMPLES],
               uint32_t now_ms);

/** Current talker uid, or -1 when idle. */
int8_t vp_rx_talker(const vp_rx_t *r);

#endif /* VOICE_PIPE_H */
