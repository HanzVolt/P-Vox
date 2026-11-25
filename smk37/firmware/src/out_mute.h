/* SPDX-License-Identifier: GPL-3.0-only */
/* Output mute for preset loads (Hans, 2026-11-12: "mute briefly during a load",
 * no engine change). A gain on the finished block, in the output stage after
 * the engine, the same place the MASTER knob scales it. The voice, filter and
 * envelope code never see it.
 *
 * The main loop asks for silence (out_mute_req = 1). The audio ISR fades the
 * next whole half buffer (256 frames, 5.8 ms) linearly to 0 and holds 0 while
 * the request stays. When the request is cleared the next half fades back up
 * the same way. With no request the gain is exactly the MASTER knob, so the
 * samples are the same as before (out_gain_block = the old loop).
 * Included by platform.c (device ISR) and by host measurement harnesses. */
#ifndef OUT_MUTE_H
#define OUT_MUTE_H
#include <stdint.h>
#define OUT_UNITY 4096
static volatile uint8_t out_mute_req;               /* main loop: 1 = fade out, then hold silence */
static volatile int32_t out_mute_g = OUT_UNITY;     /* ISR: mute gain (Q12) reached at the end of the last half */

/* o[i] * master >> 12, clamped to +-32767 (as before). During a fade the
   master gain moves linearly across the block, in Q8 steps truncated toward
   the target, so it never passes it. */
static inline void out_gain_block(int32_t *o, uint32_t nwords, int32_t master)
{
    int32_t g0 = out_mute_g, g1 = out_mute_req ? 0 : OUT_UNITY;
    int32_t a = ((master * g0) >> 12) * 256, b = ((master * g1) >> 12) * 256;
    int32_t d = (b - a) / (int32_t)nwords;          /* once per block, outside the sample loop */
    uint32_t i;
    out_mute_g = g1;
    for (i = 0; i < nwords; i++) {
        int32_t v = (o[i] * (a >> 8)) >> 12;
        a += d;
        o[i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    }
}
#endif
