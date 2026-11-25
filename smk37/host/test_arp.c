/* SPDX-License-Identifier: GPL-3.0-only */
/* Arpeggiator host checks (firmware/src/poli_arp.c, compiled unchanged). Not firmware.
 * Built with -DBOARD_SMK37=1 (37 keys, base note 60). Drives poli_arp_keys -> polivoks_panel once a
 * millisecond, as volt.c's main loop does, and watches the engine's own note and gate.
 * Checks: step order in every mode, octave range (and the note-126 top), latch, gate length and the
 * 12 ms minimum gap, internal tempo, MIDI clock follow with Start / Stop / Continue, the ARP button
 * (short = on/off, hold = page), knob clamps, arp off = keys unchanged, and the unconditional clear on
 * CC 120, CC 123, program change and every preset change (pad load, SEL load, compare, factory). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"
#include "poli_preset.c"

static int fails, passes;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t now;
static uint64_t keys;
static uint8_t vel[37];
#define NOTE_KEY(n) ((uint64_t)1u << ((n) - 60))

/* step log: one entry per new sounding arp note (gate rise or a note change while gated) */
static int ev_note[4096], ev_n;
static uint32_t ev_on[4096], ev_off[4096];
static uint8_t last_gate;
static int last_note = -1;

static void tick(void)
{
    poli_nav_tick_ms(now);
    polivoks_panel(poli_arp_keys(keys, vel, now), 0, 0, 0, 0, 0, 0, 0, 0, 0);
    if (poli_gate && (!last_gate || poli_note != last_note)) {
        if (ev_n < 4096) { ev_note[ev_n] = poli_note; ev_on[ev_n] = now; ev_off[ev_n] = 0; ev_n++; }
    }
    if (!poli_gate && last_gate && ev_n && !ev_off[ev_n - 1]) ev_off[ev_n - 1] = now;
    last_gate = poli_gate;
    last_note = poli_note;
}
static void run(uint32_t ms) { while (ms--) { now++; tick(); } }
static void midi(uint8_t st, uint8_t d1, uint8_t d2) { poli_nav_tick_ms(now); poli_nav_midi(st, d1, d2); tick(); }
static void log_reset(void) { ev_n = 0; }

static void fresh(void)
{
    poli_nav_init();
    poli_preset_init(0);
    keys = 0;
    memset(vel, 100, sizeof vel);
    now = 1000;
    last_gate = 0; last_note = -1;
    run(5);
    log_reset();
}

static int seq_is(const int *want, int n, const char *what)
{
    int i, ok = ev_n >= n;
    for (i = 0; ok && i < n; i++) ok = ev_note[i] == want[i];
    if (!ok) {
        printf("  %s got:", what);
        for (i = 0; i < ev_n && i < 16; i++) printf(" %d", ev_note[i]);
        printf("  want:");
        for (i = 0; i < n; i++) printf(" %d", want[i]);
        printf("\n");
    }
    return ok;
}

/* press G3, C3, E3 in that order (as-played G C E), each 1 ms apart, then play */
static void chord_gce(void)
{
    keys |= NOTE_KEY(67); run(1);
    keys |= NOTE_KEY(60); run(1);
    keys |= NOTE_KEY(64); run(1);
}

static void test_modes(void)
{
    /* the first key starts the arp at once (G alone). Keys added later join the set; the step counter keeps
       running (it restarts only when the set was empty), so the walk goes on at index 1 of C E G. */
    static const int up[] = { 67, 64, 67, 60, 64, 67, 60 };
    static const int up_s[] = { 60, 64, 67, 60, 64, 67 };
    static const int dn_s[] = { 67, 64, 60, 67, 64, 60 };
    static const int ud_s[] = { 60, 64, 67, 64, 60, 64, 67, 64, 60 };
    static const int pl_s[] = { 67, 60, 64, 67, 60, 64 };
    uint8_t pat[300];
    int L, i, seen[128], m;

    /* pattern table (what the step clock walks) */
    fresh(); poli_arp_set_on(1); poli_arp_set(ARP_UP, 4, 1, 50, 0);
    chord_gce();
    L = poli_arp_pattern(pat, 300);
    CHECK(L == 3 && pat[0] == 60 && pat[1] == 64 && pat[2] == 67, "UP pattern %d: %d %d %d", L, pat[0], pat[1], pat[2]);
    poli_arp_set(ARP_DOWN, 4, 1, 50, 0); L = poli_arp_pattern(pat, 300);
    CHECK(L == 3 && pat[0] == 67 && pat[1] == 64 && pat[2] == 60, "DOWN pattern");
    poli_arp_set(ARP_UPDOWN, 4, 1, 50, 0); L = poli_arp_pattern(pat, 300);
    CHECK(L == 4 && pat[0] == 60 && pat[1] == 64 && pat[2] == 67 && pat[3] == 64, "UP/DOWN pattern (ends once)");
    poli_arp_set(ARP_PLAYED, 4, 1, 50, 0); L = poli_arp_pattern(pat, 300);
    CHECK(L == 3 && pat[0] == 67 && pat[1] == 60 && pat[2] == 64, "AS PLAYED pattern");

    /* played steps: the first key starts the arp at once (step 0 = G alone), then the full chord walks */
    fresh(); poli_arp_set_on(1); poli_arp_set(ARP_UP, 4, 1, 50, 0);
    chord_gce(); run(125 * 6);
    CHECK(seq_is(up, 7, "UP played"), "UP played order");

    /* start each mode with the chord already down: press all three in one tick */
    {
        struct { int mode; const int *w; int n; const char *name; } M[] = {
            { ARP_UP, up_s, 6, "UP" }, { ARP_DOWN, dn_s, 6, "DOWN" }, { ARP_UPDOWN, ud_s, 9, "UP/DOWN" },
        };
        for (m = 0; m < 3; m++) {
            fresh(); poli_arp_set(M[m].mode, 4, 1, 50, 0); poli_arp_set_on(1);
            keys = NOTE_KEY(60) | NOTE_KEY(64) | NOTE_KEY(67); run(125 * 10);
            CHECK(seq_is(M[m].w, M[m].n, M[m].name), "%s order", M[m].name);
        }
    }
    /* AS PLAYED needs a press order: G then C then E, then restart the walk by toggling */
    fresh(); poli_arp_set(ARP_PLAYED, 4, 1, 50, 0); chord_gce(); poli_arp_set_on(1); log_reset(); run(125 * 7);
    CHECK(seq_is(pl_s, 6, "AS PLAYED"), "AS PLAYED order");

    /* RANDOM: only held notes, every one of them, deterministic */
    fresh(); poli_arp_set(ARP_RANDOM, 6, 1, 50, 0); poli_arp_set_on(1);
    keys = NOTE_KEY(60) | NOTE_KEY(64) | NOTE_KEY(67); run(63 * 120);
    memset(seen, 0, sizeof seen);
    for (i = 0; i < ev_n; i++) seen[ev_note[i] & 127]++;
    CHECK(ev_n >= 100, "RANDOM steps %d", ev_n);
    CHECK(seen[60] && seen[64] && seen[67] && seen[60] + seen[64] + seen[67] == ev_n, "RANDOM pool %d %d %d of %d", seen[60], seen[64], seen[67], ev_n);

    /* one note and two notes in UP/DOWN */
    fresh(); poli_arp_set(ARP_UPDOWN, 4, 1, 50, 0); poli_arp_set_on(1);
    keys = NOTE_KEY(62); run(125 * 4);
    { static const int w[] = { 62, 62, 62, 62 }; CHECK(seq_is(w, 4, "UPDOWN one"), "UP/DOWN one note repeats"); }
    fresh(); poli_arp_set(ARP_UPDOWN, 4, 1, 50, 0); poli_arp_set_on(1);
    keys = NOTE_KEY(62) | NOTE_KEY(65); run(125 * 5);
    { static const int w[] = { 62, 65, 62, 65 }; CHECK(seq_is(w, 4, "UPDOWN two"), "UP/DOWN two notes alternate"); }
}

static void test_octaves(void)
{
    uint8_t pat[300];
    int o, L, i, ok;
    fresh(); poli_arp_set_on(1);
    keys = NOTE_KEY(60) | NOTE_KEY(64) | NOTE_KEY(67); run(2);
    for (o = 1; o <= 4; o++) {
        poli_arp_set(ARP_UP, 4, o, 50, 0);
        L = poli_arp_pattern(pat, 300);
        ok = L == 3 * o && pat[0] == 60 && pat[L - 1] == 67 + 12 * (o - 1);
        for (i = 1; ok && i < L; i++) ok = pat[i] > pat[i - 1];
        CHECK(ok, "octaves %d: len %d top %d", o, L, L ? pat[L - 1] : -1);
        poli_arp_set(ARP_UPDOWN, 4, o, 50, 0);
        L = poli_arp_pattern(pat, 300);
        CHECK(L == 2 * 3 * o - 2, "UP/DOWN octaves %d len %d", o, L);
    }
    /* played order across 2 octaves */
    fresh(); poli_arp_set(ARP_UP, 4, 2, 50, 0); poli_arp_set_on(1);
    keys = NOTE_KEY(60) | NOTE_KEY(64) | NOTE_KEY(67); run(125 * 7);
    { static const int w[] = { 60, 64, 67, 72, 76, 79, 60 }; CHECK(seq_is(w, 7, "oct2"), "2 octaves played"); }
    /* the top: key 36 with OCT +2 is note 120; its +12 copies (132..) are dropped, never clamped */
    fresh(); polivoks_set_kb_oct(2); poli_arp_set(ARP_UP, 4, 4, 50, 0); poli_arp_set_on(1);
    keys = (uint64_t)1u << 36; run(2);
    L = poli_arp_pattern(pat, 300);
    CHECK(L == 1 && pat[0] == 120, "top note: len %d note %d", L, pat[0]);
    polivoks_set_kb_oct(0);
    /* all 37 keys x 4 octaves fits the table */
    fresh(); poli_arp_set(ARP_UPDOWN, 4, 4, 50, 0); poli_arp_set_on(1);
    keys = ((uint64_t)1u << 37) - 1u; run(2);
    L = poli_arp_pattern(pat, 300);
    CHECK(L > 0 && L <= 2 * ARP_MAXN, "37 keys x 4 UP/DOWN len %d", L);
    for (i = 0, ok = 1; i < L; i++) ok &= pat[i] <= 126;
    CHECK(ok, "no note above 126");
}

static void test_timing(void)
{
    int i, ok;
    uint32_t d;
    /* 120 BPM internal, 1/16 = 125 ms, gate 50 % = 62 ms */
    fresh(); poli_arp_set(ARP_UP, 4, 1, 50, 0); poli_arp_set_on(1);
    keys = NOTE_KEY(60) | NOTE_KEY(67); run(125 * 9 + 10);
    CHECK(ev_n >= 9, "steps %d", ev_n);
    for (i = 1, ok = 1; i < ev_n; i++) { d = ev_on[i] - ev_on[i - 1]; ok &= d == 125u; }
    CHECK(ok, "1/16 at 120 BPM every 125 ms");
    for (i = 0, ok = 1; i + 1 < ev_n; i++) { d = ev_off[i] - ev_on[i]; ok &= d == 62u; }
    CHECK(ok, "gate 50 %% = 62 ms (first %u)", ev_off[0] - ev_on[0]);
    /* 1/32 = 62.5 ms: 62/63 alternate, no drift */
    fresh(); poli_arp_set(ARP_UP, 6, 1, 50, 0); poli_arp_set_on(1);
    keys = NOTE_KEY(60) | NOTE_KEY(67); run(62 * 41);
    CHECK(ev_n >= 41 && ev_on[40] - ev_on[0] == 2500u, "1/32: 40 steps in %u ms (want 2500)", ev_n > 40 ? ev_on[40] - ev_on[0] : 0);
    /* every rate: step length = 500 ms x clocks / 24 */
    {
        static const int cps[ARP_NRATE] = { 24, 16, 12, 8, 6, 4, 3 };
        int r;
        for (r = 0; r < ARP_NRATE; r++) {
            fresh(); poli_arp_set(ARP_UP, r, 1, 50, 0); poli_arp_set_on(1);
            keys = NOTE_KEY(60) | NOTE_KEY(67); run(13u * 500u * (unsigned)cps[r] / 24u + 50u);
            /* 12 steps' span in ms */
            if (ev_n > 12) { d = ev_on[12] - ev_on[0]; CHECK(d == 12u * 500u * (unsigned)cps[r] / 24u, "rate %d: 12 steps %u ms", r, d); }
            else CHECK(0, "rate %d only %d steps", r, ev_n);
        }
    }
    /* minimum gap: 1/32 (62 ms) at 95 % would leave 3 ms; the gap is held at 12 ms */
    fresh(); poli_arp_set(ARP_UP, 6, 1, 95, 0); poli_arp_set_on(1);
    keys = NOTE_KEY(60) | NOTE_KEY(67); run(62 * 10);
    for (i = 0, ok = 1; i + 1 < ev_n; i++) ok &= ev_on[i + 1] - ev_off[i] >= ARP_MIN_OFF_MS && ev_off[i] > ev_on[i];
    CHECK(ok && ev_n >= 8, "gap >= 12 ms at 95 %% (gap0 %u)", ev_n > 1 ? ev_on[1] - ev_off[0] : 0);
    /* gate 100 % = tied: no release between steps, the note changes under one gate */
    fresh(); poli_arp_set(ARP_UP, 4, 1, 100, 0); poli_arp_set_on(1);
    keys = NOTE_KEY(60) | NOTE_KEY(67); run(125 * 4 + 5);
    for (i = 0, ok = 1; i + 1 < ev_n; i++) ok &= ev_off[i] == 0u;
    CHECK(ok && ev_n >= 4 && poli_gate, "100 %% tied");
    keys = 0; run(3);
    CHECK(!poli_gate, "tied gate releases when the keys go up");
    /* gate 5 % (shortest) at 1/4: 25 ms */
    fresh(); poli_arp_set(ARP_UP, 0, 1, 5, 0); poli_arp_set_on(1);
    keys = NOTE_KEY(60); run(1100);
    CHECK(ev_n >= 2 && ev_off[0] - ev_on[0] == 25u, "gate 5 %% at 1/4 = %u ms", ev_off[0] - ev_on[0]);
    /* velocity of the key reaches the engine (the note that starts the gate) */
    fresh(); poli_arp_set_on(1); vel[0] = 33;
    keys = NOTE_KEY(60); run(2);
    CHECK(poli_vel_next == 33, "arp note velocity %d", poli_vel_next);
}

static void test_latch(void)
{
    poli_arp_view_t v;
    fresh(); poli_arp_set(ARP_UP, 4, 1, 50, 1); poli_arp_set_on(1);
    keys = NOTE_KEY(60) | NOTE_KEY(64); run(10);
    keys = 0; run(125 * 4);
    { static const int w[] = { 60, 64, 60, 64 }; CHECK(seq_is(w, 4, "latch keeps"), "latch keeps playing after release"); }
    poli_arp_view(&v);
    CHECK(v.nheld == 2 && v.latch, "latched set of 2");
    /* a new press after every key is up replaces the set */
    log_reset(); keys = NOTE_KEY(72); run(5); keys = 0; run(125 * 3);
    { static const int w[] = { 72, 72, 72 }; CHECK(seq_is(w, 3, "latch new"), "new chord replaces the latched set"); }
    /* adding while a key is held extends the set */
    log_reset(); keys = NOTE_KEY(60); run(5); keys |= NOTE_KEY(67); run(5); keys = 0; run(125 * 4);
    poli_arp_view(&v);
    CHECK(v.nheld == 2, "held + added = 2 (%d)", v.nheld);
    /* latch off with no key down: the set empties and the gate closes */
    poli_arp_latch_toggle(); run(130);
    poli_arp_view(&v);
    CHECK(!v.latch && v.nheld == 0 && !poli_gate, "latch off stops (n %d gate %d)", v.nheld, poli_gate);
    /* latch off keeps the keys still held */
    poli_arp_latch_toggle(); keys = NOTE_KEY(60) | NOTE_KEY(64); run(5); keys = NOTE_KEY(64); run(5);
    poli_arp_latch_toggle(); poli_arp_view(&v);
    CHECK(v.nheld == 1, "latch off leaves the held key (%d)", v.nheld);
    /* without latch, releasing all keys stops at once */
    keys = 0; run(2);
    CHECK(!poli_gate, "no latch: key up stops");
    /* arp off drops latched notes */
    poli_arp_latch_toggle(); keys = NOTE_KEY(60); run(5); keys = 0; run(50);
    poli_arp_set_on(0); run(2);
    poli_arp_set_on(1); run(200);
    poli_arp_view(&v);
    CHECK(v.nheld == 0 && !poli_gate, "arp off drops the latched set");
}

static void test_off_passthrough(void)
{
    uint64_t r;
    fresh();
    CHECK(!poli_arp_on(), "arp boots off");
    keys = NOTE_KEY(64) | NOTE_KEY(60);
    r = poli_arp_keys(keys, vel, now);
    CHECK(r == keys, "arp off: keys unchanged");
    run(2);
    CHECK(poli_gate && poli_note == 60, "arp off: lowest-note rule (%d)", poli_note);
    poli_arp_set_on(1);
    CHECK(poli_arp_keys(keys, vel, now) == 0u, "arp on: the panel sees no keys");
    keys = 0; run(2);
}

static void held_chord_playing(void)
{
    poli_arp_set_on(1); poli_arp_set(ARP_UP, 4, 1, 50, 1);
    keys = NOTE_KEY(60) | NOTE_KEY(64); run(20);
}
static void expect_cleared(const char *what)
{
    poli_arp_view_t v;
    poli_arp_view(&v);
    CHECK(!poli_gate, "%s: gate closed", what);
    CHECK(v.nheld == 0 && !v.latch && v.note < 0, "%s: set %d latch %d note %d", what, v.nheld, v.latch, v.note);
    log_reset(); run(400);
    CHECK(ev_n == 0 && !poli_gate, "%s: held keys do not replay (%d steps)", what, ev_n);
    keys = 0; run(5); keys = NOTE_KEY(62); run(5);
    CHECK(poli_gate && poli_note == 62, "%s: a fresh press plays again", what);
    keys = 0; run(5);
}

static void test_clear(void)
{
    fresh(); held_chord_playing(); midi(0xB0, 120, 0); expect_cleared("CC120");
    fresh(); held_chord_playing(); midi(0xB0, 123, 0); expect_cleared("CC123");
    fresh(); held_chord_playing(); midi(0xC0, 5, 0); expect_cleared("program change");
    fresh(); held_chord_playing(); poli_preset_load_slot(7, now); expect_cleared("pad preset load");
    fresh(); held_chord_playing(); poli_preset_turn(2, now); poli_preset_sel(); expect_cleared("SEL preset load");
    fresh(); held_chord_playing(); poli_preset_save_btn(now); poli_preset_sel(); expect_cleared("save-screen compare");
    poli_preset_cancel();
    /* latched with no key down: clear stops it too */
    fresh(); poli_arp_set_on(1); poli_arp_set(ARP_UP, 4, 1, 50, 1);
    keys = NOTE_KEY(60); run(5); keys = 0; run(300);
    CHECK(ev_n >= 2, "latched running");
    midi(0xB0, 120, 0); log_reset(); run(500);
    CHECK(ev_n == 0 && !poli_gate, "CC120 stops a latched arp (%d)", ev_n);
    /* CC 120 on another channel is not ours (receive channel 1): no clear, as for the engine */
    fresh(); held_chord_playing(); midi(0xB3, 120, 0); log_reset(); run(300);
    CHECK(ev_n >= 2, "CC120 on channel 4 ignored (%d steps)", ev_n);
    /* the engine's repeat latches behave exactly as before (that fix is separate) */
    fresh(); poli_rep_f = 1; poli_rep_a = 1; midi(0xB0, 123, 0);
    CHECK(poli_rep_f == 1 && poli_rep_a == 1, "CC123 still leaves the repeat latches");
    midi(0xC0, 1, 0);
    CHECK(poli_rep_f == 1 && poli_rep_a == 1, "program change still leaves the repeat latches");
    midi(0xB0, 120, 0);
    CHECK(poli_rep_f == 0 && poli_rep_a == 0, "CC120 still clears the repeat latches");
    /* KEY HOLD is a separate synth control: arp notes must not latch into it. */
    fresh(); polivoks_set_keyhold(1); poli_arp_set_on(1);
    keys = NOTE_KEY(60); run(80);
    CHECK(!poli_gate, "arp gate releases even when KEY HOLD is on");
    keys = 0; run(300);
    CHECK(!poli_gate, "arp key release with KEY HOLD stays released");
    held_chord_playing(); midi(0xB0, 123, 0); expect_cleared("CC123 with KEY HOLD");
    /* arp settings survive a clear (they are global); only latch goes off */
    fresh(); poli_arp_set(ARP_DOWN, 2, 3, 70, 1); poli_arp_set_on(1); midi(0xB0, 120, 0);
    { poli_arp_view_t v; poli_arp_view(&v);
      CHECK(v.on && v.mode == ARP_DOWN && v.rate == 2 && v.oct == 3 && v.gate == 70 && !v.latch, "settings kept, latch off"); }
    /* preset bank is untouched by the arp: still 63 slots of 64 B */
    CHECK(sizeof(pv_bank_t) == 4044u && PV_NSLOT == 63, "preset layout unchanged");
}

static void clocks(int n, uint32_t *t_q8, uint32_t per_q8)
{
    while (n--) {
        uint32_t target = *t_q8 >> 8;
        while (now < target) { now++; tick(); }
        midi(0xF8, 0, 0);
        *t_q8 += per_q8;
    }
}

static void test_clock(void)
{
    uint32_t t, per = (uint32_t)(500.0 / 24.0 * 256.0 + 0.5);   /* 120 BPM */
    uint32_t per140 = (uint32_t)(60000.0 / 140.0 / 24.0 * 256.0 + 0.5);
    int i, ok, n0;
    poli_arp_view_t v;
    fresh(); poli_arp_set(ARP_UP, 4, 1, 50, 0); poli_arp_set_on(1);
    keys = NOTE_KEY(60) | NOTE_KEY(67);
    t = (now + 1) << 8;
    midi(0xFA, 0, 0);                    /* Start */
    log_reset();
    clocks(1, &t, per);
    CHECK(ev_n == 1 && ev_on[0] == now, "step on the first clock after Start");
    clocks(6 * 8 - 1, &t, per);
    CHECK(ev_n == 8, "1/16 = every 6 clocks: %d steps in 48 clocks", ev_n);
    poli_arp_view(&v);
    CHECK(v.ext && v.running, "view: MIDI clock");
    /* Stop: gate closes, clocks keep coming, no steps */
    midi(0xFC, 0, 0);
    CHECK(!poli_gate, "Stop closes the gate");
    n0 = ev_n; clocks(24, &t, per);
    CHECK(ev_n == n0, "no steps while stopped");
    poli_arp_view(&v);
    CHECK(v.ext && !v.running, "view: MIDI stop");
    /* Continue: steps resume on the 6-clock grid */
    midi(0xFB, 0, 0); clocks(24, &t, per);
    CHECK(ev_n == n0 + 4, "Continue resumes (%d)", ev_n - n0);
    /* Start again: back to step 0 of the pattern (C) */
    midi(0xFA, 0, 0); log_reset(); clocks(1, &t, per);
    CHECK(ev_n == 1 && ev_note[0] == 60, "Start restarts the pattern");
    /* gate off timed from the clock tempo: 125 ms x 50 % */
    clocks(6, &t, per);
    CHECK(ev_off[0] && (ev_off[0] - ev_on[0] >= 61u && ev_off[0] - ev_on[0] <= 63u), "clock gate %u ms", ev_off[0] - ev_on[0]);
    /* tempo 140: clock steps follow; then the clock stops and the internal tempo holds 140 */
    fresh(); poli_arp_set(ARP_UP, 4, 1, 50, 0); poli_arp_set_on(1);
    keys = NOTE_KEY(60) | NOTE_KEY(67);
    t = (now + 1) << 8; clocks(24 * 8, &t, per140);
    poli_arp_view(&v);
    CHECK(v.bpm == 140, "measured tempo %d", v.bpm);
    run(ARP_EXT_GAP_MS + 10); log_reset(); run(1000);
    poli_arp_view(&v);
    CHECK(!v.ext, "clock gone: internal");
    for (i = 1, ok = ev_n >= 8; i < ev_n; i++) { uint32_t d = ev_on[i] - ev_on[i - 1]; ok &= d == 107u || d == 108u; }
    CHECK(ok, "internal tempo holds the last MIDI tempo (140 BPM 1/16 = 107.1 ms), %d steps", ev_n);
    /* Stop without further clocks must NOT fall back into internal playback. */
    midi(0xFC, 0, 0); log_reset(); run(2000);
    CHECK(ev_n == 0 && !poli_gate, "Stop stays stopped through clock timeout");
    midi(0xFB, 0, 0); run(500);
    CHECK(ev_n >= 4, "Continue resumes internal tempo after clock timeout");
    /* no MIDI clock ever: 120 BPM */
    fresh(); poli_arp_view(&v);
    CHECK(v.bpm == 120 && !v.ext, "default internal tempo 120");
}

static void test_button_page(void)
{
    poli_arp_view_t v;
    poli_nav_view_t nv;
    fresh();
    poli_arp_button(1, 0, 1, now); run(100); poli_arp_button(0, 1, 0, now);
    CHECK(poli_arp_on() && !poli_arp_page_open(), "short press: on, no page");
    poli_arp_button(1, 0, 1, now); run(100); poli_arp_button(0, 1, 0, now);
    CHECK(!poli_arp_on(), "short press again: off");
    poli_arp_button(1, 0, 1, now);
    run(499); poli_arp_button(0, 0, 1, now);
    CHECK(!poli_arp_page_open(), "499 ms: no page yet");
    run(1); poli_arp_button(0, 0, 1, now);
    CHECK(poli_arp_page_open(), "500 ms hold opens the ARP page");
    run(300); poli_arp_button(0, 1, 0, now);
    CHECK(!poli_arp_on() && poli_arp_page_open(), "release after a hold: no toggle, page stays");
    poli_arp_button(1, 0, 1, now); run(50); poli_arp_button(0, 1, 0, now);
    CHECK(poli_arp_on() && poli_arp_page_open(), "short press on the page toggles, page stays");
    /* knobs: K5 mode, K6 rate, K7 octaves, K8 gate; clamped, no wrap */
    poli_arp_knob(0, 9); poli_arp_knob(1, -9); poli_arp_knob(2, 9); poli_arp_knob(3, 99);
    poli_arp_view(&v);
    CHECK(v.mode == ARP_PLAYED && v.rate == 0 && v.oct == 4 && v.gate == 100, "knob tops %d %d %d %d", v.mode, v.rate, v.oct, v.gate);
    poli_arp_knob(0, -9); poli_arp_knob(1, 9); poli_arp_knob(2, -9); poli_arp_knob(3, -99);
    poli_arp_view(&v);
    CHECK(v.mode == ARP_UP && v.rate == ARP_NRATE - 1 && v.oct == 1 && v.gate == ARP_GATE_MIN, "knob bottoms");
    poli_arp_knob(3, 1); poli_arp_view(&v);
    CHECK(v.gate == 10, "gate steps of 5 (%d)", v.gate);
    /* a page change (section button, SELECT turn, < >, a fader page jump) leaves the page */
    poli_nav_section(NAV_SEC_FX);
    CHECK(!poli_arp_page_open(), "section button leaves the ARP page");
    poli_nav_view(&nv);
    CHECK(nv.flat == 5, "and opens FILTER as before (%d)", nv.flat);
    poli_arp_button(1, 0, 1, now); run(600); poli_arp_button(0, 0, 1, now); poli_arp_button(0, 1, 0, now);
    CHECK(poli_arp_page_open(), "page again");
    poli_nav_select_turn(1);
    CHECK(!poli_arp_page_open(), "SELECT turn leaves the ARP page");
    poli_arp_button(1, 0, 1, now); run(600); poli_arp_button(0, 0, 1, now); poli_arp_button(0, 1, 0, now);
    poli_nav_view(&nv);
    poli_nav_show_slot(nv.slot_kind[0] ? nv.slot_kind[0] : SLOT_CUT);   /* a fader on the page underneath */
    CHECK(!poli_arp_page_open(), "fader page jump leaves the ARP page");
}

int main(void)
{
    test_off_passthrough();
    test_modes();
    test_octaves();
    test_timing();
    test_latch();
    test_clear();
    test_clock();
    test_button_page();
    printf("HOST-ARP %s %d pass %d fail\n", fails ? "FAIL" : "PASS", passes, fails);
    return fails ? 1 : 0;
}
