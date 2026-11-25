/* SPDX-License-Identifier: GPL-3.0-only */
/* Host check: pitch bend from MIDI and from the SMK wheel helper.
 * Min / centre / max are -200, 0, +200 cents ±1 cent [M] on both
 * generators. MIDI 8192 is exactly 0. Last-wins is change-only on the
 * wheel path. Resting bend is bit-identical to no bend. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"
#include "smk_wheel.h"

static int fail;
static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok) fail++;
    fflush(stdout);
}

static void freqs(double *f1, double *f2, uint32_t n)
{
    uint32_t p1, p2, q1, q2, i;
    double a = 0, b = 0;
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &p1, &p2, NULL, NULL);
    for (i = 0; i < n; i++) {
        int32_t s[2] = { 0, 0 };
        polivoks_render(s, 1);
        polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &q1, &q2, NULL, NULL);
        a += (double)(uint32_t)(q1 - p1);
        b += (double)(uint32_t)(q2 - p2);
        p1 = q1;
        p2 = q2;
    }
    *f1 = a / n * 44100.0 / 4294967296.0;
    *f2 = b / n * 44100.0 / 4294967296.0;
}

static double cents_of(double f, double r) { return 1200.0 * log2(f / r); }

static void midi_bend(uint16_t v)
{
    uint8_t lsb = (uint8_t)(v & 0x7Fu);
    uint8_t msb = (uint8_t)((v >> 7) & 0x7Fu);
    polivoks_midi(0xE0, lsb, msb);
}

static int cents_ok(double c, double want, char *line, size_t n, const char *tag)
{
    snprintf(line, n, "%s %.4f cents (want %.1f ±1)", tag, c, want);
    return fabs(c - want) <= 1.0;
}

int main(void)
{
    char line[200];
    double f1, f2, r1, r2, c1, c2;
    uint32_t n = 4096;
    int32_t b;
    smk_pw_t w;
    int32_t ph1a[8], i;
    uint32_t p1, p2, q1, q2;

    fail = 0;
    poli_nav_init();
    polivoks_midi(0x90, 60, 100);
    /* Drain a few samples so glide/env settle; boot has no LFO-pitch. */
    freqs(&r1, &r2, 64);
    freqs(&r1, &r2, n);
    snprintf(line, sizeof line, "ref I=%.4f II=%.4f Hz", r1, r2);
    expect("unison_at_centre_boot", fabs(cents_of(r2, r1)) < 0.05, line);

    /* MIDI 8192 is exactly 0: same increment as no-bend. */
    midi_bend(8192);
    expect("midi_8192_units", polivoks_pitch_bend() == 0, "");
    freqs(&f1, &f2, n);
    c1 = cents_of(f1, r1);
    c2 = cents_of(f2, r2);
    snprintf(line, sizeof line, "I=%.6f II=%.6f cents", c1, c2);
    expect("midi_8192_exactly_0", c1 == 0.0 && c2 == 0.0, line);

    midi_bend(0);
    expect("midi_0_units", polivoks_pitch_bend() == -8192, "");
    freqs(&f1, &f2, n);
    expect("midi_min_gen1", cents_ok(cents_of(f1, r1), -200.0, line, sizeof line, "I"), line);
    expect("midi_min_gen2", cents_ok(cents_of(f2, r2), -200.0, line, sizeof line, "II"), line);

    midi_bend(16383);
    expect("midi_16383_units", polivoks_pitch_bend() == 8191, "");
    freqs(&f1, &f2, n);
    expect("midi_max_gen1", cents_ok(cents_of(f1, r1), 200.0, line, sizeof line, "I"), line);
    expect("midi_max_gen2", cents_ok(cents_of(f2, r2), 200.0, line, sizeof line, "II"), line);

    /* Exact ±8192 via the setter (wheel end-stops report these). */
    polivoks_set_pitch_bend(-8192);
    freqs(&f1, &f2, n);
    expect("set_min_gen1", cents_ok(cents_of(f1, r1), -200.0, line, sizeof line, "I"), line);
    expect("set_min_gen2", cents_ok(cents_of(f2, r2), -200.0, line, sizeof line, "II"), line);
    polivoks_set_pitch_bend(8192);
    freqs(&f1, &f2, n);
    expect("set_max_gen1", cents_ok(cents_of(f1, r1), 200.0, line, sizeof line, "I"), line);
    expect("set_max_gen2", cents_ok(cents_of(f2, r2), 200.0, line, sizeof line, "II"), line);
    polivoks_set_pitch_bend(0);
    freqs(&f1, &f2, n);
    expect("set_centre_exactly_0",
           cents_of(f1, r1) == 0.0 && cents_of(f2, r2) == 0.0, "");

    /* Wheel helper: board-layer note, one Pro, rest ~535, travel ~445..640. */
    memset(&w, 0, sizeof w);
    b = smk_pw_bend(&w, 535, 0);
    expect("wheel_rest_0", b == 0, "");
    b = smk_pw_bend(&w, 524, 10);       /* still inside dead 12 */
    expect("wheel_deadzone_lo", b == 0, "");
    b = smk_pw_bend(&w, 546, 20);
    expect("wheel_deadzone_hi", b == 0, "");
    b = smk_pw_bend(&w, 445, 100);
    snprintf(line, sizeof line, "lo units %d", (int)b);
    expect("wheel_min_units", b == -8192, line);
    polivoks_set_pitch_bend(b);
    freqs(&f1, &f2, n);
    expect("wheel_min_gen1", cents_ok(cents_of(f1, r1), -200.0, line, sizeof line, "I"), line);
    expect("wheel_min_gen2", cents_ok(cents_of(f2, r2), -200.0, line, sizeof line, "II"), line);
    b = smk_pw_bend(&w, 640, 200);
    snprintf(line, sizeof line, "hi units %d", (int)b);
    expect("wheel_max_units", b == 8192, line);
    polivoks_set_pitch_bend(b);
    freqs(&f1, &f2, n);
    expect("wheel_max_gen1", cents_ok(cents_of(f1, r1), 200.0, line, sizeof line, "I"), line);
    expect("wheel_max_gen2", cents_ok(cents_of(f2, r2), 200.0, line, sizeof line, "II"), line);
    b = smk_pw_bend(&w, 535, 300);
    expect("wheel_back_to_rest_0", b == 0, "");

    /* Last-wins: a resting wheel (no change) must not wipe a MIDI bend.
     * A later wheel change, including a return to 0, does take over. */
    midi_bend(16383);
    expect("last_midi_holds", polivoks_pitch_bend() == 8191, "");
    /* Simulated board path: only write when wheel bend changes. */
    {
        int32_t sent = 0;              /* last wheel write, still 0 at rest */
        int32_t wb = smk_pw_bend(&w, 535, 400);
        if (wb != sent) {
            sent = wb;
            polivoks_set_pitch_bend(wb);
        }
        expect("resting_wheel_does_not_clobber_midi",
               polivoks_pitch_bend() == 8191 && wb == 0, "");
        wb = smk_pw_bend(&w, 445, 500);
        if (wb != sent) {
            sent = wb;
            polivoks_set_pitch_bend(wb);
        }
        expect("wheel_move_wins", polivoks_pitch_bend() == -8192, "");
        midi_bend(8192);
        expect("later_midi_wins", polivoks_pitch_bend() == 0, "");
    }

    /* Bit-identical at centre: same phase steps as a fresh init at C4. */
    polivoks_set_pitch_bend(0);
    poli_nav_init();
    polivoks_midi(0x90, 60, 100);
    freqs(&r1, &r2, 64);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &p1, &p2, NULL, NULL);
    {
        int32_t s[512];
        memset(s, 0, sizeof s);
        polivoks_render(s, 256);
        polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &q1, &q2, NULL, NULL);
    }
    /* Second pass after an explicit 0, same start: re-init and compare steps. */
    {
        uint32_t d1, d2, e1, e2;
        d1 = q1 - p1;
        d2 = q2 - p2;
        poli_nav_init();
        polivoks_midi(0x90, 60, 100);
        freqs(&r1, &r2, 64);
        polivoks_set_pitch_bend(0);
        polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &p1, &p2, NULL, NULL);
        {
            int32_t s[512];
            memset(s, 0, sizeof s);
            polivoks_render(s, 256);
            polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &q1, &q2, NULL, NULL);
        }
        e1 = q1 - p1;
        e2 = q2 - p2;
        snprintf(line, sizeof line, "d1=%u e1=%u d2=%u e2=%u", d1, e1, d2, e2);
        expect("zero_bend_bit_identical", d1 == e1 && d2 == e2, line);
    }

    /* Octave still applies; bend is on top. Footage +1 is +12 semitones. */
    poli_nav_init();
    polivoks_midi(0x90, 60, 100);
    freqs(&r1, &r2, 64);
    freqs(&r1, &r2, n);
    polivoks_set_footage(0, 1);
    polivoks_set_footage(1, 1);
    polivoks_set_pitch_bend(0);
    freqs(&f1, &f2, n);
    expect("octave_plus_1", cents_ok(cents_of(f1, r1), 1200.0, line, sizeof line, "I"), line);
    polivoks_set_pitch_bend(8192);
    freqs(&f1, &f2, n);
    expect("octave_plus_1_and_bend",
           cents_ok(cents_of(f1, r1), 1400.0, line, sizeof line, "I"), line);

    /* Channel filter: wrong-channel MIDI bend is ignored. */
    poli_nav_init();
    polivoks_midi(0x90, 60, 100);
    poli_nav_set_midi_ch(1);
    poli_nav_midi(0xE1, 0, 0);          /* channel 2 */
    expect("wrong_channel_ignored", polivoks_pitch_bend() == 0, "");
    poli_nav_midi(0xE0, 0, 0);          /* channel 1, min */
    expect("right_channel_applied", polivoks_pitch_bend() == -8192, "");

    /* Duo: both generators still bend together on different notes. */
    poli_nav_init();
    polivoks_set_duo(1);
    poli_note = 60;
    poli_note2 = 64;
    poli_midi_on = 1;
    /* force gate without going through one-note MIDI */
    polivoks_set_pitch_bend(0);
    {
        /* Use panel-like state: set notes directly and gate. */
        extern volatile uint8_t poli_gate;
        poli_gate = 1;
    }
    freqs(&r1, &r2, 64);
    freqs(&r1, &r2, n);
    polivoks_set_pitch_bend(8192);
    freqs(&f1, &f2, n);
    expect("duo_bend_gen1", cents_ok(cents_of(f1, r1), 200.0, line, sizeof line, "I"), line);
    expect("duo_bend_gen2", cents_ok(cents_of(f2, r2), 200.0, line, sizeof line, "II"), line);

    (void)ph1a;
    (void)i;
    printf("%s  %d failed\n", fail ? "FAIL" : "PASS", fail);
    return fail ? 1 : 0;
}
