/* SPDX-License-Identifier: GPL-3.0-only */
/* Host checks for the display-only fader hysteresis (H=12). */
#include <stdio.h>
#include <stdint.h>
#include "smk_fader_steady.h"

static int fail;

static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok)
        fail++;
}

int main(void)
{
    int shown, i, flips, first;
    char line[128];

    fail = 0;

    expect("val7_zero", smk_val7_of_med(0) == 0 && smk_val7_of_med(7) == 0, "");
    expect("val7_one", smk_val7_of_med(8) == 1 && smk_val7_of_med(15) == 1, "");
    expect("val7_top", smk_val7_of_med(1016) == 127 && smk_val7_of_med(1023) == 127, "");

    shown = 16;
    expect("hold_inside_bin", smk_steady_update(&shown, (uint16_t)(8 * 16 + 4 + 12), SMK_STEADY_H) == 0
           && shown == 16, "");
    expect("step_past_h", smk_steady_update(&shown, (uint16_t)(8 * 16 + 4 + 13), SMK_STEADY_H) == 1
           && shown == smk_val7_of_med(8 * 16 + 4 + 13), "");

    /* End snaps: a mid shown value jumps to 0 / 127 at the rails. */
    shown = 3;
    expect("snap_low", smk_steady_update(&shown, 7, SMK_STEADY_H) == 1 && shown == 0, "");
    shown = 0;
    expect("hold_low", smk_steady_update(&shown, 7, SMK_STEADY_H) == 0 && shown == 0, "");
    shown = 120;
    expect("snap_high", smk_steady_update(&shown, 1016, SMK_STEADY_H) == 1 && shown == 127, "");
    shown = 127;
    expect("hold_high", smk_steady_update(&shown, 1023, SMK_STEADY_H) == 0 && shown == 127, "");

    /* ±3 wander in the 15/16/17 bins must not flip the shown value. */
    shown = 16;
    flips = 0;
    for (i = 0; i < 400; i++) {
        int n = 8 * 16 + 4;
        uint16_t med = (uint16_t)(n + ((i % 2) ? 3 : -3) + ((i / 2) % 3) - 1);
        /* wander about ±3..8 around the bin centre of 16, covering 15/16/17 */
        int off[8] = { -8, -5, -3, -1, 1, 3, 5, 8 };
        med = (uint16_t)(n + off[i % 8]);
        if (smk_steady_update(&shown, med, SMK_STEADY_H))
            flips++;
    }
    snprintf(line, sizeof line, "flips=%d shown=%d", flips, shown);
    expect("h12_stops_15_16_17_wander", flips == 0 && shown == 16, line);

    /* The same wander without hysteresis would report 15/16/17. */
    {
        int seen[3] = {0, 0, 0};
        int n = 8 * 16 + 4;
        int off[8] = { -8, -5, -3, -1, 1, 3, 5, 8 };
        for (i = 0; i < 8; i++) {
            int v = smk_val7_of_med((uint16_t)(n + off[i]));
            if (v >= 15 && v <= 17)
                seen[v - 15] = 1;
        }
        expect("raw_val7_visits_15_16_17", seen[0] && seen[1] && seen[2], "");
    }

    /* A slow move of about 1.7 % of the throw is the first update. */
    shown = smk_val7_of_med(512);
    first = -1;
    for (i = 0; i <= 40; i++) {
        uint16_t med = (uint16_t)(512 + i);
        if (smk_steady_update(&shown, med, SMK_STEADY_H)) {
            first = i;
            break;
        }
    }
    snprintf(line, sizeof line, "first_delta=%d (%.2f%% of 1023)", first,
             first > 0 ? (100.0 * first) / 1023.0 : 0.0);
    expect("slow_move_first_update_in_1_2_pct", first >= 10 && first <= 20, line);

    printf("suite_fail=%d\n", fail);
    return fail ? 1 : 0;
}
