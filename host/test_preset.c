/* SPDX-License-Identifier: GPL-3.0-only */
/* Preset store host checks (poli_preset.c). Not firmware.
 * The flash is a model of platform.c area_save/area_load: erase to 0xFF, the
 * bytes, then the 12-byte header [magic][length][crc32] last; a load checks
 * magic, length and crc. A power cut can be injected before any step, and
 * in the middle of the byte write. Host model only: no FM-1 flash ran. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"

/* ---- flash model: 0 bank A, 1 bank B, 2 backup, 3 settings */
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
static int sim_step(void) { if (sim_dead) return 0; if (sim_cut_at >= 0 && sim_ops >= sim_cut_at) { sim_dead = 1; return 0; } sim_ops++; return 1; }
static void sim_reset(void) { memset(simf, 0xFF, sizeof simf); sim_ops = 0; sim_cut_at = -1; sim_dead = 0; memset(sim_writes, 0, sizeof sim_writes); sim_erases = 0; }
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
    h[0] = SIM_MAGIC; h[1] = n; h[2] = sim_crc((const uint8_t *)p, n);
    if (!sim_step()) return -1;
    memset(simf[w], 0xFF, 4096); sim_erases++;
    if (!sim_step()) { memcpy(simf[w] + 12, p, n / 2); return -1; }   /* cut halfway through the bytes */
    memcpy(simf[w] + 12, p, n);
    if (!sim_step()) return -1;
    memcpy(simf[w], h, 12);
    return 0;
}
#include "poli_preset.c"
static uint32_t pv_bank_v1_bytes(void) { return 12u + (uint32_t)PV_NSLOT_V1 * PV_REC; }
/* write a bank payload of n bytes (3212 for a 50-slot v1 bank, 4044 for 63) */
static void plant_bank(int w, const void *p, uint32_t n)
{
    uint32_t h[3];
    memset(simf[w], 0xFF, 4096);
    h[0] = SIM_MAGIC; h[1] = n; h[2] = sim_crc((const uint8_t *)p, n);
    memcpy(simf[w] + 12, p, n);
    memcpy(simf[w], h, 12);
}
/* 8-byte settings: ver, last, midi_ch, rsv[5]. midi_ch 0 = old record (default 1).
 * rsv[0] is key-track mode: 0 = old record -> 62%, 1 = OFF, 2 = 62%, 3 = 100%. */
static void plant_settings(uint8_t ver, uint8_t last, uint8_t midi_ch)
{
    uint8_t s[8];
    memset(s, 0, sizeof s);
    s[0] = ver; s[1] = last; s[2] = midi_ch;
    plant_bank(3, s, 8);
}
static void plant_settings_kt(uint8_t ver, uint8_t last, uint8_t midi_ch, uint8_t kt_mode)
{
    uint8_t s[8];
    memset(s, 0, sizeof s);
    s[0] = ver; s[1] = last; s[2] = midi_ch; s[3] = kt_mode;
    plant_bank(3, s, 8);
}
#include "pv_factory_law3.inc"
#include "pv_factory_law4.inc"

/* Reference for the law-2/law-3 -> law-4 migration, in double precision from
   the documented times (not the firmware's integer midpoint search):
   straight line: span / rate; curve: attack N(q), decay N(q) ln(1000)/K,
   release N(q) ln((S+26)/26)/K, K = ln(26026/26); the same 1 % edge rules. */
static double ref_nfull(int q) { return 21.0 * pow(441000.0 / 21.0, q / 127.0); }
static double ref_nfull_a(int q) { return 5.5 * pow(441000.0 / 5.5, q / 127.0); }
static double ref_lin(double rate_q15, int kind, int32_t s, double *crv_k)
{
    double kdr = log(26026.0 / 26.0), span = 26000.0;
    *crv_k = 1.0;
    if (kind == 1 && s < 26000 - 260) { span = 26000.0 - s; *crv_k = log(1000.0) / kdr; }
    if (kind == 2 && s >= 260) { span = s; *crv_k = log((s + 26.0) / 26.0) / kdr; }
    return span * 32768.0 / rate_q15;
}
/* closest q; *tie = 1 when the runner-up is within 0.05 % in log time */
static int ref_conv(double rate_q15, int kind, int sus_pos, int *tie, double *err)
{
    double k, t = ref_lin(rate_q15, kind, scale_u7(sus_pos, 0, 28000), &k), best = 1e9, second = 1e9;
    int q, bq = 0;
    for (q = 0; q < 128; q++) {
        double e = fabs(log((kind == 0 ? ref_nfull_a(q) : ref_nfull(q)) * k / t));
        if (e < best) { second = best; best = e; bq = q; } else if (e < second) second = e;
    }
    *tie = second - best < 5e-4;
    /* error only where the old time is inside the curved range; outside it the end position is the answer */
    if (err) *err = (t < (kind == 0 ? ref_nfull_a(0) : ref_nfull(0)) * k || t > ref_nfull(127) * k) ? -1.0 : exp(best) - 1.0;
    return bq;
}
static double ref_rate(int law, int p) { return law == 2 ? ENV_LAW2_RATE[p] * 32768.0 : (double)ENV_LAW3_RATE[p]; }
/* record k (law 2/3 positions) -> expected law-4 knob bytes; returns mismatches beyond near-ties */
static int ref_rec_ok(int law, const uint8_t *old, const uint8_t *got)
{
    static const int idx[6] = { 4, 5, 7, 8, 9, 11 };
    int j, bad = 0;
    for (j = 0; j < 6; j++) {
        int tie, kind = j % 3, want4 = ref_conv(ref_rate(law, old[idx[j]]), kind, old[j < 3 ? 6 : 10], &tie, NULL);
        int want = poli_law5_env((uint8_t)want4, kind);
        int g = got[idx[j]];
        /* a law-4 near-tie may land on want4±1; run both through law 5 */
        if (g != want) {
            int a = poli_law5_env((uint8_t)(want4 + 1), kind), b = poli_law5_env((uint8_t)(want4 ? want4 - 1 : 0), kind);
            if (!(tie && (g == a || g == b))) bad++;
        }
    }
    if (got[0] != poli_law5_cut(old[0])) bad++;
    return bad == 0;
}

static int fail, pass;
static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (ok) pass++; else fail++;
}

static void boot(void) { sim_dead = 0; sim_cut_at = -1; poli_nav_init(); poli_preset_init(0); }
static int32_t render_peak(int n, long *clamps, int32_t *maxstep, int32_t *last)
{
    static int32_t buf[2 * 256];
    int32_t pk = 0, prev = last ? *last : 0;
    while (n > 0) {
        int k = n > 256 ? 256 : n, i;
        memset(buf, 0, sizeof buf);
        polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        polivoks_render(buf, (uint32_t)k);
        for (i = 0; i < k; i++) {
            int32_t y = buf[2 * i], a = y < 0 ? -y : y, d = y - prev;
            if (d < 0) d = -d;
            if (maxstep && d > *maxstep) *maxstep = d;
            if (a > pk) pk = a;
            if (clamps && a >= 30000) (*clamps)++;
            prev = y;
        }
        n -= k;
    }
    if (last) *last = prev;
    return pk;
}
static void get_patch(uint8_t k[31], uint8_t *sw) { poli_nav_patch_get(k, sw); }
static int view_dirty(void) { poli_preset_view_t v; poli_preset_view(&v); return v.dirty; }
static int view_cur(void) { poli_preset_view_t v; poli_preset_view(&v); return v.cur; }
static void load_slot(int s)
{
    int d = s - view_cur();
    poli_preset_turn(d ? d : PV_NSLOT, 0);
    poli_preset_sel();
}
static int bank_eq(const pv_bank_t *a, const pv_bank_t *b) { return memcmp(a->rec, b->rec, sizeof a->rec) == 0; }

int main(void)
{
    char d[256];
    int i, ok;
    uint8_t k[31], sw;
    sim_reset();

    /* ---- factory first boot */
    boot();
    snprintf(d, sizeof d, "cur %d (0 = 01 Init), newest copy %d (none), writes A %ld B %ld S %ld", view_cur(), pv_newest,
             sim_writes[0], sim_writes[1], sim_writes[3]);
    expect("first boot: slot 01, no flash write at boot", view_cur() == 0 && pv_newest == -1 &&
           sim_writes[0] + sim_writes[1] + sim_writes[2] + sim_writes[3] == 0, d);
    get_patch(k, &sw);
    expect("first boot: Init positions = merged boot positions",
           k[0] == 59 && k[1] == 25 && k[2] == 32 && k[3] == 54 && k[4] == 0 && k[5] == 0 && k[6] == 64 && k[7] == 4 &&
           k[8] == 0 && k[9] == 0 && k[10] == 64 && k[11] == 4 &&
           k[12] == 4 && k[14] == 0 && k[20] == 0 && k[25] == 58 && k[29] == 5 && k[30] == 1 && sw == 0xA8,
           "CUT 59 RES 25 ... envelope 0/0/64/4 (law 5; law 4 was 51 and 27/31/64/39) ... sw LINK+BASS+MODOSC");
    expect("first boot: not edited", !view_dirty(), "no asterisk");

    /* ---- each of the factory presets */
    {
        int bad_rt = 0, bad_dirty = 0, bad_range = 0, bad_eng = 0;
        for (i = 0; i < PV_NSLOT; i++) {
            int j;
            load_slot(i);
            get_patch(k, &sw);
            if (memcmp(k, PV_FACTORY[i].k, 31) || sw != PV_FACTORY[i].sw) bad_rt++;
            if (view_dirty()) bad_dirty++;
            for (j = 0; j < 31; j++) if (k[j] > poli_nav_patch_max(j)) bad_range++;
            if (poli_cut < 0 || poli_cut > 30000 || poli_res < 0 || poli_res > 30000 || poli_drv < 2000 || poli_drv > 30000 ||
                poli_atk_a < 1 || poli_atk_a > (8000 << 15) || poli_rel_a < 1 || poli_rel_f < 1 || poli_sus_a < 0 || poli_sus_a > (28000 << 15) ||
                poli_shape > 4 || poli_shape2 > 4 || poli_lfo_shape > 3 || poli_keyhold) bad_eng++;
        }
        snprintf(d, sizeof d, "%d loads: patch mismatch %d, shown edited %d, stored byte out of range %d, engine out of range %d",
                 PV_NSLOT, bad_rt, bad_dirty, bad_range, bad_eng);
        expect("load each factory slot: positions, switches, ranges", !bad_rt && !bad_dirty && !bad_range && !bad_eng, d);
        for (i = 0, ok = 1; i < PV_NSLOT; i++) if (!pv_rec_ok(&pv_bank.rec[i])) ok = 0;
        expect("all factory records pass their CRC and range check", ok, "pv_rec_ok");
        for (i = 0, ok = 1; i < PV_NSLOT; i++) {
            int j; for (j = 0; j < PV_NAME; j++) if (PV_FACTORY[i].name[j] >= TN_N) ok = 0;
            if (PV_FACTORY[i].name[0] == 0) ok = 0;
        }
        expect("factory names: codes inside the 145-glyph face, none empty", ok, "TN_N 145");
    }

    /* ---- dirty flag (#12) */
    load_slot(4);
    poli_nav_section(NAV_SEC_FX);
    poli_nav_knob(0, 3);
    i = view_dirty();
    poli_nav_knob(0, -3);
    expect("asterisk on edit, off when turned back", i == 1 && view_dirty() == 0, "CUTOFF +3 then -3");
    poli_nav_select_press();       /* LP/BP */
    i = view_dirty();
    poli_nav_select_press();
    expect("asterisk on a switch change", i == 1 && view_dirty() == 0, "LP/BP twice");

    /* ---- save/recall round trip, power cycle (#4, #5) */
    sim_reset(); boot();
    load_slot(6);
    poli_nav_section(NAV_SEC_FX);
    poli_nav_knob(0, 10); poli_nav_knob(1, -5); poli_nav_select_press();
    get_patch(k, &sw);
    poli_preset_save_btn(0);
    poli_preset_turn(1, 0);            /* target 08 */
    poli_preset_knob(0, 0); poli_preset_knob(1, 2);     /* first letter +2 */
    poli_preset_save_btn(1000);        /* SAVE: no confirm (#4) */
    {
        poli_preset_view_t v; poli_preset_view(&v);
        snprintf(d, sizeof d, "mode %d cur %d msg %d, A writes %ld B %ld", v.mode, v.cur, v.msg, sim_writes[0], sim_writes[1]);
        expect("SAVE writes at once, no warning, to the target", v.mode == PV_MODE_NONE && v.cur == 7 && v.msg == PV_MSG_SAVED &&
               sim_writes[0] == 1 && sim_writes[1] == 0, d);
    }
    poli_preset_tick(5000);
    expect("last used slot written once, in silence", sim_writes[3] == 1, "settings write after SAVE");
    {
        uint8_t k2[31], sw2; uint8_t nm[PV_NAME];
        memcpy(nm, &pv_bank.rec[7].b[4], PV_NAME);
        boot();                                   /* power cycle */
        get_patch(k2, &sw2);
        snprintf(d, sizeof d, "boot cur %d, patch %s, name %s", view_cur(), memcmp(k, k2, 31) || sw != sw2 ? "DIFF" : "same",
                 memcmp(nm, &pv_bank.rec[7].b[4], PV_NAME) ? "DIFF" : "same");
        expect("power cycle: boots into 08 with the saved sound and name (#5)", view_cur() == 7 && !memcmp(k, k2, 31) && sw == sw2 &&
               !memcmp(nm, &pv_bank.rec[7].b[4], PV_NAME) && !view_dirty(), d);
        load_slot(2); load_slot(7);
        get_patch(k2, &sw2);
        expect("recall after another slot gives the same patch", !memcmp(k, k2, 31) && sw == sw2, "08 -> 03 -> 08");
        expect("slot 07 kept its factory sound (save went to 08)", bank_eq(&pv_bank, &pv_bank) &&
               !memcmp(&pv_bank.rec[6].b[18], PV_FACTORY[6].k, 31), "rec 6 == factory 6");
    }

    /* ---- A/B alternation and fallback */
    {
        uint32_t s0 = pv_bank.seq; int n0 = pv_newest;
        poli_preset_save_btn(0); poli_preset_save_btn(0);
        snprintf(d, sizeof d, "seq %u -> %u, copy %d -> %d", s0, pv_bank.seq, n0, pv_newest);
        expect("each save goes to the other copy, seq + 1", pv_bank.seq == s0 + 1 && pv_newest != n0, d);
        simf[pv_newest][20] ^= 0x55u;              /* spoil the newest copy */
        boot();
        snprintf(d, sizeof d, "after spoiling copy %d: seq %u newest %d", 1 - n0 == pv_newest ? n0 : n0, pv_bank.seq, pv_newest);
        expect("a spoiled newest copy falls back to the older one", pv_bank.seq == s0 && pv_newest == n0, d);
    }

    /* ---- power cut at every step of a bank save */
    {
        int cuts = 0, okc = 0, step;
        for (step = 0; step < 4; step++) {
            pv_bank_t before, after;
            sim_reset(); boot();
            load_slot(3); poli_nav_section(NAV_SEC_FX); poli_nav_knob(0, 7);
            poli_preset_save_btn(0); poli_preset_save_btn(0);        /* first save: copy A */
            memcpy(&before, &pv_bank, sizeof before);
            poli_nav_knob(0, 9);
            sim_ops = 0; sim_cut_at = step;
            poli_preset_save_btn(0); poli_preset_save_btn(0);        /* second save, cut at step */
            memcpy(&after, &pv_bank, sizeof after);
            boot();
            cuts++;
            if (step < 3 ? bank_eq(&pv_bank, &before) : bank_eq(&pv_bank, &after)) okc++;
        }
        snprintf(d, sizeof d, "%d cut points (before erase, mid-bytes, before header, none): %d gave the old or the new bank whole", cuts, okc);
        expect("power cut during a save never mixes or loses the bank", okc == cuts, d);
    }

    /* ---- backup, factory reset, restore (#6, #7) */
    {
        pv_bank_t mine;
        sim_reset(); boot();
        poli_preset_save_btn(0); poli_preset_knob(3, 2); poli_preset_save_btn(0);     /* RESTORE, none yet */
        { poli_preset_view_t v; poli_preset_view(&v); expect("restore with no backup says so, bank unchanged", v.msg == PV_MSG_NOBACKUP, "НЕТ КОПИИ"); }
        load_slot(9); poli_nav_section(NAV_SEC_FX); poli_nav_knob(1, 20);
        poli_preset_save_btn(0); poli_preset_save_btn(0);                             /* save into 10 */
        memcpy(&mine, &pv_bank, sizeof mine);
        poli_preset_save_btn(0); poli_preset_knob(3, 1); poli_preset_save_btn(0);     /* BACKUP */
        { poli_preset_view_t v; poli_preset_view(&v); expect("backup of all slots written to its own sector", v.msg == PV_MSG_BACKED && sim_writes[2] == 1, "blob 5"); }
        poli_preset_save_btn(0); poli_preset_knob(3, 3); poli_preset_save_btn(0);     /* FACTORY */
        for (i = 0, ok = 1; i < PV_NSLOT; i++) if (memcmp(&pv_bank.rec[i].b[18], PV_FACTORY[i].k, 31)) ok = 0;
        get_patch(k, &sw);
        expect("factory reset: all slots back to the factory sounds (#6)", ok && !memcmp(k, PV_FACTORY[9].k, 31), "and slot 10 reloaded");
        boot();
        for (i = 0, ok = 1; i < PV_NSLOT; i++) if (memcmp(&pv_bank.rec[i].b[18], PV_FACTORY[i].k, 31)) ok = 0;
        expect("factory reset survives a power cycle", ok, "bank from flash");
        poli_preset_save_btn(0); poli_preset_knob(3, 2); poli_preset_save_btn(0);     /* RESTORE */
        expect("restore brings back the backed-up bank", bank_eq(&pv_bank, &mine), "slot 10 edit is back");
        boot();
        expect("restore survives a power cycle", bank_eq(&pv_bank, &mine), "bank from flash");
    }

    /* ---- compare (#10) */
    {
        uint8_t ke[31], swe, ks[31], sws;
        sim_reset(); boot();
        load_slot(12);
        poli_nav_section(NAV_SEC_FX); poli_nav_knob(0, 15); poli_nav_select_press();
        get_patch(ke, &swe);
        poli_preset_save_btn(0);
        poli_preset_sel();
        get_patch(ks, &sws);
        ok = !memcmp(ks, PV_FACTORY[12].k, 31) && sws == PV_FACTORY[12].sw;
        poli_preset_sel();
        get_patch(k, &sw);
        ok = ok && !memcmp(k, ke, 31) && sw == swe;
        poli_preset_sel(); poli_preset_cancel();
        get_patch(k, &sw);
        ok = ok && !memcmp(k, ke, 31) && sw == swe && view_dirty();
        expect("compare: SEL plays stored, SEL again the edit; cancel returns the edit", ok, "save screen SEL");
        poli_preset_save_btn(0); poli_preset_sel(); poli_preset_save_btn(0);
        get_patch(k, &sw);
        expect("SAVE while comparing stores the edit, not the stored one", !memcmp(&pv_bank.rec[12].b[18], ke, 31) && !memcmp(k, ke, 31) && !view_dirty(), "rec 13 == edit");
    }

    /* ---- names, save-screen controls (#3) */
    {
        poli_preset_view_t v;
        poli_preset_save_btn(0);
        poli_preset_knob(0, -20); poli_preset_view(&v); ok = v.cursor == 0;
        poli_preset_knob(0, 40); poli_preset_view(&v); ok = ok && v.cursor == 11;
        poli_preset_knob(1, -1); poli_preset_view(&v); ok = ok && v.edit_name[11] == TN_N - 1;
        poli_preset_knob(1, 1); poli_preset_view(&v); ok = ok && v.edit_name[11] == 0;
        poli_preset_knob(2, 3); poli_preset_view(&v); ok = ok && v.edit_name[11] == TN_GROUP[3];
        poli_preset_knob(2, -4); poli_preset_view(&v); ok = ok && v.edit_name[11] == TN_GROUP[TN_NGROUP - 1];
        for (i = 0; i < 5000; i++) { poli_preset_knob(1, (i % 7) - 3); poli_preset_knob(2, (i % 3) - 1); poli_preset_knob(0, (i % 5) - 2); poli_preset_knob(3, (i % 9) - 4); }
        poli_preset_view(&v);
        for (i = 0; i < PV_NAME; i++) if (v.edit_name[i] >= TN_N) ok = 0;
        ok = ok && v.action >= 0 && v.action < PV_NACT && v.cursor >= 0 && v.cursor < PV_NAME;
        poli_preset_cancel();
        expect("name editor: cursor clamps, letters and groups wrap, codes stay in the face", ok, "5000 random turns");
        {
            uint8_t nm[PV_NAME] = { 1, 27, 53, 86, 59, 92, 119, 129 + 5, 0, 0, 0, 0 }; char u[40];
            poli_preset_name_utf8(nm, u, 1);
            expect("name to UTF-8: A a А а Ё ё 0 *", !strcmp(u, "AaАаЁё0*"), u);
        }
    }

    /* ---- bar: timeout, cancel, SEL loads (#11) */
    {
        poli_preset_view_t v;
        int c0 = view_cur();
        poli_preset_turn(3, 100); poli_preset_tick(100 + PV_BAR_MS + 1); poli_preset_view(&v);
        ok = v.mode == PV_MODE_NONE && v.cur == c0;
        poli_preset_turn(3, 0); poli_preset_cancel(); poli_preset_view(&v);
        ok = ok && v.mode == PV_MODE_NONE && v.cur == c0;
        poli_preset_turn(-1, 0); poli_preset_turn(-1, 0); poli_preset_sel(); poli_preset_view(&v);
        ok = ok && v.cur == (c0 + PV_NSLOT - 2) % PV_NSLOT;
        poli_preset_turn(-(c0 + 10), 0); poli_preset_view(&v);
        ok = ok && v.target == (v.cur - (c0 + 10) + 2 * PV_NSLOT) % PV_NSLOT;
        poli_preset_cancel();
        expect("twist does not load; SEL loads; timeout and cancel keep the sound", ok, "bar 4 s");
    }

    /* ---- last used: deferred to silence, not rewritten when equal */
    {
        long w0;
        sim_reset(); boot();
        polivoks_midi(0x90, 60, 100);
        render_peak(512, 0, 0, 0);
        load_slot(5);
        w0 = sim_writes[3];
        poli_preset_tick(10000); render_peak(256, 0, 0, 0); poli_preset_tick(20000);
        ok = sim_writes[3] == w0;
        polivoks_midi(0x80, 60, 0);
        for (i = 0; i < 200; i++) { render_peak(441, 0, 0, 0); poli_preset_tick(30000 + (uint32_t)i * 10u); }
        ok = ok && sim_writes[3] == w0 + 1;
        load_slot(5); poli_preset_tick(90000);
        ok = ok && sim_writes[3] == w0 + 1;
        expect("last-used write waits for silence; same slot is not rewritten", ok, "settings writes");
        boot();
        expect("boot reads last used", view_cur() == 5, "slot 06");
        {   /* an old 1-byte VOLT record or junk -> slot 01 */
            uint32_t h[3] = { SIM_MAGIC, 1u, 0 }; uint8_t one = 0;
            memset(simf[3], 0xFF, 4096); h[2] = sim_crc(&one, 1); memcpy(simf[3], h, 12); simf[3][12] = 0;
            boot();
            expect("old 1-byte settings record: boots 01, no crash", view_cur() == 0, "length check");
        }
    }

    /* ---- bad records fall back per slot; an unknown law is not read as a sound (step B build) */
    {
        sim_reset(); boot();
        load_slot(20); poli_nav_section(NAV_SEC_FX); poli_nav_knob(0, 4);
        poli_preset_save_btn(0); poli_preset_save_btn(0);          /* bank in copy A, slot 21 edited */
        expect("slot 21 holds a user edit before the damage", memcmp(&pv_bank.rec[20].b[18], PV_FACTORY[20].k, 31) != 0, "CUTOFF +4");
        {   /* spoil record 20 inside the stored payload, keep the blob CRC valid */
            pv_bank_t b; uint32_t h[3];
            memcpy(&b, simf[0] + 12, sizeof b);
            b.rec[20].b[18] ^= 1u;                                    /* record CRC now wrong */
            b.rec[21].b[2] = 9u;                                      /* unknown law, fix CRC */
            { uint32_t c = pv_crc32(b.rec[21].b, 60); b.rec[21].b[60] = (uint8_t)c; b.rec[21].b[61] = (uint8_t)(c >> 8); b.rec[21].b[62] = (uint8_t)(c >> 16); b.rec[21].b[63] = (uint8_t)(c >> 24); }
            memcpy(simf[0] + 12, &b, sizeof b);
            memcpy(h, simf[0], 12); h[2] = sim_crc(simf[0] + 12, sizeof b); memcpy(simf[0], h, 12);
        }
        boot();
        expect("a record with a bad CRC falls back to its factory sound only", !memcmp(&pv_bank.rec[20].b[18], PV_FACTORY[20].k, 31) &&
               !memcmp(&pv_bank.rec[21].b[18], PV_FACTORY[21].k, 31) && pv_bank.rec[21].b[2] == PV_LAW && pv_bank.rec[19].b[2] == PV_LAW,
               "rec 21 factory, rec 22 (law 9, unknown) factory too, not misread; others law 5");
    }

    /* ---- curved envelope: law-2 (step A) and law-3 (step B) records from earlier builds are converted on load */
    {
        pv_bank_t b; int j, conv = 1, keep = 1, crc = 1, idx[6] = { 4, 5, 7, 8, 9, 11 };
        uint8_t old_k[PV_NSLOT_V1][31], new_k[31];
        uint32_t v1 = pv_bank_v1_bytes();
        sim_reset(); boot();
        /* a bank as the brief-mute build wrote it: 50 records, law 2, user times spread over 0..127 */
        memset(&b, 0, sizeof b);
        for (i = 0; i < PV_NSLOT_V1; i++) {
            pv_rec_t *rr = &b.rec[i];
            for (j = 0; j < 31; j++) old_k[i][j] = PV_FACTORY_LAW4[i][j];
            for (j = 0; j < 6; j++) old_k[i][idx[j]] = (uint8_t)((i * 13 + j * 29) & 127);
            old_k[i][6] = (uint8_t)((i * 37) & 127); old_k[i][10] = (uint8_t)((i * 53 + 5) & 127);
            pv_rec_make(rr, PV_FACTORY[i].name, PV_FACTORY[i].sw, old_k[i]);
            rr->b[2] = 2u;
            { uint32_t c = pv_crc32(rr->b, 60); rr->b[60] = (uint8_t)c; rr->b[61] = (uint8_t)(c >> 8); rr->b[62] = (uint8_t)(c >> 16); rr->b[63] = (uint8_t)(c >> 24); }
        }
        b.magic = PV_BANK_MAGIC; b.layout = PV_LAYOUT; b.nrec = PV_NSLOT_V1; b.seq = 7;
        plant_bank(0, &b, v1);
        { pv_bank_t back = b; back.magic = PV_BACK_MAGIC; plant_bank(2, &back, v1); }
        boot();
        for (i = 0; i < PV_NSLOT_V1; i++) {
            const pv_rec_t *rr = &pv_bank.rec[i];
            if (!pv_rec_ok(rr) || rr->b[2] != PV_LAW) crc = 0;
            if (!ref_rec_ok(2, old_k[i], &rr->b[18])) conv = 0;
            for (j = 0; j < 31; j++) {
                int jj, env = 0;
                for (jj = 0; jj < 6; jj++) if (idx[jj] == j) env = 1;
                if (j == 0) continue; /* cutoff is remapped in law 5 */
                if (!env && rr->b[18 + j] != old_k[i][j]) keep = 0;
            }
            if (memcmp(&rr->b[4], PV_FACTORY[i].name, PV_NAME) || rr->b[16] != (PV_FACTORY[i].sw & 0xBFu)) keep = 0;
        }
        for (i = PV_NSLOT_V1, ok = 1; i < PV_NSLOT; i++)
            if (memcmp(&pv_bank.rec[i].b[18], PV_FACTORY[i].k, 31) || pv_bank.rec[i].b[16] != PV_FACTORY[i].sw) ok = 0;
        snprintf(d, sizeof d, "50 law-2 records: closest law-5 times/cutoff %s, other knobs/name/switches kept %s, law 5 + valid CRC %s, slots 51-63 factory %s, expanded bank written %s",
                 conv ? "yes" : "NO", keep ? "yes" : "NO", crc ? "yes" : "NO", ok ? "yes" : "NO",
                 sim_writes[0] + sim_writes[1] == 1 ? "yes" : "NO");
        expect("step A 50-slot user bank is converted to law-5 on load; 51-63 filled from factory and written back",
               conv && keep && crc && ok && sim_writes[0] + sim_writes[1] == 1, d);
        {   /* the integer conversion picks the closest end time: every law-2 and law-3 position x 8 sustains x 3 stages */
            static const int SP[8] = { 0, 1, 2, 10, 64, 100, 118, 127 };
            int law, si, kind, p, nbad = 0, nties = 0, nout = 0;
            double worst = 0, e;
            for (law = 2; law <= 3; law++)
                for (si = 0; si < 8; si++)
                    for (kind = 0; kind < 3; kind++)
                        for (p = 0; p < 128; p++) {
                            int tie, want = ref_conv(ref_rate(law, p), kind, SP[si], &tie, &e);
                            int got = pv_env_conv((uint32_t)(law == 2 ? (uint32_t)ENV_LAW2_RATE[p] << 15 : ENV_LAW3_RATE[p]), kind, (uint8_t)SP[si]);
                            if (got != want) { if (tie && abs(got - want) == 1) nties++; else nbad++; }
                            if (e < 0) nout++; else if (e > worst) worst = e;
                        }
            snprintf(d, sizeof d, "6144 cases: %d not closest, %d near-ties to neighbour, %d out of curved range (clamped); worst in-range error %.2f %%; step B 32/55/58 @sus 64 -> %d/%d/%d",
                     nbad, nties, nout, worst * 100.0, pv_env_conv(ENV_LAW3_RATE[32], 0, 64), pv_env_conv(ENV_LAW3_RATE[55], 1, 64), pv_env_conv(ENV_LAW3_RATE[58], 2, 64));
            expect("law-2/law-3 conversion is the closest end time", nbad == 0 && pv_env_conv(ENV_LAW3_RATE[32], 0, 64) == ENV_POS_ATK
                   && pv_env_conv(ENV_LAW3_RATE[55], 1, 64) == ENV_POS_DEC && pv_env_conv(ENV_LAW3_RATE[58], 2, 64) == ENV_POS_REL, d);
        }
        load_slot(3); get_patch(new_k, &sw);
        expect("a converted slot loads with its curved positions and is not shown edited",
               !memcmp(new_k, &pv_bank.rec[3].b[18], 31) && !view_dirty(), "slot 04");
        /* a save writes law 4 */
        {
            uint8_t want0 = pv_bank.rec[3].b[18];
            poli_nav_section(NAV_SEC_FX); poli_nav_knob(0, 1);
            poli_preset_save_btn(0); poli_preset_save_btn(0);
            boot();
            expect("after a save the bank on flash is law 5 and reads back the same",
                   pv_bank.rec[0].b[2] == PV_LAW && pv_bank.rec[3].b[2] == PV_LAW && pv_bank.rec[3].b[18] == (uint8_t)(want0 + 1), "re-boot");
        }
        /* RESTORE of a step A backup converts it too */
        poli_preset_save_btn(0);
        for (i = 0; i < PV_ACT_RESTORE; i++) poli_preset_knob(3, 1);
        poli_preset_save_btn(0);
        for (i = 0, conv = 1; i < PV_NSLOT_V1; i++) {
            if (pv_bank.rec[i].b[2] != PV_LAW || !pv_rec_ok(&pv_bank.rec[i])) conv = 0;
            if (!ref_rec_ok(2, old_k[i], &pv_bank.rec[i].b[18])) conv = 0;
        }
        for (i = PV_NSLOT_V1; i < PV_NSLOT; i++)
            if (memcmp(&pv_bank.rec[i].b[18], PV_FACTORY[i].k, 31)) conv = 0;
        expect("RESTORE of a step A 50-slot backup converts 1-50 and fills 51-63 from factory", conv, "63 records");
    }
    /* ---- the EnvB-Oct3 (law 3) bank, e.g. its 50 factory records, converts to exactly this build's factory table */
    {
        pv_bank_t b; int j, same = 1, crc = 1, ref = 1;
        uint32_t v1 = pv_bank_v1_bytes(); int filled = 1;
        sim_reset(); boot();
        memset(&b, 0, sizeof b);
        for (i = 0; i < PV_NSLOT_V1; i++) {
            pv_rec_t *rr = &b.rec[i];
            pv_rec_make(rr, PV_FACTORY[i].name, PV_FACTORY[i].sw, PV_FACTORY_LAW3[i]);
            rr->b[2] = 3u;
            { uint32_t c = pv_crc32(rr->b, 60); rr->b[60] = (uint8_t)c; rr->b[61] = (uint8_t)(c >> 8); rr->b[62] = (uint8_t)(c >> 16); rr->b[63] = (uint8_t)(c >> 24); }
        }
        b.magic = PV_BANK_MAGIC; b.layout = PV_LAYOUT; b.nrec = PV_NSLOT_V1; b.seq = 9;
        plant_bank(0, &b, v1);
        boot();
        for (i = 0; i < PV_NSLOT_V1; i++) {
            if (!pv_rec_ok(&pv_bank.rec[i]) || pv_bank.rec[i].b[2] != PV_LAW) crc = 0;
            for (j = 0; j < 31; j++) if (pv_bank.rec[i].b[18 + j] != PV_FACTORY[i].k[j]) same = 0;
            if (!ref_rec_ok(3, PV_FACTORY_LAW3[i], &pv_bank.rec[i].b[18])) ref = 0;
        }
        for (i = PV_NSLOT_V1; i < PV_NSLOT; i++)
            if (memcmp(&pv_bank.rec[i].b[18], PV_FACTORY[i].k, 31) || pv_bank.rec[i].b[16] != PV_FACTORY[i].sw) filled = 0;
        snprintf(d, sizeof d, "50 law-3 records -> law 5: equal to PV_FACTORY %s, closest end time then law-5 pick %s, valid %s, 51-63 factory %s, written back %s",
                 same ? "yes" : "NO", ref ? "yes" : "NO", crc ? "yes" : "NO", filled ? "yes" : "NO",
                 sim_writes[0] + sim_writes[1] == 1 ? "yes" : "NO");
        expect("step B (EnvB-Oct3) 50-slot bank converts on load; 51-63 filled from factory and written back",
               same && ref && crc && filled && sim_writes[0] + sim_writes[1] == 1, d);
    }
    /* ---- a Fix1 / EnvRC (law 4) bank converts to this factory set ---- */
    {
        pv_bank_t b; int j, same = 1, crc = 1, pick = 1;
        uint32_t v1 = pv_bank_v1_bytes(); int filled = 1;
        sim_reset(); boot();
        memset(&b, 0, sizeof b);
        for (i = 0; i < PV_NSLOT_V1; i++) {
            pv_rec_t *rr = &b.rec[i];
            pv_rec_make(rr, PV_FACTORY[i].name, PV_FACTORY[i].sw, PV_FACTORY_LAW4[i]);
            rr->b[2] = PV_LAW_RC;
            { uint32_t c = pv_crc32(rr->b, 60); rr->b[60] = (uint8_t)c; rr->b[61] = (uint8_t)(c >> 8); rr->b[62] = (uint8_t)(c >> 16); rr->b[63] = (uint8_t)(c >> 24); }
        }
        b.magic = PV_BANK_MAGIC; b.layout = PV_LAYOUT; b.nrec = PV_NSLOT_V1; b.seq = 11;
        plant_bank(0, &b, v1);
        boot();
        for (i = 0; i < PV_NSLOT_V1; i++) {
            if (!pv_rec_ok(&pv_bank.rec[i]) || pv_bank.rec[i].b[2] != PV_LAW) crc = 0;
            for (j = 0; j < 31; j++) if (pv_bank.rec[i].b[18 + j] != PV_FACTORY[i].k[j]) same = 0;
            if (pv_bank.rec[i].b[18] != poli_law5_cut(PV_FACTORY_LAW4[i][0])) pick = 0;
            { static const int idx[6] = { 4, 5, 7, 8, 9, 11 }; int j2;
              for (j2 = 0; j2 < 6; j2++)
                  if (pv_bank.rec[i].b[18 + idx[j2]] != poli_law5_env(PV_FACTORY_LAW4[i][idx[j2]], j2 % 3)) pick = 0; }
        }
        for (i = PV_NSLOT_V1; i < PV_NSLOT; i++)
            if (memcmp(&pv_bank.rec[i].b[18], PV_FACTORY[i].k, 31) || pv_bank.rec[i].b[16] != PV_FACTORY[i].sw) filled = 0;
        snprintf(d, sizeof d, "50 law-4 records -> law 5: equal to PV_FACTORY %s, floor/ceil pick %s, valid %s, 51-63 factory %s, written back %s",
                 same ? "yes" : "NO", pick ? "yes" : "NO", crc ? "yes" : "NO", filled ? "yes" : "NO",
                 sim_writes[0] + sim_writes[1] == 1 ? "yes" : "NO");
        expect("EnvRC/Fix1 (law 4) 50-slot bank converts on load; 51-63 filled from factory and written back",
               same && pick && crc && filled && sim_writes[0] + sim_writes[1] == 1, d);
    }


    /* ---- a law-5 50-slot bank with user edits: keep 1-50, fill 51-63, write back, survive power cycle and a cut */
    {
        pv_bank_t b, after;
        uint8_t edit9[31], edit20[31];
        uint32_t v1 = pv_bank_v1_bytes();
        int kept = 1, filled = 1, step, cuts = 0, okc = 0;
        expect("63-slot payload fits the 4 KiB blob", sizeof(pv_bank_t) == 4044u && sizeof(pv_bank_t) <= 4084u && v1 == 3212u,
               "12+63*64=4044, 12+50*64=3212, SECTOR_MAX 4084");
        sim_reset(); boot();
        /* start from this firmware's first 50 factory sounds (already law 5), then edit 10 and 21 */
        memset(&b, 0, sizeof b);
        for (i = 0; i < PV_NSLOT_V1; i++)
            pv_factory_rec(i, &b.rec[i]);
        memcpy(edit9, PV_FACTORY[9].k, 31); edit9[0] = (uint8_t)(edit9[0] < 100 ? edit9[0] + 11 : edit9[0] - 11);
        memcpy(edit20, PV_FACTORY[20].k, 31); edit20[1] = (uint8_t)(edit20[1] < 80 ? edit20[1] + 17 : edit20[1] - 17);
        pv_rec_make(&b.rec[9], PV_FACTORY[9].name, PV_FACTORY[9].sw, edit9);
        pv_rec_make(&b.rec[20], PV_FACTORY[20].name, (uint8_t)(PV_FACTORY[20].sw ^ 1u), edit20); /* flip LP/BP */
        b.magic = PV_BANK_MAGIC; b.layout = PV_LAYOUT; b.nrec = PV_NSLOT_V1; b.seq = 21;
        plant_bank(0, &b, v1);
        boot();
        for (i = 0; i < PV_NSLOT_V1; i++) {
            if (i == 9) {
                if (memcmp(&pv_bank.rec[i].b[18], edit9, 31)) kept = 0;
            } else if (i == 20) {
                if (memcmp(&pv_bank.rec[i].b[18], edit20, 31) || pv_bank.rec[i].b[16] != (uint8_t)(PV_FACTORY[20].sw ^ 1u)) kept = 0;
            } else if (memcmp(&pv_bank.rec[i].b[18], PV_FACTORY[i].k, 31) || pv_bank.rec[i].b[16] != PV_FACTORY[i].sw) kept = 0;
        }
        for (i = PV_NSLOT_V1; i < PV_NSLOT; i++)
            if (memcmp(&pv_bank.rec[i].b[18], PV_FACTORY[i].k, 31) || pv_bank.rec[i].b[16] != PV_FACTORY[i].sw) filled = 0;
        snprintf(d, sizeof d, "kept 1-50 %s (edits on 10 and 21), 51-63 factory %s, one write-back %s, nrec 63, seq 22",
                 kept ? "yes" : "NO", filled ? "yes" : "NO", sim_writes[0] + sim_writes[1] == 1 ? "yes" : "NO");
        expect("law-5 50-slot bank with user edits: slots 1-50 kept, 51-63 factory, written back once",
               kept && filled && sim_writes[0] + sim_writes[1] == 1 && pv_bank.nrec == PV_NSLOT && pv_bank.seq == 22, d);
        memcpy(&after, &pv_bank, sizeof after);
        { long w0 = sim_writes[0] + sim_writes[1]; boot();
          expect("migrated 63-slot bank is not rewritten on the next boot",
                 bank_eq(&pv_bank, &after) && sim_writes[0] + sim_writes[1] == w0, "edits and new factory still there"); }
        /* power cut during the expansion write: the 50-slot copy stays, next boot finishes the migrate */
        for (step = 0; step < 4; step++) {
            int ram_ok, flash_ok_later;
            sim_reset();
            plant_bank(0, &b, v1);
            /* boot() clears sim_cut_at (a clean power-up). Call init with the cut armed. */
            sim_ops = 0; sim_cut_at = step < 3 ? step : -1; sim_dead = 0;
            poli_nav_init(); poli_preset_init(0);
            ram_ok = !memcmp(&pv_bank.rec[9].b[18], edit9, 31) && !memcmp(&pv_bank.rec[20].b[18], edit20, 31);
            for (i = PV_NSLOT_V1; i < PV_NSLOT; i++)
                if (memcmp(&pv_bank.rec[i].b[18], PV_FACTORY[i].k, 31)) ram_ok = 0;
            boot();   /* clean reboot: finishes the migrate if the write was cut */
            flash_ok_later = !memcmp(&pv_bank.rec[9].b[18], edit9, 31) && !memcmp(&pv_bank.rec[20].b[18], edit20, 31)
                             && pv_bank.nrec == PV_NSLOT;
            for (i = PV_NSLOT_V1; i < PV_NSLOT; i++)
                if (memcmp(&pv_bank.rec[i].b[18], PV_FACTORY[i].k, 31)) flash_ok_later = 0;
            cuts++;
            if (ram_ok && flash_ok_later) okc++;
        }
        snprintf(d, sizeof d, "%d cut points (before erase, mid-bytes, before header, none): %d kept edits in RAM and after a clean reboot", cuts, okc);
        expect("power cut during 50-to-63 write-back never loses user slots 1-50", okc == cuts, d);
        /* a 63-slot law-5 bank is not rewritten at boot */
        {
            long w0;
            sim_reset(); boot();
            load_slot(0); poli_nav_section(NAV_SEC_FX); poli_nav_knob(0, 3);
            poli_preset_save_btn(0); poli_preset_save_btn(0);
            w0 = sim_writes[0] + sim_writes[1];
            boot();
            expect("already-63-slot law-5 bank: no bank write at boot",
                   sim_writes[0] + sim_writes[1] == w0 && pv_bank.nrec == PV_NSLOT, "only the earlier SAVE wrote");
        }
    }

    /* ---- keyboard octave -3 is a playing position, not part of a preset */
    {
        int o1, o2, o3;
        sim_reset(); boot();
        poli_nav_octave(-1); poli_nav_octave(-1); poli_nav_octave(-1); poli_nav_octave(-1);
        load_slot(5); o1 = polivoks_kb_oct();
        poli_preset_save_btn(0); poli_preset_save_btn(0); o2 = polivoks_kb_oct();
        poli_preset_save_btn(0);
        for (i = 0; i < PV_ACT_FACTORY; i++) poli_preset_knob(3, 1);
        poli_preset_save_btn(0); o3 = polivoks_kb_oct();
        polivoks_panel(1u << 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        snprintf(d, sizeof d, "octave after load %d, after save %d, after FACTORY %d; lowest key note %u; record bytes have no octave field",
                 o1, o2, o3, poli_note);
        expect("octave -3 survives load, save and factory reset (not stored)", o1 == -3 && o2 == -3 && o3 == -3 && poli_note == 29, d);
        polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        poli_nav_octave(0);
    }

    /* ---- RAM only when the flash refuses */
    {
        poli_preset_view_t v;
        sim_reset(); boot();
        sim_cut_at = 0; sim_ops = 0;
        load_slot(1); poli_nav_section(NAV_SEC_FX); poli_nav_knob(0, 5);
        poli_preset_save_btn(0); poli_preset_save_btn(0);
        poli_preset_view(&v);
        expect("flash refuses: says NOT IN FLASH, RAM copy kept", v.msg == PV_MSG_RAMONLY && !v.dirty, "msg + rec in RAM");
        sim_cut_at = -1; sim_dead = 0;
    }

    /* ---- held note across preset changes; release safety */
    {
        int stuck = 0, notsound = 0, big = 0, worst_i = -1;
        double worst = 0;
        int32_t last = 0;
        char line[640];
        sim_reset(); boot();
        for (i = 0; i < PV_NSLOT; i++) {
            int32_t sb = 0, sa = 0, sx = 0, pk;
            load_slot(i);
            polivoks_midi(0x90, 57, 100);
            render_peak(4096, 0, &sb, &last);            /* steady, preset i */
            sb = 0; render_peak(2048, 0, &sb, &last);
            load_slot((i + 1) % PV_NSLOT);                 /* change while the key is held */
            sx = 0; render_peak(1, 0, &sx, &last);        /* the first sample after the change */
            pk = render_peak(2048, 0, &sa, &last);
            if (!poli_gate) notsound++;
            (void)pk;
            if (sx > (sb > sa ? sb : sa)) { printf("INFO step %02d->%02d at change %d, own largest %d / %d (full scale 30000)\n", i + 1, (i + 1) % PV_NSLOT + 1, sx, sb, sa); big++; if ((double)sx / (double)(sb > sa ? sb : sa) > worst) { worst = (double)sx / (sb > sa ? sb : sa); worst_i = i; } }
            polivoks_midi(0x80, 57, 0);
            render_peak(44100 * 3, 0, 0, &last);
            { int32_t ega; polivoks_read(0, 0, 0, 0, 0, 0, 0, 0, &ega, 0);
              if (poli_gate || ega != 0 || render_peak(2048, 0, 0, &last) != 0) stuck++; }
        }
        snprintf(line, sizeof line, "%d changes under a held key: gate kept %d/%d; step at the change above both presets' own largest step: %d (worst x%.2f at %02d->%02d); still sounding 3 s after key up: %d",
                 PV_NSLOT, PV_NSLOT - notsound, PV_NSLOT, big, worst, worst_i + 1, worst_i + 2, stuck);
        expect("preset change during a held note: note continues, then stops; no stuck notes", !notsound && !stuck, line);
        /* Measured, not a pass/fail: the engine has no parameter smoothing
           (only the bass blend slews), so a load steps like turning the same
           knobs in one block. Reported as a known limitation. */
        printf("INFO preset change steps above both sounds' own largest step: %d of %d\n", big, PV_NSLOT);
    }
    {   /* key hold on, then a load: hold is switched off and the note ends */
        int32_t ega, last = 0;
        boot();
        polivoks_set_keyhold(1);
        polivoks_midi(0x90, 60, 100); render_peak(1024, 0, 0, &last); polivoks_midi(0x80, 60, 0); render_peak(1024, 0, 0, &last);
        ok = poli_gate == 1;
        load_slot(3);
        ok = ok && !poli_keyhold && !poli_gate;
        render_peak(44100 * 3, 0, 0, &last);
        polivoks_read(0, 0, 0, 0, 0, 0, 0, 0, &ega, 0);
        expect("key hold is off after a load, the held note releases to silence", ok && ega == 0 && render_peak(1024, 0, 0, &last) == 0, "ВКЛ. not stored (Q5)");
        load_slot(31);   /* slow pad: long release */
        polivoks_midi(0x90, 60, 100); render_peak(20000, 0, 0, &last); polivoks_midi(0x80, 60, 0); render_peak(500, 0, 0, &last);
        polivoks_midi(0xB0, 120, 0);
        expect("panic during a long release after a load: silent at once", render_peak(256, 0, 0, &last) == 0, "CC 120");
    }

    /* ---- MIDI receive channel in the 8-byte settings record (single sector, not A/B) ---- */
    {
        long w0;
        uint8_t s[8];
        /* old 8-byte record (midi_ch field is 0, as reserved was): last slot kept, channel defaults to 1 */
        sim_reset();
        plant_settings(1, 8, 0);
        boot();
        expect("old settings no channel field: last slot kept", view_cur() == 8, "slot 09");
        expect("old settings no channel field: default ch 1", poli_nav_midi_ch() == 1, "default 1");
        /* invalid saved values become 1; last slot still loads */
        plant_settings(1, 4, 17);
        boot();
        expect("invalid ch 17 defaults to 1", poli_nav_midi_ch() == 1 && view_cur() == 4, "slot 05, ch 1");
        plant_settings(1, 4, 255);
        boot();
        expect("invalid ch 255 defaults to 1", poli_nav_midi_ch() == 1 && view_cur() == 4, "ch 1");
        /* missing settings: default channel 1 */
        sim_reset(); boot();
        expect("no settings: channel 1", poli_nav_midi_ch() == 1, "first boot");
        /* change channel, write waits 3 s of silence, then survives a simulated reboot */
        sim_reset(); boot();
        w0 = sim_writes[3];
        poli_nav_set_midi_ch(7);
        poli_preset_tick(100);
        ok = sim_writes[3] == w0;
        poli_preset_tick(2000);
        ok = ok && sim_writes[3] == w0;
        poli_preset_tick(4000);
        ok = ok && sim_writes[3] == w0 + 1;
        expect("midi ch write waits 3 s of silence", ok, "same debounce as last-used");
        memcpy(s, simf[3] + 12, 8);
        expect("settings record keeps size 8", s[0] == 1 && s[2] == 7, "ver 1, ch 7");
        boot();
        expect("saved channel survives reboot", poli_nav_midi_ch() == 7, "ch 7 after init");
        /* same channel is not rewritten */
        w0 = sim_writes[3];
        poli_preset_tick(90000);
        expect("same channel is not rewritten", sim_writes[3] == w0, "no extra settings write");
        /* sounding blocks the write, then silence flushes it */
        boot();
        w0 = sim_writes[3];
        poli_nav_set_midi_ch(12);
        poli_preset_tick(0);
        polivoks_midi(0x90, 60, 100);
        render_peak(512, 0, 0, 0);
        poli_preset_tick(20000);
        ok = sim_writes[3] == w0;
        polivoks_midi(0x80, 60, 0);
        { int i; for (i = 0; i < 200; i++) { render_peak(441, 0, 0, 0); poli_preset_tick(30000 + (uint32_t)i * 10u); } }
        ok = ok && sim_writes[3] == w0 + 1 && poli_nav_midi_ch() == 12;
        expect("midi ch write waits for silence", ok, "not while a note sounds");
        boot();
        expect("ch 12 after silent write and reboot", poli_nav_midi_ch() == 12, "persisted");
        expect("old record default key track 62", poli_nav_key_track() == 62, "rsv[0] was 0");
        sim_reset();
        plant_settings_kt(1, 3, 4, 1);
        boot();
        expect("saved track OFF", poli_nav_key_track() == 0 && poli_nav_midi_ch() == 4 && view_cur() == 3, "mode 1");
        plant_settings_kt(1, 3, 4, 3);
        boot();
        expect("saved track 100", poli_nav_key_track() == 100, "mode 3");
        sim_reset(); boot();
        w0 = sim_writes[3];
        poli_nav_set_key_track(0);
        poli_preset_tick(100);
        ok = sim_writes[3] == w0;
        poli_preset_tick(4000);
        ok = ok && sim_writes[3] == w0 + 1;
        memcpy(s, simf[3] + 12, 8);
        ok = ok && s[0] == 1 && s[3] == 1 && s[2] == 1;
        expect("key track write waits 3 s, stays 8 bytes", ok, "rsv[0]=1 OFF, ch still 1");
        boot();
        expect("saved track OFF survives reboot", poli_nav_key_track() == 0, "persisted");
    }
    {
        long w0;
        sim_reset(); boot();
        poli_nav_set_midi_ch(16);
        poli_preset_tick(0);             /* arm pending, load_ms = 0 */
        sim_ops = 0; sim_cut_at = 1; sim_dead = 0;
        poli_preset_tick(4000);          /* due, write is cut */
        ok = sim_dead == 1;
        sim_cut_at = -1; sim_dead = 0;   /* power returns, but the sector is incomplete */
        boot();
        expect("power cut mid settings write: defaults, no crash", poli_nav_midi_ch() == 1 && view_cur() == 0 && ok, "single settings sector, no A/B copy");
        (void)w0;
    }

    printf("%s %d pass %d fail\n", fail ? "HOST-PRESET FAIL" : "HOST-PRESET PASS", pass, fail);
    return fail ? 1 : 0;
}
