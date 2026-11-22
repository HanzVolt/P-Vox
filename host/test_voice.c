/* SPDX-License-Identifier: GPL-3.0-only */
/* Host regressions for the Polivoks voice, panel, and MIDI gate.
 * Not a unit measurement. Repeating contours are off unless a test latches them.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "wav_write.h"

static int fail;
static char wavdir[512];

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

int main(int argc, char **argv)
{
    enum { N = 24000 };
    static int32_t a[N * 2];
    static int32_t b[N * 2];
    static int32_t left[N];
    char line[200];
    char path[640];
    uint32_t i, ph1, ph2, ph1b, ph2b;
    int32_t a2, a3, bass, cut, res, drv, ega, egf;
    int32_t ega2;
    fail = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: %s wavdir\n", argv[0]);
        return 2;
    }
    snprintf(wavdir, sizeof wavdir, "%s", argv[1]);

    /* Globals the parent volt.c branch writes still exist. */
    poli_note = 60;
    poli_note2 = 255;
    poli_gate = 0;
    poli_key_on = 0;
    poli_midi_on = 0;
    poli_shape = 0;
    poli_bp = 0;
    poli_rep_f = 0;
    poli_rep_a = 0;
    poli_glide = 0;
    poli_lfo_shape = 0;
    poli_cut = 12000;
    poli_res = 6000;
    poli_drv = 14000;

    polivoks_init();
    render_dirty(a, 2048, 12345);
    expect("no_key_exact_zero", all_equal(a, 4096, 12345), "init, no gate, dirty buffer unchanged");

    polivoks_init();
    polivoks_midi(0x90, 60, 100);
    render_add(a, 8000);
    expect("note_on_sounds", peak_abs(a, 16000) > 2000, "midi note 60");
    for (i = 0; i < 8000; i++) left[i] = a[2u * i];
    polivoks_midi(0x80, 60, 0);
    render_add(b, N);
    {
        int early = 0, tail0 = 1;
        uint32_t last_nz = 0;
        polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega, NULL);
        for (i = 0; i < 400; i++) if (b[i] != 0) early = 1;
        for (i = 0; i < (uint32_t)N * 2u; i++) if (b[i] != 0) last_nz = i;
        for (i = (uint32_t)N * 2u - 8000u; i < (uint32_t)N * 2u; i++) if (b[i] != 0) tail0 = 0;
        snprintf(line, sizeof line, "early %d last_nz %u ega %d tail0 %d", early, last_nz, ega, tail0);
        expect("release_exact_zero", early && tail0 && ega == 0 && last_nz < 8000u, line);
    }
    snprintf(path, sizeof path, "%s/voice_note_release.wav", wavdir);
    {
        /* mono: attack excerpt then the release buffer's left channel, truncated */
        static int32_t mix[16000];
        for (i = 0; i < 8000; i++) mix[i] = left[i];
        for (i = 0; i < 8000; i++) mix[8000u + i] = b[2u * i];
        if (poli_wav_write(path, mix, NULL, 16000) != 0) {
            expect("voice_wav", 0, path);
        } else {
            printf("WAV %s\n", path);
        }
    }

    polivoks_init();
    polivoks_panel(1u << 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    expect("key_sets_gate", poli_key_on == 1 && poli_gate == 1 && poli_note == 65 && poli_note2 == 255,
           "bit0 -> note 65");
    polivoks_midi(0x90, 72, 90);
    polivoks_midi(0x80, 72, 40);
    expect("key_holds_through_midi_off", poli_gate == 1 && poli_key_on == 1 && poli_midi_on == 0,
           "panel key still down");
    render_add(a, 4000);
    expect("key_still_sounds", peak_abs(a, 8000) > 1000, "after midi off");
    polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    render_add(b, N);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega, NULL);
    expect("key_release_exact_zero",
           ega == 0 && all_equal(b + (N * 2 - 4000), 4000, 0) && peak_abs(b, 800) > 0,
           "panel key up, tail then 0");

    polivoks_init();
    polivoks_midi(0x90, 67, 100);
    render_add(a, 3000);
    expect("pre_suspend_sounds", peak_abs(a, 6000) > 1000, "before suspend");
    poli_suspend();
    polivoks_read(&a2, &a3, &bass, NULL, NULL, NULL, NULL, NULL, &ega, &egf);
    snprintf(line, sizeof line, "a2 %d a3 %d bass %d ega %d egf %d gate %u",
             a2, a3, bass, ega, egf, poli_gate);
    expect("suspend_clears_tail",
           a2 == 0 && a3 == 0 && bass == 0 && ega == 0 && egf == 0
           && poli_gate == 0 && poli_key_on == 0 && poli_midi_on == 0, line);
    render_dirty(b, 512, 12345);
    expect("program_switch_exact_zero", all_equal(b, 1024, 12345), "render after suspend adds 0");
    render_dirty(b, 4096, 12345);
    expect("return_no_key_exact_zero", all_equal(b, 8192, 12345), "second render, still no key");

    polivoks_init();
    polivoks_midi(0xC0, 1, 0);
    expect("program_byte_does_not_steal_select", poli_gate == 0 && poli_note == 60,
           "0xC0 ignored; parent calls poli_suspend");
    polivoks_midi(0x90, 60, 0);
    expect("vel0_is_note_off", poli_midi_on == 0 && poli_gate == 0, "note-on velocity 0");

    polivoks_init();
    polivoks_panel((1u << 0) | (1u << 12), 0, 0, 0, 0, 0, 0, 0, 0, 0);
    expect("duophonic_low_high", poli_note == 65 && poli_note2 == 77 && poli_gate == 1,
           "bits 0 and 12");
    render_add(a, 4096);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &ph1, &ph2, NULL, NULL);
    polivoks_init();
    polivoks_panel(1u << 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    render_add(b, 4096);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &ph1b, &ph2b, NULL, NULL);
    snprintf(line, sizeof line, "duo ph %u %u single ph %u %u", ph1, ph2, ph1b, ph2b);
    expect("two_oscillators_advance",
           /* tuneAB rebaseline: a single key is now unison, so ph1b == ph2b
              (was ph1b != ph2b, which encoded the +1 semitone VCO2).
              Gen I is the low key (65) in both runs and GEN II->I depth
              boots at 0, so ph1 == ph1b (was ph1 != ph1b, true only
              because the old 7000 cross-mod carried VCO2's difference). */
           ph1 > 0 && ph2 > 0 && ph1 != ph2 && ph1b > 0 && ph2b > 0 && ph1b == ph2b
           && ph2 != ph2b && ph1 == ph1b
           && peak_abs(a, 8192) > 500 && peak_abs(b, 8192) > 500
           && memcmp(a, b, sizeof(int32_t) * 8192) != 0, line);
    for (i = 0; i < 4096; i++) left[i] = b[2u * i];
    snprintf(path, sizeof path, "%s/voice_two_osc.wav", wavdir);
    if (poli_wav_write(path, left, NULL, 4096) != 0) expect("two_osc_wav", 0, path);
    else printf("WAV %s\n", path);

    polivoks_init();
    poli_cut = 12000;   /* the *800 panel path, not the nav 0..127 map */
    render_add(a, 1);
    polivoks_read(NULL, NULL, NULL, &cut, NULL, NULL, NULL, NULL, NULL, NULL);
    snprintf(line, sizeof line, "smoothed cut %d panel %d", cut, poli_cut);
    expect("voice_cut_does_not_jump", cut > 0 && cut < 2000 && poli_cut == 12000, line);
    polivoks_panel(0, 0, 0, 0, 0, 0, 0, 10, 0, 0);
    expect("cut_detent_scales", poli_cut == 20000, "10*800");
    render_add(a, 1);
    polivoks_read(NULL, NULL, NULL, &res, NULL, NULL, NULL, NULL, NULL, NULL);
    /* res holds the new smoothed cut; the previous read's cut is the baseline. */
    {
        int32_t cut2 = res; /* placeholder overwritten below */
        polivoks_read(NULL, NULL, NULL, &cut2, NULL, NULL, NULL, NULL, NULL, NULL);
        snprintf(line, sizeof line, "cut %d -> %d panel %d", cut, cut2, poli_cut);
        expect("voice_cut_step_is_smoothed", cut2 > cut && (cut2 - cut) < 3000 && poli_cut == 20000, line);
    }

    polivoks_init();
    polivoks_panel(0, 1, 0, 0, 0, 0, 0, 0, 0, 0);
    expect("shape_step_up", poli_shape == 1, "tri to saw");
    polivoks_panel(0, -3, 0, 0, 0, 0, 0, 0, 0, 0);
    expect("shape_step_sign_only", poli_shape == 0, "negative encoder, one step");
    polivoks_panel(0, -1, 0, 0, 0, 0, 0, 0, 0, 0);
    expect("shape_wrap", poli_shape == 4, "wrap to pulse");
    polivoks_panel(0, 0, 1, 1, 1, 1, 1, 0, 0, 0);
    expect("panel_toggles", poli_bp == 1 && poli_rep_f == 1 && poli_rep_a == 1 && poli_glide == 1
           && poli_lfo_shape == 1, "bp rep glide lfo");
    polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 100, -100);
    expect("res_drv_clamps", poli_res == 30000 && poli_drv == 2000, "res ceiling, drv floor");

    /* Repeat latched with no key does not free-run. A held key cycles.
       Key-up releases. init() clears the latch. */
    polivoks_init();
    polivoks_panel(0, 0, 0, 0, 1, 0, 0, 0, 0, 0);
    render_dirty(a, 2048, 12345);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega2, NULL);
    expect("amp_repeat_silent_without_key",
           all_equal(a, 4096, 12345) && ega2 == 0 && poli_rep_a == 1,
           "latch set, no gate");
    polivoks_panel(1u << 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    {
        int32_t lo = 28000, hi = 0;
        for (i = 0; i < 700; i++) {
            render_add(a, 1);
            polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega2, NULL);
            if (ega2 < lo) lo = ega2;
            if (ega2 > hi) hi = ega2;
        }
        snprintf(line, sizeof line, "ega %d..%d gate %u", lo, hi, poli_gate);
        expect("amp_repeat_cycles_while_held", hi > 20000 && lo < 4000 && poli_gate == 1, line);
    }
    polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    render_add(a, 8000);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega2, NULL);
    expect("amp_repeat_releases_on_key_up", ega2 == 0 && poli_rep_a == 1 && poli_gate == 0, "latch remains");
    polivoks_init();
    render_dirty(a, 512, 12345);
    expect("init_clears_repeat_silence", all_equal(a, 1024, 12345) && poli_rep_a == 0, "back to exact 0");

    {
        int bounded = 1;
        int worst = 0;
        polivoks_init();
        poli_res = 28000;
        poli_cut = 20000;
        polivoks_midi(0x90, 36, 127);
        polivoks_panel(0, 0, 0, 0, 0, 1, 0, 0, 0, 0);
        for (i = 0; i < 40; i++) {
            polivoks_panel((i & 1u) ? (1u << (i % 27u)) : 0u,
                           (i == 3) ? 1 : 0, (i == 5), 0, 0, 0, (i == 9),
                           (i % 5) - 2, (i % 7) - 3, (i % 4) - 1);
            render_add(a, 128);
            if (peak_abs(a, 256) > worst) worst = peak_abs(a, 256);
            if (peak_abs(a, 256) >= 32767) bounded = 0;
            polivoks_read(&a2, &a3, &bass, &cut, &res, &drv, NULL, NULL, NULL, NULL);
            if (a2 > 15000 || a2 < -15000 || a3 > 15000 || a3 < -15000) bounded = 0;
            if (bass > 32767 || bass < -32767) bounded = 0;
            if (cut < 0 || res < 0 || drv < 0) bounded = 0;
        }
        poli_suspend();
        render_dirty(a, 256, 12345);
        snprintf(line, sizeof line, "worst %d bounded %d post-suspend", worst, bounded);
        expect("voice_extreme_panel_bounded", bounded && worst < 32767 && all_equal(a, 512, 12345), line);
    }

    /* Manual specs: common tune at least ±12%, generator II at least ±25%.
       Defaults above leave the previous voice unchanged. */
    polivoks_init();
    polivoks_set_mod_pitch(0);
    polivoks_set_fm_depth(0);
    polivoks_midi(0x90, 60, 100);
    render_add(a, 2048);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ph2, NULL, NULL);
    polivoks_init();
    polivoks_set_mod_pitch(0);
    polivoks_set_fm_depth(0);
    polivoks_set_footage(1, -1);
    polivoks_midi(0x90, 60, 100);
    render_add(b, 2048);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ph2b, NULL, NULL);
    snprintf(line, sizeof line, "8' ph2 %u  16' ph2 %u", ph2, ph2b);
    expect("footage_halves_vco2", ph2 > 1000 && ph2b > 1000 && ph2b * 2 > ph2 - ph2 / 8 && ph2b * 2 < ph2 + ph2 / 8, line);
    {
        uint32_t ph_unity = ph2;
        polivoks_init();
        polivoks_set_mod_pitch(0);
        polivoks_set_fm_depth(0);
        polivoks_set_detune(32767);
        polivoks_midi(0x90, 60, 100);
        render_add(a, 2048);
        polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ph2, NULL, NULL);
        snprintf(line, sizeof line, "detune full %u unity %u", ph2, ph_unity);
        {
            uint32_t rose = ph2 - ph_unity; /* unsigned: the accumulator wraps */
            expect("detune_ii_raises_pitch", rose > ph_unity / 8u && rose < ph_unity / 2u, line);
        }
    }

    polivoks_init();
    polivoks_set_mod_pitch(0);
    polivoks_set_fm_depth(7000); /* tuneAB: boot depth is now 0, so set the old 7000 explicitly */
    polivoks_midi(0x90, 60, 100);
    render_add(a, 1024);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &ph1, NULL, NULL, NULL);
    polivoks_init();
    polivoks_set_mod_pitch(0);
    polivoks_set_fm_depth(0);
    polivoks_midi(0x90, 60, 100);
    render_add(b, 1024);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &ph1b, NULL, NULL, NULL);
    expect("fm_depth_moves_vco1", ph1 != ph1b && ph1 > 0 && ph1b > 0, "depth 7000 vs 0");

    polivoks_init();
    polivoks_set_level(1, 0);
    polivoks_midi(0x90, 60, 100);
    render_add(a, 2048);
    polivoks_init();
    polivoks_midi(0x90, 60, 100);
    render_add(b, 2048);
    expect("vco2_level_changes_mix", peak_abs(a, 4096) > 200 && memcmp(a, b, 4096 * sizeof(int32_t)) != 0,
           "level 0 vs unity");

    polivoks_init();
    polivoks_set_level(2, 20000);
    polivoks_midi(0x90, 48, 100);
    render_add(a, 2048);
    polivoks_init();
    polivoks_midi(0x90, 48, 100);
    render_add(b, 2048);
    expect("noise_level_enters_mixer", memcmp(a, b, 4096 * sizeof(int32_t)) != 0, "noise 20000 vs 0");

    {
        static int32_t ext[64];
        uint32_t k;
        for (k = 0; k < 64; k++) ext[k] = 12000;
        polivoks_init();
        polivoks_set_ext_buf(ext);
        polivoks_midi(0x90, 60, 80);
        render_add(a, 64);
        polivoks_init();
        polivoks_set_ext_buf(ext);
        polivoks_set_level(3, 32768);
        polivoks_midi(0x90, 60, 80);
        render_add(b, 64);
        expect("external_silent_until_level", memcmp(a, b, 128 * sizeof(int32_t)) != 0, "jack unknown, level gates it");
    }

    polivoks_init();
    render_add(a, 64);
    ph1 = polivoks_lfo_phase();
    polivoks_init();
    polivoks_set_lfo_inc(486977u * 2u);
    render_add(b, 64);
    ph1b = polivoks_lfo_phase();
    expect("lfo_rate_is_phase_inc", ph1b == ph1 * 2u, "double increment");

    polivoks_init();
    polivoks_set_mod_amp(20000);
    render_dirty(a, 512, 12345);
    expect("amp_mod_keeps_silence", all_equal(a, 1024, 12345), "ega 0, tremolo does not open the VCA");

    polivoks_init();
    polivoks_set_adsr(1, 420, 50, 24000, 35);
    polivoks_midi(0x90, 60, 100);
    render_add(a, 20000);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega, NULL);
    snprintf(line, sizeof line, "ega %d", ega);
    expect("sustain_holds", ega > 23000 && ega <= 24000, line);
    polivoks_set_adsr(1, 420, 50, 24000, 0);
    polivoks_midi(0x80, 60, 0);
    render_add(a, 4000);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega2, NULL);
    snprintf(line, sizeof line, "ega %d -> %d", ega, ega2);
    {   /* curved release (V3): rel 0 acts as 1 -> c = POLI_EG_LIN_DR; 4000 one-pole steps toward -26 */
        double w = (ega + 26.0) * pow(1.0 - (double)POLI_EG_LIN_DR / 16777216.0, 4000) - 26.0;
        expect("release_zero_steps_down", ega2 < ega && fabs(ega2 - w) <= 0.01 * ega + 2.0, line);
    }
    render_add(a, 22000);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega2, NULL);
    expect("release_zero_reaches_silence", ega2 == 0 && poli_gate == 0, "floor of 1, not a hold");
    polivoks_panic();
    render_dirty(a, 256, 12345);
    expect("panic_cuts_held_release", all_equal(a, 512, 12345), "CC-style panic");

    polivoks_init();
    polivoks_midi(0x90, 64, 100);
    render_add(a, 500);
    polivoks_midi(0xB0, 123, 0);
    expect("cc123_releases_midi", poli_midi_on == 0 && poli_gate == 0, "all notes off");
    polivoks_panel(1u << 2, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    polivoks_midi(0xB0, 123, 0);
    expect("cc123_keeps_panel_key", poli_key_on == 1 && poli_gate == 1, "local key still down");
    polivoks_midi(0xB0, 120, 0);
    expect("cc120_panic", poli_gate == 0 && poli_key_on == 0, "all sound off");

    polivoks_init();
    polivoks_set_glide_rate(0);
    polivoks_panel(0, 0, 0, 0, 0, 1, 0, 0, 0, 0);
    polivoks_midi(0x90, 48, 100);
    render_add(a, 300);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &ph1, NULL, NULL, NULL);
    polivoks_midi(0x90, 72, 100);
    render_add(a, 300);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &ph1b, NULL, NULL, NULL);
    polivoks_init();
    polivoks_set_glide_rate(8000);
    polivoks_panel(0, 0, 0, 0, 0, 1, 0, 0, 0, 0);
    polivoks_midi(0x90, 48, 100);
    render_add(b, 300);
    polivoks_midi(0x90, 72, 100);
    render_add(b, 300);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, &ph2, NULL, NULL, NULL);
    expect("glide_rate_changes_vco1", ph1b != ph2, "rate 0 vs 8000");

    polivoks_init();
    polivoks_set_duo(0);
    polivoks_set_mod_pitch(0);
    polivoks_set_fm_depth(0);
    polivoks_panel((1u << 0) | (1u << 12), 0, 0, 0, 0, 0, 0, 0, 0, 0);
    render_add(a, 1024);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ph2, NULL, NULL);
    polivoks_init();
    polivoks_set_duo(1);
    polivoks_set_mod_pitch(0);
    polivoks_set_fm_depth(0);
    polivoks_panel((1u << 0) | (1u << 12), 0, 0, 0, 0, 0, 0, 0, 0, 0);
    render_add(b, 1024);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ph2b, NULL, NULL);
    expect("mono_ignores_second_key", ph2 != ph2b && poli_note2 == 77, "duo off uses the unison interval");

    polivoks_init();
    polivoks_set_output(0);
    polivoks_midi(0x90, 60, 127);
    render_dirty(a, 2000, 12345);
    expect("output_level_zero", all_equal(a, 4000, 12345), "unity default is not this path");

    polivoks_init();
    polivoks_panel(0, 0, 0, 0, 1, 0, 0, 0, 0, 0);
    polivoks_midi(0x90, 60, 100);
    render_add(a, 1000);
    polivoks_midi(0x80, 60, 0);
    render_add(b, 8000);
    polivoks_read(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ega, NULL);
    {
        int early = peak_abs(b, 800) > 0;
        int tail0 = 1;
        for (i = (uint32_t)N * 2u - 4000u; i < (uint32_t)N * 2u; i++) if (b[i] != 0) tail0 = 0;
        snprintf(line, sizeof line, "early %d tail0 %d ega %d latch %u", early, tail0, ega, poli_rep_a);
        expect("repeat_releases_on_note_off", early && tail0 && ega == 0 && poli_rep_a == 1, line);
    }
    render_dirty(a, 512, 12345);
    expect("repeat_stays_silent_after_release", all_equal(a, 1024, 12345) && poli_rep_a == 1, "latch still set, gate up");
    polivoks_panic();
    render_dirty(a, 256, 12345);
    expect("panic_clears_repeat_and_stays_silent",
           all_equal(a, 512, 12345) && poli_rep_a == 0 && poli_rep_f == 0 && poli_gate == 0,
           "CC-style panic");
    polivoks_panel(1u << 0, 0, 0, 0, 1, 0, 0, 0, 0, 0);
    render_dirty(a, 256, 12345);
    expect("panic_blocks_held_key", all_equal(a, 512, 12345) && poli_gate == 0 && poli_key_on == 0,
           "same key mask is not a new note");
    polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    polivoks_panel(1u << 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    render_add(a, 1500);
    expect("new_key_after_panic_sounds", peak_abs(a, 3000) > 500 && poli_gate == 1, "press after release");
    polivoks_init();
    render_dirty(a, 256, 12345);
    expect("init_repeat_off_is_silent", all_equal(a, 512, 12345) && poli_rep_a == 0, "latch cleared by init");

    polivoks_init();
    polivoks_set_footage(0, 2);
    polivoks_set_footage(1, -2);
    polivoks_set_tune(32767);
    polivoks_set_detune(-32768);
    polivoks_set_fm_depth(32767);
    polivoks_set_level(0, 32768);
    polivoks_set_level(1, 32768);
    polivoks_set_level(2, 32768);
    polivoks_set_mod_pitch(32767);
    polivoks_set_mod_cut(32767);
    polivoks_set_mod_amp(32767);
    polivoks_set_contour_amount(32768);
    polivoks_set_adsr(0, 8000, 8000, 28000, 8000);
    polivoks_set_adsr(1, 8000, 0, 28000, 1);
    polivoks_set_glide_rate(200000);
    polivoks_set_output(32768);
    polivoks_set_lfo_inc(4000000u);
    polivoks_midi(0x90, 10, 127);
    render_add(a, 4000);
    polivoks_read(&a2, &a3, &bass, &cut, &res, &drv, NULL, NULL, NULL, NULL);
    snprintf(line, sizeof line, "peak %d a2 %d a3 %d bass %d", peak_abs(a, 8000), a2, a3, bass);
    expect("extreme_panel_amounts_bounded",
           peak_abs(a, 8000) < 32767 && a2 <= 15000 && a2 >= -15000 && a3 <= 15000 && a3 >= -15000
           && bass <= 32767 && bass >= -32767, line);

    printf(fail ? "HOST-VOICE FAIL %d\n" : "HOST-VOICE PASS\n", fail);
    return fail ? 1 : 0;
}
