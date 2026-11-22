/* SPDX-License-Identifier: GPL-3.0-only */
/* Envelope knob law (curved envelope V3: one-pole Q24 coefficients on a Q15
 * level; rewritten from the step B test for the new tables, units and curve
 * shape. "Time" = full sweep N(q) = 21*(441000/21)^(q/127) samples: release
 * 26000 -> exact 0, decay 26000 -> -60 dB of its distance to sustain; attack
 * 0 -> 26000 in NA(q) = 5.5*(441000/5.5)^(q/127); tools/gen_poli_env_rc.py). Knobs are turned through poli_nav on the FILTER
 * ENV window (8/11, code 7) and the AMP ENV window (9/11, code 8). Times are
 * measured on the engine's own EG level, one sample at a time, at 44.1 kHz.
 * Host only: not a hardware timing. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"

#define FSR 44100.0
#define LIM (12 * 44100L)     /* longest stage is 10.0 s (attack/release 0..26000 at knob 127) */
static int fail;

static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok) fail++;
}

static void open_env(int amp)
{
    poli_nav_init();
    if (amp) poli_nav_section(NAV_SEC_AMP);
    else { poli_nav_section(NAV_SEC_FX); poli_nav_select_turn(2); }
}

static int win_flat(void) { poli_nav_view_t v; poli_nav_view(&v); return v.flat; }
static int knob_pos(int k) { poli_nav_view_t v; poli_nav_view(&v); return v.slot_pos[k]; }
static void set_knob(int k, int target) { int d = target - knob_pos(k); if (d) poli_nav_knob(k, d); }

/* the engine's own Q15 level (polivoks_read reports level >> 15, which reads
   0 up to 17 samples before the level is exactly 0 at the slowest rate) */
static int32_t eg_of(int amp) { return amp ? poli_eg_a : poli_eg_f; }

/* One sample; returns |left| */
static int32_t tick(void)
{
    int32_t o[2] = { 0, 0 };
    polivoks_render(o, 1);
    return o[0] < 0 ? -o[0] : o[0];
}

#define Q(x) ((int32_t)(x) << 15)
typedef struct { long atk, dec, rel_pk, rel_sus; double peak_out; } meas_t;

/* stage: knob index 0 atk, 1 dec, 3 rel. Samples, -1 = not reached in 12 s. */
static meas_t measure(int amp, int k, int p)
{
    meas_t m = { -1, -1, -1, -1, 0 };
    long n;
    int32_t e, o;
    /* attack and decay, note held */
    open_env(amp);
    set_knob(k, p);
    polivoks_midi(0x90, 60, 100);
    for (n = 0; n < LIM; n++) {
        o = tick(); e = eg_of(amp);
        if (o > m.peak_out) m.peak_out = o;
        if (m.atk < 0 && e >= Q(26000)) m.atk = n + 1;
        else if (m.atk >= 0 && m.dec < 0) {
            int32_t s = amp ? poli_sus_a : poli_sus_f;
            if (e <= s + (Q(26000) - s) / 1000) { m.dec = n + 1 - m.atk; break; }
        }
    }
    polivoks_midi(0x80, 60, 0);
    /* release from the 26000 peak: key off on the peak sample */
    open_env(amp);
    set_knob(k, p);
    polivoks_midi(0x90, 60, 100);
    for (n = 0; n < LIM && eg_of(amp) < Q(26000); n++) tick();
    polivoks_midi(0x80, 60, 0);
    for (n = 0; n < LIM; n++) { tick(); if (eg_of(amp) == 0) { m.rel_pk = n + 1; break; } }
    /* release from the default sustain (14000) after a 1 s hold */
    open_env(amp);
    set_knob(k, p);
    polivoks_midi(0x90, 60, 100);
    for (n = 0; n < 44100L; n++) tick();
    polivoks_midi(0x80, 60, 0);
    for (n = 0; n < LIM; n++) { tick(); if (eg_of(amp) == 0) { m.rel_sus = n + 1; break; } }
    return m;
}

static double ms(long s) { return s < 0 ? -1.0 : s * 1000.0 / FSR; }

static long stage_of(const meas_t *m, int k) { return k == 0 ? m->atk : k == 1 ? m->dec : m->rel_pk; }
/* nominal times from the documented law, independent of the engine code */
#define K_A log(34000.0 / 8000.0)
#define K_DR log(26026.0 / 26.0)
static double nfull(int q) { return 21.0 * pow(441000.0 / 21.0, q / 127.0); }
static double nfull_a(int q) { return 5.5 * pow(441000.0 / 5.5, q / 127.0); }
static double t_att(int q) { return nfull_a(q); }
static double t_dec(int q) { return nfull(q) * log(1000.0) / K_DR; }
static double t_rel(int q, int32_t from) { return nfull(q) * log((from + 26.0) / 26.0) / K_DR; }
/* allowed: 2 samples + 1.5 % (Q24 coefficient rounding is at most 0.5/c, 0.9 % at c = 55; each
   step is floored, which speeds the last -60 dB tail slightly: measured up to 1.2 % short) */
static int old_of(int k, int p)
{
    return k == 0 ? poli_env_old_atk(p) : k == 1 ? poli_env_old_dec(p) : poli_env_old_rel(p);
}
static int near_t(long got, double want, double *err)
{
    double e = got < 0 ? 1e9 : fabs(got - want);
    if (want > 0 && e / want > *err && e > 2.0) *err = e / want;
    return got >= 0 && e <= 2.0 + 0.015 * want;
}
static uint32_t coef(double k, double n, uint32_t cap)
{
    long long c = llround(16777216.0 * (1.0 - exp(-k / n)));
    if (c < 1) c = 1;
    return c > (long long)cap ? cap : (uint32_t)c;
}

int main(void)
{
    static const int SHOW[] = { 0, 1, 32, 64, 96, 127 };
    static const char *KN[4] = { "K1 attack", "K2 decay", "", "K4 release" };
    static const int KS[3] = { 0, 1, 3 };
    char d[200];
    int amp, ki, p, i;

    /* windows */
    open_env(1);
    expect("amp_window", win_flat() == 8 && knob_pos(0) == ENV_KNOB_ATK && knob_pos(1) == ENV_KNOB_DEC
           && knob_pos(3) == ENV_KNOB_REL && ENV_KNOB_ATK == 0 && ENV_KNOB_DEC == 0 && ENV_KNOB_REL == 4,
           "AMP ENV is code window 8 (9/11); law-5 start positions 0/0/4 (old 27/31/39 cannot all be kept)");
    open_env(0);
    expect("filter_window", win_flat() == 7, "FILTER ENV is code window 7 (8/11)");

    /* table shape */
    {
        int ok = 1, formula = 1;
        for (i = 0; i < 127; i++) {
            if (ENV_C_ATK[i] <= ENV_C_ATK[i + 1] || ENV_C_ATK[i + 1] < 1) ok = 0;
            if (ENV_C_DR[i] <= ENV_C_DR[i + 1] || ENV_C_DR[i + 1] < 1) ok = 0;
        }
        if (ENV_C_ATK[0] > POLI_EG_CMAX_A || ENV_C_DR[0] > POLI_EG_CMAX_DR) ok = 0;
        for (i = 0; i < 128; i++)
            if (ENV_C_ATK[i] != coef(K_A, nfull_a(i), POLI_EG_CMAX_A) || ENV_C_DR[i] != coef(K_DR, nfull(i), POLI_EG_CMAX_DR)) formula = 0;
        snprintf(d, sizeof d, "Q24: attack %u..%u, decay/release %u..%u, fall at every click, never 0, under the 8000-level caps; "
                 "c = round(2^24*(1-exp(-K/N(q)))), K = ln(34000/8000) / ln(26026/26)",
                 ENV_C_ATK[0], ENV_C_ATK[127], ENV_C_DR[0], ENV_C_DR[127]);
        expect("table", ok && formula, d);
        expect("default_pins", ENV_C_ATK[ENV_POS_ATK] == POLI_EG_CA0 && ENV_C_DR[ENV_POS_DEC] == POLI_EG_CD0
               && ENV_C_DR[ENV_POS_REL] == POLI_EG_CR0, "engine boot coefficients == table at 27/31/39");
    }

    /* untouched boot values and the one-stage-only rule */
    poli_nav_init();
    expect("boot_defaults", poli_atk_a == (int32_t)ENV_C_ATK[44] && poli_dec_a == (int32_t)ENV_C_DR[50]
           && poli_sus_a == Q(14000) && poli_rel_a == (int32_t)ENV_C_DR[36]
           && poli_atk_f == (int32_t)ENV_C_ATK[44] && poli_dec_f == (int32_t)ENV_C_DR[50]
           && poli_sus_f == Q(14000) && poli_rel_f == (int32_t)ENV_C_DR[36],
           "engine boot coefficients are the remapped 0/0/4 (old 44/50/36); sustain 14000");
    for (amp = 0; amp < 2; amp++) {
        for (ki = 0; ki < 4; ki++) {
            int32_t *v = amp ? &poli_atk_a : &poli_atk_f;
            int32_t before[4], after[4], back[4];
            int j, others = 1;
            open_env(amp);
            before[0] = amp ? poli_atk_a : poli_atk_f; before[1] = amp ? poli_dec_a : poli_dec_f;
            before[2] = amp ? poli_sus_a : poli_sus_f; before[3] = amp ? poli_rel_a : poli_rel_f;
            poli_nav_knob(ki, 1);
            after[0] = amp ? poli_atk_a : poli_atk_f; after[1] = amp ? poli_dec_a : poli_dec_f;
            after[2] = amp ? poli_sus_a : poli_sus_f; after[3] = amp ? poli_rel_a : poli_rel_f;
            poli_nav_knob(ki, -1);
            back[0] = amp ? poli_atk_a : poli_atk_f; back[1] = amp ? poli_dec_a : poli_dec_f;
            back[2] = amp ? poli_sus_a : poli_sus_f; back[3] = amp ? poli_rel_a : poli_rel_f;
            for (j = 0; j < 4; j++) if (j != ki && (after[j] != before[j] || back[j] != before[j])) others = 0;
            /* the other envelope is untouched too */
            if (amp ? (poli_atk_f != (int32_t)ENV_C_ATK[44] || poli_dec_f != (int32_t)ENV_C_DR[50] || poli_sus_f != Q(14000) || poli_rel_f != (int32_t)ENV_C_DR[36])
                    : (poli_atk_a != (int32_t)ENV_C_ATK[44] || poli_dec_a != (int32_t)ENV_C_DR[50] || poli_sus_a != Q(14000) || poli_rel_a != (int32_t)ENV_C_DR[36])) others = 0;
            (void)v;
            snprintf(d, sizeof d, "%s K%d +1/-1: own %d->%d->%d, other stages and the other envelope unchanged",
                     amp ? "amp" : "filter", ki + 1, (int)before[ki], (int)after[ki], (int)back[ki]);
            expect("one_stage_only", others && (ki == 2 || back[ki] == before[ki]), d);
        }
    }

    /* times */
    for (amp = 1; amp >= 0; amp--) {
        printf("\n%s envelope (window %d/11). ms at 44.1 kHz, host engine:\n", amp ? "AMP" : "FILTER", amp ? 9 : 8);
        printf("  knob  c_atk/c_dr(Q24)    attack 0->26000   decay 26000->14000(-60dB)   release 26000->0   release 14000->0\n");
        for (i = 0; i < 6; i++) {
            int qa = poli_env_old_atk(SHOW[i]), qd = poli_env_old_dec(SHOW[i]), qr = poli_env_old_rel(SHOW[i]);
            meas_t a = measure(amp, 0, SHOW[i]), b = measure(amp, 1, SHOW[i]), c = measure(amp, 3, SHOW[i]);
            printf("  %4d  %8u/%-8u   %10.2f        %10.2f                  %10.2f        %10.2f  (old atk/dec/rel %d/%d/%d)\n", SHOW[i], ENV_C_ATK[qa], ENV_C_DR[qd],
                   ms(a.atk), ms(b.dec), ms(c.rel_pk), ms(c.rel_sus), qa, qd, qr);
        }
        for (ki = 0; ki < 3; ki++) {
            int k = KS[ki], mono = 1, strict = 1, sound = 1, acc = 1;
            long prev = -1, sh[6];
            double worst = 0.0;
            int32_t sus;
            for (p = 0; p < 128; p++) {
                meas_t m = measure(amp, k, p);
                long t = stage_of(&m, k);
                double e1, e2 = -1.0;
                sus = (amp ? poli_sus_a : poli_sus_f) >> 15;
                /* law time: attack 0->26000, decay 26000->-60 dB of the distance, release 26000->0 and sus->0 */
                {
                    int q = old_of(k, p);
                    e1 = k == 0 ? t_att(q) : k == 1 ? t_dec(q) : t_rel(q, 26000);
                    if (!near_t(t, e1, &worst)) acc = 0;
                    if (k == 3) { e2 = t_rel(q, sus); if (!near_t(m.rel_sus, e2, &worst)) acc = 0; }
                    printf("TIME %s %s pos %3d old %3d c %8u measured %8ld law %10.1f samples (%.2f ms)", amp ? "amp" : "filter",
                           k == 0 ? "attack" : k == 1 ? "decay" : "release", p, q, k == 0 ? ENV_C_ATK[q] : ENV_C_DR[q], t, e1, ms(t));
                }
                if (k == 3) printf("; from sustain %d: measured %8ld law %10.1f (%.2f ms)", (int)sus, m.rel_sus, e2, ms(m.rel_sus));
                printf("\n");
                if (t < 0 || t < prev) mono = 0;
                if (k == 0 && (m.atk < 0 || m.peak_out < 100.0)) sound = 0;
                prev = t;
                for (i = 0; i < 6; i++) if (SHOW[i] == p) sh[i] = t;
            }
            /* 0 and 1 (21/22.7 samples; attack 5.5/6.0) may round to near-equal whole samples; only 0 <= 1 is asked */
            if (sh[1] < sh[0]) strict = 0;
            for (i = 1; i < 5; i++) if (sh[i + 1] <= sh[i]) strict = 0;
            snprintf(d, sizeof d, "%s %s: never shorter as the knob rises (all 128: %s); samples at 0/1/32/64/96/127 = %ld/%ld/%ld/%ld/%ld/%ld, longer from 1 up",
                     amp ? "amp" : "filter", KN[k], mono ? "yes" : "NO", sh[0], sh[1], sh[2], sh[3], sh[4], sh[5]);
            expect("monotonic", mono && strict, d);
            snprintf(d, sizeof d, "%s %s: all 128 positions within 2 samples + 1.5 %% of the law time (worst beyond 2 samples %.3f %%)",
                     amp ? "amp" : "filter", KN[k], worst * 100.0);
            expect("time_accuracy", acc, d);
            if (k == 0) {
                snprintf(d, sizeof d, "%s attack at every position, 0 included, reaches the peak and sounds", amp ? "amp" : "filter");
                expect("attack_sounds", sound, d);
            }
        }
        {
            meas_t m0 = measure(amp, 0, 0), r = measure(amp, 3, 127), a = measure(amp, 0, 127);
            snprintf(d, sizeof d, "%s attack knob 0: peak in %.3f ms, output peak %.0f", amp ? "amp" : "filter", ms(m0.atk), m0.peak_out);
            expect("attack0_sound", m0.atk > 0 && m0.peak_out >= 100.0, d);
            snprintf(d, sizeof d, "%s release knob 127: from the peak to exact 0 %.1f ms (law 10.0 s), from sustain 14000 %.1f ms (law 9.105 s)",
                     amp ? "amp" : "filter", ms(r.rel_pk), ms(r.rel_sus));
            expect("release_max", r.rel_pk >= (long)(9.9 * FSR) && r.rel_pk <= (long)(10.1 * FSR)
                   && r.rel_sus >= (long)(9.0 * FSR) && r.rel_sus <= (long)(9.2 * FSR), d);
            snprintf(d, sizeof d, "%s attack knob 127: 0->26000 in %.1f ms (law 10.0 s)", amp ? "amp" : "filter", ms(a.atk));
            expect("attack_max", a.atk >= (long)(9.9 * FSR) && a.atk <= (long)(10.1 * FSR), d);
        }
    }
    {
        int ok = 1, n, ua = 0, ud = 0, ur = 0, seen[128], i;
        if (poli_env_old_atk(0) != 44 || poli_env_old_atk(127) != 127) ok = 0;
        if (poli_env_old_dec(0) != 50 || poli_env_old_dec(127) != 127) ok = 0;
        if (poli_env_old_rel(0) != 0 || poli_env_old_rel(10) != 90 || poli_env_old_rel(127) != 127) ok = 0;
        if (poli_env_old_rel(4) != 36 || poli_law5_env(39, 2) != 4 || poli_law5_env(27, 0) != 0 || poli_law5_env(31, 1) != 0) ok = 0;
        if (poli_cut_of_knob(0) != 0 || poli_cut_of_knob(127) != POLI_CUT_TOP || poli_cut_of_knob(59) != 11556) ok = 0;
        if (poli_law5_cut(51) != 59) ok = 0;
        for (i = 0; i < 128; i++) seen[i] = 0;
        for (n = 0; n < 128; n++) seen[poli_env_old_atk(n)] = 1;
        for (i = 0; i < 128; i++) ua += seen[i];
        for (i = 0; i < 128; i++) seen[i] = 0;
        for (n = 0; n < 128; n++) seen[poli_env_old_dec(n)] = 1;
        for (i = 0; i < 128; i++) ud += seen[i];
        for (i = 0; i < 128; i++) seen[i] = 0;
        for (n = 0; n < 128; n++) seen[poli_env_old_rel(n)] = 1;
        for (i = 0; i < 128; i++) ur += seen[i];
        if (ua != 84 || ud != 78 || ur != 48) ok = 0;
        snprintf(d, sizeof d, "atk/dec/rel distinct %d/%d/%d, cut(127)=%d, Init 27/31/39/51 -> %d/%d/%d/%d",
                 ua, ud, ur, (int)poli_cut_of_knob(127),
                 poli_law5_env(27, 0), poli_law5_env(31, 1), poli_law5_env(39, 2), poli_law5_cut(51));
        expect("law5_maps", ok, d);
    }
    printf("\n%s\n", fail ? "HOST-ENV FAIL" : "HOST-ENV PASS");
    return fail ? 1 : 0;
}
