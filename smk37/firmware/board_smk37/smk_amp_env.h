/* SPDX-License-Identifier: GPL-3.0-only */
/* AMP / FILTER EG plot geometry. Display only; never called from the
 * sample loop or TIMER5. The engine itself is unchanged.
 *
 * Time axis: each stage's width tracks its 7-bit fader (the log of the
 * firmware V3 nfull law is linear in the knob). Knob 0 is width 0
 * (vertical). A fixed window of three full-scale stages means moving
 * one fader never rescales the others.
 *
 * Shape: one-pole RC contours from poli_eg (attack charges toward
 * 34000 and stops at the peak; decay settles to 1/1000 of the gap to
 * sustain; release aims at -26 and stops at 0), sampled per column.
 * Sustain height is linear in the 0..127 fader, baseline to top.
 */
#ifndef SMK_AMP_ENV_H
#define SMK_AMP_ENV_H

#include <stdint.h>

#ifndef POLI_AMPENV_STAGES
#define POLI_AMPENV_STAGES
enum {
    POLI_AMPENV_IDLE = 0,
    POLI_AMPENV_ATK  = 1,
    POLI_AMPENV_DEC  = 2,
    POLI_AMPENV_SUS  = 3,
    POLI_AMPENV_REL  = 4
};
#endif

enum {
    SMK_AMPENV_PEAK  = 26000,  /* engine attack clip */
    SMK_AMPENV_YMAX  = 28000,  /* engine sustain ceiling / visual top */
    SMK_AMPENV_WSUS  = 20,     /* fixed sustain-hold width, pixels */
    SMK_AMPENV_PAD   = 1,      /* keep a vertical edge inside the box */
    SMK_AMPENV_WREF  = 127 * 3,/* fixed window: three full-scale stages */
    SMK_AMPENV_OVER  = 34000,  /* attack RC rail */
    SMK_AMPENV_RELT  = 26      /* release aims here below 0 */
};

typedef struct {
    int x0, y0, w, h;
    int x_peak, x_sus0, x_sus1, x_end;
    int y_peak, y_sus, y_zero;
    int32_t sus_lvl;
} smk_ampenv_geom_t;

/* Q16 2^(k/16) for k = 0..16. */
static const uint32_t SMK_AMPENV_EXP2_16[17] = {
    65536u, 68438u, 71468u, 74632u, 77936u, 81386u, 84990u, 88752u,
    92682u, 96785u, 101070u, 105545u, 110218u, 115098u, 120194u,
    125515u, 131072u
};

/* 2^(xq8/256) as Q16. xq8 is signed Q8, about -16..16. */
static uint32_t smk_ampenv_exp2_q16(int xq8)
{
    int e, frac8, idx, sub;
    uint32_t a, b, m;
    if (xq8 >= 31 * 256) return 0xFFFFFFFFu;
    if (xq8 <= -31 * 256) return 0;
    if (xq8 >= 0) {
        e = xq8 >> 8;
        frac8 = xq8 & 255;
    } else {
        int pos = -xq8;
        e = -(pos >> 8);
        frac8 = 0;
        if (pos & 255) {
            e -= 1;
            frac8 = 256 - (pos & 255);
        }
    }
    idx = frac8 >> 4;
    sub = frac8 & 15;
    a = SMK_AMPENV_EXP2_16[idx];
    b = SMK_AMPENV_EXP2_16[idx + 1];
    m = a + ((b - a) * (uint32_t)sub) / 16u;
    if (e >= 0) {
        if (e >= 16) return 0xFFFFFFFFu;
        return m << e;
    }
    e = -e;
    if (e >= 16) return 0;
    return m >> e;
}

/* log2(1000) in Q8 ≈ 2551; log2((OVER-PEAK)/OVER)=log2(8/34) in Q8 ≈ -534. */
enum {
    SMK_AMPENV_LOG2_1000_Q8 = 2551,
    SMK_AMPENV_LOG2_ATK_Q8  = -534
};

/* Full fader-throw height: level 0 is the bottom pixel, SMK_AMPENV_YMAX
 * (sustain 127) is the top pixel. Rounded so each sustain step moves
 * about one pixel on a typical plot. */
static int smk_ampenv_y_of(int y0, int h, int32_t level)
{
    int bot, span;
    if (h < 2)
        return y0;
    if (level < 0) level = 0;
    if (level > SMK_AMPENV_YMAX) level = SMK_AMPENV_YMAX;
    bot = y0 + h - 1;
    span = bot - y0;
    return bot - (int)(((int32_t)span * level + SMK_AMPENV_YMAX / 2) / SMK_AMPENV_YMAX);
}

/* a,d,s,r are 7-bit fader positions. Width ∝ knob, 0 at 0, fixed window. */
static void smk_ampenv_geom(int a, int d, int s, int r,
                            int x0, int y0, int w, int h, smk_ampenv_geom_t *g)
{
    int inner, rest, xa, xd, xs, xr;
    if (!g) return;
    if (w < 8) w = 8;
    if (h < 8) h = 8;
    if (a < 0) a = 0;
    if (a > 127) a = 127;
    if (d < 0) d = 0;
    if (d > 127) d = 127;
    if (r < 0) r = 0;
    if (r > 127) r = 127;
    if (s < 0) s = 0;
    if (s > 127) s = 127;
    inner = w - 2 * SMK_AMPENV_PAD;
    if (inner < 1) inner = 1;
    xs = SMK_AMPENV_WSUS;
    if (xs > inner / 2) xs = inner / 2;
    if (xs < 1 && inner >= 1) xs = 1;
    rest = inner - xs;
    if (rest < 0) rest = 0;
    xa = (int)(((int32_t)a * rest) / SMK_AMPENV_WREF);
    xd = (int)(((int32_t)d * rest) / SMK_AMPENV_WREF);
    xr = (int)(((int32_t)r * rest) / SMK_AMPENV_WREF);
    g->x0 = x0 + SMK_AMPENV_PAD;
    g->y0 = y0;
    g->w = w;
    g->h = h;
    g->x_peak = g->x0 + xa;
    g->x_sus0 = g->x_peak + xd;
    g->x_sus1 = g->x_sus0 + xs;
    g->x_end = g->x_sus1 + xr;
    g->sus_lvl = (int32_t)s * SMK_AMPENV_YMAX / 127;
    g->y_zero = smk_ampenv_y_of(y0, h, 0);
    g->y_peak = smk_ampenv_y_of(y0, h, SMK_AMPENV_YMAX);
    g->y_sus  = smk_ampenv_y_of(y0, h, g->sus_lvl);
}

static void smk_ampenv_points(const smk_ampenv_geom_t *g, int xs[5], int ys[5])
{
    if (!g || !xs || !ys) return;
    xs[0] = g->x0;     ys[0] = g->y_zero;
    xs[1] = g->x_peak; ys[1] = g->y_peak;
    xs[2] = g->x_sus0; ys[2] = g->y_sus;
    xs[3] = g->x_sus1; ys[3] = g->y_sus;
    xs[4] = g->x_end;  ys[4] = g->y_zero;
}

/* RC attack, decay and release at normalised time u in Q8 (0..256). */
static int32_t smk_ampenv_atk_lvl(int uq8)
{
    uint32_t rem;
    int32_t lvl;
    if (uq8 <= 0) return 0;
    if (uq8 >= 256) return SMK_AMPENV_YMAX;
    /* remain = OVER * ((OVER-PEAK)/OVER)^u ; env = OVER - remain, then
     * scale so u=1 lands on YMAX (visual full throw). */
    rem = (uint32_t)SMK_AMPENV_OVER * smk_ampenv_exp2_q16((SMK_AMPENV_LOG2_ATK_Q8 * uq8) / 256);
    rem >>= 16;
    lvl = (int32_t)SMK_AMPENV_OVER - (int32_t)rem;
    if (lvl < 0) return 0;
    /* 0..PEAK -> 0..YMAX */
    return (int32_t)(((int64_t)lvl * SMK_AMPENV_YMAX + SMK_AMPENV_PEAK / 2) / SMK_AMPENV_PEAK);
}

static int32_t smk_ampenv_dec_lvl(int uq8, int32_t sus)
{
    uint32_t frac;
    if (uq8 <= 0) return SMK_AMPENV_YMAX;
    if (uq8 >= 256) return sus;
    /* (YMAX - sus) * 1000^{-u} */
    frac = smk_ampenv_exp2_q16((-SMK_AMPENV_LOG2_1000_Q8 * uq8) / 256);
    return sus + (int32_t)(((int64_t)(SMK_AMPENV_YMAX - sus) * frac) >> 16);
}

static int32_t smk_ampenv_rel_lvl(int uq8, int32_t sus)
{
    int logq8;
    uint32_t frac;
    int32_t lvl;
    if (sus <= 0) return 0;
    if (uq8 <= 0) return sus;
    if (uq8 >= 256) return 0;
    /* level = -REL_T + (sus+REL_T) * (REL_T/(sus+REL_T))^u
     *        = -REL_T + (sus+REL_T) * 2^{u * log2(REL_T/(sus+REL_T))} */
    {
        /* log2((sus+REL_T)/REL_T) ≈ log2(sus+REL_T) - log2(REL_T). Use
         * integer log2 of the ratio via a small loop on the Q8 log. */
        uint32_t num = (uint32_t)(sus + SMK_AMPENV_RELT);
        uint32_t den = (uint32_t)SMK_AMPENV_RELT;
        int e = 0;
        /* log2(num/den) in Q8: shift until num/den is in [1,2), then mantissa. */
        while (num >= den * 2u && e < 30) { den <<= 1; e++; }
        while (num < den && e > -30) { num <<= 1; e--; }
        /* num/den in [1,2): frac ≈ (num - den) / den */
        logq8 = (e << 8) + (int)(((num - den) * 256u) / den);
    }
    frac = smk_ampenv_exp2_q16((-logq8 * uq8) / 256);
    lvl = -SMK_AMPENV_RELT + (int32_t)(((int64_t)(sus + SMK_AMPENV_RELT) * frac) >> 16);
    if (lvl < 0) return 0;
    if (lvl > sus) return sus;
    return lvl;
}

static int smk_ampenv_u8(int x, int x0, int x1)
{
    int d = x1 - x0;
    if (d <= 0) return 0;
    if (x <= x0) return 0;
    if (x >= x1) return 256;
    return (int)(((int32_t)(x - x0) * 256 + d / 2) / d);
}

/* Envelope Y at screen column x (inclusive corners). */
static int smk_ampenv_y_at(const smk_ampenv_geom_t *g, int x)
{
    int u;
    int32_t lvl;
    if (!g) return 0;
    if (x <= g->x0)
        return g->y_zero;
    if (x < g->x_peak) {
        u = smk_ampenv_u8(x, g->x0, g->x_peak);
        lvl = smk_ampenv_atk_lvl(u);
        return smk_ampenv_y_of(g->y0, g->h, lvl);
    }
    if (x == g->x_peak)
        return g->y_peak;
    if (x < g->x_sus0) {
        u = smk_ampenv_u8(x, g->x_peak, g->x_sus0);
        lvl = smk_ampenv_dec_lvl(u, g->sus_lvl);
        return smk_ampenv_y_of(g->y0, g->h, lvl);
    }
    if (x <= g->x_sus1)
        return g->y_sus;
    if (x < g->x_end) {
        u = smk_ampenv_u8(x, g->x_sus1, g->x_end);
        lvl = smk_ampenv_rel_lvl(u, g->sus_lvl);
        return smk_ampenv_y_of(g->y0, g->h, lvl);
    }
    return g->y_zero;
}

static int smk_ampenv_lerpi(int a, int b, int32_t num, int32_t den)
{
    if (den <= 0) return a;
    if (num <= 0) return a;
    if (num >= den) return b;
    return a + (int)(((int32_t)(b - a) * num) / den);
}

/* Map stage + level onto the RC curve. Idle / no-dot returns 0. */
static int smk_ampenv_dot(const smk_ampenv_geom_t *g, int stage, int32_t level,
                          int *dx, int *dy)
{
    int u, x0, x1;
    if (!g || !dx || !dy) return 0;
    if (stage == POLI_AMPENV_IDLE) return 0;
    if (level < 0) level = 0;
    if (level > SMK_AMPENV_YMAX) level = SMK_AMPENV_YMAX;
    switch (stage) {
    case POLI_AMPENV_ATK:
        /* Invert the RC: (1-PEAK/OVER)^u = 1 - L_peak_scale, with
         * L mapped 0..PEAK. Use linear-in-level as a safe fallback
         * if the log inverts poorly at the ends, then snap y onto
         * the curve at that x. */
        x0 = g->x0;
        x1 = g->x_peak;
        if (x1 <= x0) {
            *dx = x0;
            *dy = smk_ampenv_y_of(g->y0, g->h,
                                  (int32_t)(((int64_t)level * SMK_AMPENV_YMAX) / SMK_AMPENV_PEAK));
            if (level >= SMK_AMPENV_PEAK) *dy = g->y_peak;
            return 1;
        }
        *dx = smk_ampenv_lerpi(x0, x1, level, SMK_AMPENV_PEAK);
        *dy = smk_ampenv_y_at(g, *dx);
        return 1;
    case POLI_AMPENV_DEC: {
        int32_t den = (int32_t)SMK_AMPENV_YMAX - g->sus_lvl;
        int32_t num;
        x0 = g->x_peak;
        x1 = g->x_sus0;
        if (den < 1 || x1 <= x0) {
            *dx = g->x_sus0;
            *dy = g->y_sus;
            return 1;
        }
        num = (int32_t)SMK_AMPENV_YMAX - level;
        if (num < 0) num = 0;
        *dx = smk_ampenv_lerpi(x0, x1, num, den);
        *dy = smk_ampenv_y_at(g, *dx);
        return 1;
    }
    case POLI_AMPENV_SUS:
        *dx = (g->x_sus0 + g->x_sus1) / 2;
        *dy = g->y_sus;
        return 1;
    case POLI_AMPENV_REL: {
        int32_t den = g->sus_lvl > 0 ? g->sus_lvl : 1;
        int32_t num;
        x0 = g->x_sus1;
        x1 = g->x_end;
        if (x1 <= x0) {
            *dx = x1;
            *dy = smk_ampenv_y_of(g->y0, g->h, level);
            return 1;
        }
        num = den - level;
        if (num < 0) num = 0;
        *dx = smk_ampenv_lerpi(x0, x1, num, den);
        *dy = smk_ampenv_y_at(g, *dx);
        return 1;
    }
    default:
        (void)u;
        return 0;
    }
}

#endif /* SMK_AMP_ENV_H */
