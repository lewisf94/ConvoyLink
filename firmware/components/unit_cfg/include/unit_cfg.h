/**
 * unit_cfg — NVS-backed identity and radio configuration, plus the
 * provisioning console (docs/07 §Provisioning, docs/01 §Configuration).
 *
 * All five units run an identical binary; everything that distinguishes
 * one from another lives here. An unprovisioned unit boots as `U? --`,
 * transmits nothing, and shows the PROVISION ME banner.
 */
#ifndef UNIT_CFG_H
#define UNIT_CFG_H

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum { CFG_REGION_EU = 0, CFG_REGION_US, CFG_REGION_AU } cfg_region_t;
typedef enum { CFG_VOICE_ESPNOW = 0, CFG_VOICE_SX1262 } cfg_voice_t;

typedef struct {
    uint8_t unit_id;    /* 0..4                                        */
    char initials[2];   /* exactly 2 ASCII, A-Z or 0-9, not NUL-term'd */
    cfg_region_t region;
    cfg_voice_t voice;
} unit_cfg_t;

/** Opens the NVS namespace. Call once, after nvs_flash_init. */
esp_err_t unit_cfg_init(void);

/**
 * Copy the stored configuration. Returns false if the unit has never been
 * provisioned, in which case `out` holds the safe defaults (region EU,
 * voice espnow, initials "--") and every TX path must stay disabled.
 */
bool unit_cfg_get(unit_cfg_t *out);

/** LoRa frequency in Hz for a region (docs/03 table). */
uint32_t unit_cfg_region_freq_hz(cfg_region_t r);

/** Human-readable names for logging and the console. */
const char *unit_cfg_region_name(cfg_region_t r);
const char *unit_cfg_voice_name(cfg_voice_t v);

/**
 * Register the `unitcfg` console command (set / region / voice / show).
 * Changes are written to NVS immediately but only take effect on reboot,
 * which the command tells the user.
 */
esp_err_t unit_cfg_register_console(void);

#endif /* UNIT_CFG_H */
