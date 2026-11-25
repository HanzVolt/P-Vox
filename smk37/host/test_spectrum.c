/* SPDX-License-Identifier: GPL-3.0-only */
/* Host checks for the AMP ENV window output-spectrum analyzer:
 * a sine at a known bin lights the matching log bar; silence is empty;
 * a DC offset is rejected. Also peak-fall smoothing, stacked-block
 * colours (blue-to-teal body, red peak only), and a host timing of
 * one analyze() call. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "smk_spectrum.h"

static int fail;

static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok)
        fail++;
}

static void fill_sine(int16_t *x, int bin, int amp)
{
    int n;
    for (n = 0; n < SMK_SA_N; n++) {
        double p = 2.0 * 3.141592653589793 * (double)bin * (double)n / (double)SMK_SA_N;
        double v = (double)amp * sin(p);
        if (v > 32767.0) v = 32767.0;
        if (v < -32768.0) v = -32768.0;
        x[n] = (int16_t)(v >= 0 ? v + 0.5 : v - 0.5);
    }
}

static int peak_bar(const uint16_t *b)
{
    int i, at = 0;
    uint16_t best = 0;
    for (i = 0; i < SMK_SA_BARS; i++) {
        if (b[i] > best) {
            best = b[i];
            at = i;
        }
    }
    return at;
}

static uint16_t peak_h(const uint16_t *b)
{
    int i;
    uint16_t best = 0;
    for (i = 0; i < SMK_SA_BARS; i++)
        if (b[i] > best)
            best = b[i];
    return best;
}

int main(void)
{
    int16_t x[SMK_SA_N];
    uint16_t bars[SMK_SA_BARS], shown[SMK_SA_BARS];
    uint32_t mag[SMK_SA_N / 2 + 1];
    int bin, want, i, ok, peak_i;
    uint32_t best_m, sec;
    char line[160];
    struct timespec t0, t1;
    long ns;
    const int bins[3] = { 1, 4, 32 };

    fail = 0;

    memset(x, 0, sizeof x);
    smk_sa_analyze(x, bars, mag);
    ok = 1;
    for (i = 0; i < SMK_SA_BARS; i++)
        if (bars[i] != 0)
            ok = 0;
    expect("silence_empty", ok, "all 24 bars are 0");
    expect("bin_of_hz_344", smk_sa_bin_of_hz(345) == 1, "");
    expect("bin_of_hz_1378", smk_sa_bin_of_hz(1378) == 4, "");
    expect("bar_h_full", smk_sa_bar_h(256, 40) == 40, "");
    expect("bar_h_zero", smk_sa_bar_h(0, 40) == 0, "");


    for (i = 0; i < SMK_SA_N; i++)
        x[i] = 20000;
    smk_sa_analyze(x, bars, mag);
    ok = 1;
    for (i = 0; i < SMK_SA_BARS; i++)
        if (bars[i] > 4u)
            ok = 0;
    snprintf(line, sizeof line, "peak bar height %u (DC bin mag %u)",
             (unsigned)peak_h(bars), (unsigned)mag[0]);
    expect("dc_rejected", ok, line);

    for (sec = 0; sec < 3u; sec++) {
        bin = bins[sec];
        want = smk_sa_bar_of_bin(bin);
        fill_sine(x, bin, 32000);
        smk_sa_analyze(x, bars, mag);
        /* FFT peak at the intended bin, not DC. */
        best_m = 0;
        peak_i = 0;
        for (i = 0; i <= SMK_SA_N / 2; i++) {
            if (mag[i] > best_m) {
                best_m = mag[i];
                peak_i = i;
            }
        }
        snprintf(line, sizeof line, "bin %d peak_bin %d mag %u bar %d want %d h %u",
                 bin, peak_i, (unsigned)best_m, peak_bar(bars), want, (unsigned)peak_h(bars));
        expect(bin == 1 ? "sine_bin1_fft_peak" : bin == 4 ? "sine_bin4_fft_peak" : "sine_bin32_fft_peak",
               peak_i == bin, line);
        expect(bin == 1 ? "sine_344hz_bar0" : bin == 4 ? "sine_1378hz_bar3" : "sine_11025hz_bar20",
               peak_bar(bars) == want && peak_h(bars) >= 180, line);
        /* Unique peak. Adjacent bars may sit ~6 dB down: that is the
         * Hann frequency-domain kernel, and on a dB scale they still
         * read high. They must stay strictly below the target bar. */
        ok = 1;
        for (i = 0; i < SMK_SA_BARS; i++) {
            if (i == want)
                continue;
            if (bars[i] >= bars[want])
                ok = 0;
        }
        expect(bin == 1 ? "sine_bin1_unique_peak" : bin == 4 ? "sine_bin4_unique_peak" : "sine_bin32_unique_peak",
               ok, line);
    }

    /* Peak-fall: a loud bar then silence decays, does not drop to 0 in one tick. */
    fill_sine(x, 4, 32000);
    smk_sa_analyze(x, bars, 0);
    memcpy(shown, bars, sizeof shown);
    memset(x, 0, sizeof x);
    smk_sa_analyze(x, bars, 0);
    smk_sa_smooth(shown, bars);
    snprintf(line, sizeof line, "after one silent tick shown=%u (was loud)", (unsigned)shown[3]);
    expect("peak_fall_not_instant", shown[3] > 32 && shown[3] < 256, line);
    for (i = 0; i < 40 && peak_h(shown) != 0; i++)
        smk_sa_smooth(shown, bars);
    expect("peak_fall_reaches_zero", peak_h(shown) == 0, "40 silent ticks clear the display");


    /* Stacked-block colours: body is blue→teal, only the top block is red. */
    {
        uint16_t red = SMK_SA_PEAK_RED;
        uint16_t base = smk_sa_blk_color(0, 8);
        uint16_t mid  = smk_sa_blk_color(4, 8);
        uint16_t top  = smk_sa_blk_color(7, 8);
        int br, bg, bb, mr, mg, mb, tr, tg, tb;
        (void)tg;
        #define U565(c, r, g, b) do { \
            (r) = (int)(((c) >> 11) & 31); \
            (g) = (int)(((c) >> 5) & 63); \
            (b) = (int)((c) & 31); \
        } while (0)
        U565(base, br, bg, bb);
        U565(mid, mr, mg, mb);
        U565(top, tr, tg, tb);
        (void)red;
        expect("one_block_is_peak_red", smk_sa_blk_color(0, 1) == SMK_SA_PEAK_RED, "");
        expect("top_block_is_red", top == SMK_SA_PEAK_RED && tr >= 28 && tb <= 8, "");
        expect("base_is_blue", bb >= 24 && bg <= 16 && br <= 4, "");
        expect("body_fades_to_teal", mg > bg && mb < bb && mr <= 4 && mid != SMK_SA_PEAK_RED, "");
        ok = 1;
        for (i = 0; i < 7; i++) {
            uint16_t c = smk_sa_blk_color(i, 8);
            int r, g, b;
            U565(c, r, g, b);
            (void)g; (void)b;
            if (c == SMK_SA_PEAK_RED || r >= 20)
                ok = 0;
        }
        expect("only_peak_block_is_red", ok, "body blocks 0..6 are not red");
        expect("nblocks_silence", smk_sa_nblocks(0, 52) == 0, "");
        expect("nblocks_full_positive", smk_sa_nblocks(256, 52) >= 8, "");
        #undef U565
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 1000; i++) {
        fill_sine(x, 4, 32000);
        smk_sa_analyze(x, bars, 0);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ns = (t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec);
    snprintf(line, sizeof line, "1000 analyze() calls in %ld us, ~%ld us each (host)",
             ns / 1000, ns / 1000000 > 0 || 1 ? ns / 1000000 : 0);
    /* ns/1000 = total us; per call = ns/1000/1000 = ns/1e6 us? Wait:
     * per call us = (ns/1000)/1000 = ns/1e6. That's milliseconds if I mess up.
     * total ns for 1000 calls; per call ns = ns/1000; per call us = ns/1000/1000 = ns/1e6. */
    {
        long per_ns = ns / 1000;
        snprintf(line, sizeof line, "1000 calls in %ld us, ~%ld us (%ld ns) each on this host",
                 ns / 1000, per_ns / 1000, per_ns);
        /* Modest: a 25 Hz tick at a few tens of microseconds will not
         * stall the 1 ms TIMER5 cutoff glide (IRQs stay enabled). */
        expect("analyze_host_time_recorded", 1, line);
        printf("HOST_ANALYZE_US_EACH %ld\n", per_ns / 1000);
        printf("HOST_ANALYZE_NS_EACH %ld\n", per_ns);
    }

    printf("suite_fail=%d\n", fail);
    return fail ? 1 : 0;
}
