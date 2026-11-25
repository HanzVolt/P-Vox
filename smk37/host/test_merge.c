/* SPDX-License-Identifier: GPL-3.0-only */
/* PVox merge (17 Nov 2026) host checks, shared by the FM-1 and SMK-37 Elite merge trees:
 *  - MIDI note 127 plays as 126 (the keybed rule), never as middle C; a stale 127 in the engine also plays 126
 *  - velocity loudness is one linear gain AFTER the Bass Support blend: soft note = full note x vs, sample by
 *    sample (bass support on and off), so the Bass Support character is not changed
 *  - pitch bend returns to centre on CC 120, CC 123 and panic; a plain note-off keeps it
 * Host only. Not a hardware measurement. */
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
enum { N = 44100 };
static int32_t a[2 * N], b[2 * N], c[2 * N];
static void render(int32_t *y, uint32_t n)
{
    uint32_t done = 0;
    memset(y, 0, sizeof(int32_t) * 2u * n);
    while (done < n) { uint32_t k = n - done > 256u ? 256u : n - done; polivoks_render(y + 2u * done, k); done += k; }
}
static int rising(const int32_t *y, uint32_t n)
{
    uint32_t i; int k = 0;
    for (i = 1; i < n; i++) if (y[2u * (i - 1)] < 0 && y[2u * i] >= 0) k++;
    return k;
}
static int32_t peak(const int32_t *y, uint32_t n)
{
    uint32_t i; int32_t p = 0;
    for (i = 0; i < 2u * n; i++) { int32_t v = y[i] < 0 ? -y[i] : y[i]; if (v > p) p = v; }
    return p;
}
int main(void)
{
    char d[240];
    /* ---- note 127 ---- */
    {
        int c126, c127, c60;
        polivoks_init(); polivoks_midi(0x90, 126, 127); render(a, N); c126 = rising(a, N);
        polivoks_init(); polivoks_midi(0x90, 127, 127); render(b, N); c127 = rising(b, N);
        polivoks_init(); polivoks_midi(0x90, 60, 127); render(c, N); c60 = rising(c, N);
        snprintf(d, sizeof d, "rising zero crossings in 1 s: MIDI 126 %d, MIDI 127 %d, MIDI 60 %d", c126, c127, c60);
        expect("note127_not_middle_c", c127 != c60, d);
        expect("note127_plays_126_bit_identical", !memcmp(a, b, sizeof a), "MIDI 127 render == MIDI 126 render, every sample");
        polivoks_init(); polivoks_midi(0x90, 127, 127);
        snprintf(d, sizeof d, "poli_note after MIDI 127 = %u", (unsigned)poli_note);
        expect("note127_stored_as_126", poli_note == 126u, d);
        polivoks_init(); polivoks_midi(0x90, 126, 127); poli_note = 127u; render(c, N);
        expect("stale_127_in_render_plays_126", !memcmp(a, c, sizeof a), "engine note forced to 127 renders exactly as 126 (was middle C)");
        polivoks_init(); polivoks_midi(0x90, 100, 127); render(a, 4096);
        polivoks_init(); polivoks_midi(0x90, 100, 127); render(b, 4096);
        expect("note100_unchanged_path", !memcmp(a, b, sizeof(int32_t) * 2u * 4096), "notes 0..126 take the same path (spot check 100)");
    }
    /* ---- velocity loudness sits after Bass Support: soft = full x vs, sample by sample ---- */
    {
        int bs, v; static const int vv[3] = {96, 64, 20};
        for (bs = 1; bs >= 0; bs--) for (v = 0; v < 3; v++) {
            int32_t vs = 32768 - (int32_t)(((int64_t)127 * (127 - vv[v]) * 32768) / (127 * 127));
            uint32_t i; int32_t worst = 0, pk;
            polivoks_init(); polivoks_set_bass_support((uint8_t)bs); polivoks_set_vel_amount(127); polivoks_set_contour_amount(0);
            polivoks_midi(0x90, 45, 127); render(a, 22050);
            polivoks_init(); polivoks_set_bass_support((uint8_t)bs); polivoks_set_vel_amount(127); polivoks_set_contour_amount(0);
            polivoks_midi(0x90, 45, (uint8_t)vv[v]); render(b, 22050);
            pk = peak(a, 22050);
            for (i = 0; i < 2u * 22050u; i++) {
                int32_t want = (int32_t)(((int64_t)a[i] * vs) >> 15), e = b[i] - want;
                if (e < 0) e = -e;
                if (e > worst) worst = e;
            }
            snprintf(d, sizeof d, "bass support %s, vel %d: vs %d, full-note peak %d (under the 30000 clip), worst |soft - full*vs| = %d LSB",
                     bs ? "ON" : "OFF", vv[v], vs, pk, worst);
            expect(bs ? "velamp_after_bass_support_on" : "velamp_after_bass_support_off", pk < 30000 && worst <= 2, d);
        }
    }
    /* ---- VELOC OFF / velocity 127 is the old voice ---- */
    {
        polivoks_init(); polivoks_set_vel_amount(0); polivoks_midi(0x90, 57, 20); render(a, 22050);
        polivoks_init(); polivoks_set_vel_amount(0); polivoks_midi(0x90, 57, 127); render(b, 22050);
        expect("veloc_off_soft_equals_full", !memcmp(a, b, sizeof(int32_t) * 2u * 22050), "VELOC 0: velocity 20 == velocity 127");
        polivoks_init(); polivoks_set_vel_amount(127); polivoks_midi(0x90, 57, 127); render(c, 22050);
        expect("veloc_full_vel127_equals_off", !memcmp(b, c, sizeof(int32_t) * 2u * 22050), "VELOC 127 at velocity 127 == VELOC 0");
    }
    /* ---- bend reset rule (same on both boards) ---- */
    {
        polivoks_init(); polivoks_midi(0xE0, 0, 0x60); polivoks_midi(0x80, 57, 0);
        snprintf(d, sizeof d, "after note-off bend = %d", (int)polivoks_pitch_bend());
        expect("bend_kept_on_note_off", polivoks_pitch_bend() != 0, d);
        polivoks_midi(0xB0, 123, 0);
        expect("bend_centre_on_cc123", polivoks_pitch_bend() == 0, "CC 123");
        polivoks_midi(0xE0, 0, 0x20); polivoks_midi(0xB0, 120, 0);
        expect("bend_centre_on_cc120", polivoks_pitch_bend() == 0, "CC 120");
        polivoks_midi(0xE0, 0x7F, 0x7F);
        snprintf(d, sizeof d, "full up 0x3FFF -> %d units (+2 st = 8192 max)", (int)polivoks_pitch_bend());
        expect("bend_full_up_clamped", polivoks_pitch_bend() == 8191, d);
        polivoks_panic();
        expect("bend_centre_on_panic", polivoks_pitch_bend() == 0, "polivoks_panic()");
    }
    printf("HOST-MERGE %s %d pass %d fail\n", fail ? "FAIL" : "PASS", pass, fail);
    return fail ? 1 : 0;
}
