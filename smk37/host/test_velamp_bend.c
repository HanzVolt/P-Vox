/* SPDX-License-Identifier: GPL-3.0-only */
/* Host checks for VelAmp-Bend (14 Nov 2026): MIDI velocity -> amp level (same SLOT_VEL amount and
 * law as the contour), and MIDI pitch bend +-2 semitones with reset on CC 120 / CC 123 / panic.
 * Not a hardware measurement. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"

static int fail, pass;
static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (ok) pass++; else fail++;
}
enum { N = 88200 };
static int32_t a[2 * N], b[2 * N];
static void render(int32_t *y, uint32_t n)
{
    uint32_t done = 0;
    memset(y, 0, sizeof(int32_t) * 2u * n);
    while (done < n) { uint32_t k = n - done > 256u ? 256u : n - done; polivoks_render(y + 2u * done, k); done += k; }
}
static int32_t peak(const int32_t *y, uint32_t n)
{
    uint32_t i; int32_t p = 0;
    for (i = 0; i < 2u * n; i++) { int32_t v = y[i] < 0 ? -y[i] : y[i]; if (v > p) p = v; }
    return p;
}
static int rising(const int32_t *y, uint32_t n)
{
    uint32_t i; int c = 0;
    for (i = 1; i < n; i++) if (y[2u * (i - 1)] < 0 && y[2u * i] >= 0) c++;
    return c;
}
static void voice(int amt, int contour)
{
    polivoks_init();
    polivoks_set_vel_amount(amt);
    polivoks_set_contour_amount(contour);
}
int main(void)
{
    char d[200];
    int v;
    /* ---- velocity -> amp ---- */
    {
        int32_t pk[3]; static const int vv[3] = {127, 64, 1}; int i;
        for (i = 0; i < 3; i++) { voice(127, 0); polivoks_midi(0x90, 57, (uint8_t)vv[i]); render(a, 22050); pk[i] = peak(a, 22050); }
        snprintf(d, sizeof d, "amt 127, contour 0: peak vel127 %d vel64 %d vel1 %d", pk[0], pk[1], pk[2]);
        expect("vel_amp_softer_quieter", pk[0] > pk[1] && pk[1] > pk[2] && pk[2] > 0, d);
        snprintf(d, sizeof d, "vel64/vel127 = %.4f (law 64/127 = %.4f)", (double)pk[1] / pk[0], 64.0 / 127);
        expect("vel_amp_linear_at_full_amount", fabs((double)pk[1] / pk[0] - 64.0 / 127) < 0.01, d);
    }
    for (v = 1; v <= 127; v++) {
        int32_t want = 32768 - (int32_t)(((int64_t)127 * (127 - v) * 32768) / (127 * 127));
        voice(127, 32768); polivoks_midi(0x90, 57, (uint8_t)v);
        if (poli_vel_scale != want) { snprintf(d, sizeof d, "v %d: %d want %d", v, poli_vel_scale, want); expect("vel_scale_law", 0, d); break; }
    }
    if (v > 127) expect("vel_scale_law", 1, "vs = 32768 - amt*(127-v)*32768/127^2 for v 1..127, same number the contour uses");
    voice(0, 32768); polivoks_midi(0x90, 57, 1); render(a, 22050);
    voice(0, 32768); polivoks_midi(0x90, 57, 127); render(b, 22050);
    expect("vel_amt0_bit_identical", !memcmp(a, b, sizeof(int32_t) * 2u * 22050), "amount 0: vel 1 vs vel 127");
    voice(127, 32768); polivoks_midi(0x90, 57, 127); render(b, 22050);
    expect("vel127_amt127_bit_identical", !memcmp(a, b, sizeof(int32_t) * 2u * 22050), "vel 127 at amount 127 vs amount 0");
    {   /* legato does not retake velocity */
        voice(127, 0); polivoks_midi(0x90, 57, 127); polivoks_midi(0x90, 60, 1);
        expect("vel_legato_keeps_first", poli_vel_scale == 32768, "second note-on while gate held");
    }
    {   /* turning the amount off restores full level at once */
        voice(127, 0); polivoks_midi(0x90, 57, 1); polivoks_set_vel_amount(0);
        expect("vel_off_restores_level", poli_vel_scale == 32768, "");
    }
    /* ---- pitch bend ---- */
    expect("bend_mul_centre", poli_bend_mul(0) == 32768u, "");
    snprintf(d, sizeof d, "+2 st %u (ideal %.1f), -2 st %u (ideal %.1f)", poli_bend_mul(8192), 32768 * pow(2, 2 / 12.0), poli_bend_mul(-8192), 32768 * pow(2, -2 / 12.0));
    expect("bend_mul_ends", fabs(poli_bend_mul(8192) - 32768 * pow(2, 2 / 12.0)) < 1.0 && fabs(poli_bend_mul(-8192) - 32768 * pow(2, -2 / 12.0)) < 1.0, d);
    {
        int32_t u; double worst = 0; uint32_t prev = 0; int mono = 1;
        for (u = -8192; u <= 8192; u++) {
            uint32_t m = poli_bend_mul(u);
            double c = 1200.0 * log2(m / 32768.0) - u * (100.0 / 4096), e = fabs(c);
            if (e > worst) worst = e;
            if (u > -8192 && m < prev) mono = 0;
            prev = m;
        }
        snprintf(d, sizeof d, "worst error %.3f cent (incl. Q15 rounding), monotonic %d", worst, mono);
        expect("bend_curve", worst < 0.1 && mono, d);
    }
    polivoks_init();
    polivoks_midi(0xE0, 0x00, 0x40); expect("bend_decode_centre", polivoks_pitch_bend() == 0, "");
    polivoks_midi(0xE0, 0x7F, 0x7F); expect("bend_decode_max", polivoks_pitch_bend() == 8191, "");
    polivoks_midi(0xE0, 0x00, 0x00); expect("bend_decode_min", polivoks_pitch_bend() == -8192, "");
    polivoks_set_pitch_bend(100000); expect("bend_clamp", polivoks_pitch_bend() == 8192, "");
    polivoks_set_pitch_bend(4000); polivoks_midi(0xB0, 120, 0); expect("bend_reset_cc120", polivoks_pitch_bend() == 0, "");
    polivoks_set_pitch_bend(4000); polivoks_midi(0xB0, 123, 0); expect("bend_reset_cc123", polivoks_pitch_bend() == 0, "");
    polivoks_set_pitch_bend(-4000); polivoks_panic(); expect("bend_reset_panic", polivoks_pitch_bend() == 0, "");
    polivoks_set_pitch_bend(4000); polivoks_midi(0x80, 57, 0); expect("bend_kept_on_note_off", polivoks_pitch_bend() == 4000, "a plain note-off keeps the wheel position (standard MIDI)");
    {   /* pitch: +2 st and -2 st by zero crossings over 2 s, contour 0, filter as booted */
        int c0, cu, cd; double ru, rd;
        voice(0, 0); polivoks_midi(0x90, 45, 127); render(a, N); c0 = rising(a, N);
        voice(0, 0); polivoks_midi(0xE0, 0x7F, 0x7F); polivoks_midi(0x90, 45, 127); render(a, N); cu = rising(a, N);
        voice(0, 0); polivoks_midi(0xE0, 0x00, 0x00); polivoks_midi(0x90, 45, 127); render(a, N); cd = rising(a, N);
        ru = (double)cu / c0; rd = (double)cd / c0;
        snprintf(d, sizeof d, "crossings %d / up %d / down %d: ratio %.4f (ideal %.4f), %.4f (ideal %.4f)", c0, cu, cd, ru, pow(2, 8191 / 49152.0), rd, pow(2, -2 / 12.0));
        expect("bend_pitch_both_ways", fabs(ru / pow(2, 8191 / 49152.0) - 1) < 0.005 && fabs(rd / pow(2, -2 / 12.0) - 1) < 0.005, d);
    }
    {   /* centre messages leave the sound bit-identical */
        uint32_t k;
        voice(0, 32768); polivoks_midi(0x90, 45, 100); render(a, 22050);
        voice(0, 32768); polivoks_midi(0x90, 45, 100);
        memset(b, 0, sizeof b);
        for (k = 0; k < 22050; k += 256) { uint32_t n = 22050 - k > 256 ? 256 : 22050 - k; polivoks_midi(0xE0, 0x00, 0x40); polivoks_render(b + 2u * k, n); }
        expect("bend_centre_bit_identical", !memcmp(a, b, sizeof(int32_t) * 2u * 22050), "0xE0 00 40 every block vs none");
    }
    printf("HOST-VELAMP-BEND %s %d pass %d fail\n", fail ? "FAIL" : "PASS", pass, fail);
    return fail ? 1 : 0;
}
