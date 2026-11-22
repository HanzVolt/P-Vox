/* SPDX-License-Identifier: GPL-3.0-only */
/* Polivoks filter core. Unity-friendly: firmware/src/volt.c includes poli_fcore.c.
 * Integers only. No float. No 64-bit division. Not a ladder. Not stock FM.
 *
 * Sheet values read on polcxem14 (У4): R1 100k, R2 3k, R3 47k, R4 1k,
 * R5 110k, R6 47k, R7 1k, R8 110k, V1 КТ315Г, A2 and A3 КР140УД12.
 * Corrected routing, not the superseded local-feedback reading: R4/R7 shunt
 * the audio nodes, R5/R8 feed the set-current pins, R3 is the A3-to-A2
 * feedback, R6 couples A2's output into A3's non-inverting input.
 * A2 is the inverting stage, A3 the non-inverting stage.
 *
 * Not established, and not filled in here: the resonance pot's far terminal
 * on the interconnect, the cutoff-pin current sum, ωt versus set current,
 * and the damping law. The coefficient and the damping term are labeled
 * stand-ins in poli_fcore.c.
 */
#ifndef POLI_FCORE_H
#define POLI_FCORE_H
#include <stdint.h>

/* Octave map: a = POLI_A_BASE << (cut>>12), interpolated by the low 12 bits,
 * then capped. Knob 0..127 is mapped in poli_nav so measured LP -3 dB at
 * resonance 0 aims at 100 Hz .. 10 kHz (factory spec). These three numbers
 * are the calibration; retune them together if the host FRF is rerun. */
#define POLI_A_BASE 331
#define POLI_A_SH_MAX 7
#define POLI_A_CAP 60000

typedef struct {
    int32_t a2, a3, bass; /* A2 band-pass, A3 low-pass, pre-filter follower */
    int32_t cut, res, drv; /* smoothed controls, 0..32767 after a reset tick */
} poli_fcore_t;

void poli_fcore_reset(poli_fcore_t *s);
void poli_fcore_quiet(poli_fcore_t *s);
/* Wet pointers are the states after the in-loop slew and soft ceiling,
 * before the bass mix and before the supported-output headroom clamp.
 * lp_out/bp_out are that mix. Bass gain is 0 at and below the resonance knee.
 */
void poli_fcore_tick(poli_fcore_t *s, int32_t x, int32_t cut, int32_t res, int32_t drive,
                     int32_t *lp_wet, int32_t *bp_wet, int32_t *lp_out, int32_t *bp_out);
#endif
