/* SPDX-License-Identifier: GPL-3.0-only */
/* Eleven-window navigation. Two generators. The modulator is not a third VCO.
 * External level stays unmapped. No DSP change is asserted here beyond the
 * existing setters the knobs already call. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
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

static void render_add(int32_t *stereo, uint32_t n)
{
    memset(stereo, 0, sizeof(int32_t) * 2u * n);
    polivoks_render(stereo, n);
}

static int slot(int index)
{
    poli_nav_view_t v;
    poli_nav_view(&v);
    return v.slot_kind[index];
}

int main(void)
{
    poli_nav_view_t v;
    int32_t cut0, pitch, pitch2, held;
    static int32_t a[128], b[128], ext[64];
    int i, order_ok, reserved_flat;
    uint8_t rep, glide;
    char line[160];
    static const int roll_slot0[12] = {
        SLOT_FOOT1, SLOT_FOOT2, SLOT_LFO_RATE, SLOT_MOD_PITCH, SLOT_LV1,
        SLOT_CUT, SLOT_MOD_CUT, SLOT_ATK_F, SLOT_ATK_A, SLOT_MOD_AMP, SLOT_TUNE, SLOT_EMPTY
    };
    fail = 0;

    poli_nav_init();
    poli_nav_view(&v);
    expect("boot_gen1", v.section == NAV_SEC_OSC && v.flat == 0 && v.roll == 0
           && v.window == 0 && v.nwindows == 3 && v.lang == 0 && v.kb_oct == 0,
           "EDIT's first window, not the roll");
    expect("boot_gen1_knobs", slot(0) == SLOT_FOOT1 && slot(1) == SLOT_SHAPE1
           && slot(2) == SLOT_FM && slot(3) == SLOT_MOD_PITCH, "range wave FM depth");
    expect("bass_defaults_on", polivoks_bass_support() == 1 && polivoks_bass_gain() == 32768, "optional, ON");
    expect("mod_osc_starts_on", v.switch_kind != SW_MODOSC, "enable lives on the other window");

    poli_nav_knob(1, 2);
    expect("gen1_shape_linked", poli_shape == 2 && polivoks_link_shapes() == 1, "link stays on");
    poli_nav_knob(0, 1);
    expect("gen1_footage", polivoks_footage(0) == 1 && polivoks_footage(1) == 0, "only generator I");
    poli_nav_view(&v);
    held = v.slot_pos[2];
    poli_nav_knob(2, 1);
    poli_nav_view(&v);
    expect("gen1_fm_is_k3", v.slot_kind[2] == SLOT_FM && v.slot_pos[2] == held + 1, "FM from generator II");
    pitch = polivoks_mod_pitch_vco(0);
    poli_nav_knob(3, 4);
    expect("gen1_depth_is_k4", polivoks_mod_pitch_vco(0) != pitch, "modulator depth");
    pitch = polivoks_mod_pitch_vco(0);

    poli_nav_select_turn(1);
    poli_nav_view(&v);
    expect("edit_gen2", v.flat == 1 && v.window == 1 && slot(2) == SLOT_DETUNE
           && slot(3) == SLOT_MOD_PITCH2, "range wave detune depth");
    poli_nav_knob(0, -1);
    expect("gen2_footage_independent", polivoks_footage(1) == -1 && polivoks_footage(0) == 1, "two generators");
    poli_nav_knob(1, 1);
    expect("gen2_shape_unlinks", polivoks_link_shapes() == 0 && polivoks_shape2() == 3, "started from linked shape 2");
    pitch2 = polivoks_mod_pitch_vco(1);
    poli_nav_knob(3, 5);
    expect("gen2_depth_does_not_move_gen1", polivoks_mod_pitch_vco(0) == pitch
           && polivoks_mod_pitch_vco(1) != pitch2, "two modulation amounts");
    pitch2 = polivoks_mod_pitch_vco(1);
    poli_nav_select_press();
    expect("gen2_link_switch", polivoks_link_shapes() == 1, "existing shape link, not a new oscillator");

    poli_nav_select_turn(1);
    poli_nav_view(&v);
    expect("edit_mixer", v.flat == 4 && v.window == 2 && v.nwindows == 3
           && slot(0) == SLOT_LV1 && slot(1) == SLOT_LV2 && slot(2) == SLOT_NOISE
           && slot(3) == SLOT_EXT, "three levels, external still blocked");
    poli_nav_select_turn(1);
    poli_nav_view(&v);
    expect("edit_wrap", v.flat == 0 && v.window == 0, "EDIT wraps to generator I");
    poli_nav_select_turn(-1);
    poli_nav_view(&v);
    expect("edit_wrap_back", v.flat == 4, "counter-clockwise to the mixer");
    poli_nav_section(NAV_SEC_OSC);
    poli_nav_view(&v);
    expect("edit_again_opens_first", v.flat == 0 && v.roll == 0, "button returns to generator I");

    for (i = 0; i < 64; i++) ext[i] = 16000;
    poli_nav_init();
    polivoks_set_ext_buf(ext);
    polivoks_midi(0x90, 60, 90);
    render_add(a, 64);
    poli_nav_init();
    polivoks_set_ext_buf(ext);
    polivoks_midi(0x90, 60, 90);
    poli_nav_section(NAV_SEC_OSC);
    poli_nav_select_turn(2);
    poli_nav_knob(3, 40);
    poli_nav_view(&v);
    render_add(b, 64);
    expect("external_level_not_mapped", v.msg == MSG_EXT && memcmp(a, b, sizeof a) == 0, "K4 does not open the jack");

    poli_nav_init();
    poli_nav_section(NAV_SEC_LFO);
    poli_nav_view(&v);
    expect("lfo_opens_modulator", v.flat == 2 && v.nwindows == 2 && v.window == 0
           && poli_lfo_shape == 0, "rate and the four-position shape");
    poli_nav_knob(1, 1);
    expect("lfo_shape_on_k2", poli_lfo_shape == 1, "square stand-in");
    poli_nav_select_press();
    poli_nav_view(&v);
    expect("modulator_has_no_switch", v.msg == MSG_NO_SWITCH && poli_lfo_shape == 1, "no mute-all");
    poli_nav_select_turn(1);
    poli_nav_view(&v);
    expect("lfo_mod_to_osc", v.flat == 3 && slot(0) == SLOT_MOD_PITCH && slot(1) == SLOT_MOD_PITCH2
           && v.switch_kind == SW_MODOSC && v.switch_on == 1, "same depths, not a third VCO");
    poli_nav_section(NAV_SEC_OSC);
    poli_nav_knob(3, 4);
    pitch = polivoks_mod_pitch_vco(0);
    poli_nav_view(&v);
    held = v.slot_pos[3];
    poli_nav_section(NAV_SEC_LFO);
    poli_nav_select_turn(1);
    poli_nav_view(&v);
    expect("shared_depth_gen1", v.slot_kind[0] == SLOT_MOD_PITCH && v.slot_pos[0] == held
           && polivoks_mod_pitch_vco(0) == pitch, "GEN I K4 and this K1");
    poli_nav_knob(0, 2);
    held = polivoks_mod_pitch_vco(0);
    poli_nav_view(&v);
    expect("shared_depth_moves_both", held != pitch && v.slot_kind[0] == SLOT_MOD_PITCH, "one stored amount");
    poli_nav_section(NAV_SEC_OSC);
    poli_nav_view(&v);
    expect("shared_depth_visible_on_gen1", v.slot_kind[3] == SLOT_MOD_PITCH && v.slot_pos[3] > 0, "same slot");
    poli_nav_section(NAV_SEC_LFO);
    poli_nav_select_turn(1);
    poli_nav_knob(1, 8); /* tuneAB: MOD II boots at 0 now; give it a stored amount (old boot position 8) */
    poli_nav_select_press();
    expect("mod_osc_off_keeps_store", polivoks_mod_pitch_vco(0) == 0 && polivoks_mod_pitch_vco(1) == 0, "engine muted");
    poli_nav_view(&v);
    expect("mod_osc_off_keeps_position", v.switch_on == 0 && v.slot_pos[0] > 0 && v.slot_pos[1] > 0, "knobs retained");
    poli_nav_knob(1, 1);
    expect("mod_osc_knob_while_off", polivoks_mod_pitch_vco(1) == 0, "still muted");
    poli_nav_select_press();
    expect("mod_osc_on_restores", polivoks_mod_pitch_vco(0) == held && polivoks_mod_pitch_vco(1) > 0, "stored amounts return");
    poli_nav_select_turn(1);
    poli_nav_view(&v);
    expect("lfo_wrap", v.flat == 2 && v.window == 0, "back to the modulator");

    poli_nav_fx();
    poli_nav_view(&v);
    expect("fx_opens_filter", v.section == NAV_SEC_FX && v.flat == 5 && v.nwindows == 3
           && v.roll == 0 && slot(0) == SLOT_CUT && v.switch_kind == SW_LPBP, "first filter window");
    cut0 = poli_cut;
    poli_nav_knob(0, 1);
    snprintf(line, sizeof line, "cut %d -> %d", cut0, poli_cut);
    expect("k1_cutoff", poli_cut > cut0 && poli_cut <= 30000, line);
    expect("select_lp_bp", poli_bp == 0, "starts LP");
    poli_nav_select_press();
    expect("select_toggles_lpbp", poli_bp == 1, "BP");
    poli_nav_select_press();
    expect("select_toggles_back", poli_bp == 0, "LP");
    poli_nav_select_turn(1);
    poli_nav_view(&v);
    expect("fx_filter_mod", v.flat == 6 && slot(0) == SLOT_MOD_CUT && v.switch_kind == SW_BASS, "shared cutoff amount");
    expect("bass_still_on", polivoks_bass_support() == 1, "before the toggle");
    poli_nav_select_press();
    expect("bass_toggle_off", polivoks_bass_support() == 0, "FILTER MOD select");
    poli_nav_select_press();
    expect("bass_toggle_on", polivoks_bass_support() == 1, "back");
    poli_nav_select_turn(1);
    poli_nav_view(&v);
    expect("fx_filter_env", v.flat == 7 && slot(0) == SLOT_ATK_F && v.switch_kind == SW_REP_F, "ADSR, then repeat");
    poli_nav_select_press();
    expect("filter_repeat_switch", poli_rep_f == 1 && poli_rep_a == 0, "filter env only");
    poli_nav_select_turn(1);
    poli_nav_view(&v);
    expect("fx_wrap", v.flat == 5 && v.window == 0, "back to cutoff");
    poli_nav_section(NAV_SEC_FX);
    poli_nav_view(&v);
    expect("fx_again_opens_first", v.flat == 5 && v.window == 0, "button, not the next window");

    poli_nav_section(NAV_SEC_AMP);
    poli_nav_view(&v);
    expect("env_opens_amp", v.section == NAV_SEC_AMP && v.flat == 8 && v.window == 0 && v.nwindows == 2
           && slot(0) == SLOT_ATK_A && v.switch_kind == SW_REP_A, "first window");
    poli_nav_select_press();
    expect("amp_repeat_switch", poli_rep_a == 1, "amp env select");
    poli_nav_select_turn(1);
    poli_nav_view(&v);
    rep = poli_rep_a;
    glide = poli_glide;
    expect("amp_mod_window", v.flat == 9 && slot(0) == SLOT_MOD_AMP && v.switch_kind == SW_KEYHOLD, "volume depth, key hold");
    poli_nav_select_press();
    poli_nav_view(&v);
    expect("keyhold_switch", polivoks_keyhold() == 1 && v.switch_on == 1
           && poli_rep_a == rep && poli_glide == glide, "ВКЛ. toggles, other switches stay");
    poli_nav_select_press();
    expect("keyhold_switch_off", polivoks_keyhold() == 0, "second press");
    poli_nav_select_turn(1);
    poli_nav_view(&v);
    expect("env_wrap", v.flat == 8, "back to the amp envelope");

    poli_nav_section(NAV_SEC_GLOBAL);
    poli_nav_view(&v);
    expect("glo_opens_global", v.flat == 10 && v.nwindows == 2 && v.window == 0 && v.switch_kind == SW_GLIDE
           && slot(0) == SLOT_TUNE && slot(1) == SLOT_LEVEL && slot(2) == SLOT_GLIDE_RATE
           && slot(3) == SLOT_DUO, "tune volume glide voices; GLO has two windows");
    expect("duo_default", polivoks_duo_on() == 1, "baseline duo");
    poli_nav_knob(3, -1);
    expect("k4_mono", polivoks_duo_on() == 0, "1-voice");
    poli_nav_select_press();
    expect("glide_switch", poli_glide == 1, "existing enable, separate from the rate");
    poli_nav_select_turn(1);
    poli_nav_view(&v);
    expect("glo_midi_window", v.flat == 11 && v.nwindows == 2 && v.window == 1 && v.section == NAV_SEC_GLOBAL
           && v.midi_ch == 1 && v.switch_kind == SW_NONE, "SEL from GLOBAL opens MIDI channel");
    expect("midi_ch_default", poli_nav_midi_ch() == 1, "default receive channel 1");
    poli_nav_knob(0, 4);
    expect("midi_k1_up", poli_nav_midi_ch() == 5, "K1 steps 1..16");
    poli_nav_knob(0, 20);
    expect("midi_k1_clamp_hi", poli_nav_midi_ch() == 16, "stops at 16");
    poli_nav_knob(0, -20);
    expect("midi_k1_clamp_lo", poli_nav_midi_ch() == 1, "stops at 1");
    poli_nav_set_midi_ch(9);
    expect("midi_set_9", poli_nav_midi_ch() == 9, "direct set");
    poli_nav_set_midi_ch(0);
    expect("midi_set_invalid_0", poli_nav_midi_ch() == 1, "0 becomes 1");
    poli_nav_set_midi_ch(17);
    expect("midi_set_invalid_17", poli_nav_midi_ch() == 1, "17 becomes 1");
    { uint32_t e0 = poli_nav_edits(); poli_nav_knob(0, 2); expect("midi_ch_not_preset_edit", poli_nav_edits() == e0, "channel is not a sound edit"); }
    expect("key_track_default", poli_nav_key_track() == 62 && polivoks_key_track() == 62 && v.key_track == 62,
           "Monosounds median 62%");
    { uint32_t e0 = poli_nav_edits();
      poli_nav_knob(1, -1);
      expect("track_k2_off", poli_nav_key_track() == 0 && polivoks_key_track() == 0, "K2 down is OFF");
      poli_nav_knob(1, 2);
      expect("track_k2_100", poli_nav_key_track() == 100 && polivoks_key_track() == 100, "K2 up to 100");
      poli_nav_knob(1, 8);
      expect("track_k2_clamp", poli_nav_key_track() == 100, "stops at 100");
      poli_nav_set_key_track(62);
      expect("track_set_62", poli_nav_key_track() == 62, "direct set");
      expect("track_not_preset_edit", poli_nav_edits() == e0, "track is not a sound edit"); }
    poli_nav_select_turn(1);
    poli_nav_view(&v);
    expect("glo_wrap_to_global", v.flat == 10 && v.nwindows == 2 && v.window == 0, "SEL wraps inside GLO");
    poli_nav_section(NAV_SEC_GLOBAL);
    poli_nav_view(&v);
    expect("glo_button_first_window", v.flat == 10 && v.window == 0, "GLO always opens GLOBAL, not MIDI");

    poli_nav_home();
    poli_nav_view(&v);
    expect("home_starts_gen1", v.roll == 1 && v.flat == 0 && v.window == 0 && v.nwindows == 12, "chain start");
    order_ok = 1;
    for (i = 0; i < 12; i++) {
        poli_nav_view(&v);
        if (v.flat != i || v.roll != 1 || v.nwindows != 12 || slot(0) != roll_slot0[i])
            order_ok = 0;
        poli_nav_select_turn(1);
    }
    poli_nav_view(&v);
    expect("home_order", order_ok, "gen, gen II, mod, mod-osc, mixer, filter, filter mod, filter env, amp, amp mod, global, midi");
    expect("home_wrap", v.roll == 1 && v.flat == 0 && v.window == 0, "past MIDI returns to generator I");
    poli_nav_select_turn(-1);
    poli_nav_view(&v);
    expect("home_wrap_back", v.flat == 11 && v.midi_ch >= 1, "before generator I is MIDI");
    poli_nav_section(NAV_SEC_FX);
    poli_nav_view(&v);
    expect("section_leaves_roll", v.roll == 0 && v.flat == 5 && v.nwindows == 3, "FX is the filter, not the roll");

    reserved_flat = v.flat;
    poli_nav_reserved(MSG_ARP);
    poli_nav_view(&v);
    expect("arp_reserved", v.msg == MSG_ARP && v.flat == reserved_flat && poli_lfo_shape == 1, "no voice window");
    poli_nav_reserved(MSG_SEQ);
    poli_nav_view(&v);
    expect("seq_reserved", v.msg == MSG_SEQ && v.flat == reserved_flat, "stays");
    poli_nav_reserved(MSG_PLAY);
    poli_nav_view(&v);
    expect("play_reserved", v.msg == MSG_PLAY && v.flat == reserved_flat, "stays");
    poli_nav_reserved(MSG_REC);
    poli_nav_view(&v);
    expect("rec_reserved", v.msg == MSG_REC && v.flat == reserved_flat, "stays");
    poli_nav_reserved(MSG_SAVE);
    poli_nav_view(&v);
    expect("save_reserved", v.msg == MSG_SAVE && v.flat == reserved_flat, "stays");
    poli_nav_reserved(MSG_PRESETS);
    poli_nav_view(&v);
    expect("presets_reserved", v.msg == MSG_PRESETS && v.flat == reserved_flat, "encoder, no window");

    poli_nav_octave(1);
    polivoks_panel(1u << 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    snprintf(line, sizeof line, "note %u oct %d", poli_note, polivoks_kb_oct());
    expect("octave_up_from_65", poli_note == 77 && polivoks_kb_oct() == 1, line);
    poli_nav_octave(0);
    polivoks_panel(1u << 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    expect("octave_reset_base_65", poli_note == 65 && polivoks_kb_oct() == 0, "both buttons reset");
    poli_nav_octave(-1);
    poli_nav_octave(-1);
    poli_nav_octave(-1);
    poli_nav_octave(-1);
    polivoks_panel(1u << 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    /* step B build (Hans, 2026-11-12): the keybed goes one octave lower, -3 */
    expect("octave_clamped", polivoks_kb_oct() == -3 && poli_note == 29, "minus three, not more (was minus two); lowest key = note 29");
    polivoks_panel(1u << 26, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    snprintf(line, sizeof line, "top key at -3: note %u", poli_note);
    expect("octave_minus3_top_key", poli_note == 55, line);
    {
        poli_nav_view_t ov; poli_nav_view(&ov);
        expect("octave_minus3_view", ov.kb_oct == -3 && ov.msg == MSG_OCTAVE, "view reports -3 with the OCTAVE message");
    }
    poli_nav_octave(0);
    for (i = 0; i < 6; i++) poli_nav_octave(1);
    polivoks_panel(1u << 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    snprintf(line, sizeof line, "note %u oct %d", poli_note, polivoks_kb_oct());
    expect("octave_top_clamped", polivoks_kb_oct() == 2 && poli_note == 89, line);
    poli_nav_octave(0);
    polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    expect("lang_starts_en", poli_nav_lang() == 0, "boot");
    poli_nav_lang_step(4);
    expect("lang_toggles_once", poli_nav_lang() == 1, "not one step per detent");
    poli_nav_lang_step(-2);
    expect("lang_toggles_back", poli_nav_lang() == 0, "either direction");
    poli_nav_view(&v);
    expect("lang_message", v.msg == MSG_LANG, "provisional ALGORITHM control");

    /* Encoder acceleration: wide 0..127 knobs only. Time not advancing, or
       the first clicks, stay one value a detent. */
    {
        int32_t c0, c1, c2, c3, c4;
        poli_nav_init();
        poli_nav_section(NAV_SEC_FX);   /* K1 = cutoff, 0..127 */
        c0 = poli_cut;
        poli_nav_tick_ms(0);
        poli_nav_knob(0, 1);
        poli_nav_knob(0, 1);
        poli_nav_knob(0, 1);
        c1 = poli_cut;
        expect("accel_off_without_time", c1 > c0 && (c1 - c0) < 800, "three slow-clock clicks stay 1 each");
        poli_nav_tick_ms(1000);
        poli_nav_knob(0, 1);
        c2 = poli_cut;
        poli_nav_tick_ms(1005);
        poli_nav_knob(0, 1);
        c3 = poli_cut;
        poli_nav_tick_ms(1010);
        poli_nav_knob(0, 1);
        c4 = poli_cut;
        expect("accel_first_two_are_fine", (c3 - c2) > 0 && (c3 - c2) < 800, "first two of a fast turn are x1");
        expect("accel_third_is_faster", (c4 - c3) > (c3 - c2) * 4, "third 5 ms detent is accelerated");
        poli_nav_tick_ms(1015);
        {
            int32_t before = poli_cut, after;
            poli_nav_knob(0, -1);
            after = poli_cut;
            expect("accel_reverse_is_fine", before - after > 0 && before - after < 800, "turn back is x1");
        }
        /* footage is 5 values (-2..2): never accelerates. From -2, three
         * clicks are -1, 0, +1. If the third had jumped, it would clamp at +2. */
        poli_nav_section(NAV_SEC_OSC);
        poli_nav_tick_ms(1800);
        poli_nav_knob(0, -8);   /* clamp to -2, no accel on this span */
        poli_nav_tick_ms(2000);
        poli_nav_knob(0, 1);
        poli_nav_tick_ms(2005);
        poli_nav_knob(0, 1);
        poli_nav_tick_ms(2010);
        poli_nav_knob(0, 1);
        expect("accel_skips_footage", polivoks_footage(0) == 1, "three fast clicks from -2 land on +1, not +2");
    }

    printf(fail ? "HOST-NAV FAIL %d\n" : "HOST-NAV PASS\n", fail);
    return fail ? 1 : 0;
}
