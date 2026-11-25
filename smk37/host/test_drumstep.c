/* SPDX-License-Identifier: GPL-3.0-only */
/* Drum-Step (22 Nov 2026): the DRUM STEP page. White-key map, cursor, the USER write, the Drift / fills / HAT BUSY
 * leaving USER alone, RESTORE, a pattern with no chance set playing exactly as before (no random draw), the chance
 * statistics, and the keys not reaching the voice on the page. Host only.
 * SMK-37 Elite build (-DBOARD_SMK37=1): the FM-1's 27 keys are the Elite's keys 5..31 (F3..G5, as PERFORM), so
 * kb(m) shifts an FM-1 key mask up 5; the Elite's outer keys 0..4 and 32..36 do nothing on the page. DRUM STEP is
 * window 11, PAD REC 12; PAD REC's working groove follows a DRUM STEP edit, and its pad edits reset the cell's chance. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"

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
    poli_nav_init();                              /* (poli_nav_init calls xtra_init) */
    xtra_init();
    now_ms = 0;
    xtra_set_c2(3, 0);
}
/* the FM-1 key mask k on the Elite (FM-1 key 0 = Elite key 5) */
static uint64_t kb(uint32_t keys, int oct, uint32_t t) { return xtra_keys_smk((uint64_t)keys << 5, 0, oct, t); }
static void press(int k) { (void)kb(1u << k, 0, now_ms); (void)kb(0, 0, now_ms); }
static void press_smk(int k) { (void)xtra_keys_smk((uint64_t)1u << k, 0, 0, now_ms); (void)xtra_keys_smk(0, 0, 0, now_ms); }
static void cursor(int st) { xtra_knob(XW_STEP, 0, st - xtra_get(XW_STEP, 0)); }
static int same_pat(const xt_pat_t *a, const xt_pat_t *b) { return !memcmp(a, b, sizeof *a); }
static double render_energy(uint32_t keys)
{
    static int32_t buf[2 * 256];
    double e = 0;
    int i, j;
    for (j = 0; j < 40; j++) {
        polivoks_panel(kb(keys, polivoks_kb_oct(), now_ms), 0, 0, 0, 0, 0, 0, 0, 0, 0);
        memset(buf, 0, sizeof buf);
        polivoks_render(buf, 256);
        for (i = 0; i < 512; i++) e += (double)buf[i] * buf[i];
    }
    return e;
}

int main(void)
{
    char d[160];
    int k, v, ok, i, st;
    fail = npass = 0;

    /* ---- the white-key map: F G A B C D E from the lowest F, keys 24 / 26 the 15th / 16th whites */
    {
        static const int8_t WANT[27] = { 0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6, 7, -1, 8, -1, 9, -1, 10, 11, -1, 12,
                                         -1, 13, 14, -1, 15 };
        static const uint8_t LANES[16] = { L_KICK, L_SNARE, L_CLAP, L_CH, L_OH, L_LT, L_MT, L_HT, L_RIM, L_COWB,
                                           L_CLAVE, L_LC, L_MC, L_HC, L_RIDE, L_CRASH };
        ok = 1;
        for (k = 0; k < 27; k++) if (xtra_step_white(k) != WANT[k]) ok = 0;
        for (v = 0; v < 16; v++) if (xtra_step_lane(v) != LANES[v]) ok = 0;
        expect("white_key_map", ok && xtra_step_white(-1) == -1 && xtra_step_white(27) == -1,
               "16 whites -> kick..crash, no ghost; blacks -1");
    }
    /* ---- navigation: DRUM STEP is window 11, one click after DRUMS, then PAD REC (12), GENERATE; 13 windows wrap */
    reset();
    poli_nav_xtra_open(XW_DRUM);
    poli_nav_select_turn(1);
    ok = poli_nav_xtra() == XW_STEP && XW_STEP == 11 && XW_PADREC == 12;
    poli_nav_select_turn(1);
    ok = ok && poli_nav_xtra() == XW_PADREC;
    poli_nav_select_turn(1);
    ok = ok && poli_nav_xtra() == XW_GEN;
    poli_nav_select_turn(-2);
    ok = ok && poli_nav_xtra() == XW_STEP;
    poli_nav_select_turn(NAV_XWIN);
    expect("nav_order", ok && poli_nav_xtra() == XW_STEP && NAV_XWIN == 13,
           "DRUMS -> DRUM STEP -> PAD REC -> GENERATE; 13 wrap");
    {
        poli_nav_view_t vw;
        poli_nav_view(&vw);
        expect("nav_view", vw.section == NAV_SEC_XTRA && vw.xtra == XW_STEP && vw.window == 4 && vw.nwindows == 13 &&
               vw.flat == NAV_NWIN + XW_STEP, "section 5, position 5 of 13");
    }
    {   /* PAD REC keeps its own knobs at its new number; DRUM STEP's are STEP / CHANCE */
        ok = xtra_hi(XW_PADREC, 0) == XT_PADREC_SLOTS - 1 && xtra_hi(XW_PADREC, 1) == 1 && xtra_hi(XW_STEP, 0) == 15 &&
             xtra_hi(XW_STEP, 1) == 3 && xtra_on(XW_PADREC) == 0;
        expect("padrec_renumbered", ok, "PAD REC 12: SLOT 1..8, MODE; DRUM STEP 11: STEP 0..15, CHANCE 0..3");
    }
    /* ---- the cursor (K1) 0..15, K3 / K4 empty, SEL = DRUMS on / off */
    cursor(0);
    xtra_knob(XW_STEP, 0, -5);
    ok = xtra_get(XW_STEP, 0) == 0;
    xtra_knob(XW_STEP, 0, 40);
    ok = ok && xtra_get(XW_STEP, 0) == 15;
#if BOARD_SMK37
    xtra_knob(XW_STEP, 2, 3); xtra_knob(XW_STEP, 3, -3);
    ok = ok && xtra_get(XW_STEP, 2) == 1 && xtra_get(XW_STEP, 3) == 0
         && xtra_hi(XW_STEP, 2) == 1 && xtra_hi(XW_STEP, 3) <= xtra_lo(XW_STEP, 3);
    expect("cursor_range", ok, "K1 clamps 0..15, K7 clamps 0..1, K8 empty");
    xtra_knob(XW_STEP, 2, -1);                    /* leave K7 OFF so the later PLAY checks match the parent */
#else
    xtra_knob(XW_STEP, 2, 3); xtra_knob(XW_STEP, 3, -3);
    ok = ok && xtra_get(XW_STEP, 2) == 0 && xtra_get(XW_STEP, 3) == 0 && xtra_hi(XW_STEP, 2) <= xtra_lo(XW_STEP, 2);
    expect("cursor_range", ok, "K1 clamps 0..15, K3 K4 do nothing");
#endif
    ok = !xt.on[XW_DRUM];
    poli_nav_select_press();
    ok = ok && xt.on[XW_DRUM] && xtra_on(XW_STEP) && !xt.on[XW_STEP];
    poli_nav_select_press();
    expect("sel_drums", ok && !xt.on[XW_DRUM], "SEL on DRUM STEP switches DRUMS");

    /* ---- the first edit: pattern 3 copied into USER, PATTERN = USER, the bit toggled in USER and the playing copy */
    reset();
    xtra_knob(XW_DRUM, 0, 2);                    /* pattern 3 */
    poli_nav_xtra_open(XW_STEP);
    {
        xt_pat_t src = *xt_pat(2), u;
        int had = (src.l[L_SNARE] >> 6) & 1;
        cursor(6);
        press(2);                                 /* G = snare at step 7 */
        u = src; u.l[L_SNARE] ^= 1u << 6;
        snprintf(d, sizeof d, "PATTERN %d style %d snare7 %d->%d", xtra_pattern(), xt.user_style, had, (xt.user.l[L_SNARE] >> 6) & 1);
        expect("first_edit_copies", xtra_pattern() == XT_PAT_USER && same_pat(&xt.user, &u) && xt.user_style == 0 &&
               xt.wp_src == XT_PAT_USER && ((xt.wp.l[L_SNARE] >> 6) & 1) == !had && xtra_step_voice() == 1, d);
        press(2);
        expect("toggle_back", same_pat(&xt.user, &src) && ((xt.wp.l[L_SNARE] >> 6) & 1) == had, "the same key again: off");
        press(1); press(3); press(8); press(25);    /* black keys */
        for (k = 0; k < 5; k++) press_smk(k);       /* the Elite's C3..E3 below F3 */
        for (k = 32; k < 37; k++) press_smk(k);     /* and G#5..C6 above G5 */
        expect("black_keys_inert", same_pat(&xt.user, &src), "blacks and the Elite's outer keys 0-4, 32-36 change nothing");
    }
    /* a pack pattern brings its genre; each white key reaches its lane at the cursor */
    reset();
    xtra_knob(XW_DRUM, 0, 9 + 2 * 12 + 2);       /* DNB3 */
    poli_nav_xtra_open(XW_STEP);
    {
        xt_pat_t src = *xt_pat(9 + 2 * 12 + 2);
        cursor(3);
        ok = 1;
        for (k = 0; k < 27; k++) {
            int vv = xtra_step_white(k);
            if (vv < 0) continue;
            press(k);
            if (((xt.user.l[xtra_step_lane(vv)] ^ src.l[xtra_step_lane(vv)]) >> 3 & 1) != 1) ok = 0;
        }
        for (k = 0; k < 17; k++) if (k != L_GHOST && (xt.user.l[k] ^ src.l[k]) != (1u << 3)) ok = 0;
        expect("pack_genre_and_all_whites", ok && xt.user_style == 3 && xt.user.l[L_GHOST] == src.l[L_GHOST],
               "DNB3: style 3; 16 whites flip their lane at step 4; ghost untouched");
    }
    /* OCT does not move the keys */
    reset();
    poli_nav_xtra_open(XW_STEP);
    cursor(5);
    (void)kb(1u << 0, 2, 0); (void)kb(0, 2, 0);
    expect("oct_independent", (xt.user.l[L_KICK] >> 5) & 1, "OCT +2: the lowest key is still the kick");

    /* ---- the keys do not reach the voice on the page; they do elsewhere */
    reset();
    poli_nav_home();
    {
        double off_page = render_energy(1u << 7), on_page;
        render_energy(0);
        for (i = 0; i < 200; i++) render_energy(0);   /* let the release finish */
        poli_nav_xtra_open(XW_STEP);
        ok = xtra_keys_smk(((uint64_t)1u << 37) - 1u, 0, 0, 0) == 0;   /* all 37 keys */
        (void)kb(0, 0, 0);
        on_page = render_energy(1u << 7);
        snprintf(d, sizeof d, "energy off-page %.3g, on page %.3g; return 0", off_page, on_page);
        expect("keys_not_to_voice", ok && off_page > 0 && on_page == 0, d);
    }
    /* with the arp on: keys pressed before the page opened are let go on the page */
    reset();
    xtra_toggle(XW_ARP);
    xt.hold = 0;
    (void)kb(1u << 5, 0, 0);
    ok = xt.nheld == 1;
    poli_nav_xtra_open(XW_STEP);
    (void)kb(0, 0, 10);
    expect("arp_release_on_page", ok && xt.nheld == 0 && xt.ndn == 0, "no stuck arp note");
    /* PERFORM: a held FILL let go on the page ends */
    reset();
    xtra_toggle(XW_PERF);
    (void)kb(1u << 0, 0, 0);
    ok = xt.fill_hold == 1;
    poli_nav_xtra_open(XW_STEP);
    (void)kb(0, 0, 0);
    expect("perform_release_on_page", ok && xt.fill_hold == 0, "FILL released");

    /* ---- audition: stopped, drums on and core 2 running -> one hit for a toggle on; none for off or drums off */
    reset();
    poli_nav_xtra_open(XW_STEP);
    cursor(1);
    {
        uint32_t w0 = xf_par.trw, w1, w2, w3, w4;
        press(0);                                 /* drums off: no hit */
        w1 = xf_par.trw;
        press(0);
        xt.on[XW_DRUM] = 1;
        press(0);                                 /* on: kick */
        w2 = xf_par.trw;
        press(0);                                 /* off: nothing */
        w3 = xf_par.trw;
        xtra_set_c2(1, 0);
        press(0);
        w4 = xf_par.trw;
        snprintf(d, sizeof d, "hits %u %u %u %u; inst %u", w1 - w0, w2 - w1, w3 - w2, w4 - w3,
                 (unsigned)(xf_par.trq[(w2 - 1u) & (XF_TRQ - 1u)] & 0xFFu));
        expect("audition", w1 == w0 && w2 == w1 + 1u && w3 == w2 && w4 == w3 &&
               (xf_par.trq[(w2 - 1u) & (XF_TRQ - 1u)] & 0xFFu) == XT_KF_LANE[xt_flavour()][L_KICK], d);
    }

    /* ---- K2 CHANCE: the touched voice at the cursor; an empty cell stays 100 %; K1 refreshes it */
    reset();
    xtra_knob(XW_DRUM, 0, 3);                    /* pattern 4 */
    poli_nav_xtra_open(XW_STEP);
    cursor(2);
    press(0);                                     /* kick at 3 (toggle on or off) */
    if (!((xt.user.l[L_KICK] >> 2) & 1)) press(0);
    xtra_knob(XW_STEP, 1, 2);
    ok = xtra_get(XW_STEP, 1) == 2 && xtra_chance(L_KICK, 2) == 2 && (xtra_step_cell(0, 2) >> 1) == 2;
    cursor(3);
    if ((xt.user.l[L_KICK] >> 3) & 1) press(0); /* make step 4 empty */
    xtra_knob(XW_STEP, 1, 3);
    ok = ok && xtra_get(XW_STEP, 1) == 0 && xtra_chance(L_KICK, 3) == 0;
    cursor(2);
    ok = ok && xtra_get(XW_STEP, 1) == 2;
    xtra_knob(XW_STEP, 1, 5);
    ok = ok && xtra_get(XW_STEP, 1) == 3;
    xtra_knob(XW_STEP, 1, -9);
    ok = ok && xtra_get(XW_STEP, 1) == 0 && xt.chance[L_KICK] == 0;
    xtra_knob(XW_STEP, 1, 1);
    press(0); press(0);                           /* off and on: back to 100 % */
    expect("chance_knob", ok && xtra_chance(L_KICK, 2) == 0, "set, empty cell refuses, K1 refresh, clamp, toggle clears");
    /* K2 on a pattern other than USER, on a hit: the copy happens */
    reset();
    xtra_knob(XW_DRUM, 0, 1);                    /* pattern 2 */
    poli_nav_xtra_open(XW_STEP);
    for (st = 0; st < 16 && !((xt_pat(1)->l[L_KICK] >> st) & 1); st++) {}
    cursor(st);
    xtra_knob(XW_STEP, 1, 1);
    expect("chance_copies", st < 16 && xtra_pattern() == XT_PAT_USER && xtra_chance(L_KICK, st) == 1 &&
           !memcmp(xt.user.l, XT_PAT[1].l, sizeof xt.user.l), "a kick of pattern 2: copied to USER unchanged, 75 %");
    /* GENERATE into USER clears the chance */
    xtra_generate();
    ok = 1;
    for (k = 0; k < 17; k++) if (xt.chance[k]) ok = 0;
    expect("generate_clears_chance", ok, "a new USER beat: all 100 %");

    /* ---- 100 % (nothing set): the step masks are what xt_step_lanes gives and no random number is drawn */
    reset();
    xtra_knob(XW_DRUM, 0, 5);
    poli_nav_xtra_open(XW_STEP);
    cursor(0); press(4); press(4);                /* USER = pattern 6, via an edit and back */
    ok = xt.wp_src == XT_PAT_USER;
    {
        int busy;
        for (busy = 0; busy <= 4; busy++)
            for (st = 0; st < 16; st++) {
                uint8_t r0, r1;
                uint32_t m0, m1, g0;
                xt.k[XW_GROOVE][1] = (int16_t)busy;
                xt.crash_next = st == 0;
                m0 = xt_step_lanes((uint32_t)st, &r0);
                r1 = r0;
                g0 = xt.drng;
                m1 = xt_chance(m0, (uint32_t)st, &r1);
                if (m1 != m0 || r1 != r0 || xt.drng != g0) ok = 0;
            }
        xt.k[XW_GROOVE][1] = 2;
    }
    expect("full_chance_identity", ok, "80 step masks unchanged, rolls unchanged, drng untouched");
    /* the same over the clock: 8 bars, drift off; drng unchanged, kick hits = the pattern's kicks */
    reset();
    xtra_knob(XW_DRUM, 0, 0);
    poli_nav_xtra_open(XW_STEP);
    cursor(0); press(4); press(4);
    xt.on[XW_DRUM] = 1;
    {
        uint32_t g0 = xt.drng, k0 = xf_par.drm_trig[0], want = 8u * (uint32_t)xt_pop(xt.user.l[L_KICK]);
        xtra_transport(1);
        run_ms(8u * 2000u - 5u);                  /* 8 bars at 120 BPM */
        xtra_transport(0);
        snprintf(d, sizeof d, "kicks %u want %u, drng %s", xf_par.drm_trig[0] - k0, want, xt.drng == g0 ? "same" : "MOVED");
        expect("full_chance_clock", xf_par.drm_trig[0] - k0 == want && xt.drng == g0, d);
    }

    /* ---- chance statistics: 75 / 50 / 25 % over 4000 rolls each, and 50 % through the clock */
    {
        static const int LV[3] = { 1, 2, 3 };
        int li;
        for (li = 0; li < 3; li++) {
            int keep = 0, n = 4000, want = n * (4 - LV[li]) / 4;
            uint8_t r = 0;
            reset();
            poli_nav_xtra_open(XW_STEP);
            cursor(8);
            if (!((xt.user.l[L_KICK] >> 8) & 1)) press(0);
            xtra_knob(XW_STEP, 1, LV[li]);
            for (i = 0; i < n; i++) keep += (xt_chance(1u << L_KICK, 8, &r) >> L_KICK) & 1u;
            snprintf(d, sizeof d, "%d%%: kept %d of %d (want %d +- 4%%)", 100 - 25 * LV[li], keep, n, want);
            expect("chance_stats", keep > want - n / 25 && keep < want + n / 25, d);
        }
    }
    reset();
    xtra_knob(XW_DRUM, 0, 0);                    /* pattern 1: kicks 1 5 9 13, all at 50 % */
    poli_nav_xtra_open(XW_STEP);
    for (st = 0; st < 16; st++) {
        if (!((xt_pat(0)->l[L_KICK] >> st) & 1)) continue;
        cursor(st); press(0); press(0); xtra_knob(XW_STEP, 1, 2);
    }
    xt.on[XW_DRUM] = 1;
    {
        uint32_t k0 = xf_par.drm_trig[0], n = 64u * (uint32_t)xt_pop(xt.user.l[L_KICK]), got;
        xtra_transport(1);
        run_ms(64u * 2000u - 5u);
        xtra_transport(0);
        got = xf_par.drm_trig[0] - k0;
        snprintf(d, sizeof d, "64 bars: %u of %u kicks (%.1f%%)", got, n, 100.0 * got / n);
        expect("chance_clock_50", got > n * 38u / 100u && got < n * 62u / 100u, d);
    }

    /* ---- the Drift (MUTATE MORPH every 4 bars, SURPRISE), fills and HAT BUSY FULL never write USER; RESTORE */
    reset();
    xtra_knob(XW_DRUM, 0, 9);                    /* HOUSE1 */
    poli_nav_xtra_open(XW_STEP);
    cursor(3); press(14);                         /* rim at 4: an edit */
    cursor(0); press(0); xtra_knob(XW_STEP, 1, 1);   /* (kick 1 toggled; 75 % if it is now a hit) */
    {
        xt_pat_t keep = xt.user;
        uint32_t ch[17];
        int differs = 0;
        memcpy(ch, xt.chance, sizeof ch);
        xt.on[XW_DRUM] = 1; xt.on[XW_DRIFT] = 1;
        xt.k[XW_DRIFT][0] = 0; xt.k[XW_DRIFT][1] = 1; xt.k[XW_DRIFT][2] = 2;   /* CHANGE OFF, EVERY 4, MORPH */
        {   /* (CHANGE OFF has no Drift fill; the bar-end fills are checked with CHANGE NEXT inside a 1-pattern pool
             * below, which reloads USER itself) */ }
        xt.k[XW_GROOVE][1] = 4; xt.k[XW_GROOVE][2] = 2;                       /* HAT BUSY FULL, FILL TOMS */
        xt.k[XW_GEN][2] = 200;                                                 /* SURPRISE often */
        poli_nav_xtra_open(XW_DRIFT);
        xtra_transport(1);
        for (i = 0; i < 64; i++) {
            if (i == 20) xtra_fill_button(1);     /* a held FILL (ROLL) for two bars */
            if (i == 22) xtra_fill_button(0);
            run_ms(2000);
            if (!same_pat(&xt.wp, &xt.user)) differs = 1;
        }
        snprintf(d, sizeof d, "mutations %u surprises %u, held fill, HAT BUSY FULL; playing copy moved %d", xtra_mutations(),
                 xtra_surprises(), differs);
        expect("drift_leaves_user", same_pat(&xt.user, &keep) && !memcmp(ch, xt.chance, sizeof ch) && xtra_mutations() > 0 &&
               differs && xtra_pattern() == XT_PAT_USER, d);
        {   /* Drift fills at the bar ends: CHANGE NEXT with POOL 1-9 from USER walks the patterns; it must not
             * write USER either (the pattern changes, USER stays) */
            xt_pat_t k2 = xt.user;
            xt.k[XW_DRIFT][0] = 1; xt.k[XW_DRIFT][3] = 7;
            run_ms(16u * 2000u);
            snprintf(d, sizeof d, "fills %u changes %u", xtra_fills(), xtra_changes());
            expect("drift_fills_leave_user", xtra_fills() > 0 && same_pat(&xt.user, &k2), d);
            xt.k[XW_DRIFT][0] = 0;
            xt.k[XW_DRUM][0] = XT_PAT_USER;
            xt.reload = 1;
            run_ms(600);
        }
        xtra_transport(0);
        xtra_restore();
        expect("restore", same_pat(&xt.wp, &xt.user) && xt.wp_src == XT_PAT_USER, "RESTORE: the programmed USER back");
        /* LOCK: no Drift moment moves the copy */
        xt.k[XW_GEN][3] = 1;
        {
            uint32_t m0 = xtra_mutations();
            xtra_transport(1);
            run_ms(16u * 2000u);
            xtra_transport(0);
            expect("lock", xtra_mutations() == m0 && same_pat(&xt.user, &keep), "LOCK: nothing moves, USER kept");
        }
        /* edits while it runs go into the playing copy at once */
        xt.k[XW_GEN][3] = 0;
        xtra_transport(1);
        run_ms(4100);
        poli_nav_xtra_open(XW_STEP);
        cursor(11);
        {
            int was = (xt.user.l[L_CLAVE] >> 11) & 1;
            press(18);                            /* B (2nd octave) = clave */
            snprintf(d, sizeof d, "was %d user %d copy %d pat %d src %d", was, (xt.user.l[L_CLAVE] >> 11) & 1,
                     (xt.wp.l[L_CLAVE] >> 11) & 1, xtra_pattern(), xt.wp_src);
            expect("edit_running", ((xt.user.l[L_CLAVE] >> 11) & 1) == !was && ((xt.wp.l[L_CLAVE] >> 11) & 1) == !was, d);
        }
        xtra_transport(0);
    }
    /* a pattern change still waiting for the beat: the first edit starts from the new pattern */
    reset();
    xt.on[XW_DRUM] = 1;
    xtra_transport(1);
    run_ms(130);                                  /* step 2 */
    xtra_knob(XW_DRUM, 0, 4);                    /* pattern 5, heard from the next beat */
    poli_nav_xtra_open(XW_STEP);
    cursor(9);
    press(4);                                     /* clap */
    {
        xt_pat_t u = *xt_pat(4);
        u.l[L_CLAP] ^= 1u << 9;
        expect("pending_pattern", same_pat(&xt.user, &u) && same_pat(&xt.wp, &u), "USER and the copy = pattern 5 + the edit");
    }
    xtra_transport(0);

    /* ---- SMK: PAD REC and DRUM STEP share USER */
    reset();
    xtra_knob(XW_DRUM, 0, 2);                    /* pattern 3 */
    poli_nav_xtra_open(XW_STEP);
    cursor(5);
    press(2);                                     /* snare at 6 */
    {
        int same = 1, l;
        for (l = 0; l < 17; l++) if (xtra_padrec_work_lane(l) != xt.user.l[l]) same = 0;
        expect("padrec_follows_edit", same && xtra_padrec_dirty() && xtra_padrec_can_undo(),
               "PAD REC's working groove = USER after the edit; unsaved, undo kept");
        xtra_knob(XW_STEP, 1, 2);                 /* 50 % (if the snare is now a hit) */
        if (!((xt.user.l[L_SNARE] >> 5) & 1)) { press(2); xtra_knob(XW_STEP, 1, 2); }
        ok = xtra_chance(L_SNARE, 5) == 2;
        /* PAD REC STEP mode: a pad at the cursor toggles that cell; its chance goes back to 100 % */
        poli_nav_xtra_open(XW_PADREC);
        xtra_padrec_knob(1, 1);                   /* MODE STEP */
        for (i = 0; i < 5; i++) xtra_padrec_step_move(1);
        for (k = 0; k < 16 && xtra_padrec_lane_of_pad(k) != L_SNARE; k++) {}
        xtra_padrec_pad(k, 100, 1); xtra_padrec_pad(k, 0, 2);
        snprintf(d, sizeof d, "pad %d: snare 6 %d -> %d, chance %d", k, 1, (xt.user.l[L_SNARE] >> 5) & 1, xtra_chance(L_SNARE, 5));
        expect("padrec_step_resets_chance", ok && k < 16 && !((xt.user.l[L_SNARE] >> 5) & 1) && xtra_chance(L_SNARE, 5) == 0 &&
               xtra_pattern() == XT_PAT_USER, d);
        /* back on DRUM STEP the pad's edit shows; set a chance, then PAD REC CLEAR puts all at 100 % */
        poli_nav_xtra_open(XW_STEP);
        ok = xtra_step_cell(1, 5) == 0;
        cursor(0);
        press(0);                                 /* (the kick: the voice K2 sets) */
        if (!((xt.user.l[L_KICK] >> 0) & 1)) press(0);
        xtra_knob(XW_STEP, 1, 3);
        ok = ok && xtra_chance(L_KICK, 0) == 3;
        xtra_padrec_clear();
        {
            int ok2 = ok;
            for (l = 0; l < 17; l++) if (xt.chance[l] || xt.user.l[l]) ok = 0;
            snprintf(d, sizeof d, "CLEAR: an empty groove, every cell 100 %% (before %d, after %d)", ok2, ok);
        }
        expect("padrec_clear_resets_chance", ok, d);
        /* SAVE stores the hits (not the chance); LOAD brings them back at 100 % */
        cursor(4); press(0); xtra_knob(XW_STEP, 1, 1);
        ok = xtra_chance(L_KICK, 4) == 1 && xtra_padrec_save();
        cursor(8); press(0);
        poli_nav_xtra_open(XW_PADREC);
        xtra_padrec_knob(0, 1); xtra_padrec_knob(0, -1);   /* slot 2 (empty) and back to slot 1: loads it */
        snprintf(d, sizeof d, "kick lane %04x, chance %08x", (unsigned)xt.user.l[L_KICK], (unsigned)xt.chance[L_KICK]);
        expect("padrec_save_load", ok && xt.user.l[L_KICK] == (1u << 4) && xt.chance[L_KICK] == 0, d);
    }

    printf(fail ? "HOST-DRUMSTEP FAIL %d (%d pass)\n" : "HOST-DRUMSTEP PASS %d pass 0 fail\n", fail ? fail : npass, npass);
    return fail ? 1 : 0;
}
