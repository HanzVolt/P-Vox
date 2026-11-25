/* SPDX-License-Identifier: GPL-3.0-only */
/* Fader-only cutoff mapping and 1 ms smoother. Encoder and MIDI CC do not
 * use this. The fcore still has its own per-sample err>>5 (~0.7 ms).
 *
 * Coefficient 1/2 per millisecond (err >> 1), snap when step == 0 (|err| < 2).
 * Time constant τ = -1/ln(1-1/2) ≈ 1.44 ms [E]. The first tick must move
 * poli_cut (no dead time). Ticks come from the 1 kHz TIMER5 path so a UI
 * redraw cannot freeze the ramp or the 10-bit target. */
#pragma once
#include "poli_nav.h"

#ifndef SMK_CUT_SHIFT
#define SMK_CUT_SHIFT 1
#endif

/* 10-bit reading 0..1023 onto the same law as poli_cut_of_knob, with 8
 * extra steps between adjacent 7-bit values. When u == 8*p, the result is
 * exactly poli_cut_of_knob(p). */
static int32_t smk_cut_of_fader_adc(uint16_t u)
{
    uint32_t x = (uint32_t)u;
    if (x > 1016u)          /* 127 * 8 */
        x = 1016u;
    return (int32_t)((x * (uint32_t)POLI_CUT_TOP + 504u) / 1016u);  /* 63*8, 127*8 */
}

/* fad_acc is a x16 IIR of the 10-bit raw. Kept for the host map check. */
static int32_t smk_cut_of_fader_acc(uint16_t acc)
{
    return smk_cut_of_fader_adc((uint16_t)((uint32_t)acc / 16u));
}

/* One 1 ms tick toward tgt. */
static int32_t smk_cut_smooth_tick(int32_t cur, int32_t tgt)
{
    int32_t err = tgt - cur;
    int32_t step = err >> SMK_CUT_SHIFT;
    if (step == 0)
        return tgt;
    return cur + step;
}
