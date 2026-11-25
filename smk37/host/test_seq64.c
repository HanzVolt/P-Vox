/* SPDX-License-Identifier: GPL-3.0-only */
/* Seq64 (22 Nov 2026, SMK-37 Elite port of the FM-1 test): the synth sequencer's STEPS 1..64. The knob (every value, both ways, through the panel path
 * with no acceleration), the power-on pattern, playback at 1, 3, 16, 37 and 64 steps, REC over 64 steps and its wrap,
 * STEPS turned below the REC cursor, the key transpose over 64 steps, FROM SEQ with more distinct notes than the
 * walk's list holds, and the drum track keeping its own 16 steps. Host only. */
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
static void reset(void)
{
    poli_nav_init();
    xtra_init();
    now_ms = 0;
    xtra_set_c2(3, 0);
}
static void set(int w, int i, int v) { xtra_knob(w, i, v - xtra_get(w, i)); }
/* play n steps' worth (120 BPM, 1/16 = 125 ms) and count how often each position started */
static int seen[XT_SEQ_N], order_ok, maxpos;
static void play_count(uint32_t ms)
{
    uint32_t e = now_ms + ms;
    int prev = -1, cnt = 0;
    memset(seen, 0, sizeof seen);
    order_ok = 1; maxpos = -1;
    while (now_ms < e) {
        uint32_t g = xtra_gen();
        xtra_tick(++now_ms);
        if (xtra_gen() != g && xtra_seq_pos() != prev) {
            int p = xtra_seq_pos();
            if (prev >= 0 && p != (prev + 1) % xtra_get(1, 1)) order_ok = 0;
            prev = p; seen[p]++; cnt++;
            if (p > maxpos) maxpos = p;
        }
    }
    (void)cnt;
}

int main(void)
{
    char d[200];
    int i, ok, v, n;
    fail = npass = 0;

    /* ---- the knob's range and default */
    reset();
    expect("range", xtra_lo(1, 1) == 1 && xtra_hi(1, 1) == 64 && xtra_get(1, 1) == 16 && XT_SEQ_N == 64,
           "STEPS 1..64, power-on 16");
    ok = 1;
    for (i = 0; i < 16; i++) if (xtra_seq_note(i) != XT_SEQ_DEF[i]) ok = 0;
    for (i = 16; i < 64; i++) if (xtra_seq_note(i) != 255) ok = 0;
    expect("power_on_pattern", ok && xtra_seq_note(64) == 255 && xtra_seq_note(-1) == 255,
           "steps 1-16 the old pattern, 17-64 rests");
    /* ---- every integer, one per detent, through the panel path, fast turns included (no acceleration) */
    reset();
    poli_nav_xtra_open(1);
    set(1, 1, 1);
    ok = 1;
    for (i = 0; i < 63; i++) { nav_now_ms += 2; poli_nav_knob(1, 1); if (xtra_get(1, 1) != 2 + i) ok = 0; }
    nav_now_ms += 2; poli_nav_knob(1, 1);
    v = xtra_get(1, 1);
    for (i = 0; i < 63; i++) { nav_now_ms += 2; poli_nav_knob(1, -1); if (xtra_get(1, 1) != 63 - i) ok = 0; }
    nav_now_ms += 2; poli_nav_knob(1, -1);
    snprintf(d, sizeof d, "top %d bottom %d", v, xtra_get(1, 1));
    expect("knob_every_value", ok && v == 64 && xtra_get(1, 1) == 1, d);
    for (i = 0; i < 10; i++) { nav_now_ms += 1; poli_nav_knob(1, 1); }
    snprintf(d, sizeof d, "10 fast detents from 1 -> %d", xtra_get(1, 1));
    expect("knob_no_accel", xtra_get(1, 1) == 11, d);
    nav_now_ms += 1; poli_nav_knob(2, 1);         /* (RATE, a short span, as before) */
    expect("other_knobs", xtra_get(1, 2) == 5, "RATE still moves");
    xtra_knob(1, 1, 500); v = xtra_get(1, 1); xtra_knob(1, 1, -500);
    expect("clamp", v == 64 && xtra_get(1, 1) == 1, "big turns clamp at 64 and 1");

    /* ---- playback at 1, 3, 16, 37 and 64 steps: each position in order, every one played, wrap at STEPS */
    {
        static const int L[5] = { 1, 3, 16, 37, 64 };
        int t;
        for (t = 0; t < 5; t++) {
            reset();
            for (i = 0; i < 64; i++) xtra_set_seq_note(i, 40 + (i % 30));
            set(1, 1, L[t]);
            xtra_toggle(1);
            xtra_transport(1);
            play_count((uint32_t)(125 * 2 * L[t] + 60));
            xtra_transport(0);
            ok = order_ok && maxpos == L[t] - 1;
            for (i = 0; i < L[t]; i++) if (seen[i] < 1) ok = 0;
            for (i = L[t]; i < 64; i++) if (seen[i]) ok = 0;
            snprintf(d, sizeof d, "STEPS %d: max pos %d, in order %d, pos0 x%d", L[t], maxpos + 1, order_ok, seen[0]);
            expect(t == 0 ? "play_1" : t == 1 ? "play_3" : t == 2 ? "play_16" : t == 3 ? "play_37" : "play_64", ok, d);
        }
    }
    /* a single-note pattern (STEPS 3, one note, two rests) sounds that note once a cycle */
    reset();
    xtra_set_seq_note(0, 60); xtra_set_seq_note(1, 255); xtra_set_seq_note(2, 255);
    set(1, 1, 3); set(1, 3, 10);
    xtra_toggle(1); xtra_transport(1);
    n = 0;
    {
        uint8_t was = 0;
        for (i = 0; i < 375 * 4 - 1; i++) { xtra_tick(++now_ms); if (poli_midi_on && !was && poli_note == 60) n++; was = poli_midi_on; }
    }
    xtra_transport(0);
    snprintf(d, sizeof d, "4 cycles of 375 ms (step 1 at 1, 376, 751, 1126 ms) -> %d attacks of note 60", n);
    expect("single_note_3", n == 4, d);

    /* ---- REC over 64 steps: writes every step, wraps to 1 after 64 */
    reset();
    set(1, 1, 64);
    xtra_rec_toggle();
    for (i = 0; i < 64; i++) { xtra_midi(0x90u, (uint8_t)(30 + i), 100, now_ms); xtra_midi(0x80u, (uint8_t)(30 + i), 0, now_ms); }
    ok = xtra_rec_cur() == 0;
    for (i = 0; i < 64; i++) if (xtra_seq_note(i) != 30 + i) ok = 0;
    xtra_midi(0x90u, 100, 100, now_ms); xtra_midi(0x80u, 100, 0, now_ms);
    snprintf(d, sizeof d, "65th note at step 1: %d, cursor now %d", xtra_seq_note(0), xtra_rec_cur() + 1);
    expect("rec_64_wrap", ok && xtra_seq_note(0) == 100 && xtra_rec_cur() == 1, d);
    for (i = 1; i < 40; i++) xtra_rest();
    v = xtra_rec_cur();
    set(1, 1, 33);                                /* below the cursor (40): back to step 1, as before at 16 */
    snprintf(d, sizeof d, "cursor %d -> %d with STEPS 33", v + 1, xtra_rec_cur() + 1);
    expect("rec_steps_below_cursor", v == 40 && xtra_rec_cur() == 0, d);
    for (i = 0; i < 33; i++) xtra_rest();
    expect("rec_wrap_33", xtra_rec_cur() == 0 && xtra_seq_note(32) == 255 && xtra_seq_note(40) == 70 && xtra_seq_note(63) == 93,
           "wraps at 33; steps beyond STEPS keep their notes");
    xtra_rec_toggle();

    /* ---- key transpose: the lowest note over STEPS (step 50 counts at 64, not at 16) */
    reset();
    for (i = 0; i < 64; i++) xtra_set_seq_note(i, 60);
    xtra_set_seq_note(50, 48);
    xtra_toggle(1);
    set(1, 1, 64);
    xtra_transport(1);
    for (i = 0; i < 10; i++) xtra_tick(++now_ms);
    xtra_midi(0x90u, 50, 100, now_ms); xtra_midi(0x80u, 50, 0, now_ms);
    v = xtra_transpose();
    set(1, 1, 16);
    xtra_midi(0x90u, 50, 100, now_ms); xtra_midi(0x80u, 50, 0, now_ms);
    snprintf(d, sizeof d, "key 50: STEPS 64 -> %+d, STEPS 16 -> %+d", v, xtra_transpose());
    expect("transpose_64", v == 2 && xtra_transpose() == -10, d);
    xtra_transport(0);

    /* ---- FROM SEQ (ARP MODE 6) with 64 distinct notes: the walk's list stops at 48, no overrun, notes play */
    reset();
    for (i = 0; i < 64; i++) xtra_set_seq_note(i, 20 + i);
    set(1, 1, 64);
    xtra_toggle(0);
    set(0, 0, 6);
    set(0, 2, 3);                                 /* 3 octaves: the list is capped at 48 */
    xtra_transport(1);
    n = 0;
    {
        uint8_t was = 0;
        for (i = 0; i < 8000; i++) { xtra_tick(++now_ms); if (poli_midi_on && !was) n++; was = poli_midi_on; }
    }
    v = xtra_wander_notes();
    xtra_transport(0);
    snprintf(d, sizeof d, "wander notes %d, attacks %d", v, n);
    expect("from_seq_64_distinct", v == 16 && n > 20, d);   /* 48 / 3 octaves */
    set(0, 2, 1);
    expect("from_seq_48_list", xtra_wander_notes() == 48, "1 octave: 48 of the 64 distinct notes");

    /* ---- the drum track keeps 16 steps while SEQ has 64 */
    reset();
    for (i = 0; i < 64; i++) xtra_set_seq_note(i, 50);
    set(1, 1, 64);
    xtra_toggle(1); xtra_toggle(2);
    xtra_transport(1);
    {
        int dmax = -1, smax = -1;
        for (i = 0; i < 125 * 64 + 30; i++) {
            xtra_tick(++now_ms);
            if (xtra_drum_pos() > dmax) dmax = xtra_drum_pos();
            if (xtra_seq_pos() > smax) smax = xtra_seq_pos();
        }
        snprintf(d, sizeof d, "drum pos max %d, seq pos max %d", dmax + 1, smax + 1);
        expect("drums_16", dmax == 15 && smax == 63 && xtra_hi(2, 0) == 56, d);
    }
    xtra_transport(0);


#if BOARD_SMK37
    /* ---- Elite: REC from the Elite keybed over 64 steps */
    reset();
    set(1, 1, 64);
    xtra_rec_toggle();
    for (i = 0; i < 64; i++) { (void)xtra_keys_smk((uint64_t)1u << (i % 37), 0, 0, now_ms); (void)xtra_keys_smk(0, 0, 0, now_ms); }
    ok = xtra_rec_cur() == 0;
    for (i = 0; i < 64; i++) if (xtra_seq_note(i) == 255) ok = 0;
    snprintf(d, sizeof d, "64 Elite keys -> cursor %d, steps 1 / 38 / 64 = %d / %d / %d", xtra_rec_cur() + 1,
             xtra_seq_note(0), xtra_seq_note(37), xtra_seq_note(63));
    expect("smk_keys_rec_64", ok && xtra_seq_note(0) == xtra_seq_note(37), d);
    xtra_rec_toggle();
    /* ---- Elite: the PAD REC flash blob is not touched by the sequencer (still 408 B, same bytes) */
    {
        static uint8_t a[XT_PADREC_BLOB], b[XT_PADREC_BLOB];
        int na, nb;
        reset();
        na = xtra_padrec_export(a, sizeof a);
        for (i = 0; i < 64; i++) xtra_set_seq_note(i, 30 + i);
        set(1, 1, 64);
        nb = xtra_padrec_export(b, sizeof b);
        snprintf(d, sizeof d, "export %d / %d B (XT_PADREC_BLOB %d)", na, nb, (int)XT_PADREC_BLOB);
        expect("smk_padrec_blob_unchanged", na == 408 && nb == 408 && !memcmp(a, b, sizeof a) &&
               xtra_padrec_import(a, sizeof a) >= 0 && xtra_seq_note(63) == 93, d);
    }
#endif

    printf(fail ? "HOST-SEQ64 FAIL %d (%d pass)\n" : "HOST-SEQ64 PASS %d pass 0 fail\n", fail ? fail : npass, npass);
    return fail ? 1 : 0;
}
