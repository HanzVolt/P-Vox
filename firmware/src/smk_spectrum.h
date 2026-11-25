/* SPDX-License-Identifier: GPL-3.0-only */
/* Live output spectrum for the AMP ENV window bottom strip.
 * Display only: never called from the audio ISR, uac paths, or TIMER5.
 * 128 Q15 samples, mean removed (DC rejected), Hann window, 128-point
 * radix-2 real FFT, log-spaced 24 bars, dB-ish heights, optional peak
 * fall. Bars are stacked blocks: blue at the base fading to teal,
 * with only the top block of each bar in red. */
#pragma once
#include <stdint.h>

/* Local isqrt / log2. Kept here so
 * this header does not pull the EQ display helpers into unit tests. */
static uint32_t smk_sa_isqrt(uint64_t x)
{
    uint64_t op = x, res = 0, one = (uint64_t)1 << 62;
    while (one > op)
        one >>= 2;
    while (one != 0) {
        if (op >= res + one) {
            op -= res + one;
            res = (res >> 1) + one;
        } else
            res >>= 1;
        one >>= 2;
    }
    return (uint32_t)res;
}

static int smk_sa_log2_q8(uint32_t x)
{
    int n = 0;
    if (x < 1u)
        return -16 * 256;
    while (x >= 512u) {
        x >>= 1;
        n += 256;
    }
    while (x < 256u) {
        x <<= 1;
        n -= 256;
    }
    return n + 8 * 256 + (int)(((x - 256u) * 256u + 128u) / 256u);
}

enum {
    SMK_SA_N = 128,
    SMK_SA_BARS = 24,
    SMK_SA_HMAX = 256,        /* analyze() output range */
    SMK_SA_PERIOD_MS = 40,    /* ~25 Hz UI redraw, main loop only */
    SMK_SA_STRIP_H = 52,      /* plot height inside the AMP ENV panel */
    SMK_SA_GAP = 4,
    SMK_SA_BLK = 3,           /* stacked-block body height */
    SMK_SA_BLK_GAP = 1,
    SMK_SA_BODY_N = 8
};

/* 20*log10(N/2 * 1/2 * 32767) = 20*log10(1048544) ≈ 120.410 dB, Q8. */
#define SMK_SA_FS_DB_Q8  30825

static const int16_t SMK_SA_HANN[128] = {
         0,     20,     79,    177,    315,    491,    705,    958,
      1247,   1573,   1935,   2331,   2761,   3224,   3719,   4244,
      4799,   5381,   5990,   6624,   7281,   7961,   8660,   9379,
     10114,  10864,  11628,  12403,  13187,  13980,  14778,  15580,
     16383,  17187,  17989,  18787,  19580,  20364,  21139,  21903,
     22653,  23388,  24107,  24806,  25486,  26143,  26777,  27386,
     27968,  28523,  29048,  29543,  30006,  30436,  30832,  31194,
     31520,  31809,  32062,  32276,  32452,  32590,  32688,  32747,
     32767,  32747,  32688,  32590,  32452,  32276,  32062,  31809,
     31520,  31194,  30832,  30436,  30006,  29543,  29048,  28523,
     27968,  27386,  26777,  26143,  25486,  24806,  24107,  23388,
     22653,  21903,  21139,  20364,  19580,  18787,  17989,  17187,
     16384,  15580,  14778,  13980,  13187,  12403,  11628,  10864,
     10114,   9379,   8660,   7961,   7281,   6624,   5990,   5381,
      4799,   4244,   3719,   3224,   2761,   2331,   1935,   1573,
      1247,    958,    705,    491,    315,    177,     79,     20,
};

static const int16_t SMK_SA_COS[65] = {
     32767,  32728,  32609,  32412,  32137,  31785,  31356,  30852,
     30273,  29621,  28898,  28105,  27245,  26319,  25329,  24279,
     23170,  22005,  20787,  19519,  18204,  16846,  15446,  14010,
     12539,  11039,   9512,   7962,   6393,   4808,   3212,   1608,
         0,  -1608,  -3212,  -4808,  -6393,  -7962,  -9512, -11039,
    -12539, -14010, -15446, -16846, -18204, -19519, -20787, -22005,
    -23170, -24279, -25329, -26319, -27245, -28105, -28898, -29621,
    -30273, -30852, -31356, -31785, -32137, -32412, -32609, -32728,
    -32767,
};

static const int16_t SMK_SA_SIN[65] = {
         0,   1608,   3212,   4808,   6393,   7962,   9512,  11039,
     12539,  14010,  15446,  16846,  18204,  19519,  20787,  22005,
     23170,  24279,  25329,  26319,  27245,  28105,  28898,  29621,
     30273,  30852,  31356,  31785,  32137,  32412,  32609,  32728,
     32767,  32728,  32609,  32412,  32137,  31785,  31356,  30852,
     30273,  29621,  28898,  28105,  27245,  26319,  25329,  24279,
     23170,  22005,  20787,  19519,  18204,  16846,  15446,  14010,
     12539,  11039,   9512,   7962,   6393,   4808,   3212,   1608,
         0,
};

static const uint8_t SMK_SA_BAR0[SMK_SA_BARS + 1] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 19, 23, 27, 32, 39, 46, 55, 65
};



/* RGB565, same packing as gfx.c RGB(). */
#define SMK_SA_RGB(r, g, b) ((uint16_t)(((((unsigned)(r)) >> 3) << 11) | ((((unsigned)(g)) >> 2) << 5) | (((unsigned)(b)) >> 3)))

/* Body blocks: blue at the base, fading to teal going up. Not red. */
static const uint16_t SMK_SA_BODY[SMK_SA_BODY_N] = {
    SMK_SA_RGB(  0,  32, 255), /* blue */
    SMK_SA_RGB(  0,  72, 240),
    SMK_SA_RGB(  0, 112, 224),
    SMK_SA_RGB(  0, 144, 208),
    SMK_SA_RGB(  0, 168, 192),
    SMK_SA_RGB(  0, 188, 176),
    SMK_SA_RGB(  0, 200, 160),
    SMK_SA_RGB(  0, 210, 148)  /* teal */
};
/* Only the highest block of a bar uses this red. */
#define SMK_SA_PEAK_RED  SMK_SA_RGB(255, 32, 24)

/* How many stacked blocks fit in h pixels for a 0..HMAX value. */
static int smk_sa_nblocks(uint16_t v, int h)
{
    int stride, maxb;
    if (h <= 0 || v == 0u)
        return 0;
    stride = SMK_SA_BLK + SMK_SA_BLK_GAP;
    maxb = (h + SMK_SA_BLK_GAP) / stride;
    if (maxb < 1)
        maxb = 1;
    return (int)(((uint32_t)v * (uint32_t)maxb + (uint32_t)(SMK_SA_HMAX / 2)) / (uint32_t)SMK_SA_HMAX);
}

/* k = 0 at the base (blue). The last block (k == nblk-1) is red. */
static uint16_t smk_sa_blk_color(int k, int nblk)
{
    int body;
    if (nblk <= 0)
        return 0;
    if (k < 0)
        k = 0;
    if (k >= nblk)
        k = nblk - 1;
    if (k == nblk - 1)
        return SMK_SA_PEAK_RED;
    body = nblk - 1;                            /* body blocks: 0..nblk-2 */
    if (body <= 1)
        return SMK_SA_BODY[0];
    return SMK_SA_BODY[k * (SMK_SA_BODY_N - 1) / (body - 1)];
}

/* Main-loop scratch. Not used by the audio ISR. */
#ifndef SMK_SA_STATE_ATTR
#define SMK_SA_STATE_ATTR      /* Parity (FM-1): its own RAM section, see poli_feat.h */
#endif
static int32_t smk_sa_re[SMK_SA_N] SMK_SA_STATE_ATTR;
static int32_t smk_sa_im[SMK_SA_N] SMK_SA_STATE_ATTR;

static unsigned smk_sa_rev7(unsigned x)
{
    unsigned r = 0, i;
    for (i = 0; i < 7u; i++) {
        r = (r << 1) | (x & 1u);
        x >>= 1;
    }
    return r;
}

static int32_t smk_sa_mulq15(int32_t a, int16_t b)
{
    return (int32_t)(((int64_t)a * (int64_t)b) >> 15);
}

/* In-place radix-2 DIT FFT of smk_sa_re/im, length 128. */
static void smk_sa_fft128(void)
{
    unsigned i, s, m, mh, k, j, t;
    int32_t ur, ui, vr, vi, tr, ti;
    /* bit-reversal permutation */
    for (i = 0; i < (unsigned)SMK_SA_N; i++) {
        j = smk_sa_rev7(i);
        if (j > i) {
            tr = smk_sa_re[i]; smk_sa_re[i] = smk_sa_re[j]; smk_sa_re[j] = tr;
            ti = smk_sa_im[i]; smk_sa_im[i] = smk_sa_im[j]; smk_sa_im[j] = ti;
        }
    }
    for (s = 1; s <= 7u; s++) {
        m = 1u << s;
        mh = m >> 1;
        t = (unsigned)SMK_SA_N / m;          /* twiddle stride */
        for (k = 0; k < (unsigned)SMK_SA_N; k += m) {
            for (j = 0; j < mh; j++) {
                int16_t wr = SMK_SA_COS[j * t];
                int16_t wi = (int16_t)(-SMK_SA_SIN[j * t]); /* exp(-2πi j/m) */
                unsigned a = k + j, b = a + mh;
                vr = smk_sa_mulq15(smk_sa_re[b], wr) - smk_sa_mulq15(smk_sa_im[b], wi);
                vi = smk_sa_mulq15(smk_sa_re[b], wi) + smk_sa_mulq15(smk_sa_im[b], wr);
                ur = smk_sa_re[a];
                ui = smk_sa_im[a];
                smk_sa_re[a] = ur + vr;
                smk_sa_im[a] = ui + vi;
                smk_sa_re[b] = ur - vr;
                smk_sa_im[b] = ui - vi;
            }
        }
    }
}

/* Bar index 0..23 for FFT bins 1..64. Bin 0 (DC) returns -1. */
static int smk_sa_bar_of_bin(int bin)
{
    int k;
    if (bin < 1)
        return -1;
    for (k = 0; k < SMK_SA_BARS; k++) {
        if (bin >= (int)SMK_SA_BAR0[k] && bin < (int)SMK_SA_BAR0[k + 1])
            return k;
    }
    return SMK_SA_BARS - 1;
}

/* Nearest positive bin for a frequency in Hz (bin 1 .. 64). */
static int __attribute__((unused)) smk_sa_bin_of_hz(int hz)
{
    int b;
    if (hz < 0)
        hz = 0;
    b = (int)(((int32_t)hz * (int32_t)SMK_SA_N + 22050) / 44100);
    if (b < 1)
        b = 1;
    if (b > SMK_SA_N / 2)
        b = SMK_SA_N / 2;
    return b;
}

/* Map a bin magnitude onto 0..SMK_SA_HMAX with a -60 dB floor. */
static uint16_t smk_sa_map_db(uint32_t mag)
{
    int db, rel;
    if (mag == 0u)
        return 0;
    db = (smk_sa_log2_q8(mag) * 1541) >> 8;     /* Q8 dB, 20*log10(mag) */
    rel = db - SMK_SA_FS_DB_Q8;
    if (rel <= -60 * 256)
        return 0;
    if (rel >= 0)
        return (uint16_t)SMK_SA_HMAX;
    /* (rel + 60 dB) / 60 dB * HMAX, with HMAX == 256 so this is /60.
     * Written this way so the compiler does not materialize a
     * 0xFFCxxxxx immediate (that range is reserved for chip ROM). */
    return (uint16_t)((rel + 60 * 256) / 60);
}

/* 128 Q15 frames -> 24 bar heights in 0..SMK_SA_HMAX.
 * binmag, if not NULL, receives 65 unsigned magnitudes (bins 0..64). */
static void smk_sa_analyze(const int16_t *x, uint16_t *bars, uint32_t *binmag)
{
    int i, k;
    int32_t acc = 0, mean;
    uint32_t mag;
    if (!x || !bars)
        return;
    for (i = 0; i < SMK_SA_BARS; i++)
        bars[i] = 0;
    for (i = 0; i < SMK_SA_N; i++)
        acc += (int32_t)x[i];
    mean = (acc + (acc >= 0 ? 64 : -64)) >> 7;
    for (i = 0; i < SMK_SA_N; i++) {
        int32_t y = (int32_t)x[i] - mean;
        smk_sa_re[i] = smk_sa_mulq15(y, SMK_SA_HANN[i]);
        smk_sa_im[i] = 0;
    }
    smk_sa_fft128();
    for (i = 0; i <= SMK_SA_N / 2; i++) {
        int64_t rr = (int64_t)smk_sa_re[i];
        int64_t ii = (int64_t)smk_sa_im[i];
        uint64_t p = (uint64_t)(rr * rr + ii * ii);
        mag = smk_sa_isqrt(p);
        if (binmag)
            binmag[i] = mag;
        if (i == 0)
            continue;                           /* DC rejected */
        k = smk_sa_bar_of_bin(i);
        if (k < 0)
            continue;
        if (i == (int)SMK_SA_BAR0[k])
            bars[k] = smk_sa_map_db(mag);
        else {
            uint16_t h = smk_sa_map_db(mag);
            if (h > bars[k])
                bars[k] = h;
        }
    }
}

/* Jump up to a new peak; otherwise fall by 1/8 plus 1 step per tick. */
static void smk_sa_smooth(uint16_t *shown, const uint16_t *now)
{
    int i;
    for (i = 0; i < SMK_SA_BARS; i++) {
        uint16_t s = shown[i], n = now[i];
        if (n >= s)
            shown[i] = n;
        else {
            uint16_t d = (uint16_t)((s >> 3) + 1u);
            shown[i] = (s > d) ? (uint16_t)(s - d) : 0;
        }
    }
}

/* Scale a 0..HMAX height into 0..h pixels. */
static int __attribute__((unused)) smk_sa_bar_h(uint16_t v, int h)
{
    if (h <= 0 || v == 0u)
        return 0;
    return (int)(((uint32_t)v * (uint32_t)h + (uint32_t)(SMK_SA_HMAX / 2)) / (uint32_t)SMK_SA_HMAX);
}
