/* SPDX-License-Identifier: GPL-3.0-only */
/* Polivoks filter core, corrected trace of factory sheets.
 * polcxem14.jpg (У4 principle, ruskeys Last-Modified 2009-09-22),
 * polcxem3.jpg (panel interconnect, same date), polcxem4.jpg (У4 copper).
 * No dated revision chain. Not matched to any one Hans unit.
 * The previous DERIVATION treated R5/R8 as local feedback and the core as
 * an equal-coefficient Chamberlin pair. That role assignment was wrong.
 *
 * Read on polcxem14 (drawing pin numbers, 8-pin symbols; a 14-pin
 * КР140УД12 package is not substituted). Part values confirmed on the
 * sheet: R1 100k, R2 3k, R3 47k, R4 1k, R5 110k, R6 47k, R7 1k, R8 110k,
 * V1 КТ315Г, A2 and A3 marked КР140УД12.
 *   A2 pin 3 grounded. A2 pin 2 is the summing node on X1/2.
 *   R3 47k returns A3 pin 6 to that node. R4 1k from that node to ground.
 *   A2 pin 6 goes through R6 47k to A3 pin 3. R7 1k shunts A3 pin 3 to ground.
 *   A3 pin 2 grounded. A3 pin 6 is X1/4 and the R3 return.
 *   R5 and R8 are 110k from the common V1 collector to each chip's pin 8
 *   (Iset). They are not output-to-input feedback. A fresh pixel-trace of
 *   those two wires was not re-signed in this lane; the routing is the
 *   corrected derivation, which replaced the local-feedback reading.
 *   A1 pin 3 grounded. Its inverting node takes R1 100k from +12.5 V,
 *   X1/1, and V1's base. V1 is КТ315Г, emitter at A1's output, R2 3k
 *   from base to emitter, collector feeding R5 and R8. R1's value is not
 *   used in the sample coefficient.
 *   C1 5.0 µF is on A4, the contour integrator. A4/A5 and V2–V12 are
 *   contour circuitry, not the filter stages and not a traced resonance law.
 * polcxem3 shows R54 47k and the Резонанс pot/switch on the signal and
 * resonance pins of the filter connector. The wiper's far end was not
 * signed off, so this file does not claim a pot equation.
 *
 * Derived if input current and op-amp input current are neglected:
 *   A3+ = A2out * 1/48 from the 47k+1k divider.
 *   A2's summing node loads A3out and an external current through the
 *   same 48. With A(s) ≈ ωt/(s+ωa) and ωt2 ≈ ωt3 because R5 = R8,
 *   both stages integrate with coefficient ωt/48, and the low-pass at
 *   A3out is inverted relative to the summing-node input. A2out is the
 *   band-pass. Absolute ωt(I) is not known: UA776-class sheets show
 *   higher GBW and slew at higher Iset, and the Soviet minimums are
 *   different numbers. Neither is a Hz law for this board.
 *   Neglecting V1 base current, VBE ≈ 3k * J and IC is exponential in
 *   VBE. Everything that feeds X1/1 was not traced, and ωt is not shown
 *   to be proportional to IC, so the knob-to-coefficient curve below is
 *   an assumed scale, not a calibration.
 *   A purely resistive copy of A3out back into the summing node raises
 *   the oscillation frequency and not the damping, under this dominant
 *   pole model. The resonance control is therefore an explicit damping
 *   term that can go negative. That term is a model stand-in, not the
 *   untraced pot.
 *
 * Nonlinearity (slew limit and the soft state ceiling) is a modeled
 * bound so a past-critical resonance can whistle and a large input can
 * take that whistle down inside the loop. It is not a measured clip
 * voltage and not the 0.01 V/µs Soviet minimum turned into a sample rate.
 * Wet LP/BP published below are those states, before the bass mix and
 * before the ±24000 headroom clamp on the supported outputs. The clamp
 * is not what removes the whistle.
 *
 * Bass support is not on these sheets. It is the locked sidechain:
 * a one-pole of the pre-filter sample, blended after the wet outputs
 * and before the amp envelope (the envelope is outside this file).
 * The traced loop inverts the low-pass, so the sidechain is subtracted;
 * otherwise it cancels. Gain is 0 at and below a resonance knee.
 * There is no octave divider and no extra oscillator. The same subtract
 * is applied to the band-pass output as a stand-in, not a traced BP tap.
 *
 * Integer bounds, not circuit facts. The audio sample is saturated to
 * ±32767 before the loop. Inside that headroom, with states held by
 * poli_fsoft at ±15000 and the smoothed drive at most 32767, the
 * integrator products use int64 before the >>15 so a may exceed 24000
 * (a path to the factory 100 Hz..10 kHz span). With |a3|<=15000,
 * |u|<=65534 and a<=POLI_A_CAP, those products stay inside int64 and
 * the shifted results fit in int32. The bass follower is unchanged and
 * still fits in int32. Signed shifts are arithmetic. This is not a
 * factory-faithful filter. The octave map is calibrated so a host
 * measurement of LP -3 dB at resonance 0 tracks 100 Hz at cut 0 and
 * 10 kHz at the nav knob top (see POLI_A_BASE / POLI_A_CAP / POLI_CUT_TOP).
 *
 * Stability of the discrete model, not a proof of the analog loop:
 * smoothers walk by err>>5 and do not overshoot a clamped target;
 * each integrator step is slew-limited and then soft-ceilinged at ±15000,
 * so a negative damping term can whistle only inside that box.
 */
#ifndef POLI_FCORE_C
#define POLI_FCORE_C
#include "poli_fcore.h"

/* |x| past 11000 softens. 15000 is the ceiling. Sign of the result matches x.
   Zero stays zero. The shift is arithmetic on a nonnegative magnitude. */
static inline __attribute__((always_inline)) int32_t poli_fsoft(int32_t x)
{
    int32_t ax = x < 0 ? -x : x;
    int32_t y;
    if (ax < 11000) return x;
    y = 11000 + ((ax - 11000) >> 3);
    if (y > 15000) y = 15000;
    return x < 0 ? -y : y;
}

/* In-range body. Callers guarantee |x|<=32767, cut/res/drive in 0..32767,
   and |bass|<=32767. Smoothers do not overshoot those targets, so the
   smoothed state stays inside them. |a2| and |a3| after poli_fsoft are
   <=15000. With resonance <=32767, bass gain is at most 10883, so the
   12000 cap on that gain never fires. The ±24000 output clamp can fire. */
static void poli_fcore_run(poli_fcore_t *s, int32_t x, int32_t cut, int32_t res, int32_t drive,
                           int32_t *lp_out, int32_t *bp_out)
{
    int32_t a, d, u, dy, dz, sl, sh, frac, base, sg, add, yw, yb;
    s->cut += (cut - s->cut) >> 5;
    s->res += (res - s->res) >> 5;
    s->drv += (drive - s->drv) >> 5;
    sh = s->cut >> 12;
    if (sh > POLI_A_SH_MAX) sh = POLI_A_SH_MAX;
    frac = s->cut & 4095;
    base = POLI_A_BASE << sh;
    a = base + ((base * frac) >> 12);
    if (a > POLI_A_CAP) a = POLI_A_CAP;
    d = (a >> 1) - (int32_t)(((int64_t)s->res * a) >> 15);
    /* |x|<=32767 and drive<=32767: x*drv fits in int32.
       a*(a3+u) and d*a2 are accumulated in int64: at POLI_A_CAP and
       |a3|+|u| <= 15000+65534 the product is below 2^47. The two terms
       that share coefficient a are still summed before the shift. */
    u = (x * s->drv) >> 14;
    dy = -(int32_t)(((int64_t)a * ((int64_t)s->a3 + u)) >> 15)
       - (int32_t)(((int64_t)d * s->a2) >> 15);
    /* Slew used to cap at 1800 (and 350+(a>>5) never even reached that),
     * which pinned a max-res whistle around 700 Hz. Scale with a so a
     * 10 kHz oscillation at the ±11000 soft-knee can move (~15670 counts
     * a sample). Soft-knee 11000, ceiling 15000 and output ±24000 stay. */
    sl = 350 + (a >> 1);
    if (sl > 30000) sl = 30000;
    if (dy > sl) dy = sl;
    if (dy < -sl) dy = -sl;
    s->a2 = poli_fsoft(s->a2 + dy);
    dz = (int32_t)(((int64_t)a * s->a2) >> 15)
       - (int32_t)(((int64_t)d * s->a3) >> 15);
    if (dz > sl) dz = sl;
    if (dz < -sl) dz = -sl;
    s->a3 = poli_fsoft(s->a3 + dz);
    s->bass += (int32_t)(((int64_t)(x - s->bass) * 280) >> 15);
    sg = 0;
    if (s->res > 11000) sg = (s->res - 11000) >> 1;
    add = (int32_t)(((int64_t)s->bass * sg) >> 15);
    yw = s->a3 - add;
    yb = s->a2 - add;
    if (yw > 24000) yw = 24000;
    if (yw < -24000) yw = -24000;
    if (yb > 24000) yb = 24000;
    if (yb < -24000) yb = -24000;
    *lp_out = yw;
    *bp_out = yb;
}

void poli_fcore_reset(poli_fcore_t *s)
{
    s->a2 = 0;
    s->a3 = 0;
    s->bass = 0;
    s->cut = 0;
    s->res = 0;
    s->drv = 0;
}

void poli_fcore_quiet(poli_fcore_t *s)
{
    s->a2 = 0;
    s->a3 = 0;
    s->bass = 0;
}

void poli_fcore_tick(poli_fcore_t *s, int32_t x, int32_t cut, int32_t res, int32_t drive,
                     int32_t *lp_wet, int32_t *bp_wet, int32_t *lp_out, int32_t *bp_out)
{
    /* Host probes pass values outside the firmware's ranges, including a
       bass state of INT32_MAX. Saturate here, then run the same body. */
    if (x > 32767) x = 32767;
    if (x < -32767) x = -32767;
    if (cut < 0) cut = 0;
    if (cut > 32767) cut = 32767;
    if (res < 0) res = 0;
    if (res > 32767) res = 32767;
    if (drive < 0) drive = 0;
    if (drive > 32767) drive = 32767;
    if (s->bass > 32767) s->bass = 32767;
    if (s->bass < -32767) s->bass = -32767;
    poli_fcore_run(s, x, cut, res, drive, lp_out, bp_out);
    *lp_wet = s->a3;
    *bp_wet = s->a2;
}
#endif
