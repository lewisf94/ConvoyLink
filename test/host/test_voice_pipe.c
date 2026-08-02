/* test_voice_pipe.c — framing, jitter buffer, concealment and talker lock
 * (T18; contract in docs/04-voice.md). */
#include "tinytest.h"
#include "voice_pipe.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FRAME CL_VOICE_FRAME_SAMPLES

/* ---- capture helper: collect emitted frames -------------------------- */

#define MAX_CAP 32

typedef struct {
    uint8_t frame[MAX_CAP][VF_FRAME_MAX];
    size_t len[MAX_CAP];
    int n;
} cap_t;

static void cap_emit(const uint8_t *frame, size_t len, void *ctx)
{
    cap_t *c = (cap_t *)ctx;
    if (c->n >= MAX_CAP) {
        return;
    }
    memcpy(c->frame[c->n], frame, len);
    c->len[c->n] = len;
    c->n++;
}

static const vf_hdr_t *hdr_of(const cap_t *c, int i)
{
    return (const vf_hdr_t *)(const void *)c->frame[i];
}

/* A deterministic, reasonably speech-like signal. */
static void fill_signal(int16_t *pcm, size_t n, size_t phase)
{
    for (size_t i = 0; i < n; i++) {
        double t = (double)(i + phase);
        pcm[i] = (int16_t)(6000.0 * sin(t * 0.07) + 2500.0 * sin(t * 0.31));
    }
}

/* ---- 1. framing + reseed property ------------------------------------ */

TT_TEST(framing_three_frames_seqs_flags_and_reseed)
{
    int16_t pcm[3 * FRAME];
    fill_signal(pcm, sizeof pcm / sizeof pcm[0], 0);

    vp_tx_t tx;
    vp_tx_init(&tx, 2);
    vp_tx_begin(&tx);

    cap_t cap = {0};
    vp_tx_feed(&tx, pcm, 3 * FRAME, cap_emit, &cap);

    TT_ASSERT_EQ(cap.n, 3);
    for (int i = 0; i < 3; i++) {
        const vf_hdr_t *h = hdr_of(&cap, i);
        TT_ASSERT_EQ(h->sender, 2);
        TT_ASSERT_EQ(h->seq, i);
        TT_ASSERT_EQ(h->n_bytes, FRAME / 2);
        TT_ASSERT_EQ(cap.len[i], VF_HDR_SIZE + (size_t)(FRAME / 2));
        TT_ASSERT_EQ((h->flags & VF_F_END), 0);
    }
    TT_ASSERT_EQ((hdr_of(&cap, 0)->flags & VF_F_START), VF_F_START);
    TT_ASSERT_EQ((hdr_of(&cap, 1)->flags & VF_F_START), 0);
    TT_ASSERT_EQ((hdr_of(&cap, 2)->flags & VF_F_START), 0);

    /* Reseed property: each frame's seed is the encoder state as it was
     * BEFORE that frame, so an independent chunked encode must agree. */
    adpcm_state_t ref;
    adpcm_init(&ref);
    for (int i = 0; i < 3; i++) {
        const vf_hdr_t *h = hdr_of(&cap, i);
        int16_t seed_pred =
            (int16_t)((uint16_t)h->seed[0] | ((uint16_t)h->seed[1] << 8));
        TT_ASSERT_EQ(seed_pred, ref.predictor);
        TT_ASSERT_EQ(h->seed[2], ref.step_index);

        uint8_t codes[FRAME / 2];
        adpcm_encode(&ref, pcm + i * FRAME, FRAME, codes);
        TT_ASSERT_MEMEQ(cap.frame[i] + VF_HDR_SIZE, codes, sizeof codes);
    }
}

/* ---- 2. tail + empty burst ------------------------------------------- */

TT_TEST(tail_flush_and_empty_burst)
{
    int16_t pcm[100];
    fill_signal(pcm, 100, 0);

    vp_tx_t tx;
    vp_tx_init(&tx, 0);
    vp_tx_begin(&tx);

    cap_t cap = {0};
    vp_tx_feed(&tx, pcm, 100, cap_emit, &cap);
    TT_ASSERT_EQ(cap.n, 0); /* not a full frame yet */

    vp_tx_end(&tx, cap_emit, &cap);
    TT_ASSERT_EQ(cap.n, 1);
    TT_ASSERT_EQ((hdr_of(&cap, 0)->flags & VF_F_END), VF_F_END);
    TT_ASSERT_EQ(hdr_of(&cap, 0)->n_bytes, 50); /* 100 samples -> 50 bytes */

    /* A burst with nothing in it still produces one START|END frame. */
    vp_tx_t tx2;
    vp_tx_init(&tx2, 1);
    vp_tx_begin(&tx2);
    cap_t cap2 = {0};
    vp_tx_end(&tx2, cap_emit, &cap2);
    TT_ASSERT_EQ(cap2.n, 1);
    TT_ASSERT_EQ(hdr_of(&cap2, 0)->flags, VF_F_START | VF_F_END);
    TT_ASSERT_EQ(hdr_of(&cap2, 0)->n_bytes, 0);
}

/* ---- 3. seq across bursts -------------------------------------------- */

TT_TEST(seq_monotonic_across_bursts)
{
    int16_t pcm[FRAME];
    fill_signal(pcm, FRAME, 0);

    vp_tx_t tx;
    vp_tx_init(&tx, 3);
    cap_t cap = {0};

    vp_tx_begin(&tx);
    vp_tx_feed(&tx, pcm, FRAME, cap_emit, &cap);
    vp_tx_end(&tx, cap_emit, &cap);
    int after_first = cap.n;

    vp_tx_begin(&tx);
    vp_tx_feed(&tx, pcm, FRAME, cap_emit, &cap);
    vp_tx_end(&tx, cap_emit, &cap);

    TT_ASSERT(cap.n > after_first);
    for (int i = 0; i < cap.n; i++) {
        TT_ASSERT_EQ(hdr_of(&cap, i)->seq, i); /* never restarts */
    }
    /* The second burst re-flags START on its first frame. */
    TT_ASSERT_EQ((hdr_of(&cap, after_first)->flags & VF_F_START), VF_F_START);
}

/* ---- 4. every frame validates ---------------------------------------- */

TT_TEST(every_emitted_frame_validates)
{
    int16_t pcm[2 * FRAME + 37];
    fill_signal(pcm, sizeof pcm / sizeof pcm[0], 5);

    vp_tx_t tx;
    vp_tx_init(&tx, 4);
    vp_tx_begin(&tx);
    cap_t cap = {0};
    vp_tx_feed(&tx, pcm, sizeof pcm / sizeof pcm[0], cap_emit, &cap);
    vp_tx_end(&tx, cap_emit, &cap);

    TT_ASSERT(cap.n >= 3);
    for (int i = 0; i < cap.n; i++) {
        TT_ASSERT_EQ(vf_validate(cap.frame[i], cap.len[i]), VF_OK);
    }

    /* And the validator actually rejects damage. */
    uint8_t bad[VF_FRAME_MAX];
    memcpy(bad, cap.frame[0], cap.len[0]);
    bad[0] = 0xC7; /* the beacon magic must not pass here */
    TT_ASSERT_EQ(vf_validate(bad, cap.len[0]), VF_ERR_MAGIC);

    memcpy(bad, cap.frame[0], cap.len[0]);
    bad[2] = CL_MAX_UNITS; /* sender out of range */
    TT_ASSERT_EQ(vf_validate(bad, cap.len[0]), VF_ERR_SENDER);

    /* Truncated buffer must not be trusted via n_bytes. */
    TT_ASSERT_EQ(vf_validate(cap.frame[0], VF_HDR_SIZE + 1), VF_ERR_SIZE);
    TT_ASSERT_EQ(vf_validate(cap.frame[0], 3), VF_ERR_SIZE);
}

/* ---- helpers for the RX tests ---------------------------------------- */

/* Encode `frames` frames of signal into cap, returning the reference PCM
 * that a loss-free decode must reproduce. */
static void make_burst(cap_t *cap, int16_t *ref_pcm, int frames, uint8_t uid)
{
    int16_t *pcm = malloc(sizeof(int16_t) * (size_t)frames * FRAME);
    fill_signal(pcm, (size_t)frames * FRAME, 11);

    vp_tx_t tx;
    vp_tx_init(&tx, uid);
    vp_tx_begin(&tx);
    vp_tx_feed(&tx, pcm, (size_t)frames * FRAME, cap_emit, cap);
    vp_tx_end(&tx, cap_emit, cap);

    /* Reference: decode each captured frame from its own seed. */
    for (int i = 0; i < cap->n; i++) {
        const vf_hdr_t *h = hdr_of(cap, i);
        adpcm_state_t st;
        st.predictor =
            (int16_t)((uint16_t)h->seed[0] | ((uint16_t)h->seed[1] << 8));
        st.step_index = h->seed[2];
        size_t n = (size_t)h->n_bytes * 2u;
        if (n > FRAME) {
            n = FRAME;
        }
        int16_t tmp[FRAME];
        memset(tmp, 0, sizeof tmp);
        if (n > 0) {
            adpcm_decode(&st, cap->frame[i] + VF_HDR_SIZE, n, tmp);
        }
        memcpy(ref_pcm + (size_t)i * FRAME, tmp, sizeof tmp);
    }
    free(pcm);
}

/* ---- 5. clean TX -> RX loop ------------------------------------------ */

TT_TEST(rx_clean_loop_matches_reference)
{
    enum { N = 6 };
    cap_t cap = {0};
    int16_t ref[(N + 1) * FRAME];
    memset(ref, 0, sizeof ref);
    make_burst(&cap, ref, N, 1);

    vp_rx_t rx;
    vp_rx_init(&rx);
    for (int i = 0; i < cap.n; i++) {
        TT_ASSERT(vp_rx_offer(&rx, cap.frame[i], cap.len[i], 1000));
    }
    TT_ASSERT_EQ(vp_rx_talker(&rx), 1);

    int16_t out[FRAME];
    for (int i = 0; i < cap.n; i++) {
        TT_ASSERT_EQ(vp_rx_pull(&rx, out, 1000), 1);
        TT_ASSERT_MEMEQ(out, ref + (size_t)i * FRAME, sizeof out);
    }
    /* END played -> burst closed. */
    TT_ASSERT_EQ(vp_rx_pull(&rx, out, 1000), -1);
    TT_ASSERT_EQ(vp_rx_talker(&rx), -1);
}

/* ---- 6. dropped frame: conceal, then exact again --------------------- */

TT_TEST(dropped_frame_conceals_then_resyncs_exactly)
{
    enum { N = 6, DROP = 2 };
    cap_t cap = {0};
    int16_t ref[(N + 1) * FRAME];
    memset(ref, 0, sizeof ref);
    make_burst(&cap, ref, N, 0);

    vp_rx_t rx;
    vp_rx_init(&rx);
    for (int i = 0; i < cap.n; i++) {
        if (i == DROP) {
            continue; /* frame lost in transit */
        }
        TT_ASSERT(vp_rx_offer(&rx, cap.frame[i], cap.len[i], 1000));
    }

    int16_t out[FRAME];
    for (int i = 0; i < cap.n; i++) {
        TT_ASSERT_EQ(vp_rx_pull(&rx, out, 1000), 1);
        if (i == DROP) {
            /* Concealment: previous frame at half amplitude. */
            for (int k = 0; k < FRAME; k++) {
                int16_t want = (int16_t)(ref[(size_t)(DROP - 1) * FRAME + k] / 2);
                TT_ASSERT_EQ(out[k], want);
            }
        } else {
            /* Per-frame seeding: every other frame is still exact. */
            TT_ASSERT_MEMEQ(out, ref + (size_t)i * FRAME, sizeof out);
        }
    }
}

/* ---- 7. reordering, duplicates, late frames -------------------------- */

TT_TEST(reordered_duplicate_and_late_frames)
{
    enum { N = 6 };
    cap_t cap = {0};
    int16_t ref[(N + 1) * FRAME];
    memset(ref, 0, sizeof ref);
    make_burst(&cap, ref, N, 2);

    vp_rx_t rx;
    vp_rx_init(&rx);

    /* Offer 0, then 2 before 1 — all still inside the prefill window. */
    TT_ASSERT(vp_rx_offer(&rx, cap.frame[0], cap.len[0], 1000));
    TT_ASSERT(vp_rx_offer(&rx, cap.frame[2], cap.len[2], 1000));
    TT_ASSERT(vp_rx_offer(&rx, cap.frame[1], cap.len[1], 1000));
    /* A duplicate of an already-buffered frame is ignored. */
    TT_ASSERT(!vp_rx_offer(&rx, cap.frame[1], cap.len[1], 1000));

    for (int i = 3; i < cap.n; i++) {
        TT_ASSERT(vp_rx_offer(&rx, cap.frame[i], cap.len[i], 1000));
    }

    /* Playback order is by seq, not arrival order. */
    int16_t out[FRAME];
    for (int i = 0; i < 3; i++) {
        TT_ASSERT_EQ(vp_rx_pull(&rx, out, 1000), 1);
        TT_ASSERT_MEMEQ(out, ref + (size_t)i * FRAME, sizeof out);
    }

    /* Frame 0 arriving now is behind the play cursor: dropped. */
    TT_ASSERT(!vp_rx_offer(&rx, cap.frame[0], cap.len[0], 1000));
}

/* ---- 8. talker lock + starvation ------------------------------------- */

TT_TEST(talker_lock_and_starvation)
{
    enum { N = 4 };
    cap_t a = {0}, b = {0};
    int16_t ref_a[(N + 1) * FRAME], ref_b[(N + 1) * FRAME];
    memset(ref_a, 0, sizeof ref_a);
    memset(ref_b, 0, sizeof ref_b);
    make_burst(&a, ref_a, N, 1);
    make_burst(&b, ref_b, N, 3);

    vp_rx_t rx;
    vp_rx_init(&rx);

    TT_ASSERT(vp_rx_offer(&rx, a.frame[0], a.len[0], 1000));
    TT_ASSERT_EQ(vp_rx_talker(&rx), 1);

    /* A competing talker is rejected while the burst is locked. */
    TT_ASSERT(!vp_rx_offer(&rx, b.frame[0], b.len[0], 1010));
    TT_ASSERT(!vp_rx_offer(&rx, b.frame[1], b.len[1], 1020));
    TT_ASSERT_EQ(vp_rx_talker(&rx), 1);

    /* Starvation: nothing more from U1 for the hangover window. The one
     * frame we did receive is still played out — a starved burst flushes
     * rather than discarding audio — and the burst ends on the next pull. */
    int16_t out[FRAME];
    uint32_t starve_t = 1000 + CL_VOICE_HANGOVER_MS + 1;
    TT_ASSERT_EQ(vp_rx_pull(&rx, out, starve_t), 1);
    TT_ASSERT_MEMEQ(out, ref_a, sizeof out);
    TT_ASSERT_EQ(vp_rx_pull(&rx, out, starve_t), -1);
    TT_ASSERT_EQ(vp_rx_talker(&rx), -1);

    /* Now U3 can open a fresh burst. */
    uint32_t t = 5000;
    TT_ASSERT(vp_rx_offer(&rx, b.frame[0], b.len[0], t));
    TT_ASSERT_EQ(vp_rx_talker(&rx), 3);
    for (int i = 1; i < b.n; i++) {
        TT_ASSERT(vp_rx_offer(&rx, b.frame[i], b.len[i], t));
    }
    for (int i = 0; i < b.n; i++) {
        TT_ASSERT_EQ(vp_rx_pull(&rx, out, t), 1);
        TT_ASSERT_MEMEQ(out, ref_b + (size_t)i * FRAME, sizeof out);
    }
    TT_ASSERT_EQ(vp_rx_pull(&rx, out, t), -1);
}

/* ---- prefill behaviour ------------------------------------------------ */

TT_TEST(prefill_holds_playback_until_ready)
{
    enum { N = 8 };
    cap_t cap = {0};
    int16_t ref[(N + 1) * FRAME];
    memset(ref, 0, sizeof ref);
    make_burst(&cap, ref, N, 0);

    vp_rx_t rx;
    vp_rx_init(&rx);
    int16_t out[FRAME];

    /* Fewer than CL_JITTER_PREFILL frames buffered and no END in sight:
     * playback must wait rather than start and immediately starve. */
    for (int i = 0; i < CL_JITTER_PREFILL - 1; i++) {
        TT_ASSERT(vp_rx_offer(&rx, cap.frame[i], cap.len[i], 1000));
        TT_ASSERT_EQ(vp_rx_pull(&rx, out, 1000), 0);
    }

    TT_ASSERT(vp_rx_offer(&rx, cap.frame[CL_JITTER_PREFILL - 1],
                          cap.len[CL_JITTER_PREFILL - 1], 1000));
    TT_ASSERT_EQ(vp_rx_pull(&rx, out, 1000), 1);
    TT_ASSERT_MEMEQ(out, ref, sizeof out);
}

int main(void)
{
    TT_RUN(framing_three_frames_seqs_flags_and_reseed);
    TT_RUN(tail_flush_and_empty_burst);
    TT_RUN(seq_monotonic_across_bursts);
    TT_RUN(every_emitted_frame_validates);
    TT_RUN(rx_clean_loop_matches_reference);
    TT_RUN(dropped_frame_conceals_then_resyncs_exactly);
    TT_RUN(reordered_duplicate_and_late_frames);
    TT_RUN(talker_lock_and_starvation);
    TT_RUN(prefill_holds_playback_until_ready);
    return tt_summary("voice_pipe");
}
