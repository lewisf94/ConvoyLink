/**
 * voice_transport — the swappable radio underneath digital voice
 * (docs/04-voice.md §Transport abstraction).
 *
 * voice_task holds one `const voice_transport_t *`, chosen at boot from
 * NVS. Nothing else in the pipeline knows which radio it is, which is
 * what lets the SX1262/Codec2 transport (T22) drop in later without
 * touching voice_pipe or voice_task's state machine.
 */
#ifndef VOICE_TRANSPORT_H
#define VOICE_TRANSPORT_H

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    esp_err_t (*init)(void);
    esp_err_t (*deinit)(void);
    /** Send exactly one vf_hdr_t frame. */
    esp_err_t (*send)(const uint8_t *frame, size_t len);
    /** Pop one received frame; returns its length, or 0 on timeout. */
    int (*recv)(uint8_t *frame, size_t max, uint32_t wait_ms);
    /** True while someone else's burst is in progress. */
    bool (*busy)(void);
} voice_transport_t;

/**
 * Look up a transport by its NVS name ("espnow" | "sx1262"). Returns NULL
 * for an unknown name, or for one that is not built into this firmware.
 */
const voice_transport_t *voice_transport_get(const char *name);

/** Frames in / out / dropped, for the `voice` console diagnostic. */
void voice_transport_stats(uint32_t *sent, uint32_t *recvd, uint32_t *dropped);

#endif /* VOICE_TRANSPORT_H */
