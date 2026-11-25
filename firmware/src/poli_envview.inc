/* SPDX-License-Identifier: GPL-3.0-only */
/* Parity (23 Nov 2026): the Elite's envelope snapshot for the FILTER / AMP EG plots (SMK polivoks_limited.c
 * polivoks_ampenv_view / polivoks_filtenv_view, the same code), kept out of polivoks_limited.c so the FM-1 engine
 * file is unchanged. Included from poli_ui.c (unity build, after polivoks_limited.c) under POLI_EGPLOT.
 * Main loop / host only: aligned word reads of the envelope state; never the audio interrupt or TIMER5. */
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
typedef struct {
    int32_t atk, dec, sus, rel; /* Q24 coeffs; sus is 0..28000 */
    int32_t level;              /* poli_eg_a >> 15, typically 0..26000 */
    int stage;
    int gate;
    int rpt;
} poli_ampenv_view_t;

static void poli_env_view_fill(poli_ampenv_view_t *o,
                              int32_t atk, int32_t dec, int32_t sus, int32_t rel,
                              int32_t eg, uint8_t up, uint8_t fell, uint8_t rep)
{
    int32_t lv, sus_u;
    int st, g;
    if (!o) return;
    lv = eg >> POLI_EGK;
    sus_u = sus >> POLI_EGK;
    g = (int)poli_gate;
    if (rep && g)
        st = up ? POLI_AMPENV_ATK : POLI_AMPENV_DEC;
    else if (!g && lv <= 0)
        st = POLI_AMPENV_IDLE;
    else if (!g)
        st = POLI_AMPENV_REL;
    else if (!fell)
        st = POLI_AMPENV_ATK;
    else if (lv > sus_u + 64)
        st = POLI_AMPENV_DEC;
    else
        st = POLI_AMPENV_SUS;
    o->atk = atk;
    o->dec = dec;
    o->sus = sus_u;
    o->rel = rel;
    o->level = lv;
    o->stage = st;
    o->gate = g;
    o->rpt = (int)rep;
}

static void polivoks_ampenv_view(poli_ampenv_view_t *o)
{
    poli_env_view_fill(o, poli_atk_a, poli_dec_a, poli_sus_a, poli_rel_a,
                       poli_eg_a, poli_up_a, poli_fell_a, poli_rep_a);
}

static void polivoks_filtenv_view(poli_ampenv_view_t *o)
{
    poli_env_view_fill(o, poli_atk_f, poli_dec_f, poli_sus_f, poli_rel_f,
                       poli_eg_f, poli_up_f, poli_fell_f, poli_rep_f);
}
