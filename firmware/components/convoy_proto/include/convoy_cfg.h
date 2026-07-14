/**
 * convoy_cfg.h — project-wide tunables, shared by firmware, host tests and
 * the simulator. Values here mirror the design docs; if you change one,
 * update the doc it cites in the same commit.
 *
 * Pure C header: no ESP-IDF includes allowed.
 */
#ifndef CONVOY_CFG_H
#define CONVOY_CFG_H

/* --- Convoy shape (docs/00) ------------------------------------------- */
#define CL_MAX_UNITS 5 /* unit ids 0..4 */

/* --- Radio (docs/03) --------------------------------------------------- */
#define CL_RF_CHANNEL 76 /* 2.476 GHz */
#define CL_RF_ADDR \
    { 0x43, 0x4C, 0x4E, 0x4B, 0x31 } /* "CLNK1", shared broadcast */
#define CL_PKT_SIZE 32               /* fixed payload, always   */

#define CL_BEACON_PERIOD_MS 5000
#define CL_BEACON_JITTER_MS 200  /* +/- applied per beacon   */
#define CL_BEACON_DEFER_MAX_MS 250
#define CL_RELAY_DELAY_MIN_MS 80 /* relay backoff window     */
#define CL_RELAY_DELAY_MAX_MS 280
#define CL_BUSY_HANGOVER_MS 300 /* channel-busy after last voice frame */

/* --- Audio (docs/04) ---------------------------------------------------- */
#define CL_AUDIO_RATE_HZ 8000
#define CL_FRAME_SAMPLES 44 /* per cl_voice_t packet     */
#define CL_DAC_RATE_HZ 32000 /* ZOH x4 upsample           */
#define CL_JITTER_SLOTS 16
#define CL_JITTER_PREFILL 3
#define CL_CONCEAL_MAX 3 /* consecutive concealed frames */
#define CL_TX_MAX_MS 60000 /* stuck-PTT guard           */

/* --- Neighbour staleness tiers (docs/05) -------------------------------- */
#define NT_STALE_MS 15000u  /* LIVE  below this          */
#define NT_GHOST_MS 60000u  /* STALE below this          */
#define NT_GONE_MS 900000u  /* GHOST below this, then GONE */

/* --- GPS (docs/05) ------------------------------------------------------ */
#define CL_COURSE_VALID_DM_S 15 /* >= 1.5 m/s for valid course */

#endif /* CONVOY_CFG_H */
