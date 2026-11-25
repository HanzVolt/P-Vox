/* SPDX-License-Identifier: GPL-3.0-only */
/* Fader cutoff smoother: mapping agrees with the 7-bit knob law, a fader
 * step ramps with τ ≈ 1.44 ms (err>>1 per ms), the first tick moves cutoff,
 * encoder/CC still snap, amount-0 renders stay identical. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
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

enum { N = 8192 };
static int32_t a[2 * N], b[2 * N];

static void render_n(int32_t *o, uint32_t n)
{
    uint32_t i;
    memset(o, 0, sizeof(int32_t) * 2u * n);
    for (i = 0; i < n; ) {
        uint32_t k = n - i;
        if (k > 256u) k = 256u;
        polivoks_render(o + 2u * i, k);
        i += k;
    }
}

int main(void)
{
    int p, i, first, t95, reached;
    int32_t cur, tgt, next0;
    char line[200];
    double tau_e = -1.0 / log(1.0 - 1.0 / 2.0);
    fail = 0;

    /* High-res map matches poli_cut_of_knob on every 7-bit tick. */
    for (p = 0; p <= 127; p++) {
        uint16_t acc = (uint16_t)(p * 8 * 16);
        int32_t hi = smk_cut_of_fader_acc(acc);
        int32_t lo = poli_cut_of_knob(p);
        if (hi != lo) {
            snprintf(line, sizeof line, "p=%d acc=%u hi=%d lo=%d", p, acc, hi, lo);
            expect("fader_acc_matches_knob", 0, line);
            break;
        }
        if (p == 127)
            expect("fader_acc_matches_knob", 1, "all 128 7-bit ticks");
    }
    expect("fader_acc_zero", smk_cut_of_fader_acc(0) == 0, "");
    expect("fader_acc_top", smk_cut_of_fader_acc(1016 * 16) == POLI_CUT_TOP, "");
    /* Median / raw 10-bit path uses the same law (u, not x16 acc). */
    expect("fader_adc_matches_acc",
           smk_cut_of_fader_adc(59 * 8 + 4) == smk_cut_of_fader_acc((uint16_t)((59 * 8 + 4) * 16)),
           "");
    expect("fader_adc_knob127", smk_cut_of_fader_adc(1016) == POLI_CUT_TOP, "");
    /* A mid-bin 10-bit value sits strictly between adjacent 7-bit cuts. */
    {
        int32_t c59 = poli_cut_of_knob(59), c60 = poli_cut_of_knob(60);
        int32_t mid = smk_cut_of_fader_acc((uint16_t)((59 * 8 + 4) * 16));
        expect("fader_acc_finer_than_7bit", mid > c59 && mid < c60, "");
    }

    /* First tick of a move must change cutoff (no dead time). */
    cur = 0;
    tgt = POLI_CUT_TOP;
    next0 = smk_cut_smooth_tick(cur, tgt);
    first = (next0 != cur) ? 1 : -1;
    snprintf(line, sizeof line, "first change in %d ms, cur %d -> %d, target %d", first, cur, next0, tgt);
    expect("fader_first_change_in_1ms", first == 1 && next0 != tgt && next0 > cur, line);

    /* A large fader-style step ramps and reaches the target. */
    cur = 0;
    reached = -1;
    t95 = -1;
    for (i = 0; i < 200; i++) {
        int32_t next = smk_cut_smooth_tick(cur, tgt);
        if (next < cur || next > tgt) {
            expect("fader_step_monotonic", 0, "went backwards or past target");
            break;
        }
        cur = next;
        if (t95 < 0 && cur >= (int32_t)(0.95 * (double)tgt))
            t95 = i + 1;
        if (reached < 0 && cur == tgt)
            reached = i + 1;
        if (reached >= 0 && t95 >= 0)
            break;
    }
    snprintf(line, sizeof line, "tau_E %.2f ms; first %d ms [M]; 95%% %d ms [M]; reach %d ms [M], end %d",
             tau_e, first, t95, reached, cur);
    expect("fader_step_ramps", reached > 2 && reached < 30 && cur == tgt, line);
    expect("fader_step_tau_about_1_4ms", t95 >= 3 && t95 <= 8 && reached >= 8 && reached <= 25, line);

    /* Encoder / CC still snap poli_cut on the same tick. */
    poli_nav_init();
    expect("cc_default_cut", poli_cut == poli_cut_of_knob(POLI_CUT_DEF_POS), "");
    poli_nav_cc(74, 100);
    expect("cc74_snaps", poli_cut == poli_cut_of_knob(100), "");
    poli_nav_cc(74, 0);
    expect("cc74_zero_snaps", poli_cut == 0, "");
    /* reflect updates the UI pos only */
    {
        int32_t held = poli_cut;
        poli_nav_reflect_cut(80);
        expect("reflect_cut_does_not_write_poli_cut", poli_cut == held, "");
    }

    /* Amount-0 renders: vel 1 vs 127 bit-identical (smoother not in this path). */
    poli_nav_init();
    polivoks_set_vel_amount(0);
    polivoks_set_contour_amount(32768);
    polivoks_midi(0x90, 60, 127);
    render_n(a, N);
    poli_nav_init();
    polivoks_set_vel_amount(0);
    polivoks_set_contour_amount(32768);
    polivoks_midi(0x90, 60, 1);
    render_n(b, N);
    expect("amt0_vel_bit_identical", memcmp(a, b, sizeof a) == 0, "vel 1 vs 127");

    printf("MEASURED first_change=%d ms  95pct=%d ms  reach=%d ms  tau_E=%.2f ms\n",
           first, t95, reached, tau_e);
    printf(fail ? "HOST-FADER-CUT FAIL %d\n" : "HOST-FADER-CUT PASS\n", fail);
    return fail ? 1 : 0;
}
