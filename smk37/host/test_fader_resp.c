/* SPDX-License-Identifier: GPL-3.0-only */
/* Fader responsiveness: 10-bit maps match the 7-bit knob law, a 1-2% move
 * from zero changes resonance on the first 1 ms tick, and a fader-origin
 * reflect / live CC does not bump the full-redraw generation. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"
#include "smk_cut_smooth.h"

static int fail;
static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok) fail++;
}

int main(void)
{
    int p, i, first;
    int32_t cur, tgt, next0;
    uint32_t g0, g1, v0, v1;
    char line[240];
    fail = 0;

    /* High-res resonance map matches the 7-bit knob law on every tick. */
    for (p = 0; p <= 127; p++) {
        int32_t hi = poli_res_of_fader_adc((uint16_t)(p * 8));
        int32_t lo = poli_res_of_knob(p);
        if (hi != lo) {
            snprintf(line, sizeof line, "p=%d hi=%d lo=%d", p, hi, lo);
            expect("res_adc_matches_knob", 0, line);
            break;
        }
        if (p == 127)
            expect("res_adc_matches_knob", 1, "all 128 7-bit ticks");
    }
    expect("cut_acc_header_link", smk_cut_of_fader_acc(0) == 0, "");
    expect("res_adc_zero", poli_res_of_fader_adc(0) == 0, "");
    expect("res_adc_top", poli_res_of_fader_adc(1016) == 30000, "");
    {
        int32_t r0 = poli_res_of_knob(0), r1 = poli_res_of_knob(1);
        int32_t mid = poli_res_of_fader_adc(4);
        expect("res_adc_finer_than_7bit", mid > r0 && mid < r1, "");
    }

    /* Amp ADSR maps match at every 7-bit tick; mid-bin interpolates. */
    for (i = 0; i < 4; i++) {
        int ok = 1;
        for (p = 0; p <= 127; p++) {
            if (poli_env_of_fader_adc(i, (uint16_t)(p * 8)) != poli_env_of_knob(i, p)) {
                snprintf(line, sizeof line, "stage=%d p=%d", i, p);
                expect("env_adc_matches_knob", 0, line);
                ok = 0;
                break;
            }
        }
        if (ok && i == 3)
            expect("env_adc_matches_knob", 1, "A/D/S/R, all 128 ticks");
    }
    {
        int32_t s0 = poli_env_of_knob(2, 0), s1 = poli_env_of_knob(2, 1);
        int32_t mid = poli_env_of_fader_adc(2, 4);
        expect("env_sus_finer_than_7bit", mid > s0 && mid < s1, "");
    }

    /* 1-2% travel from a zeroed fader: resonance moves on the first tick.
     * 1% of 1023 is about 10 [M]; 2% is about 20 [M]. The old IIR+deadband
     * 7-bit path stays at 0 for a lone median of 16. */
    tgt = poli_res_of_fader_adc(16);     /* ~1.6% of 1023 */
    expect("res_1p6pct_target_nonzero", tgt > 0, "");
    cur = 0;
    next0 = smk_cut_smooth_tick(cur, tgt);
    first = (next0 != cur) ? 1 : 0;
    snprintf(line, sizeof line, "med=16 tgt=%d first=%d", tgt, next0);
    expect("res_1p6pct_moves_in_1ms", first == 1 && next0 > 0 && next0 <= tgt, line);
    {
        /* Old 7-bit path: one median sample into the x16 IIR, then /8 with
         * a +-1 deadband, starting from fader=0. v stays 0. */
        int32_t acc = 0;
        uint32_t v, fader = 0;
        acc += ((int32_t)16 * 16 - acc) / 4;    /* first IIR step, acc=64 */
        v = (uint32_t)acc / 16u / 8u;
        if (v > 127u) v = 127u;
        if (!(v > fader + 1u || v + 1u < fader || (v != fader && (v == 0u || v == 127u))))
            v = fader;
        expect("old_deadband_still_zero_at_med16", v == 0, "documents the hole this fix removes");
    }
    /* A settled 2% median (20) also moves at once on the new path. */
    tgt = poli_res_of_fader_adc(20);
    next0 = smk_cut_smooth_tick(0, tgt);
    expect("res_2pct_moves_in_1ms", tgt > 0 && next0 > 0 && next0 <= tgt, "");

    /* Fader-origin reflects and live CC must not bump nav_gen. */
    poli_nav_init();
    g0 = poli_nav_generation();
    v0 = poli_nav_value_generation();
    poli_nav_reflect_cut(80);
    g1 = poli_nav_generation();
    v1 = poli_nav_value_generation();
    expect("reflect_cut_no_full_redraw", g1 == g0 && v1 != v0, "");
    g0 = g1; v0 = v1;
    poli_nav_reflect_slot(SLOT_RES, 2);
    g1 = poli_nav_generation();
    v1 = poli_nav_value_generation();
    expect("reflect_res_no_full_redraw", g1 == g0 && v1 != v0, "");
    g0 = g1; v0 = v1;
    poli_nav_reflect_slot(SLOT_ATK_A, 3);
    poli_nav_reflect_slot(SLOT_DEC_A, 4);
    poli_nav_reflect_slot(SLOT_SUS_A, 5);
    poli_nav_reflect_slot(SLOT_REL_A, 6);
    g1 = poli_nav_generation();
    v1 = poli_nav_value_generation();
    expect("reflect_adsr_no_full_redraw", g1 == g0 && v1 != v0, "");
    g0 = g1; v0 = v1;
    poli_nav_cc_live(71, 40);
    g1 = poli_nav_generation();
    v1 = poli_nav_value_generation();
    expect("cc_live_no_full_redraw", g1 == g0 && v1 != v0 && poli_res == poli_res_of_knob(40), "");

    /* MIDI / encoder-style CC still does a full redraw. */
    g0 = poli_nav_generation();
    v0 = poli_nav_value_generation();
    poli_nav_cc(71, 50);
    g1 = poli_nav_generation();
    v1 = poli_nav_value_generation();
    expect("midi_cc_still_full_redraw", g1 != g0, "");
    expect("midi_cc_res_snaps", poli_res == poli_res_of_knob(50), "");

    /* A live reflect must not write the engine (cutoff already had this). */
    {
        int32_t held = poli_res;
        g0 = poli_nav_generation();
        poli_nav_reflect_slot(SLOT_RES, 0);
        expect("reflect_res_does_not_write_engine", poli_res == held && poli_nav_generation() == g0, "");
    }

    /* Encoder / section still bump the full-redraw generation. */
    g0 = poli_nav_generation();
    poli_nav_section(NAV_SEC_FX);
    expect("section_full_redraw", poli_nav_generation() != g0, "");
    g0 = poli_nav_generation();
    poli_nav_knob(0, 1);
    expect("knob_full_redraw", poli_nav_generation() != g0, "");

    /* Single-word ADSR write does not touch the other stages. */
    {
        int32_t a0, d0, s0, r0;
        poli_nav_init();
        a0 = polivoks_adsr_stage_q(1, 0);
        d0 = polivoks_adsr_stage_q(1, 1);
        s0 = polivoks_adsr_stage_q(1, 2);
        r0 = polivoks_adsr_stage_q(1, 3);
        polivoks_set_adsr_stage_q(1, 2, 1000);
        expect("adsr_stage_write_only_sus",
               polivoks_adsr_stage_q(1, 0) == a0 &&
               polivoks_adsr_stage_q(1, 1) == d0 &&
               polivoks_adsr_stage_q(1, 2) == 1000 &&
               polivoks_adsr_stage_q(1, 2) != s0 &&
               polivoks_adsr_stage_q(1, 3) == r0, "");
    }

    printf(fail ? "HOST-FADER-RESP FAIL %d\n" : "HOST-FADER-RESP PASS\n", fail);
    return fail ? 1 : 0;
}
