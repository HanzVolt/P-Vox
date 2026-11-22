/* SPDX-License-Identifier: GPL-3.0-only */
/* Release, panic, CC120, bass-support blend. Not a factory measurement. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"

static int fail;

static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok) fail++;
}

static int peak_abs(const int32_t *y, uint32_t n)
{
    uint32_t i;
    int m = 0;
    for (i = 0; i < n; i++) {
        int32_t a = y[i] < 0 ? -y[i] : y[i];
        if (a > m) m = a;
    }
    return m;
}

static int all_equal(const int32_t *y, uint32_t n, int32_t v)
{
    uint32_t i;
    for (i = 0; i < n; i++) if (y[i] != v) return 0;
    return 1;
}

static void render_add(int32_t *stereo, uint32_t n)
{
    memset(stereo, 0, sizeof(int32_t) * 2u * n);
    polivoks_render(stereo, n);
}

static void render_dirty(int32_t *stereo, uint32_t n, int32_t mark)
{
    uint32_t i;
    for (i = 0; i < n * 2u; i++) stereo[i] = mark;
    polivoks_render(stereo, n);
}

int main(void)
{
    /* Test-only. render_add(onb, 20000) writes 40000 int32 samples.
       The previous 16000-element arrays overflowed. Not a firmware change. */
    static int32_t onb[48000], offb[48000], low_a[4000], low_b[4000];
    int32_t ega, ega2, bass1, bass2, bass3;
    int worst;
    uint32_t i, ph1, ph2, ph1b, ph2b;
    char line[160];
    fail = 0;

    polivoks_init();
    expect("gain_starts_full", polivoks_bass_gain() == 32768 && polivoks_bass_support() == 1, "ON");
    polivoks_midi(0x90, 48, 100);
    render_add(onb, 100);
    expect("gain_holds_when_on", polivoks_bass_gain() == 32768, "no slew while enabled");

    polivoks_set_bass_support(0);
    render_add(onb, 1);
    snprintf(line, sizeof line, "gain %d", polivoks_bass_gain());
    expect("gain_steps_down", polivoks_bass_gain() == 31744, line);
    render_add(onb, 5000);
    expect("gain_reaches_off", polivoks_bass_gain() == 0 && polivoks_bass_support() == 0, "endpoint");
    polivoks_set_bass_support(1);
    render_add(onb, 1);
    snprintf(line, sizeof line, "gain %d", polivoks_bass_gain());
    expect("gain_steps_up", polivoks_bass_gain() == 1024, line);
    render_add(onb, 5000);
    expect("gain_reaches_on", polivoks_bass_gain() == 32768, "endpoint");

    /* Below the knee the core's sidechain gain is 0, so ON and OFF match. */
    polivoks_init();
    poli_res = 6000;
    polivoks_midi(0x90, 36, 100);
    render_add(low_a, 2000);
    polivoks_init();
    poli_res = 6000;
    polivoks_set_bass_support(0);
    polivoks_midi(0x90, 36, 100);
    render_add(low_b, 2000);
    expect("bass_off_matches_below_knee", memcmp(low_a, low_b, sizeof low_a) == 0, "add is 0");

    polivoks_init();
    poli_res = 28000;
    poli_cut = 16000;
    polivoks_midi(0x90, 36, 110);
    render_add(onb, 8000);
    polivoks_init();
    poli_res = 28000;
    poli_cut = 16000;
    polivoks_set_bass_support(0);
    polivoks_midi(0x90, 36, 110);
    render_add(offb, 8000);
    expect("bass_changes_audio_above_knee", memcmp(onb, offb, 16000 * sizeof(int32_t)) != 0
           && peak_abs(onb, 16000) > 100 && peak_abs(offb, 16000) > 100, "support is the sidechain, not a mute");

    polivoks_init();
    poli_res = 28000;
    polivoks_midi(0x90, 40, 100);
    render_add(onb, 3000);
    polivoks_read(NULL, NULL, &bass1, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    polivoks_set_bass_support(0);
    polivoks_read(NULL, NULL, &bass2, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    render_add(onb, 200);
    polivoks_read(NULL, NULL, &bass3, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    snprintf(line, sizeof line, "bass %d %d %d", bass1, bass2, bass3);
    expect("bass_follower_keeps_phase", bass1 == bass2 && bass3 != 0, line);

    worst = 0;
    polivoks_init();
    poli_res = 30000;
    poli_cut = 24000;
    poli_drv = 30000;
    polivoks_set_level(0, 32768);
    polivoks_set_level(1, 32768);
    polivoks_set_mod_amp(0);
    polivoks_midi(0x90, 36, 127);
    for (i = 0; i < 20; i++) {
        if (i == 4) polivoks_set_bass_support(0);
        if (i == 12) polivoks_set_bass_support(1);
        render_add(onb, 256);
        if (peak_abs(onb, 512) > worst) worst = peak_abs(onb, 512);
    }
    snprintf(line, sizeof line, "worst %d", worst);
    expect("bass_slew_headroom", worst < 32767, line);

    polivoks_init();
    polivoks_midi(0x90, 60, 100);
    render_add(onb, 2000);
    polivoks_midi(0x80, 60, 0);
    polivoks_set_bass_support(0);
    render_add(offb, 8000);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega, NULL);
    expect("release_reaches_zero_while_bass_moves",
           ega == 0 && all_equal(offb + 12000, 4000, 0) && peak_abs(offb, 400) > 0,
           "blend cannot hold the VCA");

    polivoks_init();
    polivoks_panel(1u << 4, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    render_add(onb, 20000);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega, NULL);
    polivoks_panel(1u << 4, 1, 1, 0, 0, 1, 1, 3, -2, 1);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega2, NULL);
    snprintf(line, sizeof line, "ega %d -> %d gate %u", ega, ega2, poli_gate);
    expect("secondary_controls_do_not_retrigger", ega2 == ega && ega > 10000 && poli_gate == 1, line);
    render_add(onb, 200);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega2, NULL);
    expect("sustain_holds_through_knobs", ega2 > 10000 && poli_gate == 1, "not a release");

    polivoks_init();
    polivoks_panel(0, 0, 0, 1, 1, 0, 0, 0, 0, 0);
    polivoks_panel(1u << 2, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    polivoks_midi(0x90, 64, 100);
    render_add(onb, 800);
    polivoks_midi(0xB0, 120, 0);
    expect("cc120_clears_latches", poli_rep_f == 0 && poli_rep_a == 0 && poli_gate == 0 && poli_key_on == 0,
           "all sound off");
    polivoks_panel(1u << 2, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    render_dirty(onb, 400, 12345);
    expect("cc120_held_key_stays_silent", all_equal(onb, 800, 12345) && poli_gate == 0, "not a new note");
    polivoks_midi(0x90, 67, 100);
    render_add(onb, 600);
    expect("cc120_new_midi_note_sounds", peak_abs(onb, 1200) > 400 && poli_midi_on == 1, "a new note-on");
    polivoks_midi(0x80, 67, 0);
    polivoks_panel(1u << 2, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    expect("cc120_key_still_blocked_after_midi", poli_gate == 0 && poli_key_on == 0 && poli_midi_on == 0,
           "held key is not a new note; the MIDI release may still be tailing");
    render_add(offb, 8000);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega, NULL);
    expect("cc120_block_releases_to_silence", ega == 0 && all_equal(offb + 12000, 4000, 0), "no retrigger");
    polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    polivoks_panel(1u << 2, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    render_add(onb, 500);
    expect("key_after_release_sounds", peak_abs(onb, 1000) > 400 && poli_key_on == 1, "new press");

    polivoks_init();
    polivoks_panel(0, 0, 0, 0, 1, 0, 0, 0, 0, 0);
    polivoks_midi(0x90, 70, 100);
    render_add(onb, 400);
    polivoks_midi(0xB0, 123, 0);
    expect("cc123_keeps_repeat_latch", poli_rep_a == 1 && poli_gate == 0, "not all-sound-off");
    render_add(offb, 8000);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega, NULL);
    expect("cc123_releases_repeat", ega == 0 && peak_abs(offb, 400) > 0, "key-off path");
    render_dirty(onb, 200, 12345);
    expect("cc123_does_not_restart", all_equal(onb, 400, 12345), "latch alone is silent");

    polivoks_init();
    polivoks_panel(0, 0, 0, 0, 1, 0, 0, 0, 0, 0);
    polivoks_midi(0x90, 62, 90);
    render_add(onb, 300);
    polivoks_midi(0xC0, 3, 0);
    expect("program_change_suspends", poli_gate == 0 && poli_rep_a == 1, "latch kept, tail cleared");
    render_dirty(onb, 300, 12345);
    expect("suspend_does_not_restart_repeat", all_equal(onb, 600, 12345), "gate is up");
    polivoks_midi(0x90, 62, 90);
    render_add(onb, 400);
    expect("note_after_suspend_sounds", peak_abs(onb, 800) > 200 && poli_rep_a == 1, "latch still armed");

    polivoks_init();
    polivoks_set_adsr(0, 400, 40, 12000, 30);
    polivoks_panel(0, 0, 0, 1, 0, 0, 0, 0, 0, 0);
    polivoks_midi(0x90, 55, 100);
    render_add(onb, 500);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega);
    polivoks_midi(0x80, 55, 0);
    render_add(offb, 8000);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega2);
    snprintf(line, sizeof line, "egf %d -> %d", ega, ega2);
    expect("filter_repeat_releases", ega > 0 && ega2 == 0 && poli_rep_f == 1, line);

    /* Release floor: stored 0 acts as 1. Curved envelope (V3): the old-unit
       rate maps to a Q24 coefficient c = rate * POLI_EG_LIN_DR (capped), and
       the first release sample moves (level + 26) * c / 2^24 (one-pole step
       toward -26). The first sample must not jump to 0 from a high level. */
    {
        static const int rels[] = { 0, 1, 35, 400, 8000 };
        int ri;
        for (ri = 0; ri < 5; ri++) {
            int rel = rels[ri];
            int32_t cq = (rel < 1 ? 1 : rel) * POLI_EG_LIN_DR;
            double drop_a, drop_f;
            int32_t before_a, after_a, before_f, after_f;
            int left;
            polivoks_init();
            polivoks_set_adsr(1, 8000, 8000, 20000, rel);
            polivoks_set_adsr(0, 8000, 8000, 18000, rel);
            polivoks_midi(0x90, 60, 100);
            render_add(onb, 4000);
            polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &before_a, &before_f);
            polivoks_midi(0x80, 60, 0);
            render_add(onb, 1);
            polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &after_a, &after_f);
            snprintf(line, sizeof line, "rel %d amp %d->%d filt %d->%d", rel, before_a, after_a, before_f, after_f);
            if (cq > POLI_EG_CMAX_DR) cq = POLI_EG_CMAX_DR;
            drop_a = (before_a + 26.0) * cq / 16777216.0;
            drop_f = (before_f + 26.0) * cq / 16777216.0;
            expect(rel == 0 ? "rel0_no_click_amp" : "rel_step_amp",
                   before_a > drop_a && fabs((before_a - after_a) - drop_a) <= 1.0 && before_a - after_a <= 8000 && poli_gate == 0, line);
            expect(rel == 0 ? "rel0_no_click_filt" : "rel_step_filt",
                   before_f > drop_f && fabs((before_f - after_f) - drop_f) <= 1.0 && before_f - after_f <= 8000, line);
            left = 30000;
            while (left > 0 && (after_a > 0 || after_f > 0)) {
                int n = left > 512 ? 512 : left;
                render_add(onb, n);
                left -= n;
                polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &after_a, &after_f);
            }
            snprintf(line, sizeof line, "rel %d amp %d filt %d left %d", rel, after_a, after_f, left);
            expect(rel == 0 ? "rel0_both_reach_zero" : "rel_both_reach_zero",
                   after_a == 0 && after_f == 0 && poli_gate == 0, line);
            render_dirty(onb, 200, 12345);
            expect(rel == 0 ? "rel0_stays_silent" : "rel_stays_silent",
                   all_equal(onb, 400, 12345), "no retrigger");
        }
    }

    polivoks_init();
    polivoks_set_adsr(1, 8000, 8000, 20000, 0);
    polivoks_panel(1u << 3, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    render_add(onb, 2000);
    polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    {
        int left = 30000;
        polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega, NULL);
        while (left > 0 && ega > 0) {
            render_add(onb, 512);
            left -= 512;
            polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega, NULL);
        }
        expect("physical_key_rel0_reaches_zero", ega == 0 && poli_gate == 0 && poli_key_on == 0, "panel key up");
    }

    polivoks_init();
    polivoks_set_adsr(1, 8000, 50, 16000, 0);
    polivoks_set_adsr(0, 8000, 50, 16000, 0);
    polivoks_panel(0, 0, 0, 1, 1, 0, 0, 0, 0, 0);
    polivoks_midi(0x90, 64, 110);
    render_add(onb, 1500);
    polivoks_midi(0x80, 64, 0);
    {
        int left = 40000;
        polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega, &ega2);
        while (left > 0 && (ega > 0 || ega2 > 0)) {
            render_add(onb, 512);
            left -= 512;
            polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega, &ega2);
        }
        snprintf(line, sizeof line, "amp %d filt %d latch %u %u", ega, ega2, poli_rep_a, poli_rep_f);
        expect("repeat_rel0_releases", ega == 0 && ega2 == 0 && poli_rep_a == 1 && poli_rep_f == 1, line);
    }
    render_dirty(onb, 300, 12345);
    expect("repeat_rel0_does_not_restart", all_equal(onb, 600, 12345), "latch without a key");

    polivoks_init();
    polivoks_set_adsr(1, 8000, 8000, 22000, 35);
    polivoks_midi(0x90, 67, 100);
    render_add(onb, 3000);
    polivoks_midi(0x80, 67, 0);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega, NULL);
    polivoks_panel(0, 1, 1, 0, 0, 1, 1, 4, -3, 2);
    polivoks_set_mod_pitch_vco(1, 30000);
    polivoks_set_mod_cut(20000);
    render_add(onb, 20);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega2, NULL);
    snprintf(line, sizeof line, "ega %d -> %d gate %u", ega, ega2, poli_gate);
    {   /* curved release (V3): 20 one-pole steps toward -26 with c = 35 * POLI_EG_LIN_DR */
        double w = (ega + 26.0) * pow(1.0 - 35.0 * POLI_EG_LIN_DR / 16777216.0, 20) - 26.0;
        expect("secondary_during_release_no_retrigger",
               poli_gate == 0 && ega2 < ega && fabs(ega2 - w) <= 0.01 * ega + 2.0, line);
    }

    polivoks_init();
    polivoks_set_adsr(1, 8000, 8000, 20000, 0);
    polivoks_set_keyhold(1);
    polivoks_midi(0x90, 72, 100);
    render_add(onb, 3000);
    polivoks_midi(0x80, 72, 0);
    render_add(onb, 2000);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega, NULL);
    expect("keyhold_keeps_gate", polivoks_keyhold() == 1 && poli_gate == 1 && ega > 18000, "deliberate hold");
    polivoks_midi(0xB0, 120, 0);
    render_dirty(onb, 400, 12345);
    expect("keyhold_panic_stays_silent",
           all_equal(onb, 800, 12345) && poli_gate == 0 && polivoks_keyhold() == 1, "switch stays, latch cleared");
    polivoks_midi(0x90, 72, 90);
    polivoks_midi(0x80, 72, 0);
    render_add(onb, 500);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega, NULL);
    expect("keyhold_rearms_on_new_note", poli_gate == 1 && ega > 1000, "new note, then hold");
    polivoks_set_keyhold(0);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega, NULL);
    render_add(onb, 10);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega2, NULL);
    expect("keyhold_off_starts_release", poli_gate == 0 && ega2 < ega && ega2 > 0, "not a cut to zero");

    polivoks_init();
    polivoks_set_fm_depth(0);
    polivoks_set_mod_pitch(0);
    polivoks_midi(0x90, 60, 100);
    render_add(onb, 1024);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &ph1, &ph2, NULL, NULL);
    polivoks_init();
    polivoks_set_fm_depth(0);
    polivoks_set_mod_pitch(0);
    polivoks_set_mod_pitch_vco(1, 32767);
    polivoks_midi(0x90, 60, 100);
    render_add(offb, 1024);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &ph1b, &ph2b, NULL, NULL);
    snprintf(line, sizeof line, "ph1 %u/%u ph2 %u/%u", ph1, ph1b, ph2, ph2b);
    expect("gen2_mod_is_separate", ph1 == ph1b && ph2 != ph2b, line);

    printf(fail ? "HOST-SAFETY FAIL %d\n" : "HOST-SAFETY PASS\n", fail);
    return fail ? 1 : 0;
}
