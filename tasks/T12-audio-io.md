# T12 — `sa818` component: voice module control

*(v2: replaces the original `audio_io` ADC/DAC component — voice moved to
the analog SA818 module, `docs/00` decision log. Filename kept for queue
stability.)*

**Depends:** none · **Phase:** M3
**Required reading:** `docs/04-voice-sa818.md` (all); `docs/02` §SA818 wiring

## Goal

UART command/response driver for the SA818S-U: channel configuration,
PTT control, RSSI polling. This is the entire "voice" firmware surface —
no audio samples are ever handled in code.

## Deliverables

- `firmware/components/sa818/include/sa818.h`
- `firmware/components/sa818/sa818.c`
- `firmware/components/sa818/CMakeLists.txt` (REQUIRES driver, convoy_pins)

## Interface contract

```c
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t bw;            /* 0 = 12.5 kHz (all v1 channel plans use this) */
    uint32_t freq_hz_x10;  /* e.g. 4460562 for 446.05625 MHz, fits exactly */
    uint8_t ctcss_code;    /* 0 = none, else per SA818 CTCSS table */
    uint8_t squelch;       /* 0..8 */
} sa818_channel_t;

/* UART1 init on CONVOY_PIN_VHF_* @ 9600 8N1, PTT GPIO to RX (idle high),
 * AT+DMOCONNECT handshake (retry x3, 500 ms apart). */
esp_err_t sa818_init(void);

/* AT+DMOSETGROUP with tx==rx (simplex). Re-sent automatically by
 * sa818_init's caller after any esp_err_t failure from this function. */
esp_err_t sa818_set_channel(const sa818_channel_t *ch);

esp_err_t sa818_set_volume(uint8_t vol_1_to_8);

/* Drives the PTT GPIO directly (docs/02: low = TX). No UART round trip -
 * this must be fast and glitch-free. */
void sa818_ptt(bool transmit);

/* AT+RSSI? parsed to a 0-255-ish module-reported value; carrier_present
 * applies CL_VOICE_SQUELCH_DEFAULT-consistent thresholding (docs/04). */
esp_err_t sa818_rssi(uint8_t *raw_out, bool *carrier_present);
```

Implementation notes: all AT commands end `\r\n`, expect a `+DMO…:0`
reply within 500 ms (timeout → `ESP_ERR_TIMEOUT`, caller retries per
docs/04); no dynamic allocation; `sa818_ptt` never touches UART, only the
GPIO, so it can't be delayed by a pending command.

## Acceptance — CI

`./tools/ci_build_apps.sh` green (compiles standalone; add to T13's app).

## Acceptance — hardware

Deferred to T13's checklist (channel set, PTT key, RSSI read).

## Out of scope

PTT state machine / UI wiring (T18), carrier-detect polling loop (T18's
`voice_task`), any audio signal path (there isn't one in firmware).
