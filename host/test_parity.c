/* SPDX-License-Identifier: GPL-3.0-only */
/* Parity (23 Nov 2026): host checks for what the FM-1 parity build adds. Not firmware.
 *   A. PAD REC through the FM-1 keybed: the 16 white keys are the pads in DRUM STEP lane order, black keys and the
 *      synth voice stay silent, live hits, SEL undo, clear, a key held 500 ms wipes its lane, the K4 STEP cursor,
 *      the 8-slot 408-byte flash image and its checks, the page's place in the SEL walk.
 *   B. The classic arpeggiator: its page after ARPEGGIO, on / off, one arp at a time (EXTRAS ARP and SEQ), the
 *      step pattern in every mode, the notes through polivoks_midi (the engine's own note and gate), gate length,
 *      LATCH, arp off = keys unchanged, MIDI clock follow, the knobs' ranges, the clear on CC 120 / CC 123 /
 *      program change / preset load.
 *   C. The screen: FILTER EG plot and AMP EG plot with its moving dot, the 24-bar analyser band, the value-only
 *      repaint (header not sent, same pixels as a full draw). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"
#include "poli_preset.c"

/* ---- screen model (as shot_nav.c), plus the rows each blit touched */
static uint16_t fb[240 * 240];
static int blit_y0 = 999, blit_y1 = -1, nblit;
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t row, col;
    if (!w || !h || x >= 240u || y >= 240u) return;
    if (x + w > 240u) w = 240u - x;
    if (y + h > 240u) h = 240u - y;
    for (row = 0; row < h; row++)
        for (col = 0; col < w; col++)
            fb[(y + row) * 240u + x + col] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t row, col;
    if (!w || !h || x >= 240u || y >= 240u) return;
    if (x + w > 240u) w = 240u - x;
    if (y + h > 240u) h = 240u - y;
    nblit++;
    if ((int)y < blit_y0) blit_y0 = (int)y;
    if ((int)(y + h - 1) > blit_y1) blit_y1 = (int)(y + h - 1);
    for (row = 0; row < h; row++)
        for (col = 0; col < w; col++) {
            uint16_t s = px[row * w + col];
            fb[(y + row) * 240u + x + col] = (uint16_t)((s >> 8) | (s << 8));
        }
}
/* the analyser's input on the host: a sine (Hz), or silence (0) */
static double feed_hz;
int smk_spectrum_host_snap(int16_t *dst, int n)
{
    int i;
    static uint32_t ph;
    for (i = 0; i < n; i++, ph++)
        dst[i] = feed_hz > 0 ? (int16_t)(12000.0 * sin(2.0 * 3.141592653589793 * feed_hz * ph / 46875.0)) : 0;
    return n;
}
#include "gfx.c"
#include "poli_ui.c"

static int fail, npass;
static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok) fail++; else npass++;
}
static uint32_t now_ms;
static uint32_t keys_now;
static void fresh(void)
{
    poli_nav_init();
    poli_preset_init(0);
    xtra_init();
    xtra_set_c2(3, 0);
    now_ms = 0;
    keys_now = 0;
    xtra_keys(0, 0, 0);
}
static void tick(uint32_t ms)
{
    while (ms--) { now_ms++; xtra_tick(now_ms); (void)xtra_keys(keys_now, 0, now_ms); }
}
/* key k of the 27-key bed for white key v (0..15), -1 none */
static int white_key(int v) { int k; for (k = 0; k < 27; k++) if (xtra_step_white(k) == v) return k; return -1; }
static int lane_bits(int lane) { return xtra_padrec_work_lane(lane); }
static uint32_t press(int k) { uint32_t r; keys_now |= 1u << k; r = xtra_keys(keys_now, 0, now_ms); return r; }
static uint32_t release(int k) { uint32_t r; keys_now &= ~(1u << k); r = xtra_keys(keys_now, 0, now_ms); return r; }

/* ---- B helpers: the engine's own note and gate, sampled each ms */
static int ev_note[512], ev_n;
static uint32_t ev_on[512], ev_off[512];
static uint8_t last_gate;
static int last_note = -1;
static void arp_watch(void)
{
    if (poli_gate && (!last_gate || poli_note != last_note)) {
        if (ev_n > 0 && !ev_off[ev_n - 1]) ev_off[ev_n - 1] = now_ms;
        if (ev_n < 512) { ev_note[ev_n] = poli_note; ev_on[ev_n] = now_ms; ev_off[ev_n] = 0; ev_n++; }
    }
    if (!poli_gate && last_gate && ev_n > 0 && !ev_off[ev_n - 1]) ev_off[ev_n - 1] = now_ms;
    last_gate = poli_gate;
    last_note = poli_note;
}
static uint64_t arp_keys;
static void arp_run(uint32_t ms)
{
    while (ms--) {
        now_ms++;
        poli_nav_tick_ms(now_ms);                  /* (volt.c gives the nav its time every pass) */
        polivoks_panel((uint32_t)poli_arp_keys(arp_keys, 0, now_ms), 0, 0, 0, 0, 0, 0, 0, 0, 0);
        arp_watch();
    }
}
static void arp_fresh(void)
{
    fresh();
    ev_n = 0; last_gate = 0; last_note = -1; arp_keys = 0;
    polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}
static int same_seq(const int *want, int n, int from)
{
    int i;
    if (ev_n < from + n) return 0;
    for (i = 0; i < n; i++) if (ev_note[from + i] != want[i]) return 0;
    return 1;
}

/* ---- C helpers */
static int region_diff(const uint16_t *a, const uint16_t *b, int y0, int y1)
{
    int n = 0, i;
    for (i = y0 * 240; i < (y1 + 1) * 240; i++) if (a[i] != b[i]) n++;
    return n;
}
static void render_ms(uint32_t ms)
{
    static int32_t buf[2 * 256];
    uint32_t n = ms * 47u;   /* ~46.875 frames a ms */
    while (n) { uint32_t k = n > 256u ? 256u : n; memset(buf, 0, sizeof buf); polivoks_render(buf, k); n -= k; }
}

int main(void)
{
    char d[240];
    int i, k, ok;
    uint8_t blob[XT_PADREC_BLOB], blob2[XT_PADREC_BLOB];
    fail = npass = 0;

    /* ================= A. PAD REC on the FM-1 keybed ================= */
    fresh();
    poli_nav_xtra_open(XW_STEP);
    poli_nav_select_turn(1);
    expect("A_walk", poli_nav_xtra() == XW_PADREC && XW_PADREC == 12 && NAV_XWIN == 14,
           "PAD REC is one SEL click after DRUM STEP (window 13 of 14)");
    ok = 1;
    for (i = 0; i < 16; i++) {
        int lane = xtra_step_lane(i), pad = xtra_padrec_pad_of_lane(lane);
        if (pad < 0 || xtra_padrec_lane_of_pad(pad) != lane) ok = 0;
    }
    expect("A_key_lane_order", ok && xtra_padrec_pad_of_lane(-1) < 0,
           "white key v -> DRUM STEP lane v -> the pad of that lane, and back (all 16)");
    /* black keys: nothing; white keys: no voice */
    {
        int kick = white_key(0), blackk = -1;
        for (k = 0; k < 27; k++) if (xtra_step_white(k) < 0) { blackk = k; break; }
        xtra_padrec_knob(1, 1);                    /* STEP mode: a press toggles the step at the cursor */
        k = lane_bits(L_KICK);
        ok = press(blackk) == 0; ok = ok && release(blackk) == 0;
        expect("A_black_keys_nothing", ok && lane_bits(L_KICK) == k && !poli_gate, "a black key changes nothing");
        ok = press(kick) == 0;
        polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        ok = ok && !poli_gate;
        ok = ok && release(kick) == 0;
        snprintf(d, sizeof d, "kick lane 0x%04x -> 0x%04x, keys to the voice: none", k, lane_bits(L_KICK));
        expect("A_white_key_is_pad", ok && (lane_bits(L_KICK) ^ k) == 1, d);
    }
    /* K4 = STEP cursor (the FM-1 has no LEFT / RIGHT) */
    xtra_knob(XW_PADREC, 3, 5);
    ok = xtra_padrec_step() == 5 && xtra_get(XW_PADREC, 3) == 5 && xtra_hi(XW_PADREC, 3) == 15;
    xtra_knob(XW_PADREC, 3, 40);
    ok = ok && xtra_padrec_step() == 15;
    xtra_knob(XW_PADREC, 3, -40);
    ok = ok && xtra_padrec_step() == 0;
    expect("A_k4_cursor", ok, "K4 moves the STEP cursor 1..16 and clamps");
    xtra_knob(XW_PADREC, 3, 2);
    {
        int sn = white_key(1), b0 = lane_bits(L_SNARE);
        press(sn); release(sn);
        expect("A_step_at_cursor", (lane_bits(L_SNARE) ^ b0) == (1 << 2), "the snare key toggles step 3 (the K4 cursor)");
        poli_nav_select_press();
        expect("A_sel_undo", lane_bits(L_SNARE) == b0, "SEL on PAD REC undoes the last change");
        xtra_knob(XW_PADREC, 1, -1);               /* back to LIVE: K4 leaves the cursor alone */
        xtra_knob(XW_PADREC, 3, 3);
        expect("A_k4_live_inert", xtra_padrec_step() == 2 && xtra_padrec_mode() == 0, "in LIVE, K4 does not move the cursor");
    }
    /* live recording from the keys */
    fresh();
    poli_nav_xtra_open(XW_STEP);
    poli_nav_select_turn(1);
    expect("A_rec_arms", xtra_padrec_rec(now_ms) == 1, "REC arms a take (count-in)");
    tick(2100);
    {
        int b = lane_bits(L_CLAP), clap = white_key(2);
        ok = xtra_padrec_armed() && xtra_running();
        press(clap); tick(3); release(clap);
        tick(240);
        press(clap); tick(3); release(clap);
        snprintf(d, sizeof d, "clap lane 0x%04x -> 0x%04x while recording", b, lane_bits(L_CLAP));
        expect("A_live_hits", ok && lane_bits(L_CLAP) != b && (lane_bits(L_CLAP) & b) == b, d);
    }
    expect("A_rec_stop", xtra_padrec_rec(now_ms) == 1 && !xtra_padrec_armed(), "REC again ends the take");
    /* a key held 500 ms wipes its lane */
    {
        int kick = white_key(0);
        ok = lane_bits(L_KICK) != 0;
        press(kick); tick(600); release(kick);
        expect("A_hold_wipes_lane", ok && lane_bits(L_KICK) == 0, "kick key held 600 ms: the kick lane is empty");
    }
    xtra_padrec_clear();
    ok = lane_bits(L_CLAP) == 0 && lane_bits(L_SNARE) == 0;
    poli_nav_select_press();
    expect("A_clear_undo", ok && lane_bits(L_CLAP) != 0, "SEQ = clear all, SEL brings it back");
    /* save, flash image, its checks */
    {
        int clap = lane_bits(L_CLAP);
        expect("A_save", xtra_padrec_save() == 1 && xtra_padrec_slot_used(0), "SAVE stores USER 1");
        expect("A_export", xtra_padrec_export(blob, sizeof blob) == XT_PADREC_BLOB && XT_PADREC_BLOB == 408,
               "the 8-slot image is 408 B (blob 0 on the FM-1)");
        fresh();
        ok = !xtra_padrec_slot_used(0);
        ok = ok && xtra_padrec_import(blob, sizeof blob) == 0 && xtra_padrec_slot_used(0);
        ok = ok && xtra_padrec_load(0) == 1 && lane_bits(L_CLAP) == clap;
        expect("A_import", ok, "after a reboot the image loads and USER 1 is the same groove");
        memcpy(blob2, blob, sizeof blob); blob2[0] ^= 1;
        ok = xtra_padrec_import(blob2, sizeof blob2) < 0;
        memcpy(blob2, blob, sizeof blob); blob2[4] ^= 0x7F;      /* version */
        ok = ok && xtra_padrec_import(blob2, sizeof blob2) < 0;
        ok = ok && xtra_padrec_import(blob, sizeof blob - 1) < 0;
        memset(blob2, 0xFF, sizeof blob2);                        /* erased flash */
        ok = ok && xtra_padrec_import(blob2, sizeof blob2) < 0;
        expect("A_foreign_data_ignored", ok, "wrong magic, wrong version, wrong length, erased: all refused");
    }
    /* the synth keys are back once the page is left */
    poli_nav_xtra_open(XW_GEN);
    expect("A_keys_back", press(white_key(0)) != 0, "on any other page the keys reach the voice again");
    release(white_key(0));

    /* ================= B. the classic arpeggiator ================= */
    arp_fresh();
    poli_nav_xtra_open(0);
    poli_nav_select_turn(1);
    expect("B_walk", poli_nav_xtra() == XW_CARP && XW_CARP == 13, "CLASSIC ARP is one SEL click after ARPEGGIO");
    expect("B_default_off", !poli_arp_on() && !xtra_on(XW_CARP), "off at power-on");
    poli_nav_select_press();
    expect("B_sel_on", poli_arp_on() && xtra_on(XW_CARP), "SEL (or ARP) on its page switches it on");
    poli_nav_select_press();
    expect("B_sel_off", !poli_arp_on(), "and off");
    /* one arp at a time */
    xtra_toggle(0);
    ok = xtra_on(0);
    xtra_carp_toggle();
    ok = ok && poli_arp_on() && !xtra_on(0);
    expect("B_excl_extras_arp", ok, "classic on: the EXTRAS ARP goes off");
    xtra_toggle(0);
    expect("B_excl_back", xtra_on(0) && !poli_arp_on(), "EXTRAS ARP on: the classic arp goes off");
    xtra_toggle(0);
    xtra_toggle(1);
    ok = xt.on[1];
    xtra_carp_toggle();
    ok = ok && poli_arp_on() && !xt.on[1];
    expect("B_excl_seq", ok, "classic on: the EXTRAS SEQ goes off");
    /* knobs */
    xtra_knob(XW_CARP, 0, -9); xtra_knob(XW_CARP, 1, 99); xtra_knob(XW_CARP, 2, 99); xtra_knob(XW_CARP, 3, -99);
    ok = xtra_get(XW_CARP, 0) == ARP_UP && xtra_get(XW_CARP, 1) == ARP_NRATE - 1 && xtra_get(XW_CARP, 2) == 4 &&
         xtra_get(XW_CARP, 3) == ARP_GATE_MIN;
    xtra_knob(XW_CARP, 0, 99); xtra_knob(XW_CARP, 3, 99);
    ok = ok && xtra_get(XW_CARP, 0) == ARP_NMODE - 1 && xtra_get(XW_CARP, 3) == ARP_GATE_MAX;
    expect("B_knob_ranges", ok, "MODE 5 modes, RATE 7, OCT 1-4, GATE 5..100 % (100 = tie), clamped");
    /* patterns: keys 0 and 4 = notes 65 and 69 (FM-1 base 65) */
    {
        uint8_t pat[64];
        int n;
        static const int up2[4] = { 65, 69, 77, 81 }, dn2[4] = { 81, 77, 69, 65 }, ud2[6] = { 65, 69, 77, 81, 77, 69 };
        arp_fresh();
        poli_arp_set_on(1);
        poli_arp_set(ARP_UP, 4, 2, 50, 0);
        arp_keys = (1u << 0) | (1u << 4);
        arp_run(1);
        n = poli_arp_pattern(pat, 64);
        ok = n == 4; for (i = 0; i < 4 && ok; i++) ok = pat[i] == up2[i];
        poli_arp_set(ARP_DOWN, 4, 2, 50, 0); n = poli_arp_pattern(pat, 64);
        for (i = 0; i < 4 && ok; i++) ok = n == 4 && pat[i] == dn2[i];
        poli_arp_set(ARP_UPDOWN, 4, 2, 50, 0); n = poli_arp_pattern(pat, 64);
        for (i = 0; i < 6 && ok; i++) ok = n == 6 && pat[i] == ud2[i];
        expect("B_patterns", ok, "UP 65 69 77 81, DOWN reversed, UP-DN 65 69 77 81 77 69 (2 octaves)");
        /* through the engine: polivoks_midi sets the note and the gate */
        arp_fresh();
        poli_arp_set_on(1);
        poli_arp_set(ARP_UP, 4, 2, 50, 0);
        arp_keys = (1u << 0) | (1u << 4);
        arp_run(1100);
        ok = same_seq(up2, 4, 0) && same_seq(up2, 4, 4);
        snprintf(d, sizeof d, "%d notes; first %d %d %d %d %d; step %u ms, gate %u ms", ev_n, ev_note[0], ev_note[1],
                 ev_note[2], ev_note[3], ev_note[4], ev_n > 1 ? ev_on[1] - ev_on[0] : 0u, ev_off[0] - ev_on[0]);
        expect("B_engine_notes", ok, d);
        ok = ev_n > 2 && ev_on[2] - ev_on[1] >= 124 && ev_on[2] - ev_on[1] <= 126 && ev_off[1] - ev_on[1] >= 55 &&
             ev_off[1] - ev_on[1] <= 70;
        expect("B_rate_gate", ok, "1/16 at 120 BPM = 125 ms a step, GATE 50 % ~62 ms");
        expect("B_midi_path", poli_midi_on || !poli_gate, "the arp's notes are MIDI notes (poli_midi_on), no new gate source");
        /* latch */
        poli_arp_latch_toggle();
        arp_keys = 0;
        k = ev_n;
        arp_run(600);
        expect("B_latch_plays", poli_arp_latched() && ev_n >= k + 4, "LATCH: keys up, the arp keeps playing");
        poli_arp_latch_toggle();
        arp_run(300);
        k = ev_n;
        arp_run(400);
        expect("B_latch_off_stops", !poli_arp_latched() && ev_n == k && !poli_gate, "LATCH off with no keys: it stops, gate down");
        /* off = keys unchanged */
        poli_arp_set_on(0);
        expect("B_off_passthrough", poli_arp_keys(0x11u, 0, now_ms) == 0x11u, "arp off: the keys go straight through");
        /* clears */
        {
            static const uint8_t clr[4][3] = { { 0xB0, 120, 0 }, { 0xB0, 123, 0 }, { 0xC0, 5, 0 }, { 0, 0, 0 } };
            int c, allok = 1;
            for (c = 0; c < 4; c++) {
                arp_fresh();
                poli_arp_set_on(1);
                poli_arp_set(ARP_UP, 4, 1, 50, 1);
                arp_keys = 1u;
                arp_run(300);
                arp_keys = 0;
                if (c < 3) poli_nav_midi(clr[c][0], clr[c][1], clr[c][2]);
                else { poli_preset_turn(1, now_ms); poli_preset_sel(); }
                arp_run(400);
                {
                    poli_arp_view_t av;
                    poli_arp_view(&av);
                    if (av.nheld != 0 || av.latch || poli_gate) allok = 0;
                }
            }
            expect("B_clears", allok, "CC 120, CC 123, program change and a preset load empty the set and LATCH");
        }
        /* MIDI clock: 6 clocks a 1/16 step */
        arp_fresh();
        poli_arp_set_on(1);
        poli_arp_set(ARP_UP, 4, 1, 50, 0);
        arp_keys = (1u << 0) | (1u << 2);
        poli_nav_midi(0xFA, 0, 0);
        for (i = 0; i < 48; i++) { poli_nav_midi(0xF8, 0, 0); arp_run(10); }   /* 250 BPM-ish clock: 10 ms a pulse */
        snprintf(d, sizeof d, "%d steps for 48 clocks (want 8)", ev_n);
        expect("B_midi_clock", ev_n >= 7 && ev_n <= 9, d);
        poli_nav_midi(0xFC, 0, 0);
        k = ev_n;
        arp_run(300);
        expect("B_midi_stop", ev_n == k, "Stop: no more steps");
    }

    /* ================= C. the screen ================= */
    fresh();
    feed_hz = 0;
    poli_nav_home();
    for (i = 0; i < 5; i++) poli_nav_select_turn(1);       /* FILTER (window 6) */
    {
        static uint16_t a[240 * 240], b[240 * 240];
        poli_nav_view_t v;
        int x, y, w, h;
        poli_nav_view(&v);
        poli_ui_draw();
        memcpy(a, fb, sizeof fb);
        filter_page_geom(0, 0, &x, &y, &w, &h);
        ok = v.flat == 5 && h >= 28;
        polivoks_panel(1u, 0, 0, 0, 0, 0, 0, 0, 0, 0);       /* a key: the filter EG runs */
        render_ms(30);
        poli_ui_filtenv_poll(100);
        memcpy(b, fb, sizeof fb);
        snprintf(d, sizeof d, "plot %dx%d at (%d,%d); a held key moves it: %d px changed", w, h, x, y, region_diff(a, b, 0, 239));
        expect("C_filter_plot", ok && region_diff(a, b, y, y + h) > 0 && region_diff(a, b, 0, y - 1) == 0, d);
        polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        render_ms(200);
    }
    poli_nav_home();
    for (i = 0; i < 8; i++) poli_nav_select_turn(1);       /* AMP EG (window 9) */
    {
        static uint16_t a[240 * 240], b[240 * 240], c[240 * 240];
        int x, y, w, h, da, dc;
        poli_ampenv_view_t ev;
        poli_nav_knob(0, 40);                              /* a slow attack so the dot travels */
        poli_ui_draw();
        memcpy(a, fb, sizeof fb);
        layout();
        ampenv_plot_rect(&x, &y, &w, &h);
        polivoks_panel(1u, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        render_ms(40);
        poli_ui_ampenv_poll(1000);
        memcpy(b, fb, sizeof fb);
        render_ms(200);
        poli_ui_ampenv_poll(1300);
        memcpy(c, fb, sizeof fb);
        polivoks_ampenv_view(&ev);
        da = region_diff(a, b, y, y + h - 1);
        dc = region_diff(b, c, y, y + h - 1);
        snprintf(d, sizeof d, "plot %dx%d at (%d,%d); dot moved: %d then %d px in the plot, %d outside", w, h, x, y, da, dc,
                 region_diff(a, c, 0, y - 1) + region_diff(a, c, y + h, 239));
        expect("C_amp_dot", w >= 8 && h >= 8 && da > 0 && dc > 0 && region_diff(a, c, 0, y - 1) == 0, d);
        polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        render_ms(3000);
        poli_ui_ampenv_poll(5000);
        polivoks_ampenv_view(&ev);
        expect("C_amp_idle", ev.stage == POLI_AMPENV_IDLE, "key up, release over: the envelope view is idle");
    }
    /* analyser: a 1 kHz sine lights bars, silence rests at the floor; EXTRAS pages keep their footer */
    {
        static uint16_t s0[240 * 240], s1[240 * 240];
        int x, y, w, h, lit = 0;
        poli_nav_home();
        poli_ui_draw();
        layout();
        spectrum_strip_rect(&x, &y, &w, &h);
        feed_hz = 0;
        for (i = 0; i < 20; i++) poli_ui_spectrum_poll(2000 + 50u * i);
        memcpy(s0, fb, sizeof fb);
        feed_hz = 1000.0;
        for (i = 0; i < 20; i++) poli_ui_spectrum_poll(4000 + 50u * i);
        memcpy(s1, fb, sizeof fb);
        for (i = 0; i < SMK_SA_BARS; i++) if (spec_shown[i]) lit++;
        snprintf(d, sizeof d, "band %dx%d at y %d; 1 kHz: %d of %d bars up, %d px changed in the band, 0 above", w, h, y, lit,
                 SMK_SA_BARS, region_diff(s0, s1, y, y + h - 1));
        expect("C_spectrum", lit > 0 && lit < SMK_SA_BARS && region_diff(s0, s1, y, y + h - 1) > 0 &&
               region_diff(s0, s1, 0, y - 1) == 0 && y + h <= 240, d);
        poli_nav_xtra_open(0);
        poli_ui_draw();
        memcpy(s0, fb, sizeof fb);
        poli_ui_spectrum_poll(9000);
        expect("C_spectrum_not_on_extras", memcmp(s0, fb, sizeof fb) == 0, "EXTRAS pages keep their K1-K4 footer");
        feed_hz = 0;
    }
    /* value-only repaint */
    {
        static uint16_t full[240 * 240];
        uint32_t g, vg;
        /* the analyser takes a new snapshot on every paint (as on the Elite): silence and empty bars keep the band
           still, so the two paints can be compared pixel for pixel */
        feed_hz = 0;
        memset(spec_shown, 0, sizeof spec_shown);
        fresh();
        poli_nav_home();
        poli_nav_knob(0, 1);                               /* first edit: the '*' comes */
        poli_ui_draw();
        g = poli_nav_generation(); vg = poli_nav_value_generation();
        poli_nav_knob(1, 3);                               /* a value only */
        ok = poli_nav_generation() == g && poli_nav_value_generation() != vg && poli_ui_values_ok();
        blit_y0 = 999; blit_y1 = -1; nblit = 0;
        poli_ui_draw_values();
        {
            int y0 = blit_y0, y1 = blit_y1, nb = nblit;
            uint16_t keep[240 * 240];
            memcpy(keep, fb, sizeof fb);
            poli_ui_draw();
            memcpy(full, fb, sizeof fb);
            layout();
            {
                int q, n = 0, qx0 = 999, qy0 = 999, qx1 = -1, qy1 = -1;
                for (q = 0; q < 240 * 240; q++) {
                    if (keep[q] == full[q]) continue;
                    n++;
                    if (q % 240 < qx0) qx0 = q % 240;
                    if (q % 240 > qx1) qx1 = q % 240;
                    if (q / 240 < qy0) qy0 = q / 240;
                    if (q / 240 > qy1) qy1 = q / 240;
                }
                if (n) printf("  (value repaint differs from a full draw in %d px, (%d,%d)-(%d,%d))\n", n, qx0, qy0, qx1, qy1);
            }
            snprintf(d, sizeof d, "rows %d..%d sent in %d strips (panel top %d); same pixels as a full draw: %s",
                     y0, y1, nb, L_py0, memcmp(keep, full, sizeof fb) ? "no" : "yes");
            expect("C_value_repaint", ok && y0 == L_py0 && y1 == 239 && memcmp(keep, full, sizeof fb) == 0, d);
        }
        fresh();
        poli_nav_home();
        poli_ui_draw();
        poli_nav_knob(1, 1);                               /* first edit of a clean slot: the header gains its '*' */
        ok = !poli_ui_values_ok();
        poli_ui_draw();
        poli_nav_reserved(MSG_LOCK_OFF);
        expect("C_value_repaint_falls_back", ok, "the first edit (the '*' appears) takes the full draw");
    }

    printf("HOST-PARITY %s %d pass %d fail\n", fail ? "FAIL" : "PASS", npass, fail);
    return fail ? 1 : 0;
}
