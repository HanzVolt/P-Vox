/* SPDX-License-Identifier: GPL-3.0-only */
/* Host checks for AMP ENV shape geometry and stage/dot mapping. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "smk_amp_env.h"

static int fail;

static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok)
        fail++;
}

static int32_t abuf[512];

static void render_n(uint32_t n)
{
    while (n) {
        uint32_t k = n > 256u ? 256u : n;
        memset(abuf, 0, sizeof abuf);
        polivoks_render(abuf, k);
        n -= k;
    }
}

int main(void)
{
    smk_ampenv_geom_t slow, fast, hi, lo, z;
    int xs[5], ys[5], dx, dy, dx2, dy2;
    poli_ampenv_view_t ev;
    int saw_atk = 0, saw_dec = 0, saw_sus = 0, saw_rel = 0, saw_idle = 0;
    uint32_t n;
    char line[160];

    fail = 0;

    /* Larger attack knob -> wider attack segment. */
    smk_ampenv_geom(96, 16, 64, 16, 0, 0, 200, 60, &slow);
    smk_ampenv_geom(16, 16, 64, 16, 0, 0, 200, 60, &fast);
    snprintf(line, sizeof line, "slow=%d fast=%d", slow.x_peak - slow.x0, fast.x_peak - fast.x0);
    expect("longer_atk_wider", (slow.x_peak - slow.x0) > (fast.x_peak - fast.x0), line);

    smk_ampenv_geom(32, 32, 109, 32, 0, 0, 200, 60, &hi);
    smk_ampenv_geom(32, 32, 9, 32, 0, 0, 200, 60, &lo);
    snprintf(line, sizeof line, "hi y_sus=%d lo y_sus=%d y_peak=%d y_zero=%d",
             hi.y_sus, lo.y_sus, hi.y_peak, hi.y_zero);
    expect("higher_sus_higher_on_screen", hi.y_sus < lo.y_sus, line);
    expect("high_sus_below_peak", hi.y_sus >= hi.y_peak, line);
    expect("low_sus_above_zero", lo.y_sus <= lo.y_zero, line);

    smk_ampenv_geom(32, 32, 0, 32, 0, 0, 200, 60, &z);
    expect("zero_sus_at_baseline", z.y_sus == z.y_zero, "");
    expect("full_throw_zero_is_bottom", z.y_zero == 59, "y0=0 h=60");
    expect("full_throw_peak_is_top", z.y_peak == 0, "");
    {
        smk_ampenv_geom_t fh;
        smk_ampenv_geom(32, 32, 127, 32, 10, 20, 200, 80, &fh);
        expect("ymax_sus_at_top", fh.y_sus == fh.y_peak && fh.y_peak == 20, "");
        expect("zero_at_last_pixel", fh.y_zero == 20 + 80 - 1, "");
        expect("y_of_zero", smk_ampenv_y_of(20, 80, 0) == 99, "");
        expect("y_of_ymax", smk_ampenv_y_of(20, 80, SMK_AMPENV_YMAX) == 20, "");
    }

    /* Knob 0 / weight 0 must be a true vertical edge, no min width. */
    {
        smk_ampenv_geom_t box, d0, d1, d2, d3, d4, s0;
        int xd[5];
        smk_ampenv_geom(0, 0, 127, 0, 0, 0, 200, 80, &box);
        expect("square_vertical_atk", box.x_peak == box.x0, "");
        expect("square_vertical_rel", box.x_end == box.x_sus1, "");
        expect("square_hold_fixed", box.x_sus1 - box.x_sus0 == SMK_AMPENV_WSUS, "");
        expect("square_sus_at_peak", box.y_sus == box.y_peak, "");

        smk_ampenv_geom(0, 0, 64, 0, 0, 0, 200, 80, &d0);
        smk_ampenv_geom(0, 32, 64, 0, 0, 0, 200, 80, &d1);
        smk_ampenv_geom(0, 64, 64, 0, 0, 0, 200, 80, &d2);
        smk_ampenv_geom(0, 96, 64, 0, 0, 0, 200, 80, &d3);
        smk_ampenv_geom(0, 127, 64, 0, 0, 0, 200, 80, &d4);
        xd[0] = d0.x_sus0 - d0.x_peak;
        xd[1] = d1.x_sus0 - d1.x_peak;
        xd[2] = d2.x_sus0 - d2.x_peak;
        xd[3] = d3.x_sus0 - d3.x_peak;
        xd[4] = d4.x_sus0 - d4.x_peak;
        snprintf(line, sizeof line, "xd=%d %d %d %d %d", xd[0], xd[1], xd[2], xd[3], xd[4]);
        expect("decay_min_vertical", xd[0] == 0 && d0.x_peak == d0.x0, line);
        expect("decay_widens",
               xd[0] < xd[1] && xd[1] < xd[2] && xd[2] < xd[3] && xd[3] < xd[4],
               line);

        smk_ampenv_geom(0, 64, 0, 0, 0, 0, 200, 80, &s0);
        expect("sus0_hold_on_baseline", s0.y_sus == s0.y_zero, "");
        expect("sus0_hold_width", s0.x_sus1 - s0.x_sus0 == SMK_AMPENV_WSUS, "");
    }

    smk_ampenv_points(&hi, xs, ys);
    expect("polyline_starts_at_zero", xs[0] == hi.x0 && ys[0] == hi.y_zero, "");
    expect("polyline_peaks", xs[1] == hi.x_peak && ys[1] == hi.y_peak, "");
    expect("polyline_sus_flat", ys[2] == hi.y_sus && ys[3] == hi.y_sus && xs[3] > xs[2], "");
    expect("polyline_ends_at_zero", xs[4] == hi.x_end && ys[4] == hi.y_zero, "");
    expect("x_order",
           hi.x0 < hi.x_peak && hi.x_peak < hi.x_sus0 && hi.x_sus0 < hi.x_sus1 && hi.x_sus1 < hi.x_end,
           "");

    expect("idle_no_dot", smk_ampenv_dot(&hi, POLI_AMPENV_IDLE, 10000, &dx, &dy) == 0, "");

    smk_ampenv_dot(&hi, POLI_AMPENV_ATK, 0, &dx, &dy);
    expect("atk_start", dx == hi.x0 && dy == hi.y_zero, "");
    smk_ampenv_dot(&hi, POLI_AMPENV_ATK, SMK_AMPENV_PEAK, &dx, &dy);
    expect("atk_peak", dx == hi.x_peak && dy == hi.y_peak, "");
    smk_ampenv_dot(&hi, POLI_AMPENV_ATK, SMK_AMPENV_PEAK / 2, &dx, &dy);
    expect("atk_mid_between",
           dx > hi.x0 && dx < hi.x_peak && dy < hi.y_zero && dy > hi.y_peak, "");

    smk_ampenv_dot(&hi, POLI_AMPENV_SUS, 24000, &dx, &dy);
    expect("sus_on_plateau",
           dx >= hi.x_sus0 && dx <= hi.x_sus1 && dy == hi.y_sus, "");

    smk_ampenv_dot(&hi, POLI_AMPENV_REL, 12000, &dx, &dy);
    smk_ampenv_dot(&hi, POLI_AMPENV_REL, 2000, &dx2, &dy2);
    expect("rel_on_fall", dx > hi.x_sus1 && dx < hi.x_end && dy > hi.y_sus && dy < hi.y_zero, "");
    expect("rel_lower_farther", dx2 > dx && dy2 > dy, "");

    /* Engine stage mapping: moderate times, mid sustain. */
    polivoks_init();
    polivoks_set_adsr_q(1, 80000, 120000, 14000, 80000);
    polivoks_ampenv_view(&ev);
    expect("init_idle", ev.stage == POLI_AMPENV_IDLE && ev.level <= 0 && ev.gate == 0, "");

    polivoks_midi(0x90, 60, 100);
    render_n(64);
    polivoks_ampenv_view(&ev);
    expect("note_on_attack", ev.stage == POLI_AMPENV_ATK && ev.gate == 1 && ev.level > 0, "");
    saw_atk = ev.stage == POLI_AMPENV_ATK;

    for (n = 0; n < 200000u && ev.stage != POLI_AMPENV_DEC && ev.stage != POLI_AMPENV_SUS; n += 64) {
        render_n(64);
        polivoks_ampenv_view(&ev);
        if (ev.stage == POLI_AMPENV_ATK) saw_atk = 1;
    }
    if (ev.stage == POLI_AMPENV_DEC) saw_dec = 1;
    for (n = 0; n < 200000u && ev.stage != POLI_AMPENV_SUS; n += 64) {
        render_n(64);
        polivoks_ampenv_view(&ev);
        if (ev.stage == POLI_AMPENV_DEC) saw_dec = 1;
    }
    if (ev.stage == POLI_AMPENV_SUS) saw_sus = 1;
    snprintf(line, sizeof line, "stage=%d level=%d sus=%d", ev.stage, ev.level, ev.sus);
    expect("reaches_sustain", saw_atk && saw_dec && saw_sus, line);
    if (saw_sus) {
        int close = ev.level >= ev.sus - 200 && ev.level <= ev.sus + 200;
        expect("sustain_holds_level", ev.stage == POLI_AMPENV_SUS && close, line);
    }

    polivoks_midi(0x80, 60, 0);
    render_n(64);
    polivoks_ampenv_view(&ev);
    expect("note_off_release", ev.stage == POLI_AMPENV_REL && ev.gate == 0 && ev.level > 0, "");
    saw_rel = ev.stage == POLI_AMPENV_REL;
    for (n = 0; n < 200000u && ev.stage != POLI_AMPENV_IDLE; n += 64) {
        render_n(64);
        polivoks_ampenv_view(&ev);
        if (ev.stage == POLI_AMPENV_REL) saw_rel = 1;
    }
    if (ev.stage == POLI_AMPENV_IDLE) saw_idle = 1;
    expect("release_to_idle", saw_rel && saw_idle && ev.level <= 0, "");

    printf("suite_fail=%d\n", fail);
    return fail ? 1 : 0;
}
