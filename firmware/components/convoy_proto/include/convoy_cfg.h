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

/* --- Voice, SA818 analog UHF (docs/04) ---------------------------------- */
/* Frequency/power/CTCSS are runtime region+channel settings stored in NVS
 * (docs/04 has the legal options table). Defaults: */
#define CL_VOICE_CTCSS_DEFAULT 8   /* CTCSS code 8 = 88.5 Hz            */
#define CL_VOICE_SQUELCH_DEFAULT 4 /* SA818 SQ level 0-8                */
#define CL_VOICE_RSSI_POLL_MS 150  /* carrier-detect poll while idle    */
#define CL_BUSY_HANGOVER_MS 500    /* channel-busy after carrier drops  */
#define CL_TX_MAX_MS 60000         /* stuck-PTT guard                   */

/* --- Neighbour staleness tiers (docs/05) -------------------------------- */
#define NT_STALE_MS 15000u  /* LIVE  below this          */
#define NT_GHOST_MS 60000u  /* STALE below this          */
#define NT_GONE_MS 900000u  /* GHOST below this, then GONE */

/* --- GPS (docs/05) ------------------------------------------------------ */
#define CL_COURSE_VALID_DM_S 15 /* >= 1.5 m/s for valid course */

#endif /* CONVOY_CFG_H */
