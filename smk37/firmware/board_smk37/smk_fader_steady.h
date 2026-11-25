/* SPDX-License-Identifier: GPL-3.0-only */
/* Display-only hysteresis on a 10-bit fader reading.
 *
 * The engine target stays the raw fad_med (plus the existing 1 ms glide).
 * This stage feeds only: the on-screen 0-127 number, pad-meter move
 * detection, page-jump / soft-takeover arming, AMP and filter plot
 * geometry, and the FILTER depth readout when it comes from a fader.
 *
 * Keep the shown 0-127 value until the median moves more than
 * SMK_STEADY_H counts away from that value's bin centre (8*p+4).
 * Always snap at the ends so 0 and 127 stay reachable. */
#pragma once
#include <stdint.h>

enum { SMK_STEADY_H = 12 };     /* 1.5 display steps of 8 counts */

/* 7-bit of a 10-bit median. 0 when med is 0..7, 1 at 8..15. */
static int smk_val7_of_med(uint16_t m)
{
    if (m > 1016u)
        m = 1016u;
    return (int)(m / 8u);
}

/* Update *shown (0..127). Returns 1 if the shown value changed. */
static int smk_steady_update(int *shown, uint16_t med, int hyst)
{
    int p, centre, d, n;

    if (!shown)
        return 0;
    p = *shown;
    if (p < 0)
        p = 0;
    if (p > 127)
        p = 127;

    if (med <= 7u) {
        if (p != 0) {
            *shown = 0;
            return 1;
        }
        return 0;
    }
    if (med >= 1016u) {
        if (p != 127) {
            *shown = 127;
            return 1;
        }
        return 0;
    }
    centre = 8 * p + 4;
    d = (int)med - centre;
    if (d < 0)
        d = -d;
    if (d > hyst) {
        n = smk_val7_of_med(med);
        if (n != p) {
            *shown = n;
            return 1;
        }
    }
    return 0;
}
