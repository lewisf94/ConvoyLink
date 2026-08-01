/* audio_io.c — see include/audio_io.h, docs/04-voice.md, docs/02. */
#include "audio_io.h"

#include "convoy_cfg.h"
#include "convoy_pins.h"

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" /* pdMS_TO_TICKS */

#include <string.h>

static const char *TAG = "audio_io";

#define AIO_I2S_PORT I2S_NUM_0
#define AIO_SLOT_BITS I2S_DATA_BIT_WIDTH_32BIT

/* DMA: 4 descriptors of one 20 ms voice frame each = 80 ms of slack, which
 * is comfortably more than the jitter buffer's 60 ms prefill (docs/04). */
#define AIO_DMA_DESCS 4
#define AIO_DMA_FRAMES CL_VOICE_FRAME_SAMPLES

/* Conversion scratch, sized to one voice frame; longer calls loop. Static
 * so nothing allocates after init. */
#define AIO_CHUNK CL_VOICE_FRAME_SAMPLES
static int32_t s_slots[AIO_CHUNK];

/*
 * Fixed capture gain applied after taking the top 16 bits. The INMP441 is
 * a quiet mic, but the right value depends on the finished enclosure and
 * mic placement, so it stays at unity here and is T21's tuning pass to
 * raise. The soft clip below makes raising it safe.
 */
#define AIO_MIC_GAIN 1

static i2s_chan_handle_t s_tx, s_rx;
static aio_mode_t s_mode = AIO_OFF;
static bool s_inited;
static uint8_t s_volume_pct = 100;

static int16_t sat16(int32_t v)
{
    if (v > INT16_MAX) {
        return INT16_MAX;
    }
    if (v < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)v;
}

esp_err_t aio_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(AIO_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = AIO_DMA_DESCS;
    chan_cfg.dma_frame_num = AIO_DMA_FRAMES;

    /* Both handles from one call = one peripheral in full duplex, which is
     * what makes BCLK/WS shared between the mic and the amp. */
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx, &s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CL_AUDIO_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(AIO_SLOT_BITS,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONVOY_PIN_I2S_BCLK,
            .ws = CONVOY_PIN_I2S_WS,
            .dout = CONVOY_PIN_I2S_DOUT,
            .din = CONVOY_PIN_I2S_DIN,
            .invert_flags = {0},
        },
    };
    /* Mic is wired L/R -> GND and the amp decodes the left channel
     * (docs/02), so both directions use the left slot. */
    std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    err = i2s_channel_init_std_mode(s_tx, &std);
    if (err == ESP_OK) {
        err = i2s_channel_init_std_mode(s_rx, &std);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode: %s", esp_err_to_name(err));
        i2s_del_channel(s_tx);
        i2s_del_channel(s_rx);
        s_tx = s_rx = NULL;
        return err;
    }

    s_mode = AIO_OFF; /* both channels stay disabled: amp silent */
    s_inited = true;
    ESP_LOGI(TAG, "I2S%d %d Hz mono, %d-bit slots, bclk=%d ws=%d din=%d dout=%d",
             AIO_I2S_PORT, CL_AUDIO_RATE_HZ, AIO_SLOT_BITS,
             CONVOY_PIN_I2S_BCLK, CONVOY_PIN_I2S_WS, CONVOY_PIN_I2S_DIN,
             CONVOY_PIN_I2S_DOUT);
    return ESP_OK;
}

esp_err_t aio_set_mode(aio_mode_t m)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (m == s_mode) {
        return ESP_OK;
    }

    /* Stop whatever is running first — never leave both enabled. */
    esp_err_t err = ESP_OK;
    if (s_mode == AIO_CAPTURE) {
        err = i2s_channel_disable(s_rx);
    } else if (s_mode == AIO_PLAYBACK) {
        err = i2s_channel_disable(s_tx);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "disable: %s", esp_err_to_name(err));
        return err;
    }
    s_mode = AIO_OFF;

    if (m == AIO_CAPTURE) {
        err = i2s_channel_enable(s_rx);
    } else if (m == AIO_PLAYBACK) {
        err = i2s_channel_enable(s_tx);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "enable: %s", esp_err_to_name(err));
        return err;
    }

    s_mode = m;
    return ESP_OK;
}

aio_mode_t aio_mode(void)
{
    return s_mode;
}

int aio_read(int16_t *pcm, size_t max, uint32_t wait_ms)
{
    if (s_mode != AIO_CAPTURE || pcm == NULL) {
        return -1;
    }

    size_t done = 0;
    while (done < max) {
        size_t want = max - done;
        if (want > AIO_CHUNK) {
            want = AIO_CHUNK;
        }
        size_t got_bytes = 0;
        esp_err_t err = i2s_channel_read(s_rx, s_slots, want * sizeof(int32_t),
                                         &got_bytes, pdMS_TO_TICKS(wait_ms));
        if (err != ESP_OK || got_bytes == 0) {
            break; /* timeout: return whatever we already have */
        }

        size_t got = got_bytes / sizeof(int32_t);
        for (size_t i = 0; i < got; i++) {
            /* 24-bit data left-justified in the slot: the top 16 bits are
             * the sample (docs/04). */
            int32_t s = s_slots[i] >> 16;
            pcm[done + i] = sat16(s * AIO_MIC_GAIN);
        }
        done += got;

        if (got < want) {
            break;
        }
    }
    return (int)done;
}

int aio_write(const int16_t *pcm, size_t n, uint32_t wait_ms)
{
    if (s_mode != AIO_PLAYBACK || pcm == NULL) {
        return -1;
    }

    size_t done = 0;
    while (done < n) {
        size_t want = n - done;
        if (want > AIO_CHUNK) {
            want = AIO_CHUNK;
        }
        for (size_t i = 0; i < want; i++) {
            int32_t s = ((int32_t)pcm[done + i] * s_volume_pct) / 100;
            /* sample sits in the top 16 bits of the 32-bit slot */
            s_slots[i] = sat16(s) << 16;
        }

        size_t put_bytes = 0;
        esp_err_t err = i2s_channel_write(s_tx, s_slots, want * sizeof(int32_t),
                                          &put_bytes, pdMS_TO_TICKS(wait_ms));
        if (err != ESP_OK || put_bytes == 0) {
            break;
        }
        done += put_bytes / sizeof(int32_t);

        if (put_bytes < want * sizeof(int32_t)) {
            break;
        }
    }
    return (int)done;
}

void aio_set_volume(uint8_t pct)
{
    s_volume_pct = (pct > 100) ? 100 : pct;
}
