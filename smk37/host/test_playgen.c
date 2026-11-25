/* SPDX-License-Identifier: GPL-3.0-only */
/* PLAY GEN (24 Nov 2026): DRUM STEP K7 on the SMK-37 Elite. A fresh random loop on every PLAY, written
 * only into the playing copy. USER, chance, PATTERN and the PAD REC 408 B blob stay untouched. Host only. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"
#include "poli_ui_font.h"

/* Footer F_T16 / GUI_CP (poli_gui_font.inc), used only to check the K7 label glyphs. */
static const uint16_t PLAYGEN_GUI_CP[] = {
    0x0020,0x0027,0x002b,0x002d,0x002e,0x002f,0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,
    0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,0x0050,
    0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005a,0x0410,0x0411,0x0412,0x0413,0x0414,0x0415,
    0x0416,0x0417,0x0418,0x0419,0x041a,0x041b,0x041c,0x041d,0x041e,0x041f,0x0420,0x0421,0x0422,0x0423,0x0424,0x0425,
    0x0426,0x0427,0x0428,0x0429,0x042a,0x042b,0x042c,0x042d,0x042e,0x042f,0x0025
};

static int fail, npass;
static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok) fail++; else npass++;
}
static uint32_t now_ms;
static void run_ms(uint32_t ms) { uint32_t e = now_ms + ms; while (now_ms < e) xtra_tick(++now_ms); }
static void reset(void)
{
    poli_nav_init();
    xtra_init();
    now_ms = 0;
    xtra_set_c2(3, 0);
}
static void setk(int w, int i, int v) { xtra_knob(w, i, v - xtra_get(w, i)); }
static void press(int k) { (void)xtra_keys_smk((uint64_t)1u << k, 0, 0, now_ms); (void)xtra_keys_smk(0, 0, 0, now_ms); }
static int same_pat(const xt_pat_t *a, const xt_pat_t *b) { return !memcmp(a, b, sizeof *a); }

static int cp_in(const uint16_t *t, int n, uint32_t c)
{
    int i;
    for (i = 0; i < n; i++) if (t[i] == (uint16_t)c) return 1;
    return 0;
}
/* F_T16 footer width: n glyphs * 8 px (same formula as g_tw for E_FLAB). */
static int label_ok(const char *s, int *pw, int *pn)
{
    const uint8_t *p = (const uint8_t *)s;
    int n = 0, ok = 1;
    while (*p) {
        uint32_t c;
        if (*p < 0x80u) { c = *p++; }
        else if ((*p & 0xE0u) == 0xC0u && p[1]) { c = ((uint32_t)(*p & 0x1Fu) << 6) | (p[1] & 0x3Fu); p += 2; }
        else { ok = 0; break; }
        n++;
        if (!cp_in(UI_F16_CP, (int)(sizeof UI_F16_CP / sizeof UI_F16_CP[0]), c)) ok = 0;
        if (!cp_in(PLAYGEN_GUI_CP, (int)(sizeof PLAYGEN_GUI_CP / sizeof PLAYGEN_GUI_CP[0]), c)) ok = 0;
    }
    *pn = n;
    *pw = n * 8;
    return ok && *pw < 56;
}

int main(void)
{
    char d[192];
    uint8_t blob0[XT_PADREC_BLOB], blob1[XT_PADREC_BLOB];
    int ok, w, n, i;
    fail = npass = 0;

    /* ---- struct size: wp_gen / wp_style sit in the tail padding; sizeof stays 608 */
    {
        size_t tail = sizeof xt - (size_t)((char *)&xt.st_voice - (char *)&xt);
        snprintf(d, sizeof d, "sizeof xt=%zu tail_after_st_voice=%zu wp_gen/wp_style at +%zu/+%zu",
                 sizeof xt, tail,
                 (size_t)((char *)&xt.wp_gen - (char *)&xt.st_voice),
                 (size_t)((char *)&xt.wp_style - (char *)&xt.st_voice));
        expect("sizeof_xt", sizeof xt == 608 && tail == 8 && xt.wp_gen == 0 && xt.wp_style == 0, d);
    }

    /* ---- K7 footer label + values: glyphs in UI_F16_CP and GUI_CP, width < 56 px */
    {
        static const char *const L[] = { "ONPLAY", "АВТО", "OFF", "ON", "ВЫКЛ", "ВКЛ" };
        ok = 1;
        d[0] = 0;
        for (i = 0; i < 6; i++) {
            char b[48];
            if (!label_ok(L[i], &w, &n)) { ok = 0; snprintf(b, sizeof b, " %s=%dpx/%d missing", L[i], w, n); }
            else snprintf(b, sizeof b, " %s=%dpx", L[i], w);
            if (strlen(d) + strlen(b) < sizeof d) strcat(d, b);
        }
        expect("footer_glyphs", ok && label_ok("ONPLAY", &w, &n) && w == 48 && label_ok("АВТО", &w, &n) && w == 32, d);
    }

    /* ---- K7 default OFF, range 0..1, K8 empty */
    reset();
    poli_nav_xtra_open(XW_STEP);
    ok = xtra_get(XW_STEP, 2) == 0 && xtra_hi(XW_STEP, 2) == 1 && xtra_lo(XW_STEP, 2) == 0
         && xtra_hi(XW_STEP, 3) <= xtra_lo(XW_STEP, 3);
    setk(XW_STEP, 2, 1);
    ok = ok && xtra_get(XW_STEP, 2) == 1;
    xtra_knob(XW_STEP, 2, 9);
    ok = ok && xtra_get(XW_STEP, 2) == 1;
    setk(XW_STEP, 2, 0);
    xtra_knob(XW_STEP, 3, 5);
    ok = ok && xtra_get(XW_STEP, 2) == 0 && xtra_get(XW_STEP, 3) == 0;
    expect("k7_range", ok, "K7 0..1 default OFF, K8 empty");

    /* ---- 1. K7 OFF: PLAY leaves wp, USER, chance, style, PATTERN, blob, drng as the parent would */
    reset();
    setk(XW_DRUM, 0, 2);                          /* pattern 3 */
    poli_nav_xtra_open(XW_STEP);
    xt.on[XW_DRUM] = 1;
    {
        xt_pat_t u0 = xt.user, wp0 = xt.wp;
        uint32_t ch0[17], g0 = xt.drng;
        uint8_t st0 = xt.user_style, src0 = xt.wp_src;
        int pat0 = xtra_pattern();
        memcpy(ch0, xt.chance, sizeof ch0);
        xtra_padrec_export(blob0, sizeof blob0);
        xtra_transport(1);
        xtra_padrec_export(blob1, sizeof blob1);
        ok = !xt.wp_gen && same_pat(&xt.wp, &wp0) && same_pat(&xt.user, &u0)
             && !memcmp(xt.chance, ch0, sizeof ch0) && xt.user_style == st0 && xtra_pattern() == pat0
             && xt.wp_src == src0 && xt.drng == g0 && !memcmp(blob0, blob1, sizeof blob0);
        xtra_transport(0);
        expect("k7_off_identical", ok, "PLAY with K7 OFF does not generate, drng and USER untouched");
    }

    /* ---- 2/3. K7 ON: a fresh loop every STOP->PLAY; USER, chance, style, PATTERN, PAD REC blob untouched */
    reset();
    setk(XW_DRUM, 0, 0);                          /* pattern 1 */
    poli_nav_xtra_open(XW_STEP);
    press(0); press(0);                           /* toggle kick 1 twice: still pattern 1 copied? first press copies to USER */
    /* start from a factory pattern with no USER edit: re-init and do not edit */
    reset();
    setk(XW_DRUM, 0, 9);                          /* HOUSE1, so AUTO STYLE is house */
    /* put a mark in USER and a non-zero chance, and save a PAD REC slot, all before PLAY */
    {
        xt_pat_t u0, wp_prog, g1, g2;
        uint32_t ch0[17], g0;
        uint8_t st0;
        int pat0;
        xt.user = *xt_pat(3);                     /* USER holds pattern 4, distinct from HOUSE1 */
        xt.user_style = 0;
        memset(xt.chance, 0, sizeof xt.chance);
        xt.chance[L_KICK] = 0xAAAAAAAAu;          /* 50 % on even steps, never applied to the temp beat */
        xtra_padrec_follow_user();
        (void)xtra_padrec_save();                 /* writes the 408 B slot; also moves PATTERN to USER */
        setk(XW_DRUM, 0, 9);                      /* select HOUSE1 again; the slot and RAM USER stay */
        xtra_padrec_export(blob0, sizeof blob0);
        u0 = xt.user; wp_prog = *xt_pat(9);
        memcpy(ch0, xt.chance, sizeof ch0);
        st0 = xt.user_style; pat0 = xtra_pattern();
        g0 = xt.drng;
        xt.on[XW_DRUM] = 1;
        setk(XW_STEP, 2, 1);
        xtra_transport(1);
        g1 = xt.wp;
        xtra_padrec_export(blob1, sizeof blob1);
        ok = xt.wp_gen && xt.wp_style != 0 && !same_pat(&xt.wp, &wp_prog) && !same_pat(&xt.wp, &u0)
             && same_pat(&xt.user, &u0) && !memcmp(xt.chance, ch0, sizeof ch0)
             && xt.user_style == st0 && xtra_pattern() == pat0
             && xt.wp_src == (uint8_t)pat0 && !memcmp(blob0, blob1, sizeof blob0)
             && xt.drng != g0;
        snprintf(d, sizeof d, "wp_gen=%d style=%d pat=%d user_ok=%d blob_ok=%d drng %s",
                 xt.wp_gen, xt.wp_style, xtra_pattern(), same_pat(&xt.user, &u0),
                 !memcmp(blob0, blob1, sizeof blob0), xt.drng != g0 ? "moved" : "STUCK");
        expect("k7_on_play_untouched", ok, d);

        /* ---- 4. 100 % chance playback: xt_chance does not drop hits or draw the drum rng */
        {
            uint8_t r = 0;
            uint32_t m, gch, st;
            int okc = 1;
            gch = xt.drng;
            for (st = 0; st < 16u; st++) {
                m = 0;
                for (i = 0; i < 17; i++) if (xt.wp.l[i] & (1u << st)) m |= 1u << i;
                if (xt_chance(m, st, &r) != m) okc = 0;
            }
            expect("wp_gen_full_chance", okc && xt.drng == gch,
                   xt.drng == gch ? "16 step masks kept, drng not drawn" : "drng was drawn");
        }

        /* ---- 5. holds steady across bars when Drift is off */
        run_ms(4u * 2000u);
        expect("steady_bars", xt.wp_gen && same_pat(&xt.wp, &g1) && same_pat(&xt.user, &u0),
               "4 bars, Drift off: the temp beat stayed, USER stayed");

        xtra_transport(0);
        expect("stop_restores", !xt.wp_gen && same_pat(&xt.wp, &wp_prog) && same_pat(&xt.user, &u0),
               "STOP reloads the programmed pattern, USER still untouched");

        /* second PLAY: a different loop */
        xtra_transport(1);
        g2 = xt.wp;
        ok = xt.wp_gen && !same_pat(&g2, &g1) && same_pat(&xt.user, &u0) && xtra_pattern() == pat0;
        if (same_pat(&g2, &g1)) {                 /* a rare collision: one more STOP->PLAY */
            xtra_transport(0);
            xtra_transport(1);
            g2 = xt.wp;
            ok = xt.wp_gen && !same_pat(&g2, &g1) && same_pat(&xt.user, &u0);
        }
        expect("fresh_every_play", ok, "a new loop each STOP then PLAY");
        xtra_transport(0);
    }

    /* ---- 6. turning K7 OFF while running reloads the programmed pattern on the next beat */
    reset();
    setk(XW_DRUM, 0, 9);
    xt.on[XW_DRUM] = 1;
    setk(XW_STEP, 2, 1);
    xtra_transport(1);
    ok = xt.wp_gen;
    setk(XW_STEP, 2, 0);
    ok = ok && xt.reload && xt.wp_gen;            /* still the temp beat until the next beat */
    run_ms(600);                                  /* a beat at 120 BPM is 500 ms */
    expect("k7_off_restores", ok && !xt.wp_gen && !xt.reload && same_pat(&xt.wp, xt_pat(9)),
           "K7 OFF: programmed pattern back on the next beat");
    xtra_transport(0);

    /* ---- 7. LOCK blocks PLAY-GEN */
    reset();
    setk(XW_DRUM, 0, 9);
    xt.on[XW_DRUM] = 1;
    setk(XW_STEP, 2, 1);
    setk(XW_GEN, 3, 1);                           /* LOCK */
    {
        xt_pat_t wp0 = xt.wp;
        uint32_t g0 = xt.drng;
        xtra_transport(1);
        expect("lock_blocks", !xt.wp_gen && same_pat(&xt.wp, &wp0) && xt.drng == g0,
               "LOCK on GENERATE: PLAY leaves the programmed pattern, no rng draw");
        xtra_transport(0);
    }

    /* ---- 8. a DRUM STEP edit during PLAY-GEN drops the temp beat and edits real USER */
    reset();
    setk(XW_DRUM, 0, 9);
    xt.user = *xt_pat(3);
    xt.user_style = 0;
    xt.on[XW_DRUM] = 1;
    setk(XW_STEP, 2, 1);
    poli_nav_xtra_open(XW_STEP);
    xtra_transport(1);
    ok = xt.wp_gen;
    {
        xt_pat_t want;
        int st = 6, had;
        setk(XW_STEP, 0, st);
        had = (xt.user.l[L_SNARE] >> st) & 1;
        press(7);                                 /* Elite key 7 = FM-1 key 2 = G = snare (white index 2)? */
        /* Elite key 5 is F (voice 0). White: 5=F kick, 7=G snare. press(7) is snare. */
        want = xt.user;
        /* user should already have been updated by the toggle */
        expect("edit_reloads_user", ok && !xt.wp_gen && xtra_pattern() == XT_PAT_USER
               && ((xt.user.l[L_SNARE] >> st) & 1) != had
               && same_pat(&xt.wp, &xt.user),
               ((xt.user.l[L_SNARE] >> st) & 1) != had ? "temp beat dropped, USER edited, wp follows USER"
                                                       : "snare bit did not toggle");
        (void)want;
    }
    xtra_transport(0);

    /* ---- 9. a PATTERN change replaces the temp beat cleanly */
    reset();
    setk(XW_DRUM, 0, 9);
    xt.on[XW_DRUM] = 1;
    setk(XW_STEP, 2, 1);
    xtra_transport(1);
    ok = xt.wp_gen;
    setk(XW_DRUM, 0, 4);                          /* pattern 5, heard from the next beat */
    run_ms(600);
    expect("pattern_replaces", ok && !xt.wp_gen && xtra_pattern() == 4 && same_pat(&xt.wp, xt_pat(4)),
           "PATTERN change: programmed pattern 5 on the next beat, wp_gen cleared");
    xtra_transport(0);

    printf("%d PASS, %d FAIL\n", npass, fail);
    return fail ? 1 : 0;
}
