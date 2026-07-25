/**
 * frame_dump — minimal uncompressed 24-bit BMP writer, no external image
 * libraries (T07).
 */
#ifndef SIM_FRAME_DUMP_H
#define SIM_FRAME_DUMP_H

#include <stdint.h>

/**
 * Writes a w*h RGB888 image (row-major, top-down, 3 bytes/pixel, R,G,B
 * order) to path as a standard uncompressed 24bpp BMP. Returns 0 on
 * success, negative on failure (path not writable).
 */
int bmp_write(const char *path, int w, int h, const uint8_t *rgb888);

#endif /* SIM_FRAME_DUMP_H */
