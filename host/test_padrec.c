/* SPDX-License-Identifier: GPL-3.0-only */
/* Parity (23 Nov 2026): the Elite's PAD REC suite, unchanged, built for the FM-1 (no BOARD_SMK37): the same
 * poli_padrec.inc. The FM-1 keybed side (white keys, K4 cursor) is in test_parity.c. */
/* SMK-37 Elite pad-record host tests. Not the target. BOARD_SMK37=1.
 * Record / overdub / quantize / step entry / undo / clear / export-import of user slots. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"

static int fail;
static uint32_t now_ms;

static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok) fail++;
}

static void fresh(void)
{
    now_ms = 0;
    poli_nav_init();
    xtra_set_c2(3, 0);
}

static void tick(uint32_t ms)
{
    uint32_t i;
    for (i = 0; i < ms; i++) {
        now_ms++;
        xtra_tick(now_ms);
    }
}

static int kicks(void) { return xtra_padrec_work_lane(0); } /* L_KICK = 0 */
static int snares(void) { return xtra_padrec_work_lane(1); }

int main(void)
{
    uint8_t blob[XT_PADREC_BLOB], blob2[XT_PADREC_BLOB];
    char line[160];
    int p, i, nclick, nkick;
    uint32_t seen, hits;
    fail = 0;

    fresh();
    expect("boot_not_on_padrec", poli_nav_xtra() < 0, "HOME / EDIT, not an EXTRAS page");
    poli_nav_xtra_open(1);
    expect("open_seq", poli_nav_xtra() == 1, "SEQUENCER");
    poli_nav_select_turn(1);
    expect("k2_to_drums", poli_nav_xtra() == 2, "one click from SEQ is DRUMS");
    poli_nav_select_turn(1);
    expect("k2_to_drumstep", poli_nav_xtra() == 11, "Drum-Step: one more click is DRUM STEP");
    poli_nav_select_turn(1);
    expect("k2_to_padrec", poli_nav_xtra() == XW_PADREC, "one more click is PAD REC");
    poli_nav_select_turn(1);
    expect("k2_to_generate", poli_nav_xtra() == 8, "then GENERATE, as on the FM-1 after DRUMS");
    poli_nav_select_turn(-1);
    expect("back_padrec", poli_nav_xtra() == XW_PADREC, "back one click");

    expect("defaults", xtra_padrec_slot() == 0 && xtra_padrec_mode() == 0 && xtra_padrec_quant() == 1
           && !xtra_padrec_armed() && !xtra_padrec_slot_used(0),
           "slot 1, LIVE, QUANT 16TH, idle, empty library");
    expect("lane_map_bottom_kick", xtra_padrec_lane_of_pad(8) == 0, "pad 8 (bottom left) = kick");
    expect("lane_map_bottom_snare", xtra_padrec_lane_of_pad(9) == 1, "pad 9 = snare");
    expect("lane_map_top_rim", xtra_padrec_lane_of_pad(0) == 9, "pad 0 (top left) = rim"); /* L_RIM = 9 */
    expect("lane_map_top_crash", xtra_padrec_lane_of_pad(7) == 16, "pad 7 = crash");

    /* no core 2: REC refused */
    xtra_set_c2(0, 0);
    expect("rec_refused_without_c2", xtra_padrec_rec(now_ms) == 0 && !xtra_padrec_armed(), "needs XF_C2_OK");
    xtra_set_c2(3, 0);

    /* count-in: 1 bar of rim clicks, not recorded, then REC */
    seen = xf_par.trw;
    expect("rec_starts_countin", xtra_padrec_rec(now_ms) == 1 && xtra_padrec_countin() == 16, "16 sixteenths");
    tick(2100);   /* 120 BPM, 1 bar + a little */
    expect("after_countin_recording", xtra_padrec_armed() && !xtra_padrec_countin() && xtra_running(),
           "transport starts, count-in done");
    nclick = 0;
    nkick = 0;
    while (seen != xf_par.trw) {
        uint32_t e = xf_par.trq[seen & 15u];
        if ((e & 255u) == 11u || (e & 255u) == 23u) nclick++; /* 808/909 rim: DI_RS8=11, DI_RS9=23 */
        if ((e & 255u) == 0u || (e & 255u) == 1u || (e & 255u) == 15u) nkick++;
        seen++;
    }
    snprintf(line, sizeof line, "rim clicks in count-in: %d (want 4), kick lane 0x%x (pattern 1 is 0x1111, clicks not added)",
             nclick, kicks());
    expect("countin_four_rim_not_in_pattern", nclick == 4 && kicks() == 0x1111, line);

    /* play kick on pad 8 a few times over a bar; overdub */
    {
        int before;
        tick(10);
        before = kicks();
        xtra_padrec_pad(8, 100, now_ms);
        xtra_padrec_pad(8, 0, now_ms);
        expect("live_hit_sets_a_step", kicks() != before && kicks() != 0, "kick lane gained a bit");
        /* velocity is accepted by the API but the stored pattern is 1-bit; work bits only */
        before = kicks();
        xtra_padrec_pad(8, 20, now_ms);
        xtra_padrec_pad(8, 0, now_ms);
        tick(500);
        xtra_padrec_pad(8, 127, now_ms);
        xtra_padrec_pad(8, 0, now_ms);
        expect("overdub_ors_bits", (kicks() & before) == before, "existing steps stay, new hits add");
    }

    /* stop rec, save slot 1, export / import */
    expect("rec_stop", xtra_padrec_rec(now_ms) == 1 && !xtra_padrec_armed(), "second REC ends the take");
    expect("pattern_is_user", xtra_pattern() == 8, "live groove is the RAM USER slot");
    p = kicks();
    expect("save_slot1", xtra_padrec_save() == 1 && xtra_padrec_slot_used(0), "USER 1 used");
    expect("export_408", xtra_padrec_export(blob, sizeof blob) == XT_PADREC_BLOB, "packed 408 B");

    /* clear, undo */
    xtra_padrec_clear();
    expect("cleared", kicks() == 0 && snares() == 0, "working groove empty");
    xtra_padrec_undo();
    expect("undo_clear", kicks() == p, "SEL undo restores the take");

    /* load empty slot 2 does not wipe */
    expect("slot2_empty", xtra_padrec_load(1) == 0 && kicks() == p, "EMPTY, working copy kept");
    /* load() of an empty slot still selects it (SAVE would write there); K1 from slot 1 (index 0) */
    xtra_padrec_knob(0, -xtra_padrec_slot());    /* back to slot 1 */
    xtra_padrec_knob(0, 1);                      /* K1 to USER 2, empty */
    expect("k1_empty_stays", xtra_padrec_slot() == 1 && kicks() == p, "selecting EMPTY keeps the groove");
    xtra_padrec_knob(0, -1);
    xtra_padrec_clear();
    expect("load_slot1", xtra_padrec_load(0) == 1 && kicks() == p, "saved groove comes back");

    /* import into a fresh session */
    fresh();
    xtra_set_c2(3, 0);
    expect("import", xtra_padrec_import(blob, sizeof blob) == 0 && xtra_padrec_slot_used(0), "flash image loads");
    expect("import_load", xtra_padrec_load(0) == 1 && kicks() == p, "same bits after reboot-style import");
    memcpy(blob2, blob, sizeof blob);
    blob2[0] ^= 1;
    expect("bad_magic_rejected", xtra_padrec_import(blob2, sizeof blob2) < 0, "wrong magic");

    /* STEP entry: pad 9 (snare) toggles step 0, RIGHT, toggle, etc. */
    fresh();
    xtra_set_c2(3, 0);
    xtra_padrec_knob(1, 1);                      /* STEP */
    expect("step_mode", xtra_padrec_mode() == 1 && xtra_padrec_step() == 0, "cursor at 1");
    xtra_padrec_pad(9, 100, now_ms);
    xtra_padrec_pad(9, 0, now_ms);
    expect("step_toggle_on", (snares() & 1) != 0, "snare on step 1");
    xtra_padrec_pad(9, 100, now_ms);
    xtra_padrec_pad(9, 0, now_ms);
    expect("step_toggle_off", (snares() & 1) == 0, "second hit clears it");
    xtra_padrec_pad(9, 100, now_ms);
    xtra_padrec_pad(9, 0, now_ms);
    xtra_padrec_step_move(1);
    xtra_padrec_pad(9, 100, now_ms);
    xtra_padrec_pad(9, 0, now_ms);
    expect("step_advance", xtra_padrec_step() == 1 && (snares() & 3) == 3, "steps 1 and 2");
    xtra_padrec_step_move(-1);
    expect("step_back", xtra_padrec_step() == 0, "LEFT");

    /* hold-erase a lane */
    xtra_padrec_pad(9, 100, now_ms);
    tick(600);
    xtra_padrec_pad(9, 0, now_ms);
    expect("hold_erase_lane", snares() == 0, "held pad clears that voice");

    /* skip_lane: after a live rec hit on the current step, sequencer skips it */
    fresh();
    xtra_set_c2(3, 0);
    xtra_toggle(2);
    xtra_transport(1);
    tick(10);
    (void)xtra_padrec_rec(now_ms);               /* already running: wait for bar */
    tick(2100);
    expect("waitbar_then_rec", xtra_padrec_armed() && !xtra_padrec_waiting(), "starts on the bar");
    {
        int st, skipped = 0;
        xtra_padrec_pad(8, 100, now_ms);
        xtra_padrec_pad(8, 0, now_ms);
        st = xtra_padrec_step();
        skipped = xtra_padrec_skip_lane(0, st) || xtra_padrec_skip_lane(0, (st + 1) & 15);
        expect("live_hit_marks_sounded", skipped, "current or next 16th is skipped for a double");
    }

    /* fill button still has its own state (REC on DRUMS is not this code) */
    xtra_fill_button(1);
    expect("fill_still_works", xtra_fill_hold() == 2 && xtra_fill(), "DRUMS REC fill path untouched");
    xtra_fill_button(0);
    expect("fill_release", xtra_fill_hold() == 0, "release");

    /* 8 slots pack independently */
    fresh();
    xtra_set_c2(3, 0);
    for (i = 0; i < 8; i++) {
        memset(&line, 0, 8);                     /* (keep -Werror quiet if unused later) */
        xtra_padrec_knob(0, i - xtra_padrec_slot());
        xtra_padrec_clear();
        /* put a unique bit in kick: step i */
        xtra_padrec_knob(1, 1);                  /* STEP */
        while (xtra_padrec_step() != i) xtra_padrec_step_move(1);
        xtra_padrec_pad(8, 100, now_ms);
        xtra_padrec_pad(8, 0, now_ms);
        xtra_padrec_knob(1, -1);                 /* back to LIVE so K1 load is allowed */
        expect("slot_save", xtra_padrec_save() && xtra_padrec_slot_used(i)
               && xtra_padrec_work_lane(0) == (1 << i), "unique step per slot");
    }
    xtra_padrec_export(blob, sizeof blob);
    fresh();
    xtra_padrec_import(blob, sizeof blob);
    hits = 0;
    for (i = 0; i < 8; i++) {
        xtra_padrec_load(i);
        if (xtra_padrec_work_lane(0) == (1 << i)) hits++;
    }
    expect("eight_slots_roundtrip", hits == 8, "all 8 user slots survived export/import");

    printf("%s  %d failed\n", fail ? "FAILED" : "ALL PASSED", fail);
    return fail ? 1 : 0;
}
