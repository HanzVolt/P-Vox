/* SPDX-License-Identifier: GPL-3.0-only */
/* Polivoks arpeggiator (poli_arp.h). Core 0: main loop and the LCD idle hook only; the time base is
 * fm1_ms, which TIMER5 (10 kHz) counts. Nothing here runs in the audio ISR or in polivoks_render.
 * Mono, one arp note at a time, into the engine's existing note path (polivoks_arp_gate).
 * Modes: UP, DOWN, UP/DOWN (ends not repeated), RANDOM (from the UP pool), AS PLAYED (press order).
 * Octaves 1..4 (each copy +12, notes above 126 dropped). Gate 5..100 % of a step; 100 % is tied.
 * A gap is never shorter than ARP_MIN_OFF_MS, so the engine's once-per-half (5.8 ms) gate read sees it.
 * LATCH: released keys keep sounding; the first press after every key is up starts a new set.
 * Included by poli_nav.c (unity build), after polivoks_limited.c. */
#include "poli_arp.h"
#include "polivoks.h"

#ifndef BOARD_SMK37
#define BOARD_SMK37 0
#endif
#if BOARD_SMK37
#define ARP_NKEYS 37
#define ARP_KEY_BASE 60
#else
#define ARP_NKEYS 27
#define ARP_KEY_BASE 65
#endif

/* clocks (24 ppq) per step for each rate */
static const uint8_t ARP_CPS[ARP_NRATE] = { 24, 16, 12, 8, 6, 4, 3 };
#define ARP_DEF_Q8 (500u * 256u)       /* 120 BPM: 500 ms a quarter, ms x 256 */

typedef struct {
    uint8_t on, latch, mode, rate, oct, gate, page;
    uint8_t n;                          /* keys in the set */
    uint8_t order[ARP_NKEYS];           /* key indices in press order */
    uint8_t vel[ARP_NKEYS];             /* velocity of each key's press */
    uint64_t set;                       /* bit k: key k is in the set */
    uint64_t phys;                      /* keys down, after the clear block */
    uint64_t raw;                       /* keys down, as read */
    uint8_t block;                      /* after a clear: ignore keys until all are up */
    uint8_t gate_on, started;
    int16_t note;                       /* sounding note, -1 none */
    uint32_t pos;                       /* step counter */
    uint32_t next_ms, next_frac, off_ms;
    uint32_t rnd;
    /* MIDI clock */
    uint8_t have_clk, running, was_ext;
    uint32_t clk_ms, tick;
    /* ARP button */
    uint8_t btn_armed;
    uint32_t btn_ms;
    uint32_t now;                       /* last time seen */
    uint32_t ui_sig;                    /* clock status / tempo, not step notes */
} arp_state_t;
static arp_state_t arp;
static const arp_state_t ARP_ZERO;

static uint8_t arp_pat[2 * ARP_MAXN];
static uint8_t arp_pvel[2 * ARP_MAXN];

void poli_arp_init(void)
{
    arp = ARP_ZERO;
    arp.mode = ARP_UP;
    arp.rate = 4;                       /* 1/16 */
    arp.oct = 1;
    arp.gate = 50;
    arp.note = -1;
    arp.running = 1;
    arp.rnd = 0x13579BDFu;
}

static void arp_gate_off(void)
{
    if (arp.gate_on || arp.note >= 0)
        polivoks_arp_gate(arp.note >= 0 ? (uint8_t)arp.note : 60u, 0u, 0u);
    arp.gate_on = 0;
    arp.note = -1;
}

/* the UP list (ascending, octave by octave) or the press-order list */
static int arp_build(uint8_t *nt, uint8_t *vl, int played)
{
    int o, i, k, m = 0, base = ARP_KEY_BASE + polivoks_kb_oct() * 12;
    for (o = 0; o < arp.oct; o++) {
        for (i = 0; i < (played ? arp.n : ARP_NKEYS); i++) {
            int n;
            k = played ? arp.order[i] : i;
            if (!played && !(arp.set & ((uint64_t)1u << k))) continue;
            n = base + k + 12 * o;
            if (n < 0) n = 0;
            if (n > 126) continue;
            nt[m] = (uint8_t)n;
            vl[m] = arp.vel[k] ? arp.vel[k] : 127u;
            m++;
        }
    }
    return m;
}

int poli_arp_pattern(uint8_t *out, int max)
{
    int m, L, i;
    if (arp.n == 0) return 0;
    m = arp_build(arp_pat, arp_pvel, arp.mode == ARP_PLAYED);
    L = m;
    if (arp.mode == ARP_DOWN) {
        for (i = 0; i < m / 2; i++) {
            uint8_t t = arp_pat[i]; arp_pat[i] = arp_pat[m - 1 - i]; arp_pat[m - 1 - i] = t;
            t = arp_pvel[i]; arp_pvel[i] = arp_pvel[m - 1 - i]; arp_pvel[m - 1 - i] = t;
        }
    } else if (arp.mode == ARP_UPDOWN && m > 2) {
        for (i = m - 2; i >= 1; i--) { arp_pat[L] = arp_pat[i]; arp_pvel[L] = arp_pvel[i]; L++; }
    }
    if (out) for (i = 0; i < L && i < max; i++) out[i] = arp_pat[i];
    return L;
}

static uint32_t arp_quarter_q8(void)
{
    uint32_t q = poli_nav_sync_valid() ? poli_nav_sync_q8() : 0u;
    return q ? q : ARP_DEF_Q8;
}

/* one step in 1/6144 ms (ms x 256 x 24 clocks): exact for every rate, no drift */
#define ARP_U 6144u
static uint32_t arp_step_u(void) { return arp_quarter_q8() * ARP_CPS[arp.rate]; }

static uint32_t arp_on_ms(uint32_t step_ms)
{
    uint32_t on;
    if (arp.gate >= ARP_GATE_MAX) return 0xFFFFFFFFu;       /* tied */
    on = step_ms * arp.gate / 100u;
    if (step_ms < ARP_MIN_OFF_MS + 6u) on = step_ms / 2u;
    else if (step_ms - on < ARP_MIN_OFF_MS) on = step_ms - ARP_MIN_OFF_MS;
    if (on < 1u) on = 1u;
    return on;
}

static void arp_step(uint32_t now, uint32_t step_ms)
{
    int L = poli_arp_pattern(0, 0), idx;
    uint32_t on;
    if (L <= 0) { arp_gate_off(); return; }
    if (arp.mode == ARP_RANDOM) {
        arp.rnd = arp.rnd * 1664525u + 1013904223u;
        idx = (int)((arp.rnd >> 16) % (uint32_t)L);
    } else
        idx = (int)(arp.pos % (uint32_t)L);
    arp.pos++;
    polivoks_arp_gate(arp_pat[idx], 1u, arp_pvel[idx]);
    arp.note = arp_pat[idx];
    arp.gate_on = 1;
    on = arp_on_ms(step_ms);
    arp.off_ms = on == 0xFFFFFFFFu ? 0u : now + on;
    if (on == 0xFFFFFFFFu) arp.gate_on = 2;                 /* tied: no timed release */
}

static int arp_ext(uint32_t now)
{
    return arp.have_clk && (uint32_t)(now - arp.clk_ms) < ARP_EXT_GAP_MS;
}

void poli_arp_poll(uint32_t now)
{
    int ext;
    uint32_t step_u;
    arp.now = now;
    ext = arp_ext(now);
    if (arp.was_ext && !ext) arp.started = 0;
    arp.was_ext = (uint8_t)ext;
    if (arp.page) {
        uint32_t q = arp_quarter_q8();
        uint32_t sig = ((60000u * 256u + q / 2u) / q) * 4u + (uint32_t)ext * 2u + arp.running;
        if (sig != arp.ui_sig) { arp.ui_sig = sig; poli_nav_touch(); }
    }
    if (!arp.on) return;
    /* Stop is transport, not a clock timeout. It stays stopped until Start
       or Continue, including after the last F8 pulse has gone away. */
    if (!arp.running) { arp_gate_off(); return; }
    if (arp.n == 0) {
        if (arp.gate_on || arp.note >= 0) arp_gate_off();
        arp.started = 0;
        arp.pos = 0;
        return;
    }
    if (arp.gate_on == 1 && (int32_t)(now - arp.off_ms) >= 0) {
        polivoks_arp_gate((uint8_t)arp.note, 0u, 0u);
        arp.gate_on = 0;
    }
    if (ext) {
        if (!arp.running && (arp.gate_on || arp.note >= 0)) arp_gate_off();
        return;                         /* steps come from poli_arp_clock */
    }
    step_u = arp_step_u();
    if (!arp.started) {
        arp.started = 1;
        arp.pos = 0;
        arp.next_ms = now;
        arp.next_frac = 0;
    }
    if ((int32_t)(now - arp.next_ms) >= 0) {
        if ((uint32_t)(now - arp.next_ms) > 2u * (step_u / ARP_U) + 2u) {   /* a long stall: resync */
            arp.next_ms = now;
            arp.next_frac = 0;
        }
        arp_step(now, step_u / ARP_U);
        arp.next_frac += step_u;
        arp.next_ms += arp.next_frac / ARP_U;
        arp.next_frac %= ARP_U;
    }
}

uint64_t poli_arp_keys(uint64_t keys, const volatile uint8_t *vel, uint32_t now)
{
    uint64_t eff, press, rel, was = arp.phys;
    int k, i;
    arp.raw = keys;
    if (arp.block && keys == 0u) arp.block = 0;
    eff = arp.block ? 0u : keys;
    press = eff & ~arp.phys;
    rel = arp.phys & ~eff;
    arp.phys = eff;
    if (press && was == 0u && arp.on && arp.latch && arp.n) {   /* latch: a new chord replaces the set */
        arp.n = 0;
        arp.set = 0;
    }
    for (k = 0; k < ARP_NKEYS; k++) {
        uint64_t b = (uint64_t)1u << k;
        if ((press & b) && !(arp.set & b) && arp.n < ARP_NKEYS) {
            arp.order[arp.n++] = (uint8_t)k;
            arp.set |= b;
            arp.vel[k] = vel ? vel[k] : 127u;
            if (arp.vel[k] < 1u || arp.vel[k] > 127u) arp.vel[k] = 127u;
        }
        if ((rel & b) && (arp.set & b) && !(arp.on && arp.latch)) {
            for (i = 0; i < arp.n; i++) if (arp.order[i] == k) break;
            for (; i + 1 < arp.n; i++) arp.order[i] = arp.order[i + 1];
            if (arp.n) arp.n--;
            arp.set &= ~b;
        }
    }
    poli_arp_poll(now);
    return arp.on ? 0u : keys;
}

void poli_arp_clear(void)
{
    int shown = arp.latch;
    arp_gate_off();
    arp.n = 0;
    arp.set = 0;
    arp.phys = 0;
    arp.latch = 0;
    arp.block = arp.raw != 0u;          /* keys still down are not replayed */
    arp.pos = 0;
    arp.started = 0;
    if (shown) poli_nav_touch();         /* the LATCH segment changes; otherwise no redraw */
}

void poli_arp_set_on(int on)
{
    on = on ? 1 : 0;
    if (on == arp.on) return;
    arp_gate_off();
    if (!on) {                           /* drop latched keys: the set is the keys held */
        arp.set = arp.phys;
        arp.n = 0;
        { int k; for (k = 0; k < ARP_NKEYS; k++) if (arp.set & ((uint64_t)1u << k)) arp.order[arp.n++] = (uint8_t)k; }
    }
    arp.on = (uint8_t)on;
    arp.started = 0;
    arp.pos = 0;
    poli_nav_touch();
}
int poli_arp_on(void) { return arp.on; }

static int arp_clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void poli_arp_set(int mode, int rate, int oct, int gate, int latch)
{
    arp.mode = (uint8_t)arp_clampi(mode, 0, ARP_NMODE - 1);
    arp.rate = (uint8_t)arp_clampi(rate, 0, ARP_NRATE - 1);
    arp.oct = (uint8_t)arp_clampi(oct, 1, 4);
    arp.gate = (uint8_t)(arp_clampi(gate, ARP_GATE_MIN, ARP_GATE_MAX) / ARP_GATE_STEP * ARP_GATE_STEP);
    if ((latch ? 1 : 0) != arp.latch) poli_arp_latch_toggle();
}

void poli_arp_knob(int index, int steps)
{
    if (!steps) return;
    switch (index) {
    case 0: arp.mode = (uint8_t)arp_clampi(arp.mode + steps, 0, ARP_NMODE - 1); break;
    case 1: arp.rate = (uint8_t)arp_clampi(arp.rate + steps, 0, ARP_NRATE - 1); break;
    case 2: arp.oct = (uint8_t)arp_clampi(arp.oct + steps, 1, 4); break;
    default: arp.gate = (uint8_t)arp_clampi(arp.gate + steps * ARP_GATE_STEP, ARP_GATE_MIN, ARP_GATE_MAX); break;
    }
    poli_nav_touch();
}

void poli_arp_latch_toggle(void)
{
    arp.latch ^= 1u;
    if (!arp.latch) {                    /* keys no longer held leave the set */
        uint64_t gone = arp.set & ~arp.phys;
        int k, i;
        for (k = 0; k < ARP_NKEYS; k++) {
            if (!(gone & ((uint64_t)1u << k))) continue;
            for (i = 0; i < arp.n; i++) if (arp.order[i] == k) break;
            for (; i + 1 < arp.n; i++) arp.order[i] = arp.order[i + 1];
            if (arp.n) arp.n--;
        }
        arp.set &= arp.phys;
    }
    poli_nav_touch();
}

void poli_arp_button(int pressed, int released, int held, uint32_t now)
{
    if (pressed) { arp.btn_armed = 1; arp.btn_ms = now; }
    if (arp.btn_armed && held && (uint32_t)(now - arp.btn_ms) >= ARP_HOLD_MS) {
        arp.btn_armed = 0;               /* a hold: open the page, no toggle */
        arp.page = 1;
        poli_nav_touch();
    }
    if (released && arp.btn_armed) {     /* a short press: on / off */
        arp.btn_armed = 0;
        poli_arp_set_on(!arp.on);
    }
}
int poli_arp_page_open(void) { return arp.page; }
int poli_arp_page_close(void)
{
    int was = arp.page;
    arp.page = 0;
    return was;
}

void poli_arp_view(poli_arp_view_t *v)
{
    uint32_t q;
    if (!v) return;
    v->on = arp.on; v->latch = arp.latch; v->mode = arp.mode; v->rate = arp.rate;
    v->oct = arp.oct; v->gate = arp.gate; v->page = arp.page;
    v->ext = arp_ext(arp.now);
    v->running = arp.running;
    v->nheld = arp.n;
    q = arp_quarter_q8();
    v->bpm = (int)((60000u * 256u + q / 2u) / q);
    v->note = arp.note;
}

void poli_arp_clock(uint32_t now)
{
    uint32_t cps = ARP_CPS[arp.rate];
    if (!arp_ext(now)) arp.started = 0;
    arp.have_clk = 1;
    arp.clk_ms = now;
    arp.now = now;
    arp.was_ext = 1;
    if (!arp.running) return;
    if (arp.tick % cps == 0u && arp.on && arp.n) {
        uint32_t step_ms = arp_quarter_q8() * cps / ARP_U;
        if (arp.gate_on == 1) { polivoks_arp_gate((uint8_t)arp.note, 0u, 0u); arp.gate_on = 0; }
        arp_step(now, step_ms);
    }
    arp.tick++;
}
void poli_arp_start(void)
{
    arp.running = 1;
    arp.tick = 0;
    arp.pos = 0;
    arp.started = 0;
    arp_gate_off();
}
void poli_arp_continue(void) { arp.running = 1; }
void poli_arp_stop(void)
{
    arp.running = 0;
    arp_gate_off();
}
