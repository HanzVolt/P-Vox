/* SPDX-License-Identifier: GPL-3.0-only */
/* Host-only WAV writer. Not part of the instrument. */
#ifndef POLI_WAV_WRITE_H
#define POLI_WAV_WRITE_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int poli_wav_write(const char *path, const int32_t *ch0, const int32_t *ch1, int n)
{
    FILE *f;
    uint8_t hdr[44];
    int32_t rate = 44100;
    int16_t channels = ch1 ? 2 : 1;
    int32_t bytes = n * channels * 2;
    int i;
    f = fopen(path, "wb");
    if (!f) return -1;
    memset(hdr, 0, sizeof hdr);
    memcpy(hdr, "RIFF", 4);
    hdr[4] = (uint8_t)((36 + bytes) & 255);
    hdr[5] = (uint8_t)(((36 + bytes) >> 8) & 255);
    hdr[6] = (uint8_t)(((36 + bytes) >> 16) & 255);
    hdr[7] = (uint8_t)(((36 + bytes) >> 24) & 255);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    hdr[16] = 16;
    hdr[20] = 1;
    hdr[22] = (uint8_t)channels;
    hdr[24] = (uint8_t)(rate & 255);
    hdr[25] = (uint8_t)((rate >> 8) & 255);
    hdr[26] = (uint8_t)((rate >> 16) & 255);
    hdr[27] = (uint8_t)((rate >> 24) & 255);
    {
        int32_t bps = rate * channels * 2;
        hdr[28] = (uint8_t)(bps & 255);
        hdr[29] = (uint8_t)((bps >> 8) & 255);
        hdr[30] = (uint8_t)((bps >> 16) & 255);
        hdr[31] = (uint8_t)((bps >> 24) & 255);
    }
    hdr[32] = (uint8_t)(channels * 2);
    hdr[34] = 16;
    memcpy(hdr + 36, "data", 4);
    hdr[40] = (uint8_t)(bytes & 255);
    hdr[41] = (uint8_t)((bytes >> 8) & 255);
    hdr[42] = (uint8_t)((bytes >> 16) & 255);
    hdr[43] = (uint8_t)((bytes >> 24) & 255);
    if (fwrite(hdr, 1, 44, f) != 44) { fclose(f); return -1; }
    for (i = 0; i < n; i++) {
        int32_t v = ch0[i];
        int16_t s;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        s = (int16_t)v;
        if (fwrite(&s, 2, 1, f) != 1) { fclose(f); return -1; }
        if (ch1) {
            v = ch1[i];
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            s = (int16_t)v;
            if (fwrite(&s, 2, 1, f) != 1) { fclose(f); return -1; }
        }
    }
    fclose(f);
    return 0;
}
#endif
