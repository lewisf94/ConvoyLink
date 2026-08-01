/* bringup_audio — proves the digital audio chain end to end: mic captures,
 * speaker plays, and an ADPCM round-trip through RAM stays intelligible
 * (tasks/T13-bringup-audio.md, docs/04-voice.md).
 *
 * esp_console REPL on UART0. The `codec` command deliberately runs the
 * round-trip one 20 ms frame at a time with the state snapshotted per
 * frame, which is exactly how the on-air voice path works (docs/04
 * §Codecs) — a bulk encode would sound better than the real thing and
 * would be a misleading preview.
 */
#include "adpcm.h"
#include "audio_io.h"
#include "convoy_cfg.h"

#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "bringup_audio";

#define FRAME CL_VOICE_FRAME_SAMPLES /* 160 samples = 20 ms */
#define MAX_REC_SEC 5
#define MAX_REC_SAMPLES (CL_AUDIO_RATE_HZ * MAX_REC_SEC)

#define TONE_DEFAULT_HZ 440
#define TONE_DEFAULT_SEC 2
#define TONE_AMPLITUDE 12000 /* headroom below full scale */

#define METER_HZ 10
#define METER_BAR_CELLS 10

/* Statically allocated once — nothing on the audio path allocates at
 * runtime (CLAUDE.md). 5 s of 8 kHz int16 = 80 KB. */
static int16_t s_rec[MAX_REC_SAMPLES];
static int16_t s_frame[FRAME];
static uint8_t s_codes[(FRAME + 1) / 2];

/* ---- helpers ------------------------------------------------------------ */

/* Records `n` samples into s_rec, returning how many were captured. */
static size_t record(size_t n)
{
    if (aio_set_mode(AIO_CAPTURE) != ESP_OK) {
        printf("cannot enter capture mode\n");
        return 0;
    }
    size_t got = 0;
    while (got < n) {
        int r = aio_read(s_rec + got, n - got, 500);
        if (r <= 0) {
            break;
        }
        got += (size_t)r;
    }
    aio_set_mode(AIO_OFF);
    return got;
}

/* Plays `n` samples from s_rec. */
static void playback(size_t n)
{
    if (aio_set_mode(AIO_PLAYBACK) != ESP_OK) {
        printf("cannot enter playback mode\n");
        return;
    }
    size_t done = 0;
    while (done < n) {
        int w = aio_write(s_rec + done, n - done, 500);
        if (w <= 0) {
            break;
        }
        done += (size_t)w;
    }
    aio_set_mode(AIO_OFF);
}

static size_t clamp_seconds(int argc, char **argv, int idx, int def)
{
    int s = def;
    if (argc > idx) {
        s = atoi(argv[idx]);
    }
    if (s < 1) {
        s = 1;
    }
    if (s > MAX_REC_SEC) {
        printf("capped at %d s (static buffer)\n", MAX_REC_SEC);
        s = MAX_REC_SEC;
    }
    return (size_t)s * CL_AUDIO_RATE_HZ;
}

/* ---- commands ----------------------------------------------------------- */

static int cmd_tone(int argc, char **argv)
{
    int hz = (argc > 1) ? atoi(argv[1]) : TONE_DEFAULT_HZ;
    int secs = (argc > 2) ? atoi(argv[2]) : TONE_DEFAULT_SEC;
    if (hz < 20 || hz > CL_AUDIO_RATE_HZ / 2) {
        printf("hz must be 20..%d (Nyquist at %d Hz sampling)\n",
               CL_AUDIO_RATE_HZ / 2, CL_AUDIO_RATE_HZ);
        return 1;
    }
    if (secs < 1) {
        secs = 1;
    }

    if (aio_set_mode(AIO_PLAYBACK) != ESP_OK) {
        printf("cannot enter playback mode\n");
        return 1;
    }
    printf("tone %d Hz for %d s\n", hz, secs);

    int total_frames = (secs * CL_AUDIO_RATE_HZ) / FRAME;
    double phase = 0.0;
    const double step = 2.0 * M_PI * (double)hz / (double)CL_AUDIO_RATE_HZ;

    for (int f = 0; f < total_frames; f++) {
        for (int i = 0; i < FRAME; i++) {
            s_frame[i] = (int16_t)(TONE_AMPLITUDE * sin(phase));
            phase += step;
            if (phase >= 2.0 * M_PI) {
                phase -= 2.0 * M_PI;
            }
        }
        if (aio_write(s_frame, FRAME, 500) <= 0) {
            printf("write timeout\n");
            break;
        }
    }
    aio_set_mode(AIO_OFF);
    return 0;
}

static int cmd_meter(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (aio_set_mode(AIO_CAPTURE) != ESP_OK) {
        printf("cannot enter capture mode\n");
        return 1;
    }
    printf("mic meter — press Enter to stop\n");

    /* Non-blocking stdin so the meter keeps running until a keypress. */
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    const int frames_per_print = CL_AUDIO_RATE_HZ / (FRAME * METER_HZ);
    for (;;) {
        if (fgetc(stdin) != EOF) {
            break;
        }

        int64_t sumsq = 0;
        int32_t peak = 0;
        size_t counted = 0;
        for (int f = 0; f < frames_per_print; f++) {
            int n = aio_read(s_frame, FRAME, 500);
            if (n <= 0) {
                break;
            }
            for (int i = 0; i < n; i++) {
                int32_t v = s_frame[i];
                sumsq += (int64_t)v * v;
                int32_t a = (v < 0) ? -v : v;
                if (a > peak) {
                    peak = a;
                }
            }
            counted += (size_t)n;
        }
        if (counted == 0) {
            continue;
        }

        int rms = (int)sqrt((double)sumsq / (double)counted);
        int filled = (int)((int64_t)peak * METER_BAR_CELLS / 32768);
        if (filled > METER_BAR_CELLS) {
            filled = METER_BAR_CELLS;
        }

        printf("mic ▏");
        for (int i = 0; i < METER_BAR_CELLS; i++) {
            printf("%s", (i < filled) ? "█" : "░");
        }
        printf("▕ rms=%d pk=%d\n", rms, (int)peak);
    }

    fcntl(STDIN_FILENO, F_SETFL, flags);
    aio_set_mode(AIO_OFF);
    return 0;
}

static int cmd_loop(int argc, char **argv)
{
    size_t n = clamp_seconds(argc, argv, 1, 3);
    printf("recording %u samples...\n", (unsigned)n);
    size_t got = record(n);
    if (got == 0) {
        printf("captured nothing — check the mic wiring\n");
        return 1;
    }
    printf("playing back %u samples (raw)\n", (unsigned)got);
    playback(got);
    return 0;
}

static int cmd_codec(int argc, char **argv)
{
    size_t n = clamp_seconds(argc, argv, 1, 3);
    printf("recording %u samples...\n", (unsigned)n);
    size_t got = record(n);
    if (got == 0) {
        printf("captured nothing — check the mic wiring\n");
        return 1;
    }

    /* Per-frame round-trip, each frame seeded from the state snapshotted
     * before its own encode — the on-air behaviour (docs/04 §Codecs). */
    adpcm_state_t enc;
    adpcm_init(&enc);
    size_t frames = 0;
    for (size_t off = 0; off + FRAME <= got; off += FRAME) {
        adpcm_state_t seed = enc; /* snapshot BEFORE encoding this frame */
        adpcm_encode(&enc, s_rec + off, FRAME, s_codes);

        adpcm_state_t dec = seed;
        adpcm_decode(&dec, s_codes, FRAME, s_rec + off);
        frames++;
    }

    printf("playing back %u frames through ADPCM (%u bytes/frame on air)\n",
           (unsigned)frames, (unsigned)sizeof s_codes);
    playback(frames * FRAME);
    return 0;
}

static int cmd_vol(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: vol <0-100>\n");
        return 1;
    }
    int v = atoi(argv[1]);
    if (v < 0 || v > 100) {
        printf("volume must be 0..100\n");
        return 1;
    }
    aio_set_volume((uint8_t)v);
    printf("volume %d%%\n", v);
    return 0;
}

static void register_commands(void)
{
    static const esp_console_cmd_t cmds[] = {
        {.command = "tone",
         .help = "tone [hz] [s] — play a sine (default 440 Hz, 2 s)",
         .func = cmd_tone},
        {.command = "meter",
         .help = "Live mic RMS/peak bar; Enter stops",
         .func = cmd_meter},
        {.command = "loop",
         .help = "loop [s] — record then play back raw (default 3 s)",
         .func = cmd_loop},
        {.command = "codec",
         .help = "codec [s] — record, ADPCM round-trip, play back",
         .func = cmd_codec},
        {.command = "vol",
         .help = "vol <0-100> — playback volume",
         .func = cmd_vol},
    };
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}

void app_main(void)
{
    esp_err_t err = aio_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "aio_init failed: %s — check I2S wiring/power "
                      "(docs/07 §Troubleshooting)",
                 esp_err_to_name(err));
    }

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "audio>";
    repl_cfg.max_cmdline_length = 128;

    esp_console_dev_uart_config_t uart_cfg =
        ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));

    ESP_ERROR_CHECK(esp_console_register_help_command());
    register_commands();

    printf("\nConvoyLink bringup_audio — 'help' for commands, 'tone' first.\n");
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
