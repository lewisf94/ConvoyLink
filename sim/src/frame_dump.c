/* frame_dump.c — see frame_dump.h */
#include "frame_dump.h"

#include <stdio.h>

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

int bmp_write(const char *path, int w, int h, const uint8_t *rgb888)
{
    int row_bytes = w * 3;
    int pad = (4 - (row_bytes % 4)) % 4;
    uint32_t data_size = (uint32_t)((row_bytes + pad) * h);
    uint32_t file_size = 14u + 40u + data_size;

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }

    uint8_t fh[14] = {0};
    fh[0] = 'B';
    fh[1] = 'M';
    put_u32(fh + 2, file_size);
    put_u32(fh + 10, 14u + 40u); /* pixel data offset */
    fwrite(fh, 1, sizeof fh, f);

    uint8_t ih[40] = {0};
    put_u32(ih + 0, 40);
    put_u32(ih + 4, (uint32_t)w);
    put_u32(ih + 8, (uint32_t)h); /* positive height = bottom-up rows */
    put_u16(ih + 12, 1);          /* planes                          */
    put_u16(ih + 14, 24);         /* bits per pixel                  */
    fwrite(ih, 1, sizeof ih, f);

    static const uint8_t padbuf[3] = {0, 0, 0};
    for (int y = h - 1; y >= 0; y--) {
        const uint8_t *row = rgb888 + (size_t)y * (size_t)row_bytes;
        for (int x = 0; x < w; x++) {
            uint8_t bgr[3] = {row[x * 3 + 2], row[x * 3 + 1], row[x * 3 + 0]};
            fwrite(bgr, 1, sizeof bgr, f);
        }
        if (pad > 0) {
            fwrite(padbuf, 1, (size_t)pad, f);
        }
    }

    fclose(f);
    return 0;
}
