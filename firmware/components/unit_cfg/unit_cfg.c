/* unit_cfg.c — see include/unit_cfg.h, docs/07 §Provisioning. */
#include "unit_cfg.h"

#include "convoy_cfg.h"

#include "esp_console.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "unit_cfg";

#define CFG_NAMESPACE "convoylink"
#define KEY_UNIT_ID "unit_id"
#define KEY_INITIALS "initials"
#define KEY_REGION "region"
#define KEY_VOICE "voice"
#define KEY_BACKLIGHT "backlight"

#define BACKLIGHT_PCT_DEFAULT 100u

static nvs_handle_t s_nvs;
static bool s_open;

esp_err_t unit_cfg_init(void)
{
    if (s_open) {
        return ESP_OK;
    }
    esp_err_t err = nvs_open(CFG_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }
    s_open = true;
    return ESP_OK;
}

uint32_t unit_cfg_region_freq_hz(cfg_region_t r)
{
    /* docs/03 §Regulatory quick reference: AU shares the US frequency. */
    return (r == CFG_REGION_EU) ? CL_LORA_FREQ_EU_HZ : CL_LORA_FREQ_US_HZ;
}

const char *unit_cfg_region_name(cfg_region_t r)
{
    switch (r) {
    case CFG_REGION_US:
        return "US";
    case CFG_REGION_AU:
        return "AU";
    default:
        return "EU";
    }
}

const char *unit_cfg_voice_name(cfg_voice_t v)
{
    return (v == CFG_VOICE_SX1262) ? "sx1262" : "espnow";
}

bool unit_cfg_get(unit_cfg_t *out)
{
    /* Safe defaults first: an unprovisioned unit must still be a valid,
     * silent unit rather than an uninitialised one. */
    unit_cfg_t cfg = {
        .unit_id = 0,
        .initials = {'-', '-'},
        .region = CFG_REGION_EU,
        .voice = CFG_VOICE_ESPNOW,
    };

    bool provisioned = false;
    if (s_open) {
        uint8_t uid = 0;
        size_t len = sizeof cfg.initials;
        char initials[2];

        /* Identity is what "provisioned" means: both id and initials must
         * be present. Region/voice fall back to their defaults. */
        if (nvs_get_u8(s_nvs, KEY_UNIT_ID, &uid) == ESP_OK &&
            nvs_get_blob(s_nvs, KEY_INITIALS, initials, &len) == ESP_OK &&
            len == sizeof cfg.initials && uid < CL_MAX_UNITS) {
            cfg.unit_id = uid;
            cfg.initials[0] = initials[0];
            cfg.initials[1] = initials[1];
            provisioned = true;
        }

        uint8_t v = 0;
        if (nvs_get_u8(s_nvs, KEY_REGION, &v) == ESP_OK && v <= CFG_REGION_AU) {
            cfg.region = (cfg_region_t)v;
        }
        if (nvs_get_u8(s_nvs, KEY_VOICE, &v) == ESP_OK && v <= CFG_VOICE_SX1262) {
            cfg.voice = (cfg_voice_t)v;
        }
    }

    if (out != NULL) {
        *out = cfg;
    }
    return provisioned;
}

esp_err_t unit_cfg_set_backlight_pct(uint8_t pct)
{
    if (!s_open) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pct > 100) {
        pct = 100;
    }
    esp_err_t err = nvs_set_u8(s_nvs, KEY_BACKLIGHT, pct);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err;
}

uint8_t unit_cfg_get_backlight_pct(void)
{
    uint8_t pct = BACKLIGHT_PCT_DEFAULT;
    if (s_open) {
        uint8_t stored;
        if (nvs_get_u8(s_nvs, KEY_BACKLIGHT, &stored) == ESP_OK &&
            stored <= 100) {
            pct = stored;
        }
    }
    return pct;
}

/* ---- console ------------------------------------------------------------ */

static bool valid_initial(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static int cmd_show(void)
{
    unit_cfg_t cfg;
    bool prov = unit_cfg_get(&cfg);
    printf("unit_id=%u initials=%c%c region=%s voice=%s%s\n", cfg.unit_id,
           cfg.initials[0], cfg.initials[1], unit_cfg_region_name(cfg.region),
           unit_cfg_voice_name(cfg.voice),
           prov ? "" : "   (UNPROVISIONED — transmits nothing)");
    return 0;
}

static int cmd_set(int argc, char **argv)
{
    if (argc != 4) {
        printf("usage: unitcfg set <0-%d> <AA>\n", CL_MAX_UNITS - 1);
        return 1;
    }
    int id = atoi(argv[2]);
    if (id < 0 || id >= CL_MAX_UNITS) {
        printf("unit_id must be 0..%d\n", CL_MAX_UNITS - 1);
        return 1;
    }
    const char *ini = argv[3];
    if (strlen(ini) != 2 || !valid_initial(ini[0]) || !valid_initial(ini[1])) {
        printf("initials must be exactly 2 chars, A-Z or 0-9 (docs/07)\n");
        return 1;
    }

    esp_err_t err = nvs_set_u8(s_nvs, KEY_UNIT_ID, (uint8_t)id);
    if (err == ESP_OK) {
        err = nvs_set_blob(s_nvs, KEY_INITIALS, ini, 2);
    }
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    if (err != ESP_OK) {
        printf("save failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("unit_id=%d initials=%c%c saved — reboot to apply\n", id, ini[0],
           ini[1]);
    return 0;
}

static int cmd_region(int argc, char **argv)
{
    if (argc != 3) {
        printf("usage: unitcfg region <EU|US|AU>\n");
        return 1;
    }
    cfg_region_t r;
    if (strcasecmp(argv[2], "EU") == 0) {
        r = CFG_REGION_EU;
    } else if (strcasecmp(argv[2], "US") == 0) {
        r = CFG_REGION_US;
    } else if (strcasecmp(argv[2], "AU") == 0) {
        r = CFG_REGION_AU;
    } else {
        printf("region must be EU, US or AU\n");
        return 1;
    }

    esp_err_t err = nvs_set_u8(s_nvs, KEY_REGION, (uint8_t)r);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    if (err != ESP_OK) {
        printf("save failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("region=%s (%" PRIu32 " Hz) saved — reboot to apply. Every unit in "
           "the convoy must match.\n",
           unit_cfg_region_name(r), unit_cfg_region_freq_hz(r));
    return 0;
}

static int cmd_voice(int argc, char **argv)
{
    if (argc != 3) {
        printf("usage: unitcfg voice <espnow|sx1262>\n");
        return 1;
    }
    cfg_voice_t v;
    if (strcasecmp(argv[2], "espnow") == 0) {
        v = CFG_VOICE_ESPNOW;
    } else if (strcasecmp(argv[2], "sx1262") == 0) {
        v = CFG_VOICE_SX1262;
    } else {
        printf("voice transport must be espnow or sx1262\n");
        return 1;
    }

    esp_err_t err = nvs_set_u8(s_nvs, KEY_VOICE, (uint8_t)v);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    if (err != ESP_OK) {
        printf("save failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("voice=%s saved — reboot to apply. Mixing transports means units "
           "cannot hear each other (docs/07).\n",
           unit_cfg_voice_name(v));
    return 0;
}

static int cmd_unitcfg(int argc, char **argv)
{
    if (!s_open) {
        printf("NVS not open\n");
        return 1;
    }
    if (argc < 2) {
        printf("usage: unitcfg set <0-%d> <AA> | region <EU|US|AU> | "
               "voice <espnow|sx1262> | show\n",
               CL_MAX_UNITS - 1);
        return 1;
    }
    if (strcmp(argv[1], "show") == 0) {
        return cmd_show();
    }
    if (strcmp(argv[1], "set") == 0) {
        return cmd_set(argc, argv);
    }
    if (strcmp(argv[1], "region") == 0) {
        return cmd_region(argc, argv);
    }
    if (strcmp(argv[1], "voice") == 0) {
        return cmd_voice(argc, argv);
    }
    printf("unknown subcommand '%s'\n", argv[1]);
    return 1;
}

esp_err_t unit_cfg_register_console(void)
{
    static const esp_console_cmd_t cmd = {
        .command = "unitcfg",
        .help = "unitcfg set <0-4> <AA> | region <EU|US|AU> | "
                "voice <espnow|sx1262> | show",
        .func = cmd_unitcfg,
    };
    return esp_console_cmd_register(&cmd);
}
