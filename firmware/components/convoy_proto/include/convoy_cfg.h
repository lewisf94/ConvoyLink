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

/* --- LoRa data link, SX1262 (docs/03) ----------------------------------- */
#define CL_PKT_SIZE 32 /* fixed payload, always     */

/* Region is a runtime NVS setting (docs/07 provisioning); these are the
 * per-region radio profiles it selects between. */
#define CL_LORA_FREQ_EU_HZ 869525000 /* 869.40-869.65 sub-band: 500 mW, 10% duty */
#define CL_LORA_FREQ_US_HZ 915000000 /* 902-928 ISM: no duty limit    */
#define CL_LORA_SF 7                 /* SF7/BW125/CR4:5 ~= 61 ms/beacon */
#define CL_LORA_BW_KHZ 125
#define CL_LORA_CR 5                 /* 4/5                             */
#define CL_LORA_PREAMBLE 8
#define CL_LORA_SYNC_PRIVATE 0x12
#define CL_LORA_TX_DBM 22

#define CL_BEACON_PERIOD_MS 5000
#define CL_BEACON_JITTER_MS 200  /* +/- applied per beacon   */
#define CL_RELAY_DELAY_MIN_MS 150 /* relay backoff window (> beacon airtime) */
#define CL_RELAY_DELAY_MAX_MS 450

/* --- Voice, digital transport-abstracted (docs/04) ---------------------- */
/* Pipeline (same for every transport): 8 kHz mono, 20 ms frames. */
#define CL_AUDIO_RATE_HZ 8000
#define CL_VOICE_FRAME_MS 20
#define CL_VOICE_FRAME_SAMPLES 160 /* 8000 * 0.020                     */
#define CL_JITTER_SLOTS 16
#define CL_JITTER_PREFILL 3        /* ~60 ms before playback starts     */
#define CL_CONCEAL_MAX 3           /* consecutive concealed frames      */
#define CL_VOICE_HANGOVER_MS 300   /* "someone talking" holdover        */
#define CL_VOICE_TX_MAX_MS 60000   /* stuck-PTT guard                   */

/* Transport A: ESP-NOW (ships first), S3 2.4 GHz radio, <=100 mW EIRP.
 * Fixed channel; the firmware never joins/hosts a network (docs/04). */
#define CL_ESPNOW_CHANNEL 6

/* Transport B: SX1262 GFSK + Codec2 (range upgrade). 869.7-870 MHz,
 * ERP <= 5 mW, no duty limit on that sub-band (docs/04). */
#define CL_VOICE_GFSK_FREQ_HZ 869850000
#define CL_VOICE_GFSK_BR_BPS 9600

/* Voice frame wire format (docs/04 §voice frame; layout in voice_proto.h). */
#define VF_MAGIC 0xC8u             /* distinct from beacon CL_MAGIC 0xC7 */

/* --- Neighbour staleness tiers (docs/05) -------------------------------- */
#define NT_STALE_MS 15000u  /* LIVE  below this          */
#define NT_GHOST_MS 60000u  /* STALE below this          */
#define NT_GONE_MS 900000u  /* GHOST below this, then GONE */

/* --- GPS (docs/05) ------------------------------------------------------ */
#define CL_COURSE_VALID_DM_S 15 /* >= 1.5 m/s for valid course */

#endif /* CONVOY_CFG_H */
