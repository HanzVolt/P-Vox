/* SPDX-License-Identifier: GPL-3.0-only */
/* Pad spectrum meter: 8-column hue wash (blue to deep dark red) for
 * cutoff and green to deep dark red for resonance, top equals bottom,
 * fill vs poli_cut / poli_res (both pads of a column in unison,
 * partial column dimmed in its own shade), last-mover ownership,
 * 1.5 s restore, and pad taps still load presets while the meter is
 * up. Lights only. Encoder / CC 74 and CC 71 do not start it. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"
#include "smk_pad_spectrum.h"

#define SIM_SECT 4
#define SIM_MAGIC 0x544C4F56u
static uint8_t simf[SIM_SECT][4096];
static long sim_ops, sim_cut_at = -1, sim_writes[SIM_SECT], sim_erases;
static int sim_dead;
static uint32_t sim_crc(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) { c ^= *p++; for (k = 0; k < 8u; k++) c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1; }
    return ~c;
}
static int sim_step(void)
{
    if (sim_dead) return 0;
    if (sim_cut_at >= 0 && sim_ops >= sim_cut_at) { sim_dead = 1; return 0; }
    sim_ops++;
    return 1;
}
static void sim_reset(void)
{
    memset(simf, 0xFF, sizeof simf);
    sim_ops = 0;
    sim_cut_at = -1;
    sim_dead = 0;
    memset(sim_writes, 0, sizeof sim_writes);
    sim_erases = 0;
}
#define PV_STORE 1
static int pv_store_load(int w, void *p, uint32_t n)
{
    uint32_t h[3];
    if (n > 4096u - 12u) return -1;
    memcpy(h, simf[w], 12);
    if (h[0] != SIM_MAGIC) return -1;
    if (h[1] == n) {
        if (sim_crc(simf[w] + 12, n) != h[2]) return -1;
        memcpy(p, simf[w] + 12, n);
        return 0;
    }
    if (h[1] < n && h[1] >= 12u && h[1] <= 4096u - 12u) {
        if (sim_crc(simf[w] + 12, h[1]) != h[2]) return -1;
        memcpy(p, simf[w] + 12, h[1]);
        memset((uint8_t *)p + h[1], 0, n - h[1]);
        return 0;
    }
    return -1;
}
static int pv_store_save(int w, const void *p, uint32_t n)
{
    uint32_t h[3];
    if (n > 4096u - 12u) return -1;
    sim_writes[w]++;
    h[0] = SIM_MAGIC;
    h[1] = n;
    h[2] = sim_crc((const uint8_t *)p, n);
    if (!sim_step()) return -1;
    memset(simf[w], 0xFF, 4096);
    sim_erases++;
    if (!sim_step()) { memcpy(simf[w] + 12, p, n / 2); return -1; }
    memcpy(simf[w] + 12, p, n);
    if (!sim_step()) return -1;
    memcpy(simf[w], h, 12);
    return 0;
}
#include "poli_preset.c"

static int fail;
static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok) fail++;
}

/* Hue in degrees on the blue→red arc (G is 0 on this wash). 240..360. */
static int wash_hue(int r, int g, int b)
{
    int mx, d;
    (void)g;
    mx = r > b ? r : b;
    d = mx;
    if (d <= 0)
        return 240;
    if (b >= r)
        return 240 + (60 * r) / d;
    return 360 - (60 * b) / d;
}

/* Hue in degrees on the green→red arc (B is 0 on this wash). 120..0. */
static int wash_hue_res(int r, int g, int b)
{
    int mx, d;
    (void)b;
    mx = r > g ? r : g;
    d = mx;
    if (d <= 0)
        return 120;
    if (g >= r)
        return 120 - (60 * r) / d;
    return (60 * g) / d;
}

int main(void)
{
    int i, full, frac, ok, prev_h, h;
    uint8_t r, g, b, r0, g0, b0, r8, g8, b8;
    int32_t held;
    char line[200];
    poli_preset_view_t view;
    fail = 0;

    /* ---- column colours: table, top equals bottom, hue rises ---- */
    ok = 1;
    prev_h = 0;
    for (i = 0; i < 8; i++) {
        if (SMK_SPEC_RGB[i][0] != SMK_SPEC_COL_RGB[i][0]
            || SMK_SPEC_RGB[i][1] != SMK_SPEC_COL_RGB[i][1]
            || SMK_SPEC_RGB[i][2] != SMK_SPEC_COL_RGB[i][2]
            || SMK_SPEC_RGB[i + 8][0] != SMK_SPEC_COL_RGB[i][0]
            || SMK_SPEC_RGB[i + 8][1] != SMK_SPEC_COL_RGB[i][1]
            || SMK_SPEC_RGB[i + 8][2] != SMK_SPEC_COL_RGB[i][2]
            || smk_spec_col(i) != i || smk_spec_col(i + 8) != i)
            ok = 0;
        h = wash_hue(SMK_SPEC_COL_RGB[i][0], SMK_SPEC_COL_RGB[i][1], SMK_SPEC_COL_RGB[i][2]);
        if (i && h < prev_h)
            ok = 0;
        prev_h = h;
    }
    expect("column_table_and_top_equals_bottom", ok, "");
    expect("col1_blue",
           SMK_SPEC_COL_RGB[0][0] == 0 && SMK_SPEC_COL_RGB[0][1] == 0
           && SMK_SPEC_COL_RGB[0][2] == 224, "(0,0,224)");
    expect("col8_deep_dark_red",
           SMK_SPEC_COL_RGB[7][0] >= 110 && SMK_SPEC_COL_RGB[7][0] <= 140
           && SMK_SPEC_COL_RGB[7][1] == 0 && SMK_SPEC_COL_RGB[7][2] <= 4
           && SMK_SPEC_COL_RGB[7][0] == 125, "(125,0,0)");
    {
        int h0 = wash_hue(0, 0, 224);
        int h3 = wash_hue(182, 0, 212);
        int h4 = wash_hue(200, 0, 171);
        int h7 = wash_hue(125, 0, 0);
        snprintf(line, sizeof line, "h=%d %d %d %d", h0, h3, h4, h7);
        expect("hue_monotonic_blue_to_red",
               h0 == 240 && h3 > h0 && h3 < 300 && h4 > 300 && h4 < h7 && h7 == 360, line);
    }
    expect("columns_are_k_and_k8", smk_spec_col(0) == 0 && smk_spec_col(8) == 0
           && smk_spec_col(7) == 7 && smk_spec_col(15) == 7
           && SMK_SPEC_COLS == 8, "");

    /* ---- 8-column fill vs poli_cut; both pads of a column in unison ---- */
    smk_spec_colour(0, 0, &r, &g, &b);
    expect("cut0_top_dark", r == 0 && g == 0 && b == 0, "");
    smk_spec_colour(8, 0, &r, &g, &b);
    expect("cut0_bottom_dark", r == 0 && g == 0 && b == 0, "");
    smk_spec_level(0, &full, &frac);
    expect("cut0_level", full == 0 && frac == 0, "");

    smk_spec_level(POLI_CUT_TOP, &full, &frac);
    expect("cut_top_all_eight_columns", full == 8 && frac == 0, "");
    smk_spec_colour(0, POLI_CUT_TOP, &r0, &g0, &b0);
    smk_spec_colour(8, POLI_CUT_TOP, &r8, &g8, &b8);
    expect("cut_top_col1_top_blue", r0 == 0 && g0 == 0 && b0 == 224, "");
    expect("cut_top_col1_bottom_matches", r8 == r0 && g8 == g0 && b8 == b0, "");
    smk_spec_colour(7, POLI_CUT_TOP, &r, &g, &b);
    smk_spec_colour(15, POLI_CUT_TOP, &r0, &g0, &b0);
    expect("cut_top_col8_top_deep_red", r == 125 && g == 0 && b == 0, "");
    expect("cut_top_col8_bottom_matches", r0 == 125 && g0 == 0 && b0 == 0, "");

    smk_spec_level(POLI_CUT_TOP / 2, &full, &frac);
    snprintf(line, sizeof line, "full=%d frac=%d", full, frac);
    expect("cut_half_fills_first_four_columns", full == 4 && frac == 0, line);
    smk_spec_colour(3, POLI_CUT_TOP / 2, &r, &g, &b);
    smk_spec_colour(11, POLI_CUT_TOP / 2, &r0, &g0, &b0);
    expect("cut_half_col4_top_full", r == 182 && b == 212, "");
    expect("cut_half_col4_bottom_matches", r0 == r && g0 == g && b0 == b, "");
    smk_spec_colour(4, POLI_CUT_TOP / 2, &r, &g, &b);
    expect("cut_half_col5_top_dark", r == 0 && b == 0, "");
    smk_spec_colour(12, POLI_CUT_TOP / 2, &r, &g, &b);
    expect("cut_half_col5_bottom_dark", r == 0 && b == 0, "");

    /* Partial next column: both pads dimmed in that column's own shade. */
    {
        int32_t cut = (int32_t)(((uint32_t)4 * 256u + 128u) * (uint32_t)POLI_CUT_TOP / (8u * 256u));
        uint8_t rt, gt, bt, rb, gb, bb;
        smk_spec_level(cut, &full, &frac);
        smk_spec_colour(4, cut, &rt, &gt, &bt);
        smk_spec_colour(12, cut, &rb, &gb, &bb);
        snprintf(line, sizeof line, "full=%d frac=%d top=%d,%d,%d bot=%d,%d,%d",
                 full, frac, rt, gt, bt, rb, gb, bb);
        expect("partial_column_both_pads_dimmed",
               full == 4 && frac >= 120 && frac <= 136
               && rt == (uint8_t)((200u * (uint16_t)frac) / 256u)
               && bt == (uint8_t)((171u * (uint16_t)frac) / 256u)
               && rt == rb && gt == gb && bt == bb
               && rt > 0 && rt < 200 && bt > 0 && bt < 171,
               line);
        smk_spec_colour(3, cut, &r, &g, &b);
        expect("partial_prev_col_top_full", r == 182 && b == 212, "");
        smk_spec_colour(11, cut, &r0, &g0, &b0);
        expect("partial_prev_col_bottom_matches", r0 == r && b0 == b, "");
        smk_spec_colour(5, cut, &r, &g, &b);
        expect("partial_next_col_top_dark", r == 0 && b == 0, "");
        smk_spec_colour(13, cut, &r, &g, &b);
        expect("partial_next_col_bottom_dark", r == 0 && b == 0, "");
    }

    /* ---- timeout restore ---- */
    smk_spec_reset();
    expect("idle_not_active", smk_spec_active(0) == 0, "");
    smk_spec_note_move(1000, SMK_SPEC_SRC_CUT);
    expect("active_at_move", smk_spec_active(1000) == 1, "");
    expect("active_just_before_timeout", smk_spec_active(1000 + 1499) == 1, "");
    expect("inactive_at_1500", smk_spec_active(1000 + 1500) == 0, "");
    expect("stays_inactive", smk_spec_active(1000 + 2000) == 0, "");
    smk_spec_note_move(4000, SMK_SPEC_SRC_CUT);
    expect("retriggers_after_restore", smk_spec_active(4000) == 1, "");
    expect("hold_constant", SMK_SPEC_HOLD_MS == 1500, "");
    expect("update_40hz", SMK_SPEC_PERIOD_MS == 25, "");

    /* Encoder / CC 74 snap and do not start the meter. */
    smk_spec_reset();
    poli_nav_init();
    held = poli_cut;
    poli_nav_cc(74, 100);
    expect("cc74_still_snaps", poli_cut == poli_cut_of_knob(100) && poli_cut != held, "");
    expect("cc74_does_not_trigger_meter", smk_spec_active(5000) == 0, "");

    /* ---- pad taps still load presets while the meter is showing ---- */
    sim_reset();
    poli_nav_init();
    poli_preset_init(0);
    poli_preset_view(&view);
    smk_spec_note_move(8000, SMK_SPEC_SRC_CUT);
    expect("meter_up_before_tap", smk_spec_active(8000) == 1, "");
    {
        int before = view.cur;
        int want = (before + 5) % PV_NSLOT;
        int32_t cut_before = poli_cut;
        poli_preset_load_slot(want, 8000);
        poli_preset_view(&view);
        snprintf(line, sizeof line, "cur %d -> %d (want %d)", before, view.cur, want);
        expect("tap_loads_preset_during_meter", view.cur == want, line);
        expect("meter_still_up_after_tap", smk_spec_active(8100) == 1, "");
        /* Spectrum helpers themselves must not write poli_cut.
         * A preset load may change it; the colour call must not. */
        held = poli_cut;
        smk_spec_colour(3, held, &r, &g, &b);
        expect("colour_does_not_write_poli_cut", poli_cut == held, "");
        (void)cut_before;
    }


    /* ---- resonance palette: table, top equals bottom, hue falls green to red ---- */
    ok = 1;
    prev_h = 200;
    for (i = 0; i < 8; i++) {
        if (SMK_SPEC_RES_RGB[i][0] != SMK_SPEC_RES_COL_RGB[i][0]
            || SMK_SPEC_RES_RGB[i][1] != SMK_SPEC_RES_COL_RGB[i][1]
            || SMK_SPEC_RES_RGB[i][2] != SMK_SPEC_RES_COL_RGB[i][2]
            || SMK_SPEC_RES_RGB[i + 8][0] != SMK_SPEC_RES_COL_RGB[i][0]
            || SMK_SPEC_RES_RGB[i + 8][1] != SMK_SPEC_RES_COL_RGB[i][1]
            || SMK_SPEC_RES_RGB[i + 8][2] != SMK_SPEC_RES_COL_RGB[i][2]
            || SMK_SPEC_RES_COL_RGB[i][2] != 0)
            ok = 0;
        h = wash_hue_res(SMK_SPEC_RES_COL_RGB[i][0], SMK_SPEC_RES_COL_RGB[i][1],
                         SMK_SPEC_RES_COL_RGB[i][2]);
        if (i && h > prev_h)
            ok = 0;
        prev_h = h;
    }
    expect("res_column_table_and_top_equals_bottom", ok, "");
    expect("res_col1_green",
           SMK_SPEC_RES_COL_RGB[0][0] == 0 && SMK_SPEC_RES_COL_RGB[0][1] == 224
           && SMK_SPEC_RES_COL_RGB[0][2] == 0, "(0,224,0)");
    expect("res_col8_deep_dark_red",
           SMK_SPEC_RES_COL_RGB[7][0] == 125 && SMK_SPEC_RES_COL_RGB[7][1] == 0
           && SMK_SPEC_RES_COL_RGB[7][2] == 0, "(125,0,0)");
    {
        int h0 = wash_hue_res(0, 224, 0);
        int h1 = wash_hue_res(75, 224, 0);
        int h2 = wash_hue_res(220, 220, 0);
        int h3 = wash_hue_res(212, 170, 0);
        int h7 = wash_hue_res(125, 0, 0);
        snprintf(line, sizeof line, "h=%d %d %d %d %d", h0, h1, h2, h3, h7);
        expect("res_hue_monotonic_green_to_red",
               h0 == 120 && h1 < h0 && h1 > 60 && h2 == 60
               && h3 < 60 && h3 > h7 && h7 == 0, line);
    }
    /* Yellow arrives one column sooner (column 3, hue 60°). Not a G/B swap. */
    expect("res_col2_quick_yellow_green",
           SMK_SPEC_RES_COL_RGB[1][0] == 75 && SMK_SPEC_RES_COL_RGB[1][1] == 224
           && SMK_SPEC_RES_COL_RGB[1][2] == 0, "(75,224,0)");
    expect("res_col3_yellow",
           SMK_SPEC_RES_COL_RGB[2][0] == 220 && SMK_SPEC_RES_COL_RGB[2][1] == 220
           && SMK_SPEC_RES_COL_RGB[2][2] == 0
           && wash_hue_res(220, 220, 0) == 60, "(220,220,0) hue 60");
    expect("res_col4_orange_after_yellow",
           SMK_SPEC_RES_COL_RGB[3][0] == 212 && SMK_SPEC_RES_COL_RGB[3][1] == 170
           && SMK_SPEC_RES_COL_RGB[3][2] == 0
           && wash_hue_res(212, 170, 0) < 60, "(212,170,0)");

    /* ---- 8-column fill vs poli_res; both pads of a column in unison ---- */
    smk_spec_res_colour(0, 0, &r, &g, &b);
    expect("res0_top_dark", r == 0 && g == 0 && b == 0, "");
    smk_spec_res_colour(8, 0, &r, &g, &b);
    expect("res0_bottom_dark", r == 0 && g == 0 && b == 0, "");
    smk_spec_res_level(0, &full, &frac);
    expect("res0_level", full == 0 && frac == 0, "");

    smk_spec_res_level(POLI_RES_TOP, &full, &frac);
    expect("res_top_all_eight_columns", full == 8 && frac == 0, "");
    smk_spec_res_colour(0, POLI_RES_TOP, &r0, &g0, &b0);
    smk_spec_res_colour(8, POLI_RES_TOP, &r8, &g8, &b8);
    expect("res_top_col1_top_green", r0 == 0 && g0 == 224 && b0 == 0, "");
    expect("res_top_col1_bottom_matches", r8 == r0 && g8 == g0 && b8 == b0, "");
    smk_spec_res_colour(7, POLI_RES_TOP, &r, &g, &b);
    smk_spec_res_colour(15, POLI_RES_TOP, &r0, &g0, &b0);
    expect("res_top_col8_top_deep_red", r == 125 && g == 0 && b == 0, "");
    expect("res_top_col8_bottom_matches", r0 == 125 && g0 == 0 && b0 == 0, "");

    smk_spec_res_level(POLI_RES_TOP / 2, &full, &frac);
    snprintf(line, sizeof line, "full=%d frac=%d", full, frac);
    expect("res_half_fills_first_four_columns", full == 4 && frac == 0, line);
    smk_spec_res_colour(3, POLI_RES_TOP / 2, &r, &g, &b);
    smk_spec_res_colour(11, POLI_RES_TOP / 2, &r0, &g0, &b0);
    expect("res_half_col4_top_full", r == 212 && g == 170 && b == 0, "");
    expect("res_half_col4_bottom_matches", r0 == r && g0 == g && b0 == b, "");
    smk_spec_res_colour(4, POLI_RES_TOP / 2, &r, &g, &b);
    expect("res_half_col5_top_dark", r == 0 && g == 0 && b == 0, "");
    smk_spec_res_colour(12, POLI_RES_TOP / 2, &r, &g, &b);
    expect("res_half_col5_bottom_dark", r == 0 && g == 0 && b == 0, "");

    {
        int32_t resv = (int32_t)(((uint32_t)4 * 256u + 128u) * (uint32_t)POLI_RES_TOP / (8u * 256u));
        uint8_t rt, gt, bt, rb, gb, bb;
        smk_spec_res_level(resv, &full, &frac);
        smk_spec_res_colour(4, resv, &rt, &gt, &bt);
        smk_spec_res_colour(12, resv, &rb, &gb, &bb);
        snprintf(line, sizeof line, "full=%d frac=%d top=%d,%d,%d bot=%d,%d,%d",
                 full, frac, rt, gt, bt, rb, gb, bb);
        expect("res_partial_column_both_pads_dimmed",
               full == 4 && frac >= 120 && frac <= 136
               && rt == (uint8_t)((200u * (uint16_t)frac) / 256u)
               && gt == (uint8_t)((120u * (uint16_t)frac) / 256u)
               && rt == rb && gt == gb && bt == bb
               && rt > 0 && rt < 200 && gt > 0 && gt < 120 && bt == 0,
               line);
        smk_spec_res_colour(3, resv, &r, &g, &b);
        expect("res_partial_prev_col_top_full", r == 212 && g == 170, "");
        smk_spec_res_colour(11, resv, &r0, &g0, &b0);
        expect("res_partial_prev_col_bottom_matches", r0 == r && g0 == g, "");
        smk_spec_res_colour(5, resv, &r, &g, &b);
        expect("res_partial_next_col_top_dark", r == 0 && g == 0, "");
        smk_spec_res_colour(13, resv, &r, &g, &b);
        expect("res_partial_next_col_bottom_dark", r == 0 && g == 0, "");
    }

    /* ---- last-mover ownership and shared timeout ---- */
    smk_spec_reset();
    expect("reset_owner_is_cut", smk_spec_owner() == SMK_SPEC_SRC_CUT, "");
    smk_spec_note_move(1000, SMK_SPEC_SRC_CUT);
    expect("cut_move_owns", smk_spec_owner() == SMK_SPEC_SRC_CUT
           && smk_spec_active(1000) == 1, "");
    smk_spec_note_move(1200, SMK_SPEC_SRC_RES);
    expect("res_move_takes_ownership", smk_spec_owner() == SMK_SPEC_SRC_RES
           && smk_spec_active(1200) == 1, "");
    expect("timeout_from_last_mover", smk_spec_active(1200 + 1499) == 1, "");
    expect("inactive_1500_after_res", smk_spec_active(1200 + 1500) == 0, "");
    smk_spec_note_move(4000, SMK_SPEC_SRC_RES);
    smk_spec_note_move(4100, SMK_SPEC_SRC_CUT);
    expect("cut_move_reclaims", smk_spec_owner() == SMK_SPEC_SRC_CUT
           && smk_spec_active(4100) == 1, "");
    expect("timeout_from_reclaim", smk_spec_active(4100 + 1500) == 0, "");

    /* Encoder / CC 71 snap resonance and do not start the meter. */
    smk_spec_reset();
    poli_nav_init();
    held = poli_res;
    poli_nav_cc(71, 100);
    expect("cc71_still_snaps", poli_res == (int32_t)((30000ll * 100) / 127) && poli_res != held, "");
    expect("cc71_does_not_trigger_meter", smk_spec_active(5000) == 0, "");
    expect("cc71_does_not_change_owner_while_idle", smk_spec_owner() == SMK_SPEC_SRC_CUT, "");

    printf(fail ? "HOST-PAD-SPECTRUM FAIL %d\n" : "HOST-PAD-SPECTRUM PASS\n", fail);
    return fail ? 1 : 0;
}
