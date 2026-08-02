/* voice_pipe.c — see include/voice_pipe.h and docs/04-voice.md. */
#include "voice_pipe.h"

#include "convoy_proto.h" /* cl_seq_newer — one seq comparator everywhere */

#include <string.h>

/* ---- voice_proto validation --------------------------------------------- */

int vf_validate(const uint8_t *buf, size_t len)
{
    if (buf == NULL || len < VF_HDR_SIZE) {
        return VF_ERR_SIZE;
    }
    vf_hdr_t h;
    memcpy(&h, buf, sizeof h); /* buf may be unaligned off a radio driver */

    if (h.magic != VF_MAGIC) {
        return VF_ERR_MAGIC;
    }
    if (vf_version(&h) != VF_PROTO_VER) {
        return VF_ERR_VERSION;
    }
    uint8_t codec = vf_codec(&h);
    if (codec != VF_CODEC_ADPCM && codec != VF_CODEC_CODEC2_3200) {
        return VF_ERR_CODEC;
    }
    if (h.sender >= CL_MAX_UNITS) {
        return VF_ERR_SENDER;
    }
    /* Check the claimed length against the buffer before trusting it. */
    if (len < (size_t)VF_HDR_SIZE + h.n_bytes) {
        return VF_ERR_SIZE;
    }
    if (codec == VF_CODEC_ADPCM && h.n_bytes > VF_ADPCM_MAX_BYTES) {
        return VF_ERR_LENGTH;
    }
    return VF_OK;
}

/* ---- TX ------------------------------------------------------------------ */

void vp_tx_init(vp_tx_t *t, uint8_t sender_uid)
{
    memset(t, 0, sizeof *t);
    adpcm_init(&t->enc);
    t->sender_uid = sender_uid;
    t->pending_start = true;
}

void vp_tx_begin(vp_tx_t *t)
{
    t->pending_start = true;
}

/* Encodes `n` samples into one frame and hands it to `emit`. The seed is
 * snapshotted BEFORE encoding, which is what makes each frame
 * independently decodable (docs/04 §Codecs). */
static void emit_frame(vp_tx_t *t, const int16_t *pcm, size_t n, uint8_t flags,
                       vp_emit_fn emit, void *ctx)
{
    uint8_t buf[VF_FRAME_MAX];
    vf_hdr_t h;

    adpcm_state_t seed = t->enc;

    h.magic = VF_MAGIC;
    h.ver_codec = (uint8_t)((VF_PROTO_VER << 4) | VF_CODEC_ADPCM);
    h.sender = t->sender_uid;
    h.flags = flags;
    h.seq = t->seq++;
    h.n_bytes = (uint8_t)((n + 1) / 2);
    h.seed[0] = (uint8_t)(seed.predictor & 0xFFu);
    h.seed[1] = (uint8_t)((uint16_t)seed.predictor >> 8);
    h.seed[2] = seed.step_index;

    memcpy(buf, &h, sizeof h);
    if (n > 0) {
        adpcm_encode(&t->enc, pcm, n, buf + VF_HDR_SIZE);
    }

    if (emit != NULL) {
        emit(buf, (size_t)VF_HDR_SIZE + h.n_bytes, ctx);
    }
}

void vp_tx_feed(vp_tx_t *t, const int16_t *pcm, size_t n, vp_emit_fn emit,
                void *ctx)
{
    size_t off = 0;
    while (off < n) {
        size_t space = CL_VOICE_FRAME_SAMPLES - t->acc_n;
        size_t take = (n - off < space) ? (n - off) : space;
        memcpy(t->acc + t->acc_n, pcm + off, take * sizeof(int16_t));
        t->acc_n += take;
        off += take;

        if (t->acc_n == CL_VOICE_FRAME_SAMPLES) {
            uint8_t flags = t->pending_start ? VF_F_START : 0u;
            t->pending_start = false;
            emit_frame(t, t->acc, t->acc_n, flags, emit, ctx);
            t->acc_n = 0;
        }
    }
}

void vp_tx_end(vp_tx_t *t, vp_emit_fn emit, void *ctx)
{
    uint8_t flags = VF_F_END;
    if (t->pending_start) {
        flags |= VF_F_START; /* burst with nothing in it: one silent frame */
        t->pending_start = false;
    }
    emit_frame(t, t->acc, t->acc_n, flags, emit, ctx);
    t->acc_n = 0;
    t->pending_start = true; /* the next burst starts fresh */
}

/* ---- RX ------------------------------------------------------------------ */

void vp_rx_init(vp_rx_t *r)
{
    memset(r, 0, sizeof *r);
    r->talker = -1;
}

int8_t vp_rx_talker(const vp_rx_t *r)
{
    return r->talker;
}

static vp_slot_t *slot_for(vp_rx_t *r, uint16_t seq)
{
    return &r->ring[seq % CL_JITTER_SLOTS];
}

static void reset_burst(vp_rx_t *r)
{
    memset(r->ring, 0, sizeof r->ring);
    r->active = false;
    r->playing = false;
    r->talker = -1;
    r->have_next_seq = false;
    r->conceal_run = 0;
    r->saw_end = false;
    r->have_prev = false;
}

bool vp_rx_offer(vp_rx_t *r, const uint8_t *frame, size_t len,
                 uint32_t now_ms)
{
    if (vf_validate(frame, len) != VF_OK) {
        return false;
    }
    vf_hdr_t h;
    memcpy(&h, frame, sizeof h);

    if (vf_codec(&h) != VF_CODEC_ADPCM) {
        return false; /* Codec2 decode is T22's transport */
    }

    /* Talker lock (docs/04): the first sender of a burst owns it. A START
     * from anyone is only allowed to open a burst when none is active. */
    if (!r->active) {
        if ((h.flags & VF_F_START) == 0) {
            return false; /* mid-burst frame with no burst to join */
        }
        reset_burst(r);
        r->active = true;
        r->talker = (int8_t)h.sender;
        r->next_seq = h.seq;
        r->have_next_seq = true;
    } else if (h.sender != (uint8_t)r->talker) {
        return false; /* competing talker: dropped until END/starvation */
    } else if (r->have_next_seq && cl_seq_newer(r->next_seq, h.seq)) {
        return false; /* older than the play cursor: too late to use */
    }

    vp_slot_t *s = slot_for(r, h.seq);
    if (s->used && s->seq == h.seq) {
        return false; /* duplicate */
    }

    s->used = true;
    s->seq = h.seq;
    s->flags = h.flags;
    s->n_bytes = h.n_bytes;
    memcpy(s->seed, h.seed, sizeof s->seed);
    memset(s->payload, 0, sizeof s->payload);
    if (h.n_bytes > 0) {
        memcpy(s->payload, frame + VF_HDR_SIZE, h.n_bytes);
    }

    if (h.flags & VF_F_END) {
        r->saw_end = true;
    }
    r->last_rx_ms = now_ms;
    return true;
}

/* How many buffered frames are ready from next_seq forward. */
static int buffered_ahead(const vp_rx_t *r)
{
    int n = 0;
    for (int i = 0; i < CL_JITTER_SLOTS; i++) {
        uint16_t want = (uint16_t)(r->next_seq + i);
        const vp_slot_t *s = &r->ring[want % CL_JITTER_SLOTS];
        if (!s->used || s->seq != want) {
            break;
        }
        n++;
    }
    return n;
}

static void decode_slot(const vp_slot_t *s, int16_t *pcm)
{
    adpcm_state_t st;
    st.predictor = (int16_t)((uint16_t)s->seed[0] | ((uint16_t)s->seed[1] << 8));
    st.step_index = s->seed[2];

    size_t n = (size_t)s->n_bytes * 2u;
    if (n > CL_VOICE_FRAME_SAMPLES) {
        n = CL_VOICE_FRAME_SAMPLES;
    }
    if (n > 0) {
        adpcm_decode(&st, s->payload, n, pcm);
    }
    for (size_t i = n; i < CL_VOICE_FRAME_SAMPLES; i++) {
        pcm[i] = 0; /* short tail frame: pad with silence */
    }
}

int vp_rx_pull(vp_rx_t *r, int16_t pcm[CL_VOICE_FRAME_SAMPLES],
               uint32_t now_ms)
{
    if (!r->active) {
        return -1;
    }

    /* Starvation: nothing new heard for the hangover window ends the
     * burst, even without an END (the talker drove out of range). */
    bool starved =
        (uint32_t)(int32_t)(now_ms - r->last_rx_ms) >= CL_VOICE_HANGOVER_MS;

    if (starved && buffered_ahead(r) == 0) {
        reset_burst(r);
        return -1;
    }

    if (!r->playing) {
        /* Wait for the prefill — unless the whole burst is already here
         * (a short burst can be fewer frames than the prefill), or we
         * have starved. Without that last condition a burst that
         * delivered fewer than CL_JITTER_PREFILL frames and then went
         * quiet would sit in prefill forever, holding the talker lock and
         * never playing the audio it did receive. */
        if (buffered_ahead(r) < CL_JITTER_PREFILL && !r->saw_end && !starved) {
            return 0;
        }
        r->playing = true;
    }

    vp_slot_t *s = slot_for(r, r->next_seq);
    if (s->used && s->seq == r->next_seq) {
        decode_slot(s, pcm);
        memcpy(r->prev_pcm, pcm, sizeof r->prev_pcm);
        r->have_prev = true;
        r->conceal_run = 0;

        bool was_end = (s->flags & VF_F_END) != 0;
        s->used = false;
        r->next_seq++;

        if (was_end) {
            reset_burst(r);
        }
        return 1;
    }

    /* Frame missing. Conceal by repeating the previous one at half
     * amplitude, up to CL_CONCEAL_MAX, then silence (docs/04). */
    if (r->conceal_run < CL_CONCEAL_MAX && r->have_prev) {
        for (int i = 0; i < CL_VOICE_FRAME_SAMPLES; i++) {
            r->prev_pcm[i] = (int16_t)(r->prev_pcm[i] / 2);
        }
        memcpy(pcm, r->prev_pcm, sizeof r->prev_pcm);
    } else {
        memset(pcm, 0, sizeof(int16_t) * CL_VOICE_FRAME_SAMPLES);
    }
    r->conceal_run++;
    r->next_seq++;

    /* If the END was buffered further ahead and we have now walked past
     * everything, stop rather than concealing forever. */
    if (r->conceal_run > CL_CONCEAL_MAX && buffered_ahead(r) == 0 &&
        r->saw_end) {
        reset_burst(r);
        return -1;
    }
    return 1;
}
