/* SPDX-License-Identifier: GPL-3.0-only */
/* Host check: generator tuning (tuneAB). One held note, MIDI or keybed,
 * gives exact unison; DETUNE ±1 detent is about ±6.75 cents; boot has no
 * LFO->pitch and no GEN II->I modulation. Frequencies from the phase
 * accumulators over one second, through the poli_nav boot and knob paths. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"

static int fail;
static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok) fail++;
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
        p1 = q1; p2 = q2;
    }
    *f1 = a / n * 44100.0 / 4294967296.0;
    *f2 = b / n * 44100.0 / 4294967296.0;
}

static double cents(double f, double r) { return 1200.0 * log2(f / r); }

static void goto_flat(int f)
{
    poli_nav_home();
    if (f) poli_nav_select_turn(f);
}

int main(void)
{
    static const int midi_notes[] = { 36, 48, 60, 72, 84 };
    static const int key_bits[] = { 0, 7, 12, 19, 26 };   /* keybed notes 65..91 */
    char line[160];
    double f1, f2, worst = 0, dmin = 1e9, dmax = -1e9;
    unsigned i;
    poli_nav_view_t v;

    poli_nav_init();
    expect("boot_no_lfo_pitch", polivoks_mod_pitch_vco(0) == 0 && polivoks_mod_pitch_vco(1) == 0, "MOD I/II depth 0");
    expect("boot_no_gen2_mod", poli_fm == 0, "GEN II->I depth 0");
    goto_flat(0); poli_nav_view(&v);
    snprintf(line, sizeof line, "GEN I K3 %d K4 %d", v.slot_pos[2], v.slot_pos[3]);
    expect("boot_gen1_knobs_show_0", v.slot_pos[2] == 0 && v.slot_pos[3] == 0, line);
    goto_flat(1); poli_nav_view(&v);
    snprintf(line, sizeof line, "GEN II K3 detune %d K4 %d", v.slot_pos[2], v.slot_pos[3]);
    expect("boot_gen2_knobs_show_0", v.slot_pos[2] == 0 && v.slot_pos[3] == 0, line);
    goto_flat(3); poli_nav_view(&v);
    expect("boot_mod_osc_knobs_show_0", v.slot_pos[0] == 0 && v.slot_pos[1] == 0, "MOD->OSC K1 K2");

    /* Zero vibrato at boot: Gen I's phase step is the same on every sample. */
    poli_nav_init();
    polivoks_midi(0x90, 60, 100);
    {
        uint32_t p, q, d0 = 0, k, steady = 1;
        int32_t s[2];
        polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &p, NULL, NULL, NULL);
        for (k = 0; k < 44100u; k++) {
            s[0] = s[1] = 0;
            polivoks_render(s, 1);
            polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &q, NULL, NULL, NULL);
            if (k == 0) d0 = q - p; else if (q - p != d0) steady = 0;
            p = q;
        }
        expect("boot_zero_vibrato", steady && d0 == NOTE_INC[60], "gen I step constant 1 s, equals NOTE_INC[60]");
    }

    for (i = 0; i < 5; i++) {
        int n = midi_notes[i];
        poli_nav_init();
        polivoks_midi(0x90, (uint8_t)n, 100);
        freqs(&f1, &f2, 44100u);
        if (fabs(cents(f2, f1)) > fabs(worst)) worst = cents(f2, f1);
        snprintf(line, sizeof line, "MIDI %d: I %.4f Hz II %.4f Hz, II-I %+.4f c, I vs ET %+.4f c", n, f1, f2,
                 cents(f2, f1), cents(f1, 440.0 * pow(2.0, (n - 69) / 12.0)));
        expect("midi_unison", fabs(cents(f2, f1)) < 0.01 && fabs(cents(f1, 440.0 * pow(2.0, (n - 69) / 12.0))) < 0.01, line);
        polivoks_midi(0x80, (uint8_t)n, 0);
    }
    for (i = 0; i < 5; i++) {
        int n = 65 + key_bits[i];
        poli_nav_init();
        polivoks_panel(1u << key_bits[i], 0, 0, 0, 0, 0, 0, 0, 0, 0);
        freqs(&f1, &f2, 44100u);
        if (fabs(cents(f2, f1)) > fabs(worst)) worst = cents(f2, f1);
        snprintf(line, sizeof line, "key %d: I %.4f Hz II %.4f Hz, II-I %+.4f c", n, f1, f2, cents(f2, f1));
        expect("keybed_unison", fabs(cents(f2, f1)) < 0.01 && fabs(cents(f1, 440.0 * pow(2.0, (n - 69) / 12.0))) < 0.01, line);
    }
    snprintf(line, sizeof line, "worst II-I %+.6f c over 10 notes", worst);
    expect("unison_summary", fabs(worst) < 0.01, line);

    /* Duo: two keys still split (low to I, high to II). */
    poli_nav_init();
    polivoks_panel((1u << 0) | (1u << 7), 0, 0, 0, 0, 0, 0, 0, 0, 0);
    freqs(&f1, &f2, 44100u);
    snprintf(line, sizeof line, "keys 65+72: I %+.4f c vs 65, II %+.4f c vs 72", cents(f1, 440.0 * pow(2.0, (65 - 69) / 12.0)),
             cents(f2, 440.0 * pow(2.0, (72 - 69) / 12.0)));
    expect("duo_two_keys_split", fabs(cents(f1, 440.0 * pow(2.0, (65 - 69) / 12.0))) < 0.01
           && fabs(cents(f2, 440.0 * pow(2.0, (72 - 69) / 12.0))) < 0.01, line);

    /* Detune ±1 detent at several notes. */
    for (i = 0; i < 5; i++) {
        int n = midi_notes[i], d;
        for (d = -1; d <= 1; d += 2) {
            double c;
            poli_nav_init();
            goto_flat(1);
            poli_nav_knob(2, d);
            polivoks_midi(0x90, (uint8_t)n, 100);
            freqs(&f1, &f2, 44100u);
            c = cents(f2, f1);
            if (d > 0 && c > dmax) dmax = c;
            if (d > 0 && c < dmin) dmin = c;
            snprintf(line, sizeof line, "MIDI %d detune %+d: II-I %+.3f c", n, d, c);
            expect("detune_one_detent", (d > 0 ? c > 6.5 && c < 7.0 : c < -6.5 && c > -7.0), line);
        }
    }
    (void)dmin; (void)dmax;

    printf(fail ? "HOST-TUNE FAIL %d\n" : "HOST-TUNE PASS\n", fail);
    return fail ? 1 : 0;
}
