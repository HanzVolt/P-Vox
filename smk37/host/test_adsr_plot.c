/* SPDX-License-Identifier: GPL-3.0-only */
/* Host checks for the natural ADSR / FILTER EG plot: fixed window,
 * monotonic fader-tracking widths, linear sustain, RC corners, and
 * no leftover orange after a redraw. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"
#include "smk_amp_env.h"

static int fail;

static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok)
        fail++;
}

int main(void)
{
    smk_ampenv_geom_t g, g2;
    int i, p, prev, xr, xa, ys, yprev;
    int dec_pos[] = { 0, 32, 64, 96, 127 };
    int xd_v[5], xr_v[5];
    char line[200];
    int jumped_r = 0, jumped_s = 0, held_r = -1;

    fail = 0;
    poli_nav_init();

    smk_ampenv_geom(0, 0, 127, 0, 0, 0, 216, 158, &g);
    expect("square_atk_vertical", g.x_peak == g.x0, "");
    expect("square_dec_vertical", g.x_sus0 == g.x_peak, "");
    expect("square_rel_vertical", g.x_end == g.x_sus1, "");
    expect("square_hold_px", g.x_sus1 - g.x_sus0 == SMK_AMPENV_WSUS, "");
    expect("square_sus_top", g.y_sus == g.y_peak && g.y_peak == 0, "");
    {
        int xs[5], ys[5];
        smk_ampenv_points(&g, xs, ys);
        expect("square_points_vertical",
               xs[0] == xs[1] && xs[1] == xs[2] && xs[3] == xs[4] &&
               ys[0] == g.y_zero && ys[1] == g.y_peak &&
               ys[2] == g.y_peak && ys[3] == g.y_peak && ys[4] == g.y_zero, "");
        expect("y_of_peak", smk_ampenv_y_of(0, 158, SMK_AMPENV_YMAX) == 0, "");
    }

    for (i = 0; i < 5; i++) {
        smk_ampenv_geom(0, dec_pos[i], 64, 0, 0, 0, 216, 158, &g);
        xd_v[i] = g.x_sus0 - g.x_peak;
        smk_ampenv_geom(0, 32, 64, dec_pos[i], 0, 0, 216, 158, &g);
        xr_v[i] = g.x_end - g.x_sus1;
    }
    snprintf(line, sizeof line, "xd=%d %d %d %d %d", xd_v[0], xd_v[1], xd_v[2], xd_v[3], xd_v[4]);
    expect("decay_min_vertical", xd_v[0] == 0, line);
    expect("decay_widens",
           xd_v[0] < xd_v[1] && xd_v[1] < xd_v[2] && xd_v[2] < xd_v[3] && xd_v[3] < xd_v[4],
           line);
    snprintf(line, sizeof line, "xr=%d %d %d %d %d", xr_v[0], xr_v[1], xr_v[2], xr_v[3], xr_v[4]);
    expect("release_min_vertical", xr_v[0] == 0, line);
    expect("release_widens",
           xr_v[0] < xr_v[1] && xr_v[1] < xr_v[2] && xr_v[2] < xr_v[3] && xr_v[3] < xr_v[4],
           line);

    /* Monotonic, no backward jumps, across the whole fader. */
    prev = -1;
    for (p = 0; p <= 127; p++) {
        smk_ampenv_geom(0, 32, 64, p, 0, 0, 216, 158, &g);
        xr = g.x_end - g.x_sus1;
        if (prev >= 0 && xr < prev) jumped_r++;
        prev = xr;
        /* When the segment has width, it starts on the hold and ends
         * on the baseline. A zero-width release is a vertical at x_sus1. */
        if (xr > 0) {
            if (smk_ampenv_y_at(&g, g.x_sus1) != g.y_sus) jumped_r += 1000;
            if (smk_ampenv_y_at(&g, g.x_end) != g.y_zero) jumped_r += 1000;
        }
    }
    snprintf(line, sizeof line, "backwards=%d last_xr=%d", jumped_r, prev);
    expect("release_monotonic_0_127", jumped_r == 0, line);

    jumped_r = 0;
    prev = -1;
    for (p = 0; p <= 127; p++) {
        smk_ampenv_geom(p, 32, 64, 32, 0, 0, 216, 158, &g);
        xa = g.x_peak - g.x0;
        if (prev >= 0 && xa < prev) jumped_r++;
        prev = xa;
    }
    expect("attack_monotonic_0_127", jumped_r == 0 && prev > 20, "");

    /* Moving A must not change R (fixed window). */
    smk_ampenv_geom(0, 32, 64, 64, 0, 0, 216, 158, &g);
    smk_ampenv_geom(127, 32, 64, 64, 0, 0, 216, 158, &g2);
    snprintf(line, sizeof line, "xr0=%d xr127=%d", g.x_end - g.x_sus1, g2.x_end - g2.x_sus1);
    expect("release_stable_when_attack_moves",
           (g.x_end - g.x_sus1) == (g2.x_end - g2.x_sus1), line);
    smk_ampenv_geom(32, 0, 64, 80, 0, 0, 216, 158, &g);
    smk_ampenv_geom(32, 127, 64, 80, 0, 0, 216, 158, &g2);
    expect("release_stable_when_decay_moves",
           (g.x_end - g.x_sus1) == (g2.x_end - g2.x_sus1), "");

    /* Linear sustain: 0 on baseline, 127 at top, monotonic, and
     * decay ends / release starts on the same pixel. */
    smk_ampenv_geom(0, 48, 0, 48, 0, 0, 216, 158, &g);
    expect("sus0_on_baseline", g.y_sus == g.y_zero, "");
    smk_ampenv_geom(0, 48, 127, 48, 0, 0, 216, 158, &g);
    expect("sus127_at_top", g.y_sus == g.y_peak, "");
    yprev = 9999;
    held_r = 0;
    for (p = 0; p <= 127; p++) {
        smk_ampenv_geom(0, 48, p, 48, 0, 0, 216, 158, &g);
        ys = g.y_sus;
        if (ys > yprev) jumped_s++;
        if (p > 0 && ys == yprev) held_r++;
        if (smk_ampenv_y_at(&g, g.x_sus0) != g.y_sus) jumped_s += 1000;
        if (smk_ampenv_y_at(&g, g.x_sus1) != g.y_sus) jumped_s += 1000;
        yprev = ys;
    }
    snprintf(line, sizeof line, "backwards=%d same_pixel_steps=%d height=%d",
             jumped_s, held_r, 157);
    expect("sustain_monotonic", jumped_s == 0, line);
    /* On a 158 px plot, 127 steps should rarely stall, and never go backwards. */
    expect("sustain_mostly_moves", held_r < 20, line);

    /* Attack RC is a charging curve: mid-column is above the straight line
     * (smaller y than the linear interpolation — closer to the peak). */
    smk_ampenv_geom(96, 0, 64, 0, 0, 0, 216, 158, &g);
    {
        int mid = (g.x0 + g.x_peak) / 2;
        int ymid = smk_ampenv_y_at(&g, mid);
        int ylin = (g.y_zero + g.y_peak) / 2;
        snprintf(line, sizeof line, "ymid=%d ylin=%d y_peak=%d y_zero=%d",
                 ymid, ylin, g.y_peak, g.y_zero);
        expect("attack_rc_above_linear", ymid < ylin && ymid > g.y_peak, line);
    }
    /* Decay RC starts steep: a quarter of the way is below a straight line
     * (larger y, closer to sustain than linear). */
    smk_ampenv_geom(0, 96, 32, 0, 0, 0, 216, 158, &g);
    {
        int q = g.x_peak + (g.x_sus0 - g.x_peak) / 4;
        int yq = smk_ampenv_y_at(&g, q);
        int ylin = g.y_peak + (g.y_sus - g.y_peak) / 4;
        snprintf(line, sizeof line, "yq=%d ylin=%d y_peak=%d y_sus=%d",
                 yq, ylin, g.y_peak, g.y_sus);
        expect("decay_rc_faster_than_linear", yq > ylin && yq < g.y_sus, line);
    }
    /* Release RC starts at sustain, ends on the baseline, curves toward 0. */
    smk_ampenv_geom(0, 32, 80, 96, 0, 0, 216, 158, &g);
    expect("release_starts_at_sus", smk_ampenv_y_at(&g, g.x_sus1) == g.y_sus, "");
    expect("release_ends_on_baseline", smk_ampenv_y_at(&g, g.x_end) == g.y_zero, "");
    {
        int q = g.x_sus1 + (g.x_end - g.x_sus1) / 4;
        int yq = smk_ampenv_y_at(&g, q);
        int ylin = g.y_sus + (g.y_zero - g.y_sus) / 4;
        snprintf(line, sizeof line, "yq=%d ylin=%d", yq, ylin);
        expect("release_rc_faster_than_linear", yq > ylin && yq < g.y_zero, line);
    }

    /* All-max still fits. */
    smk_ampenv_geom(127, 127, 127, 127, 0, 0, 216, 158, &g);
    snprintf(line, sizeof line, "xa=%d xd=%d xs=%d xr=%d end=%d inner=%d",
             g.x_peak - g.x0, g.x_sus0 - g.x_peak, g.x_sus1 - g.x_sus0,
             g.x_end - g.x_sus1, g.x_end, 216 - 2 * SMK_AMPENV_PAD);
    expect("allmax_fits",
           g.x_end - g.x0 <= 216 - 2 * SMK_AMPENV_PAD &&
           g.x_peak > g.x0 && g.x_sus0 > g.x_peak && g.x_end > g.x_sus1,
           line);

    /* Init 0/0/64/4: vertical A/D, mid hold, short release. */
    smk_ampenv_geom(0, 0, 64, 4, 0, 0, 216, 158, &g);
    snprintf(line, sizeof line, "xa=%d xd=%d xs=%d xr=%d y_sus=%d",
             g.x_peak - g.x0, g.x_sus0 - g.x_peak, g.x_sus1 - g.x_sus0,
             g.x_end - g.x_sus1, g.y_sus);
    expect("init_vertical_atk_dec", g.x_peak == g.x0 && g.x_sus0 == g.x_peak, line);
    expect("init_release_short", g.x_end > g.x_sus1 && (g.x_end - g.x_sus1) < 20, line);
    expect("init_sus_mid", g.y_sus > g.y_peak && g.y_sus < g.y_zero, line);
    {
        int dx, dy;
        expect("idle_dot_hidden", smk_ampenv_dot(&g, POLI_AMPENV_IDLE, 0, &dx, &dy) == 0, "");
    }

    /* Stray-pixel style check: after a wide release, a zero-release
     * redraw's y_at past the new x_end is the baseline, not a leftover
     * mid-level (geometry, not a framebuffer). */
    smk_ampenv_geom(0, 32, 64, 127, 0, 0, 216, 158, &g);
    smk_ampenv_geom(0, 32, 64, 0, 0, 0, 216, 158, &g2);
    expect("no_stale_release_tail",
           g2.x_end == g2.x_sus1 &&
           smk_ampenv_y_at(&g2, g2.x_end + 8) == g2.y_zero, "");

    printf("suite_fail=%d\n", fail);
    return fail ? 1 : 0;
}
