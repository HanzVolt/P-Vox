/* SPDX-License-Identifier: GPL-3.0-only */
/* Host model regressions for poli_fcore. Not a Polivoks unit. Not CPU. */
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "poli_fcore.c"
#include "wav_write.h"

static int fail;
static char wavdir[512];

static double mag_at(const int32_t *y, int n, double freq)
{
    double acc_r = 0, acc_i = 0, w = 2.0 * 3.141592653589793 * freq / 44100.0;
    int i, a = n - 8192;
    if (a < 0) a = 0;
    for (i = a; i < n; i++) {
        acc_r += (double)y[i] * cos(w * (i - a));
        acc_i -= (double)y[i] * sin(w * (i - a));
    }
    return sqrt(acc_r * acc_r + acc_i * acc_i) / (double)(n - a);
}

static int peak_abs(const int32_t *y, int n)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        int32_t a = y[i] < 0 ? -y[i] : y[i];
        if (a > m) m = a;
    }
    return m;
}

static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok) fail++;
}

static int near_i(double x, int want)
{
    int r = (int)(x >= 0 ? x + 0.5 : x - 0.5);
    return r == want;
}

static void render(poli_fcore_t *s, int32_t *dst, int n, double freq, int32_t amp,
                   int32_t cut, int32_t res, int32_t drive, int which)
{
    int i;
    double ph = 0, w = 2.0 * 3.141592653589793 * freq / 44100.0;
    for (i = 0; i < n; i++) {
        int32_t x = (int32_t)(amp * sin(ph));
        int32_t lw, bw, lo, bo;
        ph += w;
        poli_fcore_tick(s, x, cut, res, drive, &lw, &bw, &lo, &bo);
        if (lw != s->a3 || bw != s->a2) {
            fprintf(stderr, "WET IS NOT STATE lw %d a3 %d bw %d a2 %d\n", lw, s->a3, bw, s->a2);
            fail++;
        }
        dst[i] = which == 0 ? lw : which == 1 ? bw : which == 2 ? lo : bo;
    }
}

static int wav_ok(const char *name, const int32_t *y, int n)
{
    char path[640];
    snprintf(path, sizeof path, "%s/%s", wavdir, name);
    if (poli_wav_write(path, y, NULL, n) != 0) {
        printf("FAIL wav_write  %s\n", path);
        fail++;
        return 0;
    }
    printf("WAV %s\n", path);
    return 1;
}

int main(int argc, char **argv)
{
    enum { N = 30000 };
    static int32_t buf[N];
    static int32_t buf2[N];
    poli_fcore_t s;
    double lp80, lp2k, lp6k, bp80, bp800, w0, lp_lo, lp_hi;
    char line[200];
    int i;
    fail = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: %s wavdir\n", argv[0]);
        return 2;
    }
    snprintf(wavdir, sizeof wavdir, "%s", argv[1]);

    poli_fcore_reset(&s);
    render(&s, buf, N, 80, 4000, 9000, 2000, 14000, 0);
    lp80 = mag_at(buf, N, 80);
    wav_ok("lp_wet_80hz.wav", buf, N);
    poli_fcore_reset(&s);
    render(&s, buf, N, 2000, 4000, 9000, 2000, 14000, 0);
    lp2k = mag_at(buf, N, 2000);
    poli_fcore_reset(&s);
    render(&s, buf, N, 6000, 4000, 9000, 2000, 14000, 0);
    lp6k = mag_at(buf, N, 6000);
    snprintf(line, sizeof line, "LP wet 80=%.0f 2k=%.0f 6k=%.0f", lp80, lp2k, lp6k);
    expect("lp_rolloff", lp80 > lp2k && lp2k > lp6k && lp80 > 3.0 * lp6k
           && near_i(lp80, 1485) && near_i(lp2k, 51) && near_i(lp6k, 6), line);

    /* Positive DC settles to a negative low-pass. A2's first move is negative. */
    {
        int32_t lw, bw, lo, bo;
        double mean_lp = 0, mean_bp = 0;
        poli_fcore_reset(&s);
        s.cut = 14000;
        s.res = 2000;
        s.drv = 14000;
        poli_fcore_tick(&s, 4000, 14000, 2000, 14000, &lw, &bw, &lo, &bo);
        snprintf(line, sizeof line, "first bp %d lp %d", bw, lw);
        expect("bp_first_step_inverting", bw < 0 && lw <= 0, line);
        poli_fcore_reset(&s);
        for (i = 0; i < 12000; i++)
            poli_fcore_tick(&s, 4000, 9000, 2000, 14000, &lw, &bw, &lo, &bo);
        for (i = 0; i < 2000; i++) {
            poli_fcore_tick(&s, 4000, 9000, 2000, 14000, &lw, &bw, &lo, &bo);
            mean_lp += lw;
            mean_bp += bw;
        }
        mean_lp /= 2000.0;
        mean_bp /= 2000.0;
        snprintf(line, sizeof line, "DC mean lp %.1f bp %.1f", mean_lp, mean_bp);
        /* Positive DC lands negative on A3. A2 is inverting too, and the
           shared loss term leaves a DC residue on A2, so this is not a
           textbook zero-DC band-pass. A3 still carries more of that DC. */
        expect("lp_inverted_dc", mean_lp < -500.0 && mean_bp < 0.0 && mean_lp < mean_bp, line);
    }

    poli_fcore_reset(&s);
    render(&s, buf, N, 800, 4000, 800, 1500, 14000, 0);
    lp_lo = mag_at(buf, N, 800);
    poli_fcore_reset(&s);
    render(&s, buf, N, 800, 4000, 20000, 1500, 14000, 0);
    lp_hi = mag_at(buf, N, 800);
    snprintf(line, sizeof line, "800 Hz at cut 800 -> %.0f, at cut 20000 -> %.0f", lp_lo, lp_hi);
    expect("lp_cutoff_opens", lp_hi > 2.0 * lp_lo && lp_hi > lp_lo, line);

    poli_fcore_reset(&s);
    render(&s, buf, N, 80, 4000, 14000, 2000, 14000, 1);
    bp80 = mag_at(buf, N, 80);
    poli_fcore_reset(&s);
    render(&s, buf, N, 800, 4000, 14000, 2000, 14000, 1);
    bp800 = mag_at(buf, N, 800);
    wav_ok("bp_wet_800hz.wav", buf, N);
    snprintf(line, sizeof line, "BP wet 80=%.0f 800=%.0f", bp80, bp800);
    expect("bp_nasal", bp800 > 2.0 * bp80 && near_i(bp80, 653) && near_i(bp800, 2048), line);

    {
        int fq = 0;
        double best = 0;
        poli_fcore_reset(&s);
        s.a2 = 8;
        s.cut = 18000;
        s.res = 30000;
        s.drv = 14000;
        render(&s, buf, N, 100, 0, 18000, 30000, 14000, 0);
        w0 = 0;
        for (i = 300; i < 3000; i += 20) {
            double m = mag_at(buf, N, (double)i);
            if (m > best) { best = m; fq = i; }
            if (m > w0) w0 = m;
        }
        wav_ok("whistle_quiet_wet.wav", buf, N);
        snprintf(line, sizeof line, "quiet whistle %d Hz mag %.0f peak %d a3 %d",
                 fq, best, peak_abs(buf, N), s.a3);
        expect("self_osc", best > 200.0 && fq > 200 && fq < 2000
               && peak_abs(buf, N) <= 15000 && peak_abs(buf, N) < 24000
               && (s.a3 >= 11000 || s.a3 <= -11000), line);

        {
            double loud_at;
            int quiet_peak = peak_abs(buf, N);
            int quiet_a3 = s.a3 < 0 ? -s.a3 : s.a3;
            poli_fcore_reset(&s);
            s.a2 = 8;
            s.cut = 18000;
            s.res = 30000;
            s.drv = 20000;
            render(&s, buf2, N, 100, 7000, 18000, 30000, 20000, 0);
            loud_at = mag_at(buf2, N, (double)fq);
            wav_ok("whistle_driven_wet.wav", buf2, N);
            snprintf(line, sizeof line,
                     "wet %d Hz quiet %.0f loud %.0f quiet_peak %d loud_peak %d quiet_|a3| %d",
                     fq, best, loud_at, quiet_peak, peak_abs(buf2, N), quiet_a3);
            /* loud magnitude moved 54 -> 55 when the two a* products
               in dy were rounded once. The whistle is still killed. */
            expect("drive_kills_whistle_inside_wet",
                   loud_at < 0.55 * best && near_i(best, 770) && near_i(loud_at, 74) && fq >= 1000 && fq <= 1600
                   && quiet_peak < 24000 && peak_abs(buf2, N) <= 15000,
                   line);
        }
        snprintf(line, sizeof line, "a2 %d a3 %d", s.a2, s.a3);
        expect("states_bounded", s.a3 <= 15000 && s.a3 >= -15000 && s.a2 <= 15000 && s.a2 >= -15000, line);
    }

    {
        static int32_t wet[N], sup[N];
        double dw = 0;
        int exact = 1;
        poli_fcore_reset(&s);
        {
            int i2; double ph = 0;
            for (i2 = 0; i2 < N; i2++) {
                int32_t lw, bw, lo, bo, x = (int32_t)(5000 * sin(ph));
                ph += 2.0 * 3.141592653589793 * 80 / 44100.0;
                poli_fcore_tick(&s, x, 6000, 1000, 12000, &lw, &bw, &lo, &bo);
                wet[i2] = lw; sup[i2] = lo;
                if (lo != lw) exact = 0;
            }
        }
        for (i = N - 4000; i < N; i++) {
            double d = (double)sup[i] - (double)wet[i];
            dw += d * d;
        }
        dw = sqrt(dw / 4000.0);
        snprintf(line, sizeof line, "low-res support rms delta %.1f exact %d", dw, exact);
        expect("bass_off_at_and_below_knee", dw < 1.0 && exact, line);
    }

    {
        static int32_t wet[N], sup[N], xin[N];
        double fw, fs, h2, cw = 0, cs = 0;
        poli_fcore_reset(&s);
        {
            int i2; double ph = 0;
            for (i2 = 0; i2 < N; i2++) {
                int32_t lw, bw, lo, bo, x = (int32_t)(6000 * sin(ph));
                ph += 2.0 * 3.141592653589793 * 110 / 44100.0;
                poli_fcore_tick(&s, x, 16000, 26000, 10000, &lw, &bw, &lo, &bo);
                wet[i2] = lw; sup[i2] = lo; xin[i2] = x;
            }
        }
        fw = mag_at(wet, N, 110);
        fs = mag_at(sup, N, 110);
        h2 = mag_at(sup, N, 55);
        for (i = N - 8192; i < N; i++) {
            cw += (double)xin[i] * (double)wet[i];
            cs += (double)xin[i] * (double)sup[i];
        }
        wav_ok("bass_wet_110.wav", wet, N);
        wav_ok("bass_supported_110.wav", sup, N);
        snprintf(line, sizeof line, "fund wet %.0f sup %.0f half %.0f corr_wet %.0f corr_sup %.0f",
                 fw, fs, h2, cw, cs);
        /* Magnitudes moved 687/957/28 -> 686/956/27 with the merged
           dy rounding. The level and half-harmonic inequalities are the same. */
        expect("bass_fundamental_no_octave",
               fs > fw * 1.05 && h2 < 0.25 * fs
               && near_i(fw, 1294) && near_i(fs, 1488) && near_i(h2, 22), line);
        expect("bass_keeps_inverted_lp_phase", cw < 0 && cs < 0 && cs < cw, line);
    }

    {
        int32_t lw = 0, bw = 0, lo = 0, bo = 0;
        int below = 1, crossed = 0;
        poli_fcore_reset(&s);
        for (i = 0; i < 6; i++) {
            poli_fcore_tick(&s, 8000, 9000, 30000, 12000, &lw, &bw, &lo, &bo);
            if (s.res > 11000 || lo != lw) below = 0;
        }
        snprintf(line, sizeof line, "after 6 ticks res %d lo-lw %d", s.res, lo - lw);
        expect("res_smoother_holds_bass_off", below && s.res < 11000, line);
        /* 30000>>5 = 937 on the first tick only. Later ticks are smaller steps.
           The check above only requires the knee stay closed. Re-check first step. */
        poli_fcore_reset(&s);
        poli_fcore_tick(&s, 0, 16384, 20000, 10000, &lw, &bw, &lo, &bo);
        snprintf(line, sizeof line, "cut %d res %d drv %d", s.cut, s.res, s.drv);
        expect("control_smooth_first_step", s.cut == 512 && s.res == 625 && s.drv == 312, line);
        {
            int32_t prev = 0;
            int mono = 1;
            poli_fcore_reset(&s);
            for (i = 0; i < 400; i++) {
                poli_fcore_tick(&s, 0, 16384, 16384, 16384, &lw, &bw, &lo, &bo);
                if (s.cut < prev || s.cut > 16384) mono = 0;
                prev = s.cut;
            }
            snprintf(line, sizeof line, "cut after 400 %d mono %d", s.cut, mono);
            expect("control_smooth_approaches", mono && s.cut > 15000 && s.cut <= 16384
                   && (16384 - s.cut) <= 31, line);
        }
        poli_fcore_reset(&s);
        for (i = 0; i < 4000; i++) {
            poli_fcore_tick(&s, 8000, 9000, 30000, 12000, &lw, &bw, &lo, &bo);
            if (s.res > 11000 && lo != lw) crossed = 1;
        }
        snprintf(line, sizeof line, "settled res %d lo %d lw %d", s.res, lo, lw);
        expect("bass_engages_after_smoother_crosses_knee", crossed && s.res > 11000, line);
    }

    {
        int32_t lw, bw, lo, bo;
        poli_fcore_reset(&s);
        for (i = 0; i < 8000; i++)
            poli_fcore_tick(&s, 8000, 10000, 28000, 12000, &lw, &bw, &lo, &bo);
        for (i = 0; i < 12000; i++)
            poli_fcore_tick(&s, 0, 10000, 28000, 12000, &lw, &bw, &lo, &bo);
        snprintf(line, sizeof line, "after silence bass %d support_delta %d", s.bass, lo - lw);
        expect("no_hang_on_silence", s.bass > -400 && s.bass < 400 && (lo - lw) > -400 && (lo - lw) < 400, line);
        poli_fcore_reset(&s);
        poli_fcore_tick(&s, 0, 10000, 28000, 12000, &lw, &bw, &lo, &bo);
        snprintf(line, sizeof line, "after reset out %d bass %d", lo, s.bass);
        expect("reset_clears", lo > -50 && lo < 50 && s.bass == 0 && s.a2 == 0 && s.a3 == 0, line);
    }

    {
        int32_t lw, bw, lo, bo, worst = 0;
        int bounded = 1;
        poli_fcore_reset(&s);
        for (i = 0; i < 20000; i++) {
            int32_t x = (i & 32) ? 4000 : -4000;
            poli_fcore_tick(&s, x, (i * 3) & 32767, (i * 5) & 32767, 8000 + (i & 4095),
                            &lw, &bw, &lo, &bo);
            if (lo > worst) worst = lo;
            if (-lo > worst) worst = -lo;
            if (lo > 24000 || lo < -24000 || bo > 24000 || bo < -24000) bounded = 0;
            if (s.a2 > 15000 || s.a2 < -15000 || s.a3 > 15000 || s.a3 < -15000) bounded = 0;
            if (s.bass > 32767 || s.bass < -32767) bounded = 0;
        }
        snprintf(line, sizeof line, "sweep peak %d", worst);
        /* Peak moved 11309 -> 11303 with the merged dy rounding.
           The bound is still the ±24000 clamp, not this pin. */
        expect("sweep_finite", bounded && worst < 32767 && near_i((double)worst, 12870), line);
    }

    {
        int32_t lw, bw, lo, bo;
        int bounded = 1;
        uint32_t rng = 1u;
        const int32_t xs[6] = { 0, 1, -1, 32767, -32767, INT32_MIN };
        poli_fcore_reset(&s);
        s.bass = INT32_MAX;
        s.drv = 32767;
        poli_fcore_tick(&s, INT32_MIN, INT32_MAX, INT32_MIN, INT32_MAX, &lw, &bw, &lo, &bo);
        if (s.a2 > 15000 || s.a2 < -15000 || s.a3 > 15000 || s.a3 < -15000) bounded = 0;
        if (s.bass > 32767 || s.bass < -32767) bounded = 0;
        if (lo > 24000 || lo < -24000 || bo > 24000 || bo < -24000) bounded = 0;
        if (s.cut < 0 || s.cut > 32767 || s.res < 0 || s.res > 32767 || s.drv < 0 || s.drv > 32767)
            bounded = 0;
        snprintf(line, sizeof line, "extreme one-tick a2 %d a3 %d bass %d lo %d cut %d",
                 s.a2, s.a3, s.bass, lo, s.cut);
        expect("extreme_one_tick_bounded", bounded, line);
        bounded = 1;
        poli_fcore_reset(&s);
        for (i = 0; i < 8000; i++) {
            int32_t x, cut, res, drv;
            rng = rng * 1664525u + 1013904223u;
            x = xs[rng % 6u];
            if ((rng & 7u) == 0u) x = (int32_t)rng;
            cut = (int32_t)((rng >> 3) & 65535u) - 2000;
            res = (int32_t)((rng >> 9) & 65535u) - 2000;
            drv = (int32_t)((rng >> 15) & 65535u) - 2000;
            poli_fcore_tick(&s, x, cut, res, drv, &lw, &bw, &lo, &bo);
            if (s.a2 > 15000 || s.a2 < -15000 || s.a3 > 15000 || s.a3 < -15000) bounded = 0;
            if (s.bass > 32767 || s.bass < -32767) bounded = 0;
            if (lo > 24000 || lo < -24000 || bo > 24000 || bo < -24000) bounded = 0;
            if (lw != s.a3 || bw != s.a2) bounded = 0;
        }
        snprintf(line, sizeof line, "rng sweep a2 %d a3 %d bass %d", s.a2, s.a3, s.bass);
        expect("extreme_sweep_bounded", bounded, line);
    }

    printf(fail ? "HOST-FCORE FAIL %d\n" : "HOST-FCORE PASS\n", fail);
    return fail ? 1 : 0;
}
