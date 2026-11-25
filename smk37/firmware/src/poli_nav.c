/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 section windows. Two generators and the modulator. No third VCO.
 * Knob positions are the on-screen 0..127 (or a named step). They are not
 * a traced pot law. Law-5 envelope defaults are pushed at boot so the engine
 * matches the displayed 0/0/4; cutoff pos is 59 (cut 11556)
 * until a load or a turn (nearest new-map value is 11556). See ASSIGNMENTS.md.
 */
#include "poli_nav.h"
#include "polivoks.h"
#include "poli_xtra.h"

/* Flat voice order, also the HOME roll:
 * 0 GEN I, 1 GEN II, 2 MODULATOR, 3 MOD->OSC, 4 MIXER,
 * 5 FILTER, 6 FILTER MOD, 7 FILTER ENV, 8 AMP ENV, 9 AMP MOD, 10 GLOBAL, 11 MIDI.
 * EDIT cycles 0, 1, 4. LFO cycles 2, 3. FX cycles 5, 6, 7. GLO is 10, 11.
 * Depths are one slot each: GEN I K4 and MOD->OSC K1 are SLOT_MOD_PITCH.
 * FILTER MOD K1 is the only cutoff-modulation amount. */
static const int8_t WIN_SLOT[NAV_NWIN][4] = {
    { SLOT_FOOT1, SLOT_SHAPE1, SLOT_FM, SLOT_MOD_PITCH },
    { SLOT_FOOT2, SLOT_SHAPE2, SLOT_DETUNE, SLOT_MOD_PITCH2 },
    { SLOT_LFO_RATE, SLOT_LFO_SHAPE, SLOT_LFO_RST, SLOT_LFO_SYNC },
    { SLOT_MOD_PITCH, SLOT_MOD_PITCH2, SLOT_EMPTY, SLOT_EMPTY },
    { SLOT_LV1, SLOT_LV2, SLOT_NOISE, SLOT_EXT },
    { SLOT_CUT, SLOT_RES, SLOT_CONT, SLOT_DRIVE },
    { SLOT_MOD_CUT, SLOT_VEL, SLOT_EMPTY, SLOT_EMPTY },
    { SLOT_ATK_F, SLOT_DEC_F, SLOT_SUS_F, SLOT_REL_F },
    { SLOT_ATK_A, SLOT_DEC_A, SLOT_SUS_A, SLOT_REL_A },
    { SLOT_MOD_AMP, SLOT_EMPTY, SLOT_EMPTY, SLOT_EMPTY },
    { SLOT_TUNE, SLOT_LEVEL, SLOT_GLIDE_RATE, SLOT_DUO },
    { SLOT_EMPTY, SLOT_EMPTY, SLOT_EMPTY, SLOT_EMPTY },
};
static const int8_t WIN_SW[NAV_NWIN] = {
    SW_NONE, SW_LINK, SW_NONE, SW_MODOSC, SW_NONE,
    SW_LPBP, SW_BASS, SW_REP_F,
    SW_REP_A, SW_KEYHOLD,
    SW_GLIDE,
    SW_NONE
};
static const int8_t OWNER[NAV_NWIN] = {
    NAV_SEC_OSC, NAV_SEC_OSC, NAV_SEC_LFO, NAV_SEC_LFO, NAV_SEC_OSC,
    NAV_SEC_FX, NAV_SEC_FX, NAV_SEC_FX,
    NAV_SEC_AMP, NAV_SEC_AMP, NAV_SEC_GLOBAL, NAV_SEC_GLOBAL
};
static const int8_t IN_SEC[NAV_NWIN] = { 0, 1, 0, 1, 2, 0, 1, 2, 0, 1, 0, 1 };
static const int8_t SEC_LEN[5] = { 3, 2, 3, 2, 2 };
static const int8_t SEC_WIN[5][3] = {
    { 0, 1, 4 },
    { 2, 3, -1 },
    { 5, 6, 7 },
    { 8, 9, -1 },
    { 10, 11, -1 }
};

static int sec, sec_i, flat, roll, lang, msg;
static int xw XT_KEEP = -1;              /* Core2-FxSeq: -1, or the ADDED window shown (poli_xtra.c) */
static uint32_t xgen_seen XT_KEEP;
static uint8_t midi_ch = 1;   /* receive channel 1..16; default 1 */
static uint8_t key_track = 62; /* 0 / 62 / 100; default 62 (Monosounds median) */
static uint32_t nav_now_ms;
static uint32_t acc_ms[4], acc_dt[4];
static int8_t acc_dir[4];
static uint8_t acc_have[4];
static uint8_t mod_osc_en;
static uint32_t nav_gen;
static uint32_t nav_val_gen;     /* fader value-only dirty; volt.c must not full-redraw on this */
static uint32_t nav_edits;       /* sound edits by knob or switch (preset dirty flag) */
static int16_t pos[SLOT_COUNT];
static int nav_klo(int kind);
static int nav_khi(int kind);
static void lfo_push(void);
#ifndef POLI_ARP
#define POLI_ARP 0
#endif
#if POLI_ARP
#include "poli_arp.c"      /* Elite classic arp (unity build; compiled out on the FM-1) */
#endif

static int32_t scale_u7(int p, int32_t lo, int32_t hi)
{
    if (p < 0) p = 0;
    if (p > 127) p = 127;
    return lo + (int32_t)(((int64_t)(hi - lo) * p) / 127);
}

/* 10-bit 0..1016 (127*8). When u == 8*p this equals scale_u7(p, lo, hi). */
static int32_t scale_u10(uint16_t u, int32_t lo, int32_t hi)
{
    uint32_t x = u;
    if (x > 1016u)
        x = 1016u;
    return lo + (int32_t)(((int64_t)(hi - lo) * (int64_t)x) / 1016);
}

static int nearest_u7(int32_t v, int32_t lo, int32_t hi)
{
    int32_t span = hi - lo;
    int p;
    if (v <= lo || span <= 0) return 0;
    if (v >= hi) return 127;
    p = (int)(((int64_t)(v - lo) * 127 + span / 2) / span);
    if (p < 0) p = 0;
    if (p > 127) p = 127;
    return p;
}

static int step_to(int v, int dv, int lo, int hi)
{
    int n = v + dv;
    if (n < lo) n = lo;
    if (n > hi) n = hi;
    return n;
}

static int32_t scale_pm64(int p)
{
    if (p > 64) p = 64;
    if (p < -64) p = -64;
    if (p >= 64) return 32767;
    if (p <= -64) return -32768;
    return p * 512;
}

/* LFO rate knob (LFOTail, 13 Nov 2026). Knob 0..110 unchanged: linear
   8192*(p+1), 0.084 .. 9.34 Hz at the assumed 44100. Knob 111..127 is a
   geometric tail, x1.57 (about 7.8 semitones) per click, 14.7 Hz up to
   20 kHz (audio rate), round(f * 2^32 / 44100). Not a Polivoks Hz law. */
static const uint32_t LFO_TAIL[17] = {1427725u,2241694u,3519718u,5526365u,8677033u,13623946u,21391173u,33586617u,52734876u,82799859u,130005362u,204123464u,320497464u,503218114u,790110681u,1240565216u,1947830973u};
static uint32_t lfo_inc_of(int p)
{
    if (p < 0) p = 0;
    if (p > 127) p = 127;
    if (p > 110) return LFO_TAIL[p - 111];
    return 8192u + (uint32_t)p * 8192u;
}

/* ---- LFO SYNC to MIDI clock (LFOTail-Sync-Vel, 13 Nov 2026). Main program only (volt.c midi_in):
 * the sample loop just reads the increment, as for the RATE knob. 24 clocks per quarter note.
 * Tempo: the span of the last 24 clocks (fewer at the start of a run) in milliseconds, smoothed 1/4
 * per clock; a jump over 12.5 % is taken only after 3 clocks in a row agree (tempo change, not a
 * burst after a stall). A gap over 500 ms ends the run; the tempo is HELD (last value) until clocks
 * come back. With no tempo ever measured, a synced LFO plays the free RATE knob. MIDI Start
 * restarts the LFO cycle on the next clock (beat 1); Continue and Stop only start a new run.
 * Frequency lock only: no continuous phase lock, so phase can drift slowly (unmeasured on hardware).
 * Hz assume the 44100 sample rate, like every other rate in this firmware. */
static const uint8_t SYNC_CLK[10] = { 0, 96, 48, 24, 16, 12, 8, 6, 4, 3 };   /* FREE 1/1 1/2 1/4 1/4T 1/8 1/8T 1/16 1/16T 1/32 */
static const int8_t X_SLOT[3] = { SLOT_LFO_RST, SLOT_LFO_SYNC, SLOT_VEL };
#define CLK_W 24u
#define CLK_GAP_MS 500u
static uint32_t clk_t[CLK_W + 1u];
static uint32_t clk_n, clk_q8, lfo_inc_now;
static uint8_t clk_odd, clk_start;

/* (hi:lo) / d, 32-bit words only (this build links no 64-bit divide). d < 2^31. Saturates at 2^31 - 1. */
static uint32_t udiv64_32(uint32_t hi, uint32_t lo, uint32_t d)
{
    uint32_t r = 0, q = 0, over = 0;
    int i;
    for (i = 63; i >= 0; i--) {
        uint32_t bit = i >= 32 ? (hi >> (i - 32)) & 1u : (lo >> i) & 1u;
        r = (r << 1) | bit;
        if (r >= d) {
            r -= d;
            if (i >= 31) over = 1u;
            else q |= 1u << i;
        }
    }
    return over ? 0x7FFFFFFFu : q;
}
/* LFO increment for 24 clocks spanning q8/256 ms: 2^32 * 1000 * 24 * 256 / (44100 * clocks per cycle * q8) */
#define SYNC_K 598373674980ull /* round(2^32 * 6144000 / 44100) */

static void lfo_push(void)
{
    int s = pos[SLOT_LFO_SYNC];
    uint32_t inc;
    if (s > 0 && s <= 9 && clk_q8) {
        uint32_t d = (uint32_t)SYNC_CLK[s] * clk_q8;
        uint64_t n = (uint64_t)SYNC_K + d / 2u;
        inc = udiv64_32((uint32_t)(n >> 32), (uint32_t)n, d);
        if (inc < 1u) inc = 1u;
    } else
        inc = lfo_inc_of(pos[SLOT_LFO_RATE]);
    lfo_inc_now = inc;
    polivoks_set_lfo_inc(inc);
}

static void clk_tick(void)
{
    uint32_t now = nav_now_ms, w, span, est;
    if (clk_n && (uint32_t)(now - clk_t[(clk_n - 1u) % (CLK_W + 1u)]) > CLK_GAP_MS)
        clk_n = 0;                                 /* dropout: a new run; the tempo is held */
    clk_t[clk_n % (CLK_W + 1u)] = now;
    clk_n++;
    if (clk_start) {
        clk_start = 0;
        if (pos[SLOT_LFO_SYNC]) polivoks_lfo_reset();
    }
    if (clk_n < 2u) return;
    w = clk_n - 1u < CLK_W ? clk_n - 1u : CLK_W;   /* intervals in the window */
    span = now - clk_t[(clk_n - 1u - w) % (CLK_W + 1u)];
    if (span == 0u) return;                        /* a burst of queued clocks: no time information */
    est = (span * 256u * CLK_W + w / 2u) / w;      /* ms x 256 per 24 clocks */
    if (!clk_q8) {
        clk_q8 = est;
        clk_odd = 0;
    } else {
        uint32_t d = est > clk_q8 ? est - clk_q8 : clk_q8 - est;
        if (d * 8u > clk_q8) {
            if (++clk_odd >= 3u) { clk_q8 = est; clk_odd = 0; }
        } else {
            clk_odd = 0;
            clk_q8 = (uint32_t)((int32_t)clk_q8 + ((int32_t)est - (int32_t)clk_q8) / 4);
        }
    }
    if (pos[SLOT_LFO_SYNC]) lfo_push();
}
int poli_nav_sync_valid(void) { return clk_q8 != 0u; }
uint32_t poli_nav_sync_q8(void) { return clk_q8; }
uint32_t poli_nav_lfo_inc_now(void) { return lfo_inc_now; }

/* Core2-FxSeq (15 Nov 2026): the ADDED features' main-program side (arp, seq, drum pattern, clock, settings) */
#include "poli_xtra.c"

/* Attack, decay and release tables (curved envelope V3). Position q in the
 * table gives N(q) = 21*(441000/21)^(q/127) samples for decay/release
 * (0.48 ms .. 10.0 s at 44.1 kHz, ~8.2 %/click) and NA(q) for attack
 * (0.12 ms .. 10.0 s, ~9.3 %/click). Law 5 (Hans 2026-11-12) does not
 * retune those tables. The 0-127 knob is remapped onto them so the span
 * is the audible range: attack old = 44 + round(new*83/127); decay
 * old = 50 + round(new*77/127); release new<=10: old=9*new, else
 * old = 90 + round((new-10)*37/117). The maps are many-to-one
 * (attack 84 distinct times, 44 plateaus; decay 78 / 50; release 48 / 80).
 * Boot / Init knobs are 0 / 0 / 4 (old 27/31 could not be kept; old 39
 * becomes 36). Tables: poli_env_rc.inc, tools/gen_poli_env_rc.py. */
#include "poli_env_rc.inc"

/* Law-5 index maps. round_half_up(n/d) = (n + d/2)/d; for odd d there is no exact half. */
int poli_env_old_atk(int n)
{
    if (n < 0) n = 0;
    if (n > 127) n = 127;
    return 44 + (n * 83 + 63) / 127;
}
int poli_env_old_dec(int n)
{
    if (n < 0) n = 0;
    if (n > 127) n = 127;
    return 50 + (n * 77 + 63) / 127;
}
int poli_env_old_rel(int n)
{
    if (n < 0) n = 0;
    if (n > 127) n = 127;
    if (n <= 10) return 9 * n;
    return 90 + ((n - 10) * 37 + 58) / 117;
}

/* Octave-linear cutoff into the 4096-steps-per-octave fcore map.
 * Knob 0 is cut 0 (a = POLI_A_BASE). Knob 127 is POLI_CUT_TOP (24874),
 * the cut whose measured LP -3 dB at resonance 0 is 10 kHz. The span
 * is the factory 100 Hz .. 10 kHz map (host-calibrated). */
int32_t poli_cut_of_knob(int p)
{
    if (p < 0) p = 0;
    if (p > 127) p = 127;
    return (p * POLI_CUT_TOP + 63) / 127;
}

static int law5_pick(int old, int n0, int n1, int (*fwd)(int))
{
    int q0, q1, e0, e1;
    if (n0 < 0) n0 = 0;
    if (n0 > 127) n0 = 127;
    if (n1 < 0) n1 = 0;
    if (n1 > 127) n1 = 127;
    if (n1 == n0) return (uint8_t)n0;
    q0 = fwd(n0); q1 = fwd(n1);
    e0 = q0 > old ? q0 - old : old - q0;
    e1 = q1 > old ? q1 - old : old - q1;
    /* smaller log-time error (distance in old index); tie -> snappier (smaller q), then smaller n */
    if (e1 < e0) return n1;
    if (e0 < e1) return n0;
    if (q1 != q0) return q1 < q0 ? n1 : n0;
    return n0 < n1 ? n0 : n1;
}

uint8_t poli_law5_env(uint8_t old, int stage)
{
    int o = (int)old, n0, n1, rem;
    if (stage == 0) {
        if (o <= 44) return 0;                  /* below the new floor: cannot be reproduced */
        n0 = (o - 44) * 127 / 83;
        rem = (o - 44) * 127 % 83;
        n1 = rem ? n0 + 1 : n0;
        return (uint8_t)law5_pick(o, n0, n1, poli_env_old_atk);
    }
    if (stage == 1) {
        if (o <= 50) return 0;
        n0 = (o - 50) * 127 / 77;
        rem = (o - 50) * 127 % 77;
        n1 = rem ? n0 + 1 : n0;
        return (uint8_t)law5_pick(o, n0, n1, poli_env_old_dec);
    }
    if (o <= 90) {
        n0 = o / 9;
        rem = o % 9;
        n1 = rem ? n0 + 1 : n0;
        if (n0 > 10) n0 = 10;
        if (n1 > 10) n1 = 10;
        return (uint8_t)law5_pick(o, n0, n1, poli_env_old_rel);
    }
    n0 = 10 + (o - 90) * 117 / 37;
    rem = (o - 90) * 117 % 37;
    n1 = rem ? n0 + 1 : n0;
    if (n0 < 10) n0 = 10;
    if (n1 < 10) n1 = 10;
    return (uint8_t)law5_pick(o, n0, n1, poli_env_old_rel);
}

static int32_t cut_eff(int32_t c)
{
    if (c < 0) c = 0;
    if ((c >> 12) > 6) return (6 << 12) + (c & 4095);
    return c;
}

/* Playback is POLI_CUT_TOP (24874, 100 Hz..10 kHz). Conversion of
 * pre-law-5 banks must keep the map the 50 factory slots were authored
 * on, so a law-3/4 record still lands on the same stored knob. */
#define POLI_CUT_TOP_LAW5 25965

static int32_t poli_cut_of_knob_law5(int p)
{
    if (p < 0) p = 0;
    if (p > 127) p = 127;
    return (p * POLI_CUT_TOP_LAW5 + 63) / 127;
}

uint8_t poli_law5_cut(uint8_t old)
{
    int32_t oc = 30000 * (int32_t)old / 127, eff = cut_eff(oc);
    int n0, n1, rem, c0, c1, e0, e1;
    n0 = (int)((int32_t)eff * 127 / POLI_CUT_TOP_LAW5);
    rem = (int)((int32_t)eff * 127 % POLI_CUT_TOP_LAW5);
    n1 = rem ? n0 + 1 : n0;
    if (n0 < 0) n0 = 0;
    if (n0 > 127) n0 = 127;
    if (n1 < 0) n1 = 0;
    if (n1 > 127) n1 = 127;
    if (n1 == n0) return (uint8_t)n0;
    c0 = poli_cut_of_knob_law5(n0); c1 = poli_cut_of_knob_law5(n1);
    e0 = c0 > eff ? c0 - eff : eff - c0;
    e1 = c1 > eff ? c1 - eff : eff - c1;
    if (e1 < e0) return (uint8_t)n1;
    if (e0 < e1) return (uint8_t)n0;
    /* tie in cents: keep the lower cutoff */
    if (c1 != c0) return (uint8_t)(c1 < c0 ? n1 : n0);
    return (uint8_t)(n0 < n1 ? n0 : n1);
}

/* Last value sent per stage (atk, dec, sus, rel), 0 filter, 1 amp. Starts
 * at the law-5 boot coefficients (table at remapped 0/0/4 = old 44/50/36).
 * A knob turn changes only its own stage, so the other three keep their exact values. */
static int32_t adsr_v[2][4];
_Static_assert(SLOT_REL_F - SLOT_ATK_F == 3 && SLOT_REL_A - SLOT_ATK_A == 3, "ADSR slots must be in order");

static void push_adsr(int which)
{
    polivoks_set_adsr_q(which, adsr_v[which][0], adsr_v[which][1],
                        adsr_v[which][2], adsr_v[which][3]);
}

static void env_knob(int which, int stage, int p)
{
    int q = p;
    if (stage == 0) q = poli_env_old_atk(p);
    else if (stage == 1) q = poli_env_old_dec(p);
    else if (stage == 3) q = poli_env_old_rel(p);
    adsr_v[which][stage] = stage == 2 ? scale_u7(p, 0, 28000)
                         : (int32_t)(stage == 0 ? ENV_C_ATK[q] : ENV_C_DR[q]);
    push_adsr(which);
}

int32_t poli_res_of_knob(int p)
{
    return scale_u7(p, 0, 30000);
}

int32_t poli_res_of_fader_adc(uint16_t u)
{
    return scale_u10(u, 0, 30000);
}

int32_t poli_env_of_knob(int stage, int p)
{
    int q;
    if (p < 0) p = 0;
    if (p > 127) p = 127;
    if (stage == 2)
        return scale_u7(p, 0, 28000);
    if (stage == 0) q = poli_env_old_atk(p);
    else if (stage == 1) q = poli_env_old_dec(p);
    else q = poli_env_old_rel(p);
    if (q < 0) q = 0;
    if (q > 127) q = 127;
    return (int32_t)(stage == 0 ? ENV_C_ATK[q] : ENV_C_DR[q]);
}

int32_t poli_env_of_fader_adc(int stage, uint16_t u)
{
    uint32_t x = u;
    int p, frac;
    int32_t a, b;
    if (x > 1016u)
        x = 1016u;
    if (stage == 2)
        return scale_u10((uint16_t)x, 0, 28000);
    p = (int)(x / 8u);
    frac = (int)(x % 8u);
    a = poli_env_of_knob(stage, p);
    if (frac == 0 || p >= 127)
        return a;
    b = poli_env_of_knob(stage, p + 1);
    return a + ((b - a) * frac) / 8;
}

void poli_nav_adsr_shadow(int which, int stage, int32_t v)
{
    if (which < 0 || which > 1 || stage < 0 || stage > 3)
        return;
    adsr_v[which][stage] = v;
}

static void apply_kind(int kind, int dv)
{
    switch (kind) {
    case SLOT_EMPTY:
        msg = MSG_EMPTY;
        return;
    case SLOT_EXT:
        /* NAV: external level is not mapped. Do not call polivoks_set_level(3). */
        msg = MSG_EXT;
        return;
    case SLOT_CUT:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        poli_cut = poli_cut_of_knob(pos[kind]);
        break;
    case SLOT_RES:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        poli_res = scale_u7(pos[kind], 0, 30000);
        break;
    case SLOT_DRIVE:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        poli_drv = scale_u7(pos[kind], 2000, 30000);
        break;
    case SLOT_CONT:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        polivoks_set_contour_amount(scale_u7(pos[kind], 0, 32768));
        break;
    case SLOT_ATK_F: case SLOT_DEC_F: case SLOT_SUS_F: case SLOT_REL_F:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        env_knob(0, kind - SLOT_ATK_F, pos[kind]);
        break;
    case SLOT_ATK_A: case SLOT_DEC_A: case SLOT_SUS_A: case SLOT_REL_A:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        env_knob(1, kind - SLOT_ATK_A, pos[kind]);
        break;
    case SLOT_MOD_CUT:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        polivoks_set_mod_cut(scale_u7(pos[kind], 0, 32767));
        break;
    case SLOT_MOD_AMP:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        polivoks_set_mod_amp(scale_u7(pos[kind], 0, 32767));
        break;
    case SLOT_MOD_PITCH:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        polivoks_set_mod_pitch_vco(0, mod_osc_en ? scale_u7(pos[kind], 0, 32767) : 0);
        break;
    case SLOT_MOD_PITCH2:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        polivoks_set_mod_pitch_vco(1, mod_osc_en ? scale_u7(pos[kind], 0, 32767) : 0);
        break;
    case SLOT_FOOT1:
        pos[kind] = (int16_t)step_to(pos[kind], dv, -2, 2);
        polivoks_set_footage(0, pos[kind]);
        break;
    case SLOT_FOOT2:
        pos[kind] = (int16_t)step_to(pos[kind], dv, -2, 2);
        polivoks_set_footage(1, pos[kind]);
        break;
    case SLOT_SHAPE1:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 4);
        poli_shape = (uint8_t)pos[kind];
        if (polivoks_link_shapes())
            pos[SLOT_SHAPE2] = pos[SLOT_SHAPE1];
        break;
    case SLOT_SHAPE2:
        /* Turning generator II's shape unlinks the two shapes. */
        if (polivoks_link_shapes())
            pos[SLOT_SHAPE2] = pos[SLOT_SHAPE1];
        pos[SLOT_SHAPE2] = (int16_t)step_to(pos[SLOT_SHAPE2], dv, 0, 4);
        polivoks_set_shape2((uint8_t)pos[SLOT_SHAPE2]);
        break;
    case SLOT_FM:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        polivoks_set_fm_depth(scale_u7(pos[kind], 0, 32767));
        break;
    case SLOT_DETUNE:
        pos[kind] = (int16_t)step_to(pos[kind], dv, -64, 64);
        polivoks_set_detune(scale_pm64(pos[kind]));
        break;
    case SLOT_LV1:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        polivoks_set_level(0, scale_u7(pos[kind], 0, 32768));
        break;
    case SLOT_LV2:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        polivoks_set_level(1, scale_u7(pos[kind], 0, 32768));
        break;
    case SLOT_NOISE:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        polivoks_set_level(2, scale_u7(pos[kind], 0, 32768));
        break;
    case SLOT_LFO_RATE:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        lfo_push();
        break;
    case SLOT_LFO_SYNC:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 9);
        lfo_push();
        break;
    case SLOT_LFO_RST:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 1);
        polivoks_set_lfo_restart((uint8_t)pos[kind]);
        break;
    case SLOT_VEL:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        polivoks_set_vel_amount(pos[kind]);
        break;
    case SLOT_LFO_SHAPE:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 3);
        poli_lfo_shape = (uint8_t)pos[kind];
        break;
    case SLOT_TUNE:
        pos[kind] = (int16_t)step_to(pos[kind], dv, -64, 64);
        polivoks_set_tune(scale_pm64(pos[kind]));
        break;
    case SLOT_LEVEL:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        polivoks_set_output(scale_u7(pos[kind], 0, 32768));
        break;
    case SLOT_GLIDE_RATE:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 127);
        polivoks_set_glide_rate(scale_u7(pos[kind], 0, 200000));
        break;
    case SLOT_DUO:
        pos[kind] = (int16_t)step_to(pos[kind], dv, 0, 1);
        polivoks_set_duo((uint8_t)pos[kind]);
        break;
    default:
        break;
    }
    msg = MSG_NONE;
    nav_edits++;
}

static void place(int f, int rolling)
{
#if POLI_ARP
    (void)poli_arp_page_close();   /* any page change leaves the ARP page */
#endif
    f %= NAV_NWIN;
    if (f < 0) f += NAV_NWIN;
    flat = f;
    sec = OWNER[f];
    sec_i = IN_SEC[f];
    roll = rolling;
}

static void push_mod_osc(void)
{
    polivoks_set_mod_pitch_vco(0, mod_osc_en ? scale_u7(pos[SLOT_MOD_PITCH], 0, 32767) : 0);
    polivoks_set_mod_pitch_vco(1, mod_osc_en ? scale_u7(pos[SLOT_MOD_PITCH2], 0, 32767) : 0);
}

void poli_nav_init(void)
{
    int i;
    polivoks_init();
#if POLI_ARP
    poli_arp_init();
#endif
    place(0, 0);             /* Generator I, section mode. HOME enters the roll. */
    lang = 0;
    msg = MSG_NONE;
    midi_ch = 1;
    key_track = 62;
    polivoks_set_key_track(62);
    nav_now_ms = 0;
    acc_have[0] = acc_have[1] = acc_have[2] = acc_have[3] = 0;
    mod_osc_en = 1;          /* stored depths stay applied; the toggle can mute them */
    pos[SLOT_CUT] = POLI_CUT_DEF_POS;   /* cut 11556; engine default matches after a load or a turn */
    pos[SLOT_RES] = (int16_t)nearest_u7(6000, 0, 30000);
    pos[SLOT_DRIVE] = (int16_t)nearest_u7(14000, 2000, 30000);
    pos[SLOT_CONT] = (int16_t)nearest_u7(8192, 0, 32768);
    pos[SLOT_ATK_F] = pos[SLOT_ATK_A] = ENV_KNOB_ATK;
    pos[SLOT_DEC_F] = pos[SLOT_DEC_A] = ENV_KNOB_DEC;
    pos[SLOT_SUS_F] = pos[SLOT_SUS_A] = (int16_t)nearest_u7(14000, 0, 28000);
    pos[SLOT_REL_F] = pos[SLOT_REL_A] = ENV_KNOB_REL;
    for (i = 0; i < 2; i++) {
        adsr_v[i][0] = (int32_t)ENV_C_ATK[poli_env_old_atk(ENV_KNOB_ATK)]; /* old 44 */
        adsr_v[i][1] = (int32_t)ENV_C_DR[poli_env_old_dec(ENV_KNOB_DEC)];  /* old 50 */
        adsr_v[i][2] = 14000;         /* sustain stays plain 0..28000 */
        adsr_v[i][3] = (int32_t)ENV_C_DR[poli_env_old_rel(ENV_KNOB_REL)];  /* old 36 */
        push_adsr(i);                 /* display, nav and engine agree on the remapped defaults */
    }
    pos[SLOT_MOD_CUT] = (int16_t)nearest_u7(1024, 0, 32767);
    pos[SLOT_MOD_AMP] = 0;
    pos[SLOT_MOD_PITCH] = 0;     /* LFO->pitch depth 0 at boot (was 2048) */
    pos[SLOT_MOD_PITCH2] = 0;
    pos[SLOT_FOOT1] = 0;
    pos[SLOT_FOOT2] = 0;
    pos[SLOT_SHAPE1] = 0;
    pos[SLOT_SHAPE2] = 0;
    pos[SLOT_FM] = 0;            /* GEN II->I depth 0 at boot (was 7000) */
    pos[SLOT_DETUNE] = 0;
    pos[SLOT_LV1] = 127;
    pos[SLOT_LV2] = 127;
    pos[SLOT_NOISE] = 0;
    pos[SLOT_LFO_RATE] = (int16_t)((486977u - 8192u) / 8192u);
    pos[SLOT_LFO_SHAPE] = 0;
    pos[SLOT_TUNE] = 0;
    pos[SLOT_LEVEL] = 127;
    pos[SLOT_GLIDE_RATE] = (int16_t)nearest_u7(8000, 0, 200000);
    pos[SLOT_DUO] = 1;
    pos[SLOT_LFO_RST] = 0;      /* OFF */
    pos[SLOT_LFO_SYNC] = 0;     /* FREE */
    pos[SLOT_VEL] = 0;          /* off */
    clk_n = 0; clk_q8 = 0; clk_odd = 0; clk_start = 0;
    lfo_inc_now = 486977u;      /* the engine boot value, until a load or a turn */
    xw = -1;
    xtra_init();                /* Core2-FxSeq: the ADDED features, all OFF */
    xgen_seen = xtra_gen();
    nav_gen++;
}

void poli_nav_section(int section)
{
    if (section < 0 || section > 4) return;
    xw = -1;
    place(SEC_WIN[section][0], 0);
    msg = MSG_NONE;
    nav_gen++;
}

void poli_nav_home(void)
{
    xw = -1;
    place(0, 1);
    msg = MSG_NONE;
    nav_gen++;
}

/* Wander-Drift: the order SELECT walks: ARPEGGIO, WANDER, SEQUENCER, DRUMS, DRUM DRIFT, GROOVE, DELAY, REVERB */
/* Drift-Full: ARPEGGIO, WANDER, SEQUENCER, DRUMS, GENERATE, DRUM DRIFT, GROOVE, DRUM FX, PERFORM, DELAY, REVERB */
/* Elite pad-record: PAD REC (11) sits one click after DRUMS. FM-1 keeps the 11-page walk. */
#if BOARD_SMK37
static const int8_t XW_ORDER[NAV_XWIN] = { 0, 5, 1, 2, 11, 12, 8, 6, 7, 9, 10, 3, 4 };   /* Drum-Step: DRUM STEP, PAD REC */
#else
static const int8_t XW_ORDER[NAV_XWIN] = { 0, 5, 1, 2, 11, 8, 6, 7, 9, 10, 3, 4 };   /* Drum-Step: DRUM STEP after DRUMS */
#endif
static int xw_pos(int w) { int i; for (i = 0; i < NAV_XWIN; i++) if (XW_ORDER[i] == w) return i; return 0; }
void poli_nav_select_turn(int steps)
{
    int n, w;
    if (steps == 0) return;
    if (xw >= 0) {               /* Core2-FxSeq: SELECT walks the ADDED windows and wraps (Wander-Drift: 8, in XW_ORDER) */
        int p = xw_pos(xw);
        p = ((p + steps) % NAV_XWIN + NAV_XWIN) % NAV_XWIN;
        xw = XW_ORDER[p];
    } else if (roll) {
        place(flat + steps, 1);
    } else {
        n = SEC_LEN[sec];
        w = sec_i + steps;
        w %= n;
        if (w < 0) w += n;
        place(SEC_WIN[sec][w], 0);
    }
    msg = MSG_NONE;
    nav_gen++;
}

void poli_nav_select_press(void)
{
    if (xw >= 0) {               /* Core2-FxSeq: SEL switches the window's feature on or off (not a preset edit) */
#if BOARD_SMK37
        if (xw == XW_PADREC) {   /* PAD REC: SEL is one-level undo, not on/off */
            xtra_padrec_undo();
            msg = MSG_PAD_UNDO;
            nav_gen++;
            return;
        }
#endif
        xtra_toggle(xw);
        msg = (((xw >= 2 && xw <= 4) || xw == 6) && xtra_on(xw) && xtra_c2() != 3) ? MSG_NO_C2 :
              (xw == 7 || xw == 8) ? (xtra_c2() != 3 ? MSG_NO_C2 : xtra_locked() ? MSG_LOCKED : MSG_GENERATED) :
              (xw >= 9 && xtra_on(xw) && xtra_c2() != 3) ? MSG_NO_C2 : MSG_NONE;
        nav_gen++;
        return;
    }
    switch (WIN_SW[flat]) {
    case SW_LPBP: poli_bp = (uint8_t)(poli_bp ^ 1u); msg = MSG_NONE; break;
    case SW_REP_F: poli_rep_f = (uint8_t)(poli_rep_f ^ 1u); msg = MSG_NONE; break;
    case SW_REP_A: poli_rep_a = (uint8_t)(poli_rep_a ^ 1u); msg = MSG_NONE; break;
    case SW_GLIDE: poli_glide = (uint8_t)(poli_glide ^ 1u); msg = MSG_NONE; break;
    case SW_LINK:
        polivoks_set_link_shapes(polivoks_link_shapes() ? 0u : 1u);
        if (polivoks_link_shapes())
            pos[SLOT_SHAPE2] = pos[SLOT_SHAPE1];
        msg = MSG_NONE;
        break;
    case SW_BASS:
        polivoks_set_bass_support(polivoks_bass_support() ? 0u : 1u);
        msg = MSG_NONE;
        break;
    case SW_KEYHOLD:
        polivoks_set_keyhold(polivoks_keyhold() ? 0u : 1u);
        msg = MSG_NONE;
        break;
    case SW_MODOSC:
        mod_osc_en = (uint8_t)(mod_osc_en ^ 1u);
        push_mod_osc();
        msg = MSG_NONE;
        break;
    default:
        msg = MSG_NO_SWITCH;
        nav_gen++;
        return;
    }
    nav_edits++;
    nav_gen++;
}


void poli_nav_tick_ms(uint32_t ms) { nav_now_ms = ms; }

#define ACC_GAP 60u
#define ACC_RATE 80u

/* Speed-based acceleration for wide knobs only (span > 16). Slow, first
 * clicks of a turn, reversals, and a clock that is not advancing stay
 * one value a detent. Time is the longer of this read and the previous,
 * so a single quick detent after a pause does not jump. */
static int nav_accel(int index, int steps, int span)
{
    uint32_t elapsed, i, dt, m, cap;
    int up, a;
    if (span <= 16 || steps == 0) return steps;
    up = steps > 0;
    a = up ? steps : -steps;
    elapsed = nav_now_ms - acc_ms[index];
    i = elapsed / (uint32_t)a;
    if (elapsed > 0u && i == 0u) i = 1u;
    if (!acc_have[index] || elapsed == 0u || acc_dir[index] != (up ? 1 : -1) || i >= ACC_GAP) {
        m = 1u;
        i = 127u;
    } else {
        dt = acc_dt[index];
        if (i > dt) dt = i;
        if (dt == 0u) dt = 1u;
        m = ACC_RATE / dt;
        cap = span > 100 ? 16u : span > 64 ? 8u : span > 32 ? 4u : 2u;
        if (m < 1u) m = 1u;
        if (m > cap) m = cap;
    }
    acc_have[index] = 1u;
    acc_ms[index] = nav_now_ms;
    acc_dir[index] = (int8_t)(up ? 1 : -1);
    acc_dt[index] = i ? i : 127u;
    return steps * (int)m;
}

void poli_nav_knob(int index, int steps)
{
    int kind;
    if (index < 0 || index > 3 || steps == 0) return;
    if (xw >= 0) {               /* Core2-FxSeq: K1..K4 of the ADDED window (not preset edits) */
        if (!(xw == 1 && index == 1))   /* Seq64: SEQ STEPS 1..64 moves one per detent (every value, no skip) */
            steps = nav_accel(index, steps, xtra_hi(xw, index) - xtra_lo(xw, index));
        xtra_knob(xw, index, steps);
        msg = MSG_NONE;
#if BOARD_SMK37
        if (xw == XW_PADREC && index == 0 && !xtra_padrec_armed() && !xtra_padrec_slot_used(xtra_padrec_slot()))
            msg = MSG_PAD_EMPTY;
#endif
        nav_gen++;
        return;
    }
    if (flat == 11) {   /* K1 receive channel 1..16; K2 key track 0/62/100. Not preset edits. */
        if (index == 0) {
            int n = (int)midi_ch + steps;
            if (n < 1) n = 1;
            if (n > 16) n = 16;
            if (n == (int)midi_ch) return;
            midi_ch = (uint8_t)n;
            msg = MSG_NONE;
            nav_gen++;
            return;
        }
        if (index == 1) {
            int n, cur = key_track == 0 ? 0 : key_track >= 100 ? 2 : 1;
            n = cur + steps;
            if (n < 0) n = 0;
            if (n > 2) n = 2;
            key_track = (uint8_t)(n == 0 ? 0 : n == 2 ? 100 : 62);
            polivoks_set_key_track(key_track);
            msg = MSG_NONE;
            nav_gen++;
            return;
        }
        return;
    }
    kind = WIN_SLOT[flat][index];
    if (kind != SLOT_EMPTY && kind != SLOT_EXT)
        steps = nav_accel(index, steps, nav_khi(kind) - nav_klo(kind));
    if (steps == 0) return;
    apply_kind(kind, steps);
    nav_gen++;
}

void poli_nav_octave(int dir)
{
    int o = polivoks_kb_oct();
    if (dir == 0) o = 0;
    else o += dir;
    polivoks_set_kb_oct(o);
    msg = MSG_OCTAVE;
    nav_gen++;
}

void poli_nav_lang_step(int steps)
{
    if (steps == 0) return;
    lang ^= 1;               /* one toggle per gesture, not per detent */
    msg = MSG_LANG;
    nav_gen++;
}

void poli_nav_fx(void)
{
    poli_nav_section(NAV_SEC_FX);
}

/* Main-loop screen jump: find the first WIN_SLOT window that holds kind and
 * open it the same way a section button does (section mode, not the HOME
 * roll). No-op if that window is already showing in section mode, if kind
 * has no page, or if kind is empty. Does not write a parameter and does not
 * bump nav_edits. MIDI CC must not call this (poli_nav_cc stays as it is). */
void poli_nav_show_slot(int kind)
{
    int f, k, arp_was;
    if (kind <= SLOT_EMPTY || kind >= SLOT_COUNT)
        return;
    arp_was = xw >= 0;            /* merge: a fader / wheel page jump leaves an EXTRAS window */
#if POLI_ARP
    if (poli_arp_page_close()) arp_was = 1;   /* and leaves the classic ARP page */
#endif
    xw = -1;
    for (f = 0; f < NAV_NWIN; f++) {
        for (k = 0; k < 4; k++) {
            if (WIN_SLOT[f][k] == kind) {
                if (flat == f && !roll) {
                    if (arp_was) { msg = MSG_NONE; nav_gen++; }   /* back from an EXTRAS window */
                    return;
                }
                place(f, 0);
                msg = MSG_NONE;
                nav_gen++;
                return;
            }
        }
    }
}

void poli_nav_reserved(int m)
{
    msg = m;
    nav_gen++;
}

int poli_nav_lang(void) { return lang; }
uint32_t poli_nav_value_generation(void) { return nav_val_gen; }   /* (Elite value-only repaint) */
uint32_t poli_nav_generation(void)
{
    if (xw >= 0 && xtra_gen() != xgen_seen) {   /* Core2-FxSeq: an ADDED window repaints for its own changes only */
        xgen_seen = xtra_gen();
        nav_gen++;
    }
    return nav_gen;
}
void poli_nav_xtra_open(int w)
{
    if (w < 0 || w >= NAV_XWIN) return;
    xw = w;
    xgen_seen = xtra_gen();
    msg = MSG_NONE;
    nav_gen++;
}
int poli_nav_xtra(void) { return xw; }
int poli_nav_midi_ch(void) { return midi_ch; }
void poli_nav_set_midi_ch(int ch)
{
    if (ch < 1 || ch > 16) ch = 1;
    if (midi_ch == (uint8_t)ch) return;
    midi_ch = (uint8_t)ch;
    nav_gen++;
}

int poli_nav_key_track(void) { return key_track; }
void poli_nav_set_key_track(int pct)
{
    uint8_t v;
    if (pct >= 100) v = 100;
    else if (pct <= 0) v = 0;
    else v = 62;
    if (key_track == v) return;
    key_track = v;
    polivoks_set_key_track(v);
    nav_gen++;
}

void poli_nav_view(poli_nav_view_t *v)
{
    int i;
    if (!v) return;
    v->section = sec;
    v->window = roll ? flat : sec_i;
    v->nwindows = roll ? NAV_NWIN : SEC_LEN[sec];
    v->roll = roll;
    v->flat = flat;
    v->lang = lang;
    v->kb_oct = polivoks_kb_oct();
    v->switch_kind = WIN_SW[flat];
    v->msg = msg;
    v->midi_ch = midi_ch;
    v->key_track = key_track;
    v->sync_ok = clk_q8 != 0u;
    v->xtra = xw;
    if (xw >= 0) {               /* Core2-FxSeq: section 5, windows 12..16; no slot kinds (poli_ui.c draws them) */
        v->section = NAV_SEC_XTRA;
        v->window = xw_pos(xw);
        v->nwindows = NAV_XWIN;
        v->roll = 0;
        v->flat = NAV_NWIN + xw;
        v->switch_kind = SW_NONE;
        v->switch_on = xtra_on(xw);
        for (i = 0; i < 4; i++) {
            v->slot_kind[i] = SLOT_EMPTY;
            v->slot_pos[i] = xtra_get(xw, i);
        }
        return;
    }
    switch (v->switch_kind) {
    case SW_LPBP: v->switch_on = poli_bp; break;
    case SW_REP_F: v->switch_on = poli_rep_f; break;
    case SW_REP_A: v->switch_on = poli_rep_a; break;
    case SW_LINK: v->switch_on = polivoks_link_shapes(); break;
    case SW_GLIDE: v->switch_on = poli_glide; break;
    case SW_BASS: v->switch_on = polivoks_bass_support(); break;
    case SW_KEYHOLD: v->switch_on = polivoks_keyhold(); break;
    case SW_MODOSC: v->switch_on = mod_osc_en; break;
    default: v->switch_on = 0; break;
    }
    for (i = 0; i < 4; i++) {
        int kind = WIN_SLOT[flat][i];
        int p = pos[kind];
        if (kind == SLOT_SHAPE2 && polivoks_link_shapes())
            p = pos[SLOT_SHAPE1];
        v->slot_kind[i] = kind;
        v->slot_pos[i] = p;
    }
}

/* ---- preset hooks (poli_preset.c). A patch is the 31 knob positions in
 * SLOT_CUT..SLOT_DUO order, signed ones offset to 0.., plus one switch byte.
 * Applying a patch goes through the same knob law as a turn (step B
 * envelope table), so a stored position always gives the same engine value.
 * Key hold is a playing switch: not stored, and turned off on a load. */
#define NAV_NKNOB (SLOT_DUO - SLOT_CUT + 1)
_Static_assert(NAV_NKNOB == 31, "preset knob count");
enum { PSW_LPBP = 1u, PSW_REP_F = 2u, PSW_REP_A = 4u, PSW_LINK = 8u, PSW_GLIDE = 16u, PSW_BASS = 32u,
       PSW_MODOSC = 128u };
static int nav_klo(int kind)
{
    switch (kind) {
    case SLOT_FOOT1: case SLOT_FOOT2: return -2;
    case SLOT_DETUNE: case SLOT_TUNE: return -64;
    default: return 0;
    }
}
static int nav_khi(int kind)
{
    switch (kind) {
    case SLOT_FOOT1: case SLOT_FOOT2: return 2;
    case SLOT_SHAPE1: case SLOT_SHAPE2: return 4;
    case SLOT_LFO_SHAPE: return 3;
    case SLOT_DUO: return 1;
    case SLOT_LFO_RST: return 1;
    case SLOT_LFO_SYNC: return 9;
    case SLOT_DETUNE: case SLOT_TUNE: return 64;
    default: return 127;
    }
}
/* stored byte range for knob i: 0..poli_nav_patch_max(i) */
int poli_nav_patch_max(int i)
{
    int kind = SLOT_CUT + i;
    if (i < 0 || i >= NAV_NKNOB) return 0;
    return nav_khi(kind) - nav_klo(kind);
}
void poli_nav_patch_get(uint8_t k[NAV_NKNOB], uint8_t *sw)
{
    int i;
    uint8_t b = 0;
    for (i = 0; i < NAV_NKNOB; i++) {
        int kind = SLOT_CUT + i, p = pos[kind];
        if (kind == SLOT_SHAPE2 && polivoks_link_shapes()) p = pos[SLOT_SHAPE1];
        k[i] = (uint8_t)(p - nav_klo(kind));
    }
    if (poli_bp) b |= PSW_LPBP;
    if (poli_rep_f) b |= PSW_REP_F;
    if (poli_rep_a) b |= PSW_REP_A;
    if (polivoks_link_shapes()) b |= PSW_LINK;
    if (poli_glide) b |= PSW_GLIDE;
    if (polivoks_bass_support()) b |= PSW_BASS;
    if (mod_osc_en) b |= PSW_MODOSC;
    *sw = b;
}
void poli_nav_patch_set(const uint8_t k[NAV_NKNOB], uint8_t sw)
{
    int i;
    uint32_t keep = nav_edits;
    polivoks_set_keyhold(0u);
    poli_bp = (uint8_t)((sw & PSW_LPBP) != 0);
    poli_rep_f = (uint8_t)((sw & PSW_REP_F) != 0);
    poli_rep_a = (uint8_t)((sw & PSW_REP_A) != 0);
    poli_glide = (uint8_t)((sw & PSW_GLIDE) != 0);
    polivoks_set_bass_support((sw & PSW_BASS) ? 1u : 0u);
    mod_osc_en = (uint8_t)((sw & PSW_MODOSC) != 0);
    polivoks_set_link_shapes((sw & PSW_LINK) ? 1u : 0u);
    for (i = 0; i < NAV_NKNOB; i++) {
        int kind = SLOT_CUT + i, p = (int)k[i] + nav_klo(kind);
        if (p < nav_klo(kind)) p = nav_klo(kind);
        if (p > nav_khi(kind)) p = nav_khi(kind);
        pos[kind] = (int16_t)p;
    }
    for (i = 0; i < NAV_NKNOB; i++) {
        int kind = SLOT_CUT + i;
        if (kind == SLOT_SHAPE1) {
            poli_shape = (uint8_t)pos[SLOT_SHAPE1];
        } else if (kind == SLOT_SHAPE2) {
            if (polivoks_link_shapes()) {
                pos[SLOT_SHAPE2] = pos[SLOT_SHAPE1];
                poli_shape2 = (uint8_t)pos[SLOT_SHAPE1];
            } else
                polivoks_set_shape2((uint8_t)pos[SLOT_SHAPE2]);
        } else
            apply_kind(kind, 0);
    }
    for (i = 0; i < NAV_NX; i++) {   /* extras back to OFF / FREE / 0; poli_nav_patch_set_x follows for a stored record */
        pos[X_SLOT[i]] = 0;
        apply_kind(X_SLOT[i], 0);
    }
    msg = MSG_NONE;
    nav_edits = keep;          /* a load is not an edit */
    nav_gen++;
}
int poli_nav_patch_max_x(int i) { return (i < 0 || i >= NAV_NX) ? 0 : nav_khi(X_SLOT[i]); }
void poli_nav_patch_get_x(uint8_t x[NAV_NX])
{
    int i;
    for (i = 0; i < NAV_NX; i++) x[i] = (uint8_t)pos[X_SLOT[i]];
}
void poli_nav_patch_set_x(const uint8_t x[NAV_NX])
{
    int i;
    uint32_t keep = nav_edits;
    for (i = 0; i < NAV_NX; i++) {
        int p = x ? x[i] : 0;
        if (p > nav_khi(X_SLOT[i])) p = 0;   /* not a value this build knows: the default, not a rejected record */
        pos[X_SLOT[i]] = (int16_t)p;
        apply_kind(X_SLOT[i], 0);
    }
    nav_edits = keep;
    nav_gen++;
}
uint32_t poli_nav_edits(void) { return nav_edits; }
void poli_nav_touch(void) { nav_gen++; }

/* MIDI CC from the main loop (volt.c midi_in / poli_nav_midi). Not called from
 * the audio ISR or polivoks_render. 0-127 is placed on the slot's real range
 * (identity for 0..127 slots; detune -64..64 is lo + val*(hi-lo)/127, so
 * CC 0 = -64, 64 = 0, 127 = +64) and applied with apply_kind(kind, 0), the
 * same law as a knob or a preset load. Switches are set, not toggled:
 * 0-63 off, 64-127 on. CC 120/123 are not in this map (panic stays in
 * polivoks_midi). LINK and unmapped slots are untouched. */
static const uint8_t CC_SLOT[][2] = {
    {1, SLOT_MOD_CUT}, {5, SLOT_GLIDE_RATE}, {7, SLOT_LEVEL}, {16, SLOT_DRIVE},
    {70, SLOT_CONT}, {71, SLOT_RES}, {72, SLOT_REL_A}, {73, SLOT_ATK_A},
    {74, SLOT_CUT}, {75, SLOT_DEC_A}, {76, SLOT_LFO_RATE}, {77, SLOT_FM},
    {78, SLOT_NOISE}, {79, SLOT_SUS_A}, {92, SLOT_MOD_AMP}, {94, SLOT_DETUNE},
    {102, SLOT_ATK_F}, {103, SLOT_DEC_F}, {104, SLOT_SUS_F}, {105, SLOT_REL_F}
};

int poli_nav_pos(int kind)
{
    if (kind <= SLOT_EMPTY || kind >= SLOT_COUNT)
        return 0;
    return (int)pos[kind];
}

void poli_nav_reflect_cut(int p)
{
    poli_nav_reflect_slot(SLOT_CUT, p);
}

void poli_nav_reflect_slot(int kind, int p)
{
    int lo, hi;
    if (kind <= SLOT_EMPTY || kind >= SLOT_COUNT)
        return;
    lo = nav_klo(kind);
    hi = nav_khi(kind);
    if (p < lo) p = lo;
    if (p > hi) p = hi;
    if (pos[kind] == (int16_t)p)
        return;
    pos[kind] = (int16_t)p;
    nav_edits++;
    nav_val_gen++;
}

static void nav_cc_apply(uint8_t cc, uint8_t val, int full_redraw)
{
    unsigned i;
    uint8_t on;

    val = (uint8_t)(val & 0x7Fu);
    on = (uint8_t)(val >= 64u);
    switch (cc) {
    case 64: polivoks_set_keyhold(on); break;
    case 65: poli_glide = on; break;
    case 85: poli_bp = on; break;           /* on = band pass */
    case 86: polivoks_set_bass_support(on); break;
    default:
        for (i = 0; i < (unsigned)(sizeof CC_SLOT / sizeof CC_SLOT[0]); i++) {
            if (CC_SLOT[i][0] == cc) {
                int kind = CC_SLOT[i][1];
                int lo = nav_klo(kind), hi = nav_khi(kind);
                int p = lo + ((int)val * (hi - lo)) / 127;
                if (p < lo) p = lo;
                if (p > hi) p = hi;
                pos[kind] = (int16_t)p;
                apply_kind(kind, 0);
                if (full_redraw) nav_gen++;
                else nav_val_gen++;
                return;
            }
        }
        return;
    }
    msg = MSG_NONE;
    nav_edits++;
    if (full_redraw) nav_gen++;
    else nav_val_gen++;
}

void poli_nav_cc(uint8_t cc, uint8_t val)
{
    nav_cc_apply(cc, val, 1);
}

void poli_nav_cc_live(uint8_t cc, uint8_t val)
{
    nav_cc_apply(cc, val, 0);
}

void poli_nav_midi(uint8_t status, uint8_t data1, uint8_t data2)
{
    uint8_t st = (uint8_t)(status & 0xF0u);
    if (status >= 0xF8u) {         /* realtime: clock and transport, any channel (no engine effect before) */
        if (status == 0xF8u) {
            clk_tick();
#if POLI_ARP
            poli_arp_clock(nav_now_ms);
#endif
        }
        else if (status == 0xFAu) { clk_n = 0; clk_start = 1u;
#if POLI_ARP
            poli_arp_start();
#endif
        }   /* Start: the next clock is beat 1 */
        else if (status == 0xFBu || status == 0xFCu) {              /* Continue / Stop: new run, tempo held */
            clk_n = 0;
#if POLI_ARP
            if (status == 0xFBu) poli_arp_continue(); else poli_arp_stop();
#endif
        }
        return;
    }
    /* Channel messages (notes, CC, pitch bend, pressure, program change,
     * CC 120/123) must match the receive channel. System messages (0xF0..)
     * have no channel and still pass. USB and TRS both arrive here. */
    if (status < 0xF0u && (uint8_t)((status & 0x0Fu) + 1u) != midi_ch)
        return;
    polivoks_midi(status, data1, data2);
    /* merge: the EXTRAS arpeggiator takes CC 120 / 123 in volt.c midi_in (xtra_midi -> xtra_panic), as on the FM-1 */
#if POLI_ARP
    /* Elite classic arp: CC 120, CC 123 and program change clear its gate, latch and held notes. */
    if (st == 0xC0u || (st == 0xB0u && (data1 == 120u || data1 == 123u)))
        poli_arp_clear();
#endif
    if (st == 0xB0u && data1 != 120u && data1 != 123u)
        poli_nav_cc(data1, data2);
}
