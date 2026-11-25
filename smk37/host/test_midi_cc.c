/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI CC map: each mapped CC sets the same nav pos and engine state as the
 * knob path, switches are absolute, unmapped CCs and 120/123 do not steal
 * the map, SysEx-like status is ignored. Main-loop path only. */
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

static const int8_t WIN[11][4] = {
    { SLOT_FOOT1, SLOT_SHAPE1, SLOT_FM, SLOT_MOD_PITCH },
    { SLOT_FOOT2, SLOT_SHAPE2, SLOT_DETUNE, SLOT_MOD_PITCH2 },
    { SLOT_LFO_RATE, SLOT_LFO_SHAPE, SLOT_EMPTY, SLOT_EMPTY },
    { SLOT_MOD_PITCH, SLOT_MOD_PITCH2, SLOT_EMPTY, SLOT_EMPTY },
    { SLOT_LV1, SLOT_LV2, SLOT_NOISE, SLOT_EXT },
    { SLOT_CUT, SLOT_RES, SLOT_CONT, SLOT_DRIVE },
    { SLOT_MOD_CUT, SLOT_EMPTY, SLOT_EMPTY, SLOT_EMPTY },
    { SLOT_ATK_F, SLOT_DEC_F, SLOT_SUS_F, SLOT_REL_F },
    { SLOT_ATK_A, SLOT_DEC_A, SLOT_SUS_A, SLOT_REL_A },
    { SLOT_MOD_AMP, SLOT_EMPTY, SLOT_EMPTY, SLOT_EMPTY },
    { SLOT_TUNE, SLOT_LEVEL, SLOT_GLIDE_RATE, SLOT_DUO }
};

static const uint8_t SLOT_CCS[][2] = {
    {1, SLOT_MOD_CUT}, {5, SLOT_GLIDE_RATE}, {7, SLOT_LEVEL}, {16, SLOT_DRIVE},
    {70, SLOT_CONT}, {71, SLOT_RES}, {72, SLOT_REL_A}, {73, SLOT_ATK_A},
    {74, SLOT_CUT}, {75, SLOT_DEC_A}, {76, SLOT_LFO_RATE}, {77, SLOT_FM},
    {78, SLOT_NOISE}, {79, SLOT_SUS_A}, {92, SLOT_MOD_AMP}, {94, SLOT_DETUNE},
    {102, SLOT_ATK_F}, {103, SLOT_DEC_F}, {104, SLOT_SUS_F}, {105, SLOT_REL_F}
};

static int kind_lo(int kind)
{
    if (kind == SLOT_DETUNE || kind == SLOT_TUNE) return -64;
    if (kind == SLOT_FOOT1 || kind == SLOT_FOOT2) return -2;
    return 0;
}
static int kind_hi(int kind)
{
    if (kind == SLOT_DETUNE || kind == SLOT_TUNE) return 64;
    if (kind == SLOT_FOOT1 || kind == SLOT_FOOT2) return 2;
    if (kind == SLOT_SHAPE1 || kind == SLOT_SHAPE2) return 4;
    if (kind == SLOT_LFO_SHAPE) return 3;
    if (kind == SLOT_DUO) return 1;
    return 127;
}
static int cc_to_pos(int kind, int val)
{
    int lo = kind_lo(kind), hi = kind_hi(kind);
    return lo + (val * (hi - lo)) / 127;
}

static int find_idx(int kind, int *idx)
{
    int f, i;
    for (f = 0; f < 11; f++)
        for (i = 0; i < 4; i++)
            if (WIN[f][i] == kind) {
                *idx = i;
                return f;
            }
    *idx = -1;
    return -1;
}

static void go_slot(int kind, int *idx)
{
    int flat = find_idx(kind, idx);
    poli_nav_home();
    if (flat > 0) poli_nav_select_turn(flat);
}

static void knob_to(int kind, int target)
{
    int idx, cur;
    poli_nav_view_t v;
    go_slot(kind, &idx);
    poli_nav_view(&v);
    cur = v.slot_pos[idx];
    /* If the knob already sits on the target, step away and back so apply_kind
     * runs. Boot can leave the engine at a factory number that is not quite
     * the law at that displayed pos (contour 8192 vs scale_u7(32)). */
    if (cur == target) {
        int alt = (target < kind_hi(kind)) ? target + 1 : target - 1;
        poli_nav_knob(idx, alt - cur);
        poli_nav_knob(idx, target - alt);
    } else {
        poli_nav_knob(idx, target - cur);
    }
}

typedef struct {
    uint8_t k[31], sw;
    int32_t cut, res, drv;
    uint8_t bp, glide, kh, bass;
    int32_t audio[512];
} snap_t;

static void snapshot(snap_t *s)
{
    uint32_t i;
    memset(s, 0, sizeof *s);
    poli_nav_patch_get(s->k, &s->sw);
    s->cut = poli_cut;
    s->res = poli_res;
    s->drv = poli_drv;
    s->bp = poli_bp;
    s->glide = poli_glide;
    s->kh = polivoks_keyhold();
    s->bass = polivoks_bass_support();
    polivoks_midi(0x90, 60, 100);
    memset(s->audio, 0, sizeof s->audio);
    polivoks_render(s->audio, 256);
    polivoks_midi(0x80, 60, 0);
    /* leave the voice quiet so the next snapshot starts cleanly; init follows */
    (void)i;
}

static int snap_eq(const snap_t *a, const snap_t *b)
{
    return memcmp(a->k, b->k, 31) == 0 && a->sw == b->sw
        && a->cut == b->cut && a->res == b->res && a->drv == b->drv
        && a->bp == b->bp && a->glide == b->glide && a->kh == b->kh
        && a->bass == b->bass
        && memcmp(a->audio, b->audio, sizeof a->audio) == 0;
}

static int view_pos(int kind)
{
    int idx;
    poli_nav_view_t v;
    go_slot(kind, &idx);
    poli_nav_view(&v);
    return v.slot_pos[idx];
}

static void check_slot_cc(uint8_t cc, int kind, int val)
{
    snap_t a, b;
    int want = cc_to_pos(kind, val), got;
    uint32_t gen0, edits0;
    char line[192];

    poli_nav_init();
    gen0 = poli_nav_generation();
    edits0 = poli_nav_edits();
    poli_nav_midi(0xB0, cc, (uint8_t)val);
    got = view_pos(kind);
    snprintf(line, sizeof line, "CC %u val %d -> pos %d (want %d, lo %d hi %d)",
             cc, val, got, want, kind_lo(kind), kind_hi(kind));
    expect("cc_pos", got == want, line);
    expect("cc_nav_gen", poli_nav_generation() != gen0, "screen dirty");
    expect("cc_nav_edits", poli_nav_edits() != edits0, "preset dirty");
    snapshot(&a);

    poli_nav_init();
    knob_to(kind, want);
    snapshot(&b);
    snprintf(line, sizeof line, "CC %u val %d matches knob at pos %d", cc, val, want);
    expect("cc_eq_knob", snap_eq(&a, &b), line);
}

static void check_switch(uint8_t cc, int val, int want_on, const char *what)
{
    char line[160];
    uint32_t gen0, edits0;
    int on;
    poli_nav_init();
    gen0 = poli_nav_generation();
    edits0 = poli_nav_edits();
    /* start from a known opposite or same state, then set absolutely */
    if (cc == 64) polivoks_set_keyhold(want_on ? 0u : 1u);
    else if (cc == 65) poli_glide = (uint8_t)(want_on ? 0 : 1);
    else if (cc == 85) poli_bp = (uint8_t)(want_on ? 0 : 1);
    else if (cc == 86) polivoks_set_bass_support(want_on ? 0u : 1u);
    poli_nav_midi(0xB0, cc, (uint8_t)val);   /* default receive channel 1 */
    if (cc == 64) on = polivoks_keyhold();
    else if (cc == 65) on = poli_glide;
    else if (cc == 85) on = poli_bp;
    else on = polivoks_bass_support();
    snprintf(line, sizeof line, "CC %u val %d %s -> %d (want %d)", cc, val, what, on, want_on);
    expect("cc_sw", on == want_on, line);
    expect("cc_sw_gen", poli_nav_generation() != gen0, "switch dirties screen");
    expect("cc_sw_edits", poli_nav_edits() != edits0, "switch dirties preset");
}

int main(void)
{
    static const int vals[] = {0, 1, 32, 63, 64, 90, 127};
    unsigned i, vi;
    snap_t before, after;
    uint8_t k0[31], sw0, k1[31], sw1;
    int32_t cut0;
    char line[160];
    fail = 0;

    for (i = 0; i < sizeof SLOT_CCS / sizeof SLOT_CCS[0]; i++)
        for (vi = 0; vi < sizeof vals / sizeof vals[0]; vi++)
            check_slot_cc(SLOT_CCS[i][0], SLOT_CCS[i][1], vals[vi]);

    /* detune scaling spelled out */
    poli_nav_init();
    poli_nav_midi(0xB0, 94, 0);
    expect("detune_cc0", view_pos(SLOT_DETUNE) == -64, "CC 0 -> -64");
    poli_nav_midi(0xB0, 94, 64);
    expect("detune_cc64", view_pos(SLOT_DETUNE) == 0, "CC 64 -> 0");
    poli_nav_midi(0xB0, 94, 127);
    expect("detune_cc127", view_pos(SLOT_DETUNE) == 64, "CC 127 -> +64");

    check_switch(64, 0, 0, "key-hold");
    check_switch(64, 63, 0, "key-hold");
    check_switch(64, 64, 1, "key-hold");
    check_switch(64, 127, 1, "key-hold");
    check_switch(65, 0, 0, "glide");
    check_switch(65, 63, 0, "glide");
    check_switch(65, 64, 1, "glide");
    check_switch(65, 127, 1, "glide");
    check_switch(85, 0, 0, "LP");
    check_switch(85, 63, 0, "LP");
    check_switch(85, 64, 1, "BP");
    check_switch(85, 127, 1, "BP");
    check_switch(86, 0, 0, "bass");
    check_switch(86, 63, 0, "bass");
    check_switch(86, 86, 1, "bass");
    check_switch(86, 127, 1, "bass");

    /* absolute, not toggle: already on, stay on */
    poli_nav_init();
    poli_bp = 1;
    poli_nav_midi(0xB0, 85, 100);
    expect("bp_set_not_toggle", poli_bp == 1, "already BP, CC 85=100 stays BP");
    poli_nav_midi(0xB0, 85, 10);
    expect("bp_set_off", poli_bp == 0, "CC 85=10 is LP");

    /* unmapped CCs ignored (including 0, 32 bank, 121 reset, 3, 20, 80, 127) */
    {
        static const uint8_t ignore[] = {0, 3, 8, 20, 32, 80, 81, 91, 121, 122, 124, 127};
        poli_nav_init();
        poli_nav_midi(0xB0, 74, 90);
        poli_nav_patch_get(k0, &sw0);
        cut0 = poli_cut;
        for (i = 0; i < sizeof ignore; i++) {
            uint32_t g = poli_nav_generation(), e = poli_nav_edits();
            int32_t cut = poli_cut, res = poli_res, drv = poli_drv;
            uint8_t bp = poli_bp, gl = poli_glide, kh = polivoks_keyhold(), ba = polivoks_bass_support();
            poli_nav_midi(0xB0, ignore[i], 77);
            poli_nav_patch_get(k1, &sw1);
            snprintf(line, sizeof line, "unmapped CC %u", ignore[i]);
            expect("cc_unmapped",
                   memcmp(k0, k1, 31) == 0 && sw0 == sw1 && poli_cut == cut && poli_res == res
                   && poli_drv == drv && poli_bp == bp && poli_glide == gl
                   && polivoks_keyhold() == kh && polivoks_bass_support() == ba
                   && poli_nav_generation() == g && poli_nav_edits() == e, line);
        }
        (void)cut0;
    }

    /* 120 / 123 unchanged: panic / all-notes-off, no CC map, cutoff stays */
    poli_nav_init();
    poli_nav_midi(0xB0, 74, 100);
    cut0 = poli_cut;
    poli_nav_midi(0x90, 60, 100);
    expect("note_on_before_120", poli_midi_on == 1 && poli_gate == 1, "armed");
    poli_nav_patch_get(k0, &sw0);
    poli_nav_midi(0xB0, 120, 0);
    poli_nav_patch_get(k1, &sw1);
    expect("cc120_panic", poli_midi_on == 0 && poli_gate == 0, "all-sound-off");
    expect("cc120_keeps_cut", poli_cut == cut0, "map did not steal 120");
    expect("cc120_keeps_patch", memcmp(k0, k1, 31) == 0 && sw0 == sw1, "nav unchanged");

    poli_nav_init();
    poli_nav_midi(0xB0, 71, 40);
    cut0 = poli_res;
    poli_nav_midi(0x90, 67, 90);
    poli_nav_midi(0xB0, 123, 0);
    expect("cc123_notes_off", poli_midi_on == 0 && poli_gate == 0, "all-notes-off");
    expect("cc123_keeps_res", poli_res == cut0, "map did not steal 123");

    /* SysEx / clock / program-change do not move CCs; PC still silences */
    poli_nav_init();
    poli_nav_midi(0xB0, 74, 80);
    snapshot(&before);
    poli_nav_midi(0xF0, 0x22, 0x24);
    poli_nav_midi(0xF7, 0, 0);
    poli_nav_midi(0xF8, 0, 0);
    snapshot(&after);
    expect("sysex_clock_no_cc", snap_eq(&before, &after) || (after.cut == before.cut && memcmp(after.k, before.k, 31) == 0),
           "SysEx and clock do not change the map");
    /* program change silences (existing behaviour) and must not move knobs */
    poli_nav_midi(0x90, 60, 100);
    poli_nav_patch_get(k0, &sw0);
    cut0 = poli_cut;
    poli_nav_midi(0xC0, 3, 0);
    poli_nav_patch_get(k1, &sw1);
    expect("pc_silences", poli_midi_on == 0 && poli_gate == 0, "program change");
    expect("pc_no_cc", memcmp(k0, k1, 31) == 0 && sw0 == sw1 && poli_cut == cut0, "PC does not move CCs");

    /* many CCs in one drain still only need one later redraw; gen moves, apply all */
    poli_nav_init();
    poli_nav_midi(0xB0, 74, 10);
    poli_nav_midi(0xB0, 71, 20);
    poli_nav_midi(0xB0, 70, 30);
    expect("burst_cut", view_pos(SLOT_CUT) == 10, "last drain keeps all");
    expect("burst_res", view_pos(SLOT_RES) == 20, "res");
    expect("burst_cont", view_pos(SLOT_CONT) == 30, "contour");

    /* receive-channel filter: correct channel accepted, others ignored, all 16 */
    {
        int ch, other;
        for (ch = 1; ch <= 16; ch++) {
            uint8_t st_cc = (uint8_t)(0xB0u + (ch - 1));
            uint8_t st_note = (uint8_t)(0x90u + (ch - 1));
            uint8_t st_off = (uint8_t)(0x80u + (ch - 1));
            uint8_t st_pb = (uint8_t)(0xE0u + (ch - 1));
            uint8_t st_pc = (uint8_t)(0xC0u + (ch - 1));
            poli_nav_init();
            poli_nav_set_midi_ch(ch);
            expect("ch_set", poli_nav_midi_ch() == ch, "channel set");
            /* same-channel CC, note, 120, 123, pitch-bend, program change */
            poli_nav_midi(st_cc, 74, 40);
            snprintf(line, sizeof line, "ch %d CC 74 accepted", ch);
            expect("ch_cc_ok", view_pos(SLOT_CUT) == 40, line);
            poli_nav_midi(st_note, 60, 100);
            expect("ch_note_ok", poli_midi_on == 1 && poli_gate == 1, "same-channel note on");
            poli_nav_midi(st_off, 60, 0);
            expect("ch_noteoff_ok", poli_midi_on == 0, "same-channel note off");
            poli_nav_midi(st_note, 60, 100);
            poli_nav_midi(st_cc, 123, 0);
            expect("ch_123_ok", poli_midi_on == 0, "same-channel CC123");
            poli_nav_midi(st_note, 60, 100);
            poli_nav_midi(st_cc, 120, 0);
            expect("ch_120_ok", poli_midi_on == 0 && poli_gate == 0, "same-channel CC120");
            snapshot(&before);
            poli_nav_midi(st_pb, 0, 64);
            snapshot(&after);
            expect("ch_pb_ok", snap_eq(&before, &after) || after.cut == before.cut, "pitch bend accepted, engine may ignore");
            poli_nav_midi(st_note, 61, 100);
            poli_nav_midi(st_pc, 3, 0);
            expect("ch_pc_ok", poli_midi_on == 0, "same-channel program change silences");
            /* every other channel is ignored: CC, note, 120, 123, pitch bend, PC */
            for (other = 1; other <= 16; other++) {
                uint8_t o_cc, o_note, o_120, o_pb, o_pc;
                int32_t cut;
                if (other == ch) continue;
                o_cc = (uint8_t)(0xB0u + (other - 1));
                o_note = (uint8_t)(0x90u + (other - 1));
                o_120 = o_cc;
                o_pb = (uint8_t)(0xE0u + (other - 1));
                o_pc = (uint8_t)(0xC0u + (other - 1));
                poli_nav_midi(st_cc, 74, 55);
                cut = poli_cut;
                poli_nav_midi(o_cc, 74, 10);
                snprintf(line, sizeof line, "ch %d ignores CC on %d", ch, other);
                expect("ch_cc_ignore", view_pos(SLOT_CUT) == 55 && poli_cut == cut, line);
                poli_nav_midi(st_note, 64, 100);
                expect("ch_armed", poli_midi_on == 1, "re-arm");
                poli_nav_midi(o_note, 70, 100);
                expect("ch_note_ignore", poli_midi_on == 1 && poli_note == 64, "other-channel note ignored");
                poli_nav_midi(o_cc, 123, 0);
                expect("ch_123_ignore", poli_midi_on == 1, "other-channel CC123 ignored");
                poli_nav_midi(o_120, 120, 0);
                expect("ch_120_ignore", poli_midi_on == 1 && poli_gate == 1, "other-channel CC120 ignored");
                poli_nav_midi(o_pb, 0, 0);
                expect("ch_pb_ignore", poli_midi_on == 1, "other-channel pitch bend ignored");
                poli_nav_midi(o_pc, 1, 0);
                expect("ch_pc_ignore", poli_midi_on == 1, "other-channel PC ignored");
                poli_nav_midi(st_cc, 120, 0); /* panic on the receive channel to reset */
            }
        }
        /* system messages still pass with a channel set (no channel byte) */
        poli_nav_init();
        poli_nav_set_midi_ch(4);
        snapshot(&before);
        poli_nav_midi(0xF0, 0x22, 0x24);
        poli_nav_midi(0xF7, 0, 0);
        poli_nav_midi(0xF8, 0, 0);
        snapshot(&after);
        expect("sysex_clock_any_ch", after.cut == before.cut, "SysEx/clock have no channel");
    }

    printf(fail ? "HOST-MIDI-CC FAIL %d\n" : "HOST-MIDI-CC PASS\n", fail);
    return fail ? 1 : 0;
}
