/* SPDX-License-Identifier: GPL-3.0-only */
/* Core2-FxSeq (15 Nov 2026): the ADDED features' main-program side (core 0, never the audio interrupt).
 * NOT Polivoks features: the original has no arpeggiator, sequencer, drum track, delay or reverb.
 * Included from poli_nav.c (one translation unit). Functions are not static so the host tests that include
 * poli_nav.c build with -Wall -Werror whether or not they call them.
 *
 * CLOCK  24 per quarter note. Internal tempo 40..240 BPM (SEQ window K1) from the millisecond clock, or the
 *        MIDI clock while one arrives (an F8 in the last 500 ms; the tempo is poli_nav.c's measured one). The
 *        transport (PLAY, or MIDI Start / Continue / Stop) runs the sequencer and the drum track; the
 *        arpeggiator follows the clock whenever it is on and a note is held.
 * ARP    plays the held keys (keybed, and MIDI notes on the receive channel) one at a time: UP, DOWN, UP-DN,
 *        RANDOM, ORDER (as pressed); 1..3 octaves; RATE 1/4..1/32 with triplets; GATE 5..100 % (100 = legato,
 *        no new attack). The first key sounds at once. While it is on, the keys do not play the voice
 *        directly. Notes reach the voice through polivoks_midi, as a MIDI note would.
 * SEQ    64 steps (Seq64, 22 Nov 2026; was 16), each a note or a rest; STEPS 1..64 (every value, one per knob
 *        detent, no acceleration), RATE, GATE. REC (button) writes keys or MIDI notes at
 *        the cursor and steps on; SEQ pressed during REC writes a rest. Mono: a key held while it plays
 *        takes the voice (the panel's last word, as before); MIDI notes too.
 * DRUMS  8 built-in 16-step patterns, kick / snare / hat, one step per 1/16 while the transport runs. The
 *        hits go to the drum voices on core 1 (poli_c2fx.c).
 * None of these settings is in a preset (the preset format is unchanged); all start OFF at power-on. */
#include "poli_xtra.h"
#ifndef XT_HOLD_DEFAULT
#define XT_HOLD_DEFAULT 1u                     /* Arp-Hold: ON at power-on (host suites written for the old arp build it 0) */
#endif

xf_par_t xf_par;                           /* main writes, core 1 reads */
xf_stat_t xf_st;                           /* core 1 writes, main reads */

enum { XW_ARP = 0, XW_SEQ, XW_DRUM, XW_DLY, XW_REV, XW_WAND, XW_DRIFT, XW_GROOVE, XW_GEN, XW_DFX, XW_PERF, XW_STEP, XW_N };
/* (Wander-Drift: 5..7; Drift-Full: 8 GENERATE, 9 DRUM FX, 10 PERFORM; Drum-Step: 11 DRUM STEP; Parity: PAD REC is
 * 12 and CLASSIC ARP 13, poli_xtra.h) */
enum { XT_SEQ_N = 64, XT_HELD_N = 12 };       /* Seq64 (22 Nov 2026): 64 sequencer steps (was 16) */
enum { XT_SEQ_DEF_N = 16 };                    /* the power-on pattern's steps; 17..64 start as rests */
static const uint8_t XT_RATE_CLK[7] = { 24, 16, 12, 8, 6, 4, 3 };              /* 1/4 1/4T 1/8 1/8T 1/16 1/16T 1/32 */
static const uint8_t XT_DLY_CLK[11] = { 3, 4, 6, 8, 9, 12, 16, 18, 24, 36, 48 }; /* 1/32 1/16T 1/16 1/8T 1/16D 1/8 1/4T 1/8D 1/4 1/4D 1/2 */
/* Wander-Drift: ARP MODE 5 WANDER, 6 FROM SEQ; DRUMS PATTERN 0..28 (1-8, USER, 5 genre packs of 4); WANDER
 * (Drums12, 21 Nov 2026: PATTERN 0..56 = 1-8, USER, then HOUSE, DUBST, DNB, TECHNO packs of 12; TRAP / PHONK packs
 * removed, their kit flavours stay on GROOVE KIT; GENERATE STYLE AUTO HOUSE DUBST DNB TECHNO)
 * LEAP REPEAT REST HOME; DRUM DRIFT CHANGE EVERY MUTATE POOL; GROOVE SWING HAT-BUSY FILL KIT */
/* Drift-Full: TEMPO 50..190 BPM (was 40..240); DRUM DRIFT CHANGE OFF NEXT RANDOM GEN, EVERY CHORD 4 8 16 32 64,
 * MUTATE OFF SHIFT MORPH, POOL; GROOVE FILL OFF ROLL TOMS, KIT AUTO + 8 flavours; GENERATE STYLE DENSITY SURPRISE
 * LOCK; DRUM FX DRIVE DUCK ECHO REVERB; PERFORM GATE REPEAT LANE STATE */
#define XT_BPM_MIN 50
#define XT_BPM_MAX 190
static const int16_t XT_LO[XW_N][4] = { {0, 0, 1, 1}, {XT_BPM_MIN, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0},
                                        {0, 0, 0, 0}, {0, 0, 0, 0}, {50, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0},
                                        {0, 0, 0, 0}, {0, 0, 0, 0} };
static const int16_t XT_HI[XW_N][4] = { {6, 6, 3, 20}, {XT_BPM_MAX, XT_SEQ_N, 6, 20}, {56, 127, 127, 127}, {10, 127, 127, 127},
                                        {127, 127, 0, 127}, {127, 127, 127, 127}, {3, 5, 2, 9}, {75, 4, 2, 8},
                                        {4, 4, 127, 1}, {127, 127, 127, 127}, {2, 5, 5, 2},
                                        {15, 3, 1, 0} };   /* Drum-Step: K3/K7 PLAY GEN 0..1 (K4/K8 empty) */
static const int16_t XT_DEF[XW_N][4] = { {0, 4, 1, 10}, {120, 16, 4, 10}, {0, 90, 64, 64}, {7, 50, 80, 70},
                                         {80, 60, 0, 90}, {40, 20, 10, 30}, {1, 2, 1, 8}, {50, 2, 1, 0},
                                         {0, 2, 32, 0}, {0, 64, 0, 40}, {0, 1, 0, 0}, {0, 0, 0, 0} };
/* the drum patterns (Wander-Drift): 17 lanes, bit i = step i (1/16); tools/gen_poli_drums.py writes the table */
typedef struct { uint16_t l[17]; uint8_t kit, lng; uint32_t roll; uint32_t bell[2]; } xt_pat_t;
enum { L_KICK = 0, L_SNARE, L_GHOST, L_CLAP, L_CH, L_OH, L_LT, L_MT, L_HT, L_RIM, L_COWB, L_CLAVE, L_LC, L_MC, L_HC,
       L_RIDE, L_CRASH };
#include "poli_drum_pat.inc"
enum { XT_PAT_N = XT_PAT_ROM + 1, XT_PAT_USER = 8 };   /* the knob: 1-8, USER (9th slot), then the packs */
#define XT_PACK  12                            /* Drums12: patterns per genre pack */
#define XT_PACKS 4                             /* HOUSE, DUBST, DNB, TECHNO */
_Static_assert(XT_PAT_ROM == 8 + XT_PACKS * XT_PACK, "poli_drum_pat.inc must match the 12-per-pack knob");
static const uint8_t XT_SEQ_DEF[XT_SEQ_DEF_N] = { 41, 41, 53, 41, 255, 41, 48, 51, 41, 41, 53, 41, 46, 255, 48, 44 };

static struct {
    uint8_t on[XW_N];
    int16_t k[XW_N][4];
    uint8_t seq[XT_SEQ_N];
    uint8_t held[XT_HELD_N], hvel[XT_HELD_N], nheld;
    uint8_t arp_note, seq_note, prev_note;       /* sounding (255 = none) */
    uint32_t arp_off_ms, seq_off_ms;
    uint8_t arp_off_pend, seq_off_pend;
    uint32_t arp_cnt, arp_i, rng;
    uint8_t run, rec, rec_cur;
    uint32_t tick_n, acc_q8, last_ms, ext_ms;
    uint8_t ext, ext_start;
    uint32_t prev_keys;
    uint8_t seq_pos, drum_pos;
    uint8_t c2;                                  /* core 1's state, from volt.c (poli_c2fx.c XF_C2_*) */
    uint32_t c2_late;
    uint32_t gen;                                /* screen generation: bumps when the screen should change */
    uint32_t ticks_total;                        /* tests */
    /* Wander-Drift */
    xt_pat_t user, wp;                           /* the 9th slot (NEW BEAT); the bar's working copy */
    uint8_t wp_src, pend_pat;                    /* the knob value wp came from; a pattern for the next bar (255) */
    uint32_t bar, changes, fills, mutations;     /* bars since PLAY (shared clock); counts for the tests */
    uint8_t fill_from, crash_next, brk, brk_req, rec_down, rec_used;
    uint32_t rec_t0;
    uint8_t roll_cur, kit_now;
    uint32_t sw_d_mask, sw_d_due; uint8_t sw_d_st, sw_d_pend;   /* swing: a held-back drum step */
    uint8_t sw_s_pend, sw_s_st; uint32_t sw_s_due;              /* ... sequencer step */
    uint8_t sw_a_pend; uint32_t sw_a_due;                       /* ... arp step */
    uint8_t w_idx, w_prev, w_hist[16], w_hpos, w_hn, w_lock, w_lpos, w_last;
    uint32_t patch_key;
    /* Arp-Hold (17 Nov 2026): HOLD latches the last key set; dn = the keys physically down (keybed + MIDI);
     * arp_t0 = when the sounding arp step began; drng = the drums' own random numbers (never drawn by the arp) */
    uint8_t hold, dn[XT_HELD_N], ndn;
    uint32_t arp_t0, drng;
    /* Drift-Full: the playing copy reloads on the next beat; genre of USER; the chord count; the fill's kind, the held
     * FILL, a step in a fill; MUTE / SOLO per lane group (0 on, 1 mute, 2 solo); the held PERFORM effects (bits as
     * xf_par.perf) and a REPEAT waiting for the next 16th; MIDI notes down with ARP off (chord changes); counts */
    uint8_t reload, user_style, chord_cnt, fill_kind, fill_hold, fill_now, lane_st[6], rep_pend;
    uint32_t perf_bits, mdn, surprises, gens;
    /* Drums12-Transpose (21 Nov 2026): the sequencer's key transpose (semitones, added when a step plays; the stored
     * steps never change) and the MIDI notes it took (their note-offs are taken too) */
    int16_t tr;
    uint32_t tr_mid[4];
    /* Drum-Step (22 Nov 2026): the USER slot's chance per hit, 2 bits a step (0 = 100 %, 1 = 75, 2 = 50, 3 = 25 %),
     * by position (a hit the Drift moves takes its new cell's chance); the voice last touched on DRUM STEP (0..15) */
    uint32_t chance[17];
    uint8_t st_voice;
    uint8_t wp_gen, wp_style;            /* PLAY GEN: temp live beat (fits in the tail after st_voice) */
} xt XT_KEEP;
xf_patch_t xf_patch[XF_DI] XT_KEEP;
#if POLI_PADREC
void xtra_padrec_follow_user(void);              /* poli_padrec.inc (end of this file) */
#endif
#if POLI_ARP
void xtra_carp_toggle(void);                     /* (end of this file) */
#endif

/* ---- settings */
#if POLI_PADREC || POLI_ARP
/* Parity (23 Nov 2026): PAD REC (12) and CLASSIC ARP (13) keep their settings in their own modules */
#if POLI_ARP
static int xt_carp_lo(int i);
static int xt_carp_hi(int i);
static int xt_carp_get(int i);
#endif
int xtra_lo(int w, int i)
{
#if POLI_PADREC
    if (w == XW_PADREC) return xtra_padrec_lo(i);
#endif
#if POLI_ARP
    if (w == XW_CARP) return xt_carp_lo(i);
#endif
    return (w < 0 || w >= XW_N || i < 0 || i > 3) ? 0 : XT_LO[w][i];
}
int xtra_hi(int w, int i)
{
#if POLI_PADREC
    if (w == XW_PADREC) return xtra_padrec_hi(i);
#endif
#if POLI_ARP
    if (w == XW_CARP) return xt_carp_hi(i);
#endif
    return (w < 0 || w >= XW_N || i < 0 || i > 3) ? 0 : XT_HI[w][i];
}
static int xt_step_chance_now(void);
int xtra_get(int w, int i)
{
    if (w == XW_STEP && i == 1) return xt_step_chance_now();   /* Drum-Step: CHANCE shows the cell's, live */
#if POLI_PADREC
    if (w == XW_PADREC) return xtra_padrec_get(i);
#endif
#if POLI_ARP
    if (w == XW_CARP) return xt_carp_get(i);
#endif
    return (w < 0 || w >= XW_N || i < 0 || i > 3) ? 0 : xt.k[w][i];
}
#else
int xtra_lo(int w, int i) { return (w < 0 || w >= XW_N || i < 0 || i > 3) ? 0 : XT_LO[w][i]; }
int xtra_hi(int w, int i) { return (w < 0 || w >= XW_N || i < 0 || i > 3) ? 0 : XT_HI[w][i]; }
static int xt_step_chance_now(void);
int xtra_get(int w, int i)
{
    if (w == XW_STEP && i == 1) return xt_step_chance_now();   /* Drum-Step: CHANCE shows the cell's, live */
    return (w < 0 || w >= XW_N || i < 0 || i > 3) ? 0 : xt.k[w][i];
}
#endif
#if POLI_ARP
int xtra_on(int w)
{
    if (w == XW_CARP) return poli_arp_on();      /* Parity: the CLASSIC ARP page shows the classic arp's on / off */
    return (w < 0 || w >= XW_N) ? 0 : xt.on[w == XW_STEP ? XW_DRUM : w];
}
#else
int xtra_on(int w) { return (w < 0 || w >= XW_N) ? 0 : xt.on[w == XW_STEP ? XW_DRUM : w]; }   /* (DRUM STEP: DRUMS') */
#endif
int xtra_running(void) { return xt.run; }
int xtra_rec(void) { return xt.rec; }
int xtra_rec_cur(void) { return xt.rec_cur; }
int xtra_ext(void) { return xt.ext; }
int xtra_seq_pos(void) { return xt.seq_pos; }
int xtra_drum_pos(void) { return xt.drum_pos; }
int xtra_seq_note(int i) { return (i < 0 || i >= XT_SEQ_N) ? 255 : xt.seq[i]; }
void xtra_set_seq_note(int i, int n) { if (i >= 0 && i < XT_SEQ_N) xt.seq[i] = (uint8_t)(n < 0 || n > 127 ? 255 : n); }
uint32_t xtra_gen(void) { return xt.gen; }
int xtra_c2(void) { return xt.c2; }
void xtra_set_c2(int s, uint32_t late) { if (xt.c2 != (uint8_t)s) xt.gen++; xt.c2 = (uint8_t)s; xt.c2_late = late; }
uint32_t xtra_c2_late(void) { return xt.c2_late; }
uint32_t xtra_ticks_total(void) { return xt.ticks_total; }
static const xt_pat_t *xt_pat(int k) { return k == XT_PAT_USER ? &xt.user : &XT_PAT[k < XT_PAT_USER ? k : k - 1]; }
int xtra_drum_hit(int pat, int voice, int step)   /* voice 0 kick, 1 snare, 2 closed hat (the 8 original lanes) */
{
    static const uint8_t LN[3] = { L_KICK, L_SNARE, L_CH };
    if (pat < 0 || pat >= XT_PAT_N || voice < 0 || voice > 2 || step < 0 || step > 15) return 0;
    return (xt_pat(pat)->l[LN[voice]] >> step) & 1u;
}
/* ms x 256 per clock (24 per quarter): the MIDI clock's measured tempo while it runs, else the TEMPO knob */
uint32_t xtra_tick_q8(void)
{
    if (xt.ext && poli_nav_sync_valid() && poli_nav_sync_q8() >= 24u * 256u)
        return poli_nav_sync_q8() / 24u;
    return 640000u / (uint32_t)(xt.k[XW_SEQ][0] < XT_BPM_MIN ? XT_BPM_MIN : xt.k[XW_SEQ][0]);
}
int xtra_bpm(void) { return (int)((640000u + xtra_tick_q8() / 2u) / xtra_tick_q8()); }
/* the delay's spacing in samples (44100 Hz assumed, as every rate in this firmware); the longest is the line */
uint32_t xtra_dly_samples(void)
{
    uint32_t n = (uint32_t)XT_DLY_CLK[xt.k[XW_DLY][0]] * xtra_tick_q8() * 441u / 2560u;
    if (n > XF_DLY_MAX) n = XF_DLY_MAX;
    if (n < XF_DLY_MIN) n = XF_DLY_MIN;
    return n;
}
static int32_t xt_q15_sq(int p) { return (int32_t)p * p * 2; }   /* 0..127 -> 0..32258, a level law */
/* per-16-sample decay for a time constant of tau_ms: 32768 - 16 * 32768 / (tau * 44.1) */
static int32_t xt_decay(int32_t tau_ms)
{
    int32_t n = tau_ms * 441 / 10;
    if (n < 32) n = 32;
    return 32768 - (16 * 32768) / n;
}
/* ---- Wander-Drift: the two drum kits. Ground-up voicing in the spirit of the TR-808 / TR-909 families, from the
 * well-known published descriptions of how those circuits sound (a bridged-T style sine that falls in pitch, noise
 * through filters, six square oscillators for the metal, two for the cowbell, the clap's re-triggered noise): no
 * samples, no copied code. hz1 = the pitch it settles to, hz0 = where it starts; taus in ms (time to 1/e; TONE and
 * N/M paths; DECAY scales ttau and ntau); fhz the filter; q in %, mode 0 low 1 band 2 high pass; mset the
 * oscillator set (0 none, 1 808 metal, 2 808 cowbell, 3 909 metal, 4 ride). */
typedef struct {
    uint16_t hz1, hz0, tamp, ttau, ptau, namp, mamp, ntau, btau, fhz;
    uint8_t q, mode, bursts, gapms, mset;
    int8_t choke;
} xt_voice_t;
enum { DI_BD8 = 0, DI_BDL8, DI_SD8, DI_CP8, DI_CH8, DI_OH8, DI_LT8, DI_MT8, DI_HT8, DI_CB8, DI_CL8, DI_RS8, DI_LC8,
       DI_MC8, DI_HC8, DI_BD9, DI_SD9, DI_CP9, DI_CH9, DI_OH9, DI_LT9, DI_MT9, DI_HT9, DI_RS9, DI_RD9, DI_CR9 };
static const xt_voice_t XT_VOICE[XF_DI] = {
    /*  hz1   hz0  tamp ttau ptau  namp  mamp ntau btau  fhz    q mode b gap set choke */
    {   49,   98, 30000, 220, 25,     0,    0,   1,  1,    0, 100, 0, 0, 0, 0, -1 },   /* 808 kick */
    {   45,   90, 31000, 650, 35,     0,    0,   1,  1,    0, 100, 0, 0, 0, 0, -1 },   /* 808 long sub kick */
    {  180,  260, 13000,  45, 12, 14000,    0,  65,  1, 2000,  80, 2, 0, 0, 0, -1 },   /* 808 snare */
    {    0,    0,     0,   1,  1, 20000,    0,  90,  4, 1100,  55, 1, 3, 9, 0, -1 },   /* 808 clap */
    {    0,    0,     0,   1,  1,  2500, 3200,  22,  1, 6800,  90, 2, 0, 0, 1, DI_OH8 },   /* 808 closed hat */
    {    0,    0,     0,   1,  1,  2500, 3200, 260,  1, 6800,  90, 2, 0, 0, 1, -1 },   /* 808 open hat */
    {   85,  115, 26000, 200, 60,  1500,    0,  25,  1, 1500, 100, 0, 0, 0, 0, -1 },   /* 808 low tom */
    {  125,  165, 26000, 170, 60,  1500,    0,  25,  1, 1800, 100, 0, 0, 0, 0, -1 },   /* 808 mid tom */
    {  180,  240, 26000, 140, 60,  1500,    0,  25,  1, 2200, 100, 0, 0, 0, 0, -1 },   /* 808 high tom */
    {    0,    0,     0,   1,  1,     0, 9000, 110,  1,  800,  45, 1, 0, 0, 2, -1 },   /* 808 cowbell */
    { 2500, 2500, 22000,  14,  1,     0,    0,   1,  1,    0, 100, 0, 0, 0, 0, -1 },   /* 808 clave */
    { 1700, 1750, 16000,   9,  2,  7000,    0,   5,  1, 3000, 100, 2, 0, 0, 0, -1 },   /* 808 rimshot */
    {  165,  200, 22000, 120, 20,     0,    0,   1,  1,    0, 100, 0, 0, 0, 0, -1 },   /* 808 low conga */
    {  250,  300, 22000, 100, 20,     0,    0,   1,  1,    0, 100, 0, 0, 0, 0, -1 },   /* 808 mid conga */
    {  370,  440, 22000,  85, 20,     0,    0,   1,  1,    0, 100, 0, 0, 0, 0, -1 },   /* 808 high conga */
    {   52,  210, 30000, 170,  9, 16000,    0,   3,  1, 3000, 100, 2, 0, 0, 0, -1 },   /* 909 kick (sweep + click) */
    {  190,  330, 14000,  38,  8, 18000,    0, 110,  1, 3200,  70, 2, 0, 0, 0, -1 },   /* 909 snare */
    {    0,    0,     0,   1,  1, 21000,    0, 140,  4, 1500,  50, 1, 4, 8, 0, -1 },   /* 909 clap */
    {    0,    0,     0,   1,  1,  7000, 2400,  30,  1, 7500,  80, 2, 0, 0, 3, DI_OH9 },   /* 909 closed hat */
    {    0,    0,     0,   1,  1,  7000, 2400, 330,  1, 7500,  80, 2, 0, 0, 3, -1 },   /* 909 open hat */
    {   95,  150, 26000, 260, 45,  3000,    0,  40,  1,  900, 100, 0, 0, 0, 0, -1 },   /* 909 low tom */
    {  140,  215, 26000, 230, 45,  3000,    0,  40,  1, 1100, 100, 0, 0, 0, 0, -1 },   /* 909 mid tom */
    {  200,  300, 26000, 200, 45,  3000,    0,  40,  1, 1400, 100, 0, 0, 0, 0, -1 },   /* 909 high tom */
    { 1100, 1150, 15000,  10,  2,  9000,    0,   6,  1, 4000,  60, 1, 0, 0, 0, -1 },   /* 909 rimshot */
    {    0,    0,     0,   1,  1,  3000, 2600, 900,  1, 5200,  40, 1, 0, 0, 4, -1 },   /* 909 ride */
    {    0,    0,     0,   1,  1, 11000, 3000,1100,  1, 4500,  90, 2, 0, 0, 3, -1 },   /* 909 crash */
};
static const uint16_t XT_MSET[5][6] = { {0, 0, 0, 0, 0, 0}, {205, 304, 370, 523, 540, 800}, {540, 800, 0, 0, 0, 0},
                                        {310, 457, 612, 845, 1110, 1478}, {380, 529, 727, 955, 1290, 1680} };
/* the state-variable filter's f = 2 sin(pi fc / fs), Q15 (x - x^3 / 6), held under 0.98 for stability */
static int32_t xt_svf_f(int32_t hz)
{
    int32_t x = hz * 2334 / 1000, x3 = (x * x >> 15) * x >> 15, f = 2 * (x - x3 / 6);
    return f > 32000 ? 32000 : f < 16 ? 16 : f;
}
/* main program, at power-on and when TUNE or DECAY turns: the 26 instruments for core 1 */
static void xt_patches(void)
{
    int32_t tq = 128 + xt.k[XW_DRUM][2] * 2, dq = 32 + xt.k[XW_DRUM][3] * 3 / 2;   /* 0.5 .. 1.49; 0.25 .. 1.74 */
    uint32_t i, j;
    for (i = 0; i < XF_DI; i++) {
        const xt_voice_t *v = &XT_VOICE[i];
        xf_patch_t *p = &xf_patch[i];
        int32_t i1 = (v->hz1 * tq >> 8) * 97392, i0 = (v->hz0 * tq >> 8) * 97392;   /* 2^32 / 44100 = 97391.5 */
        p->tamp = v->tamp;
        p->inc1 = i1;
        p->dinc = i0 - i1;
        p->tdec = xt_decay(v->ttau * dq >> 7);
        p->pdec = xt_decay(v->ptau);
        p->namp = v->namp;
        p->mamp = v->mamp;
        p->ndec = xt_decay(v->ntau * dq >> 7);
        p->bdec = xt_decay(v->btau);
        p->f = xt_svf_f(v->fhz);
        p->q = v->q * 328;
        p->mode = v->mode;
        p->bursts = v->bursts;
        p->bgap = v->gapms * 441 / 160 + 1;              /* blocks of 16 samples */
        p->choke = v->choke;
        p->mn = v->mset == 0 ? 0 : v->mset == 2 ? 2 : 6;
        for (j = 0; j < 6u; j++) p->minc[j] = (XT_MSET[v->mset][j] * tq >> 8) * 97392;
        /* Drift-Full: the echo / reverb send weight (kick none, hats a little, snare / clap / rim more, the rest
         * between) and the kick flag (DRIVE, DUCK) */
        p->kick = i == DI_BD8 || i == DI_BDL8 || i == DI_BD9;
        p->send = p->kick ? 0 : (i == DI_CH8 || i == DI_OH8 || i == DI_CH9 || i == DI_OH9 || i == DI_RD9 || i == DI_CR9) ? 4915 :
                  (i == DI_SD8 || i == DI_CP8 || i == DI_RS8 || i == DI_SD9 || i == DI_CP9 || i == DI_RS9) ? 19660 : 14745;
    }
}
static int xt_flavour(void);
static uint32_t xt_audible(void);
static const uint16_t XT_KF_DRV[8];
static const uint8_t XT_KF_CRUSH[8];
/* Drift-Full: MIX mode is wanted while DRUMS is on, the PERFORM keys are armed or a TRANCE GATE is set */
static int xt_mix_wanted(void) { return xt.on[XW_DRUM] || xt.on[XW_PERF] || xt.k[XW_PERF][0] != 0; }
/* settings -> what core 1 reads (main program; a few words, any time) */
void xtra_push(void)
{
    int p;
    xf_par.dly_len = (int32_t)xtra_dly_samples();
    xf_par.dly_fb = xt.k[XW_DLY][1] * 29491 / 127;                    /* up to 0.90 */
    xf_par.dly_tone = 4000 + xt.k[XW_DLY][2] * 28767 / 127;           /* dark .. open */
    xf_par.dly_lvl = xt_q15_sq(xt.k[XW_DLY][3]);
    xf_par.rev_fb = 22938 + xt.k[XW_REV][0] * 7864 / 127;             /* 0.70 .. 0.94 */
    xf_par.rev_damp = 32767 - xt.k[XW_REV][1] * 26000 / 127;          /* damper coefficient: 127 = darkest */
    xf_par.rev_lvl = xt_q15_sq(xt.k[XW_REV][3]);
    xf_par.drm_lvl = xt_q15_sq(xt.k[XW_DRUM][1]);
    p = (int)((uint32_t)xt.k[XW_DRUM][2] << 8 | (uint32_t)xt.k[XW_DRUM][3]);   /* TUNE, DECAY: the patches */
    if ((uint32_t)p + 1u != xt.patch_key) { xt.patch_key = (uint32_t)p + 1u; xt_patches(); }
    xf_par.dly_on = xt.on[XW_DLY] && xt.c2 == 3;
    xf_par.rev_on = xt.on[XW_REV] && xt.c2 == 3;
    {   /* Drift-Full: MIX mode and its settings */
        static const uint8_t RN[6] = { 8, 4, 8, 2, 1, 1 }, RD[6] = { 1, 1, 3, 1, 1, 2 };   /* 1/2 1/4 1/6 1/8 1/16 1/32 */
        int32_t ss = (int32_t)(xtra_tick_q8() * 6u * 441u / 2560u), f = xt_flavour(), rl;
        int ri = xt.k[XW_PERF][1];
        xf_par.step_smp = ss;
        xf_par.duck_on = xt.on[XW_DFX] && (xt_audible() & 1u);
        xf_par.duck_depth = xt.k[XW_DFX][1] * 27852 / 127;            /* up to 0.85 */
        xf_par.duck_k = 32768 - 699051 / (ss > 64 ? ss : 64);          /* about 1/e^4 after 3 sixteenths */
        if (xf_par.duck_k < 16384) xf_par.duck_k = 16384;
        xf_par.gate = xt.k[XW_PERF][0];
        rl = ss * RN[ri] / RD[ri];
        while (rl > (int32_t)XF_MR_MAX) rl >>= 1;                      /* a REPEAT longer than the ring: halved */
        xf_par.rep_len = rl;
        xf_par.perf = (int32_t)xt.perf_bits;
        xf_par.kdrive = 4096 + xt.k[XW_DFX][0] * 128 + XT_KF_DRV[f];
        xf_par.crush = XT_KF_CRUSH[f];
        xf_par.d_echo = xt_q15_sq(xt.k[XW_DFX][2]);
        xf_par.d_rev = xt_q15_sq(xt.k[XW_DFX][3]);
        xf_par.mix_on = xt_mix_wanted() && xt.c2 == 3;
    }
}
int xtra_fx_wanted(void) { return xt.on[XW_DLY] || xt.on[XW_REV] || xt.on[XW_DRUM] || xt_mix_wanted(); }

/* ---- notes to the voice (polivoks_midi: the one way in, as MIDI) */
static void xt_off(uint8_t *cur)
{
    if (*cur != 255u) {
        polivoks_midi(0x80u, *cur, 0);
        *cur = 255u;
    }
}
static void xt_on(uint8_t *cur, uint8_t n, uint8_t v)
{
    polivoks_midi(0x90u, n, v ? v : 1u);
    *cur = n;
}
static uint32_t xt_gate_ms(int rate_clk, int gate20)
{
    return xtra_tick_q8() * (uint32_t)rate_clk * (uint32_t)gate20 / (256u * 20u);
}

/* ---- the arpeggiator */
static void xt_held_add(uint8_t n, uint8_t v)
{
    int i;
    for (i = 0; i < xt.nheld; i++) if (xt.held[i] == n) return;
    if (xt.nheld >= XT_HELD_N) return;
    xt.held[xt.nheld] = n;
    xt.hvel[xt.nheld] = v;
    xt.nheld++;
}
static void xt_held_del(uint8_t n)
{
    int i, j;
    for (i = 0; i < xt.nheld; i++)
        if (xt.held[i] == n) {
            for (j = i; j + 1 < xt.nheld; j++) { xt.held[j] = xt.held[j + 1]; xt.hvel[j] = xt.hvel[j + 1]; }
            xt.nheld--;
            return;
        }
}
/* the note at position i of the current pattern (mode, octaves), and the pattern's length */
int xtra_arp_len(void)
{
    int n = xt.nheld * xt.k[XW_ARP][2], m = xt.k[XW_ARP][0];
    if (m == 2 && n > 1) return 2 * n - 2;
    return n;
}
int xtra_arp_note(uint32_t i, uint8_t *vel)
{
    uint8_t s[XT_HELD_N], v[XT_HELD_N];
    int n = xt.nheld, j, k, m = xt.k[XW_ARP][0], len = n * xt.k[XW_ARP][2], o, idx, note;
    if (n == 0) return -1;
    for (j = 0; j < n; j++) { s[j] = xt.held[j]; v[j] = xt.hvel[j]; }
    if (m != 4)                                   /* ORDER keeps the pressing order; the rest sort up */
        for (j = 1; j < n; j++)
            for (k = j; k > 0 && s[k - 1] > s[k]; k--) {
                uint8_t t = s[k]; s[k] = s[k - 1]; s[k - 1] = t;
                t = v[k]; v[k] = v[k - 1]; v[k - 1] = t;
            }
    if (m == 3) {
        xt.rng = xt.rng * 1664525u + 1013904223u;
        idx = (int)((xt.rng >> 16) % (uint32_t)len);
    } else {
        idx = (int)(i % (uint32_t)xtra_arp_len());
        if (m == 1) idx = len - 1 - idx;
        else if (m == 2 && idx >= len) idx = 2 * len - 2 - idx;
    }
    o = idx / n;
    note = s[idx % n] + 12 * o;
    if (note > 127) note = 127;
    if (vel) *vel = v[idx % n];
    return note;
}
/* ---- Wander-Drift: small helpers (main program; no part of this runs in the audio interrupt) */
static uint32_t xt_rnd(void) { xt.rng = xt.rng * 1664525u + 1013904223u; return xt.rng >> 16; }   /* 0..65535 */
static int xt_rndn(int n) { return (int)((xt_rnd() * (uint32_t)n) >> 16); }                     /* 0..n-1 */
/* Arp-Hold: the drums (Drift CHANGE / MUTATE, NEW BEAT) draw from their own generator, so the arp's RAND mode and
 * WANDER's Markov walk can never change which drum pattern or mutation comes next (and the other way round) */
static uint32_t xt_drnd(void) { xt.drng = xt.drng * 1664525u + 1013904223u; return xt.drng >> 16; }   /* 0..65535 */
static int xt_drndn(int n) { return (int)((xt_drnd() * (uint32_t)n) >> 16); }                      /* 0..n-1 */
static int xt_pop(uint32_t m) { int n = 0; while (m) { m &= m - 1u; n++; } return n; }
static int xt_nth(uint32_t m, int k) { int i; for (i = 0; i < 32; i++) if ((m >> i) & 1u) { if (!k--) return i; } return -1; }
/* swing: how long a swung (odd) 16th waits, ms; 0 = straight. SWING 50..75 %: the second 16th of each 8th lands at
 * that share of the 8th */
static uint32_t xt_swing_ms(void)
{
    int sw = xt.k[XW_GROOVE][0];
    if (sw <= 50) return 0;
    return xtra_tick_q8() * 6u * (uint32_t)(sw - 50) / 12800u;
}
static int xt_swung_tick(uint32_t t) { return xt.k[XW_GROOVE][0] > 50 && t % 12u == 6u; }

/* ---- the arpeggiator's step (UP .. ORDER as before; 5 WANDER, 6 FROM SEQ) */
static void xt_wander_step(uint32_t now);
static void xt_arp_fire(uint32_t now, int n, uint8_t v)
{
    int gate = xt.k[XW_ARP][3];
    if (n < 0) { xt_off(&xt.arp_note); xt.arp_off_pend = 0; return; }   /* (WANDER: a rest) */
    xt.arp_t0 = now;
    if (gate >= 20 && xt.arp_note != 255u) {
        xt_on(&xt.arp_note, (uint8_t)n, v);      /* legato: the gate stays up, the pitch moves */
        xt.arp_off_pend = 0;
        return;
    }
    xt_off(&xt.arp_note);
    xt_on(&xt.arp_note, (uint8_t)n, v);
    xt.arp_off_ms = now + xt_gate_ms(XT_RATE_CLK[xt.k[XW_ARP][1]], gate);
    xt.arp_off_pend = gate < 20;
}
static void xt_arp_step(uint32_t now)
{
    uint8_t v = 127;
    int n;
    if (xt.k[XW_ARP][0] >= 5) { xt_wander_step(now); return; }
    n = xtra_arp_note(xt.arp_i++, &v);
    if (n < 0) return;
    xt_arp_fire(now, n, v);
}
static void xt_arp_held_changed(uint32_t now, int was)
{
    if (!was && xt.nheld) {                      /* the first key: at once, the clock count starts here */
        xt.arp_i = 0;
        xt.arp_cnt = 0;
        xt_arp_step(now);
    } else if (was && !xt.nheld) {               /* Arp-Hold: the sounding step finishes (no cut, no click) */
        uint32_t end = xt.arp_t0 + xt_gate_ms(XT_RATE_CLK[xt.k[XW_ARP][1]], 20);
        if (xt.arp_note != 255u && (!xt.arp_off_pend || (int32_t)(xt.arp_off_ms - end) > 0)) xt.arp_off_ms = end;
        xt.arp_off_pend = xt.arp_note != 255u;
        xt.sw_a_pend = 0;
    }
}
/* ---- Arp-Hold: keys down (keybed and MIDI) and the latched set. HOLD on: a release leaves the note in the set;
 * the first key pressed while no key is down replaces the set (a fresh chord, from its first note at the next
 * step, the clock phase kept); keys added while others are down join it. HOLD off: a release removes the note. */
static void xt_dn_add(uint8_t n)
{
    int i;
    for (i = 0; i < xt.ndn; i++) if (xt.dn[i] == n) return;
    if (xt.ndn < XT_HELD_N) xt.dn[xt.ndn++] = n;
}
static int xt_dn_has(uint8_t n) { int i; for (i = 0; i < xt.ndn; i++) if (xt.dn[i] == n) return 1; return 0; }
static void xt_chord(void);
static void xt_key_on(uint8_t n, uint8_t v)
{
    if (!xt.ndn) xt_chord();                      /* Drift-Full: a new key set = a chord change */
    if (xt.hold && !xt.ndn && xt.nheld) { xt.nheld = 0; xt.arp_i = 0; }   /* a fresh chord */
    xt_dn_add(n);
    xt_held_add(n, v);
    xt.gen++;                                     /* (the ARPEGGIO window shows LATCH n) */
}
static void xt_key_off(uint8_t n)
{
    int i;
    for (i = 0; i < xt.ndn; i++)
        if (xt.dn[i] == n) { xt.dn[i] = xt.dn[--xt.ndn]; break; }
    if (!xt.hold) xt_held_del(n);
    xt.gen++;
}
/* the latch lets go: only the keys still down stay (none down: the arp ends after its sounding step) */
static void xt_unlatch(uint32_t now)
{
    int i, j = 0, was = xt.nheld;
    for (i = 0; i < xt.nheld; i++)
        if (xt_dn_has(xt.held[i])) { xt.held[j] = xt.held[i]; xt.hvel[j] = xt.hvel[i]; j++; }
    xt.nheld = (uint8_t)j;
    xt_arp_held_changed(now, was);
    xt.gen++;
}
int xtra_hold(void) { return xt.hold; }
int xtra_held_n(void) { return xt.nheld; }
int xtra_down_n(void) { return xt.ndn; }
/* REC on the ARPEGGIO window: HOLD on / off (off lets the latched notes go) */
void xtra_hold_toggle(uint32_t now)
{
    xt.hold = (uint8_t)!xt.hold;
    if (!xt.hold) xt_unlatch(now);
    xt.gen++;
}

/* ---- WANDER: a first-order Markov walk over the note list. The state is the last move (repeat, step up, step
 * down, leap up, leap down); the next move is drawn from a small table of tendencies (after a leap it usually
 * steps back: the gap is filled), scaled by LEAP (K1) and REPEAT (K2); REST (K3) leaves a gap, HOME (K4) goes
 * back to the lowest note. FROM SEQ: the notes are the sequencer's steps (transposed so its lowest note is the
 * lowest held key, if one is held) and the table also counts the moves the sequence itself makes. LOCK (SEL on
 * the WANDER window) loops the last 16 notes played. Our own design. */
static const uint8_t XT_WBASE[5][5] = {          /* rows: the last move; columns: repeat, step up, down, leap up, down */
    { 2, 4, 4, 2, 2 }, { 2, 5, 3, 2, 2 }, { 2, 3, 5, 2, 2 }, { 1, 2, 6, 1, 2 }, { 1, 6, 2, 2, 1 } };
static int xt_wmove(int d) { return d == 0 ? 0 : d == 1 ? 1 : d == -1 ? 2 : d > 1 ? 3 : 4; }
/* the sorted, de-duplicated notes the walk uses (octaves added), and the sequencer's own move counts */
static int xt_wlist(uint8_t *s, uint8_t cnt[5][5])
{
    int n = 0, i, j, k, oct = xt.k[XW_ARP][2], fs = xt.k[XW_ARP][0] == 6;
    uint8_t q;
    if (fs) {
        int lo = 127, hl = 127, steps = xt.k[XW_SEQ][1], prev = -1, pm = 0, nr = 0, tr;
        uint8_t r[XT_SEQ_N];                     /* Seq64: ranks (< 48) of the steps' notes */
        for (i = 0; i < steps; i++) {
            q = xt.seq[i];
            if (q == 255u) continue;
            for (j = 0; j < n && s[j] != q; j++) { }
            if (j == n && n < 48) s[n++] = q;    /* Seq64: s[] holds 48 (more distinct notes than that: dropped) */
            if (q < lo) lo = q;
        }
        for (i = 0; i < xt.nheld; i++) if (xt.held[i] < hl) hl = xt.held[i];
        tr = (xt.nheld && n) ? hl - lo : 0;
        for (j = 1; j < n; j++) for (k = j; k > 0 && s[k - 1] > s[k]; k--) { q = s[k]; s[k] = s[k - 1]; s[k - 1] = q; }
        if (cnt) {                               /* the sequence's moves, by rank, around its loop */
            memset(cnt, 0, 25);
            for (i = 0; i < steps; i++) if (xt.seq[i] != 255u) {
                for (j = 0; j < n && s[j] != xt.seq[i]; j++) { }
                if (j < n) r[nr++] = (uint8_t)j;   /* (a dropped note is not counted) */
            }
            for (i = 0; i < nr && nr > 1; i++) {
                int m = xt_wmove(r[(i + 1) % nr] - r[i]);
                if (prev >= 0 && cnt[pm][m] < 15u) cnt[pm][m]++;
                pm = m; prev = 1;
            }
        }
        for (j = 0; j < n; j++) { int t = s[j] + tr; s[j] = (uint8_t)(t < 0 ? 0 : t > 127 ? 127 : t); }
    } else {
        for (i = 0; i < xt.nheld; i++) s[n++] = xt.held[i];
        for (j = 1; j < n; j++) for (k = j; k > 0 && s[k - 1] > s[k]; k--) { q = s[k]; s[k] = s[k - 1]; s[k - 1] = q; }
        if (cnt) memset(cnt, 0, 25);
    }
    for (k = 1; k < oct; k++)
        for (j = 0; j < n && n * k + j < 48; j++) { int t = s[j] + 12 * k; s[n * k + j] = (uint8_t)(t > 127 ? 127 : t); }
    return n * oct > 48 ? 48 : n * oct;
}
static void xt_whist(uint8_t n)
{
    xt.w_hist[xt.w_hpos] = n;
    xt.w_hpos = (uint8_t)((xt.w_hpos + 1u) & 15u);
    if (xt.w_hn < 16u) xt.w_hn++;
    xt.gen++;
}
static void xt_wander_step(uint32_t now)
{
    uint8_t s[48], cnt[5][5], v = xt.nheld ? xt.hvel[xt.nheld - 1] : 127;
    int L, idx, m, w[5], tot = 0, i, r, dmax;
    xt.arp_i++;
    if (xt.w_lock && xt.w_hn) {                  /* LOCK: the last notes, as a loop */
        uint8_t n = xt.w_hist[(xt.w_hpos + 16u - xt.w_hn + xt.w_lpos) & 15u];
        xt.w_last = (uint8_t)xt.w_lpos;
        if (++xt.w_lpos >= xt.w_hn) xt.w_lpos = 0;
        xt.gen++;
        xt_arp_fire(now, n == 255u ? -1 : n, v);
        return;
    }
    L = xt_wlist(s, cnt);
    if (L == 0) return;
    if ((int)(xt_rnd() >> 8) < xt.k[XW_WAND][2]) { xt_whist(255u); xt_arp_fire(now, -1, v); return; }   /* REST */
    idx = xt.w_idx < L ? xt.w_idx : L - 1;
    if ((int)(xt_rnd() >> 8) < xt.k[XW_WAND][3]) {   /* HOME: back to the lowest note */
        m = idx == 0 ? 0 : idx == 1 ? 2 : 4;
        idx = 0;
    } else {
        int lp = xt.k[XW_WAND][0], rp = xt.k[XW_WAND][1];
        for (i = 0; i < 5; i++) {
            int f = i == 0 ? rp + 1 : i < 3 ? 128 - lp : lp + 1;
            w[i] = (XT_WBASE[xt.w_prev][i] + 4 * cnt[xt.w_prev][i]) * f;
            tot += w[i];
        }
        r = (int)((xt_rnd() * (uint32_t)tot) >> 16);
        for (m = 0; m < 4 && r >= w[m]; m++) r -= w[m];
        dmax = 2 + ((lp * (L > 3 ? L - 3 : 0)) >> 7);
        switch (m) {
        case 1: idx = idx + 1 < L ? idx + 1 : idx - 1; break;
        case 2: idx = idx > 0 ? idx - 1 : idx + 1; break;
        case 3: idx += 2 + xt_rndn(dmax - 1); break;
        case 4: idx -= 2 + xt_rndn(dmax - 1); break;
        default: break;
        }
        if (idx >= L) idx = 2 * (L - 1) - idx;   /* reflected at the ends */
        if (idx < 0) idx = -idx;
        if (idx >= L) idx = L - 1;
        if (idx < 0) idx = 0;
        m = xt_wmove(idx - (xt.w_idx < L ? xt.w_idx : L - 1));
    }
    xt.w_idx = (uint8_t)idx;
    xt.w_prev = (uint8_t)m;
    xt_whist(s[idx]);
    xt_arp_fire(now, s[idx], v);
}
int xtra_wander_hist(int i)  { return (i < 0 || i > 15 || i >= xt.w_hn) ? -1 : xt.w_hist[(xt.w_hpos + 16u - xt.w_hn + (uint32_t)i) & 15u]; }
int xtra_wander_locked(void) { return xt.w_lock; }
int xtra_wander_pos(void)    { return xt.w_lock ? xt.w_last : (xt.w_hn ? xt.w_hn - 1 : -1); }
int xtra_wander_notes(void)  { uint8_t s[48]; return xt.k[XW_ARP][0] >= 5 ? xt_wlist(s, 0) / xt.k[XW_ARP][2] : 0; }
/* the arpeggiator runs: keys held, or FROM SEQ while the transport runs */
static int xt_arp_live(void)
{
    if (!xt.on[XW_ARP]) return 0;
    if (xt.nheld) return 1;
    if (xt.k[XW_ARP][0] == 6 && xt.run) { uint8_t s[48]; return xt_wlist(s, 0) > 0; }
    return 0;
}

/* ---- the sequencer's record */
static void xt_rec_write(uint8_t n)
{
    xt.seq[xt.rec_cur] = n;
    xt.rec_cur = (uint8_t)((xt.rec_cur + 1u) % (uint32_t)xt.k[XW_SEQ][1]);
    xt.gen++;
}
/* ---- Drums12-Transpose (21 Nov 2026): key transpose. With SEQ on, the transport running and REC off, a key (keybed,
 * or a MIDI note on the receive channel) sets the offset = key - the pattern's lowest note (over STEPS); the last
 * key pressed wins and the offset stays after the release. It is added when a step plays (the stored steps never
 * change) and the sounding step moves at once. Those keys do not play the voice. Clipped to 0..126 (the keybed's
 * note-127 rule). Back to 0 on panic, CC 120 / 123, STOP, SEQ off, ARP on. */
static int xt_tr_live(void) { return xt.on[XW_SEQ] && xt.run && !xt.rec; }
static uint8_t xt_seq_play(uint8_t n)
{
    int t;
    if (n == 255u || !xt.tr || xt.rec) return n;
    t = n + xt.tr;
    return (uint8_t)(t < 0 ? 0 : t > 126 ? 126 : t);
}
int xtra_transpose(void) { return xt.tr; }
static void xt_tr_set(int key)
{
    int i, lo = 255;
    for (i = 0; i < xt.k[XW_SEQ][1]; i++) if (xt.seq[i] != 255u && xt.seq[i] < lo) lo = xt.seq[i];
    xt.tr = (int16_t)(lo == 255 ? 0 : key - lo);
    if (xt.seq_note != 255u && xt.seq[xt.seq_pos] != 255u) {   /* the sounding step moves now (legato, gate kept) */
        uint8_t nn = xt_seq_play(xt.seq[xt.seq_pos]);
        if (nn != xt.seq_note) xt_on(&xt.seq_note, nn, 127);
    }
    xt.gen++;
}
static void xt_tr_reset(void)
{
    if (xt.tr) xt.gen++;
    xt.tr = 0;
}
static void xt_seq_fire(uint32_t st, uint32_t now)
{
    uint8_t n = xt_seq_play(xt.seq[st]);
    int gate = xt.k[XW_SEQ][3];
    uint32_t rc = XT_RATE_CLK[xt.k[XW_SEQ][2]];
    xt.seq_pos = (uint8_t)st;
    if (n != 255u) {
        if (gate >= 20 && xt.seq_note != 255u) xt_on(&xt.seq_note, n, 127);
        else { xt_off(&xt.seq_note); xt_on(&xt.seq_note, n, 127); }
        xt.seq_off_ms = now + xt_gate_ms((int)rc, gate);
        xt.seq_off_pend = gate < 20;
    } else {
        xt_off(&xt.seq_note);
        xt.seq_off_pend = 0;
    }
    xt.gen++;
}

/* ---- DRUM DRIFT, GROOVE, the kits (Wander-Drift; Drift-Full 17 Nov 2026). One shared clock: bars count from PLAY
 * (16 steps of 1/16).
 * Drift-Full: six lane groups (kick; snare + ghost; clap; closed hat + ride; open hat + crash; percussion: toms,
 * rim, cowbell, clave, congas) for MUTE / SOLO and the Drift rules; eight kit flavours built from the 808 and 909
 * instruments; the mutations OVERWRITE the playing copy (wp) and stay in it; the factory patterns stay in the
 * flash, untouched, and come back whenever a pattern is loaded (PATTERN turned, a Drift change, REC on DRUM DRIFT
 * = RESTORE, the RESTORE key in KEYS-FX). */
enum { G_KICK = 0, G_SNARE, G_CLAP, G_CH, G_OH, G_PERC, G_N };
static const uint8_t XT_GROUP[17] = { G_KICK, G_SNARE, G_SNARE, G_CLAP, G_CH, G_OH, G_PERC, G_PERC, G_PERC, G_PERC,
                                      G_PERC, G_PERC, G_PERC, G_PERC, G_PERC, G_CH, G_OH };
enum { KF_808 = 0, KF_909, KF_HOUSE, KF_TECHNO, KF_TRAP, KF_PHONK, KF_BREAK, KF_HARD, KF_N };
/* lanes: KICK SNARE GHOST CLAP CH OH LT MT HT RIM COWB CLAVE LC MC HC RIDE CRASH */
static const uint8_t XT_KF_LANE[KF_N][17] = {
    { DI_BD8, DI_SD8, DI_SD8, DI_CP8, DI_CH8, DI_OH8, DI_LT8, DI_MT8, DI_HT8, DI_RS8, DI_CB8, DI_CL8, DI_LC8, DI_MC8, DI_HC8, DI_OH8, DI_OH8 },   /* 808 */
    { DI_BD9, DI_SD9, DI_SD9, DI_CP9, DI_CH9, DI_OH9, DI_LT9, DI_MT9, DI_HT9, DI_RS9, DI_RS9, DI_RS9, DI_LT9, DI_MT9, DI_HT9, DI_RD9, DI_CR9 },   /* 909 */
    { DI_BD9, DI_SD9, DI_SD9, DI_CP9, DI_CH9, DI_OH9, DI_LT9, DI_MT9, DI_HT9, DI_RS9, DI_CB8, DI_CL8, DI_LC8, DI_MC8, DI_HC8, DI_RD9, DI_CR9 },   /* HOUSE: 909 + 808 cowbell, clave, congas */
    { DI_BD9, DI_SD9, DI_SD9, DI_CP9, DI_CH9, DI_OH9, DI_LT9, DI_MT9, DI_HT9, DI_RS9, DI_RS9, DI_CL8, DI_LT9, DI_MT9, DI_HT9, DI_RD9, DI_CR9 },   /* TECHNO: 909, a lower kick */
    { DI_BDL8, DI_SD8, DI_SD8, DI_CP8, DI_CH8, DI_OH8, DI_LT8, DI_MT8, DI_HT8, DI_RS8, DI_CB8, DI_CL8, DI_LC8, DI_MC8, DI_HC8, DI_OH8, DI_CR9 }, /* TRAP: 808 long sub kick + cowbell */
    { DI_BDL8, DI_CP8, DI_SD8, DI_CP8, DI_CH8, DI_OH8, DI_LT8, DI_MT8, DI_HT8, DI_RS8, DI_CB8, DI_CL8, DI_LC8, DI_MC8, DI_HC8, DI_OH8, DI_CR9 }, /* PHONK: 808 long kick, clap snare, cowbell */
    { DI_BD9, DI_SD9, DI_SD9, DI_CP9, DI_CH8, DI_OH8, DI_LT9, DI_MT9, DI_HT9, DI_RS9, DI_CB8, DI_CL8, DI_LC8, DI_MC8, DI_HC8, DI_OH8, DI_CR9 },   /* BREAK: crushed */
    { DI_BD9, DI_SD9, DI_SD9, DI_CP9, DI_CH9, DI_OH9, DI_LT9, DI_MT9, DI_HT9, DI_RS9, DI_RS9, DI_RS9, DI_LT9, DI_MT9, DI_HT9, DI_RD9, DI_CR9 },   /* HARD: driven + crushed */
};
static const uint16_t XT_KF_KPM[KF_N] = { 4096, 4096, 4096, 3650, 4096, 3870, 4300, 3870 };   /* the kick's pitch, Q12 */
static const uint8_t XT_KF_CRUSH[KF_N] = { 0, 0, 0, 0, 0, 0, 1, 2 };
static const uint16_t XT_KF_DRV[KF_N] = { 0, 0, 0, 0, 0, 1024, 0, 6144 };                 /* added to DRIVE, Q12 */
/* the packs' tempos (applied when PATTERN moves into a pack; TEMPO still turns after) */
static const uint8_t XT_PACK_BPM[XT_PACKS] = { 126, 140, 174, 130 };   /* HOUSE DUBSTEP DNB TECHNO */
/* per genre (0 = 1-8 / generic, 1 house, 2 dubstep, 3 DnB, 4 techno): the most hits per bar the Drift may
 * leave in the snare (with ghosts), closed hat, open hat and percussion groups; mutations per Drift moment; hat
 * rolls allowed. Kick and clap are never touched by a mutation. (Drums12: trap and phonk rows removed) */
static const uint8_t XT_CAP[1 + XT_PACKS][4] = { {6, 16, 4, 6}, {4, 16, 4, 6}, {3, 12, 2, 4}, {6, 16, 3, 4},
                                                 {4, 16, 4, 4} };
static const uint8_t XT_MUTN[1 + XT_PACKS] = { 2, 2, 1, 2, 2 };
static const uint8_t XT_ROLLOK[1 + XT_PACKS] = { 0, 0, 0, 1, 0 };
static const uint8_t XT_LANE_VEL[17] = { 127, 120, 40, 120, 100, 105, 115, 115, 115, 110, 110, 110, 110, 110, 110, 95,
                                         120 };
static const uint16_t XT_SEMI_Q12[16] = { 4096, 4340, 4598, 4871, 5161, 5468, 5793, 6137, 6502, 6889, 7298, 7732,
                                          8192, 8679, 9195, 9742 };
static int xt_pack(int p) { return p > XT_PAT_USER ? (p - 9) / XT_PACK : -1; }
static int xt_genre(void)
{
    int p = xt.wp_src;
    if (xt.wp_gen) return xt.wp_style;   /* PLAY GEN: KIT AUTO / trim caps follow the generated style */
    return p == XT_PAT_USER ? xt.user_style : p > XT_PAT_USER ? 1 + (p - 9) / XT_PACK : 0;
}
static int xt_flavour(void)
{
    static const uint8_t AUTO[1 + XT_PACKS] = { 0, KF_HOUSE, KF_HARD, KF_BREAK, KF_TECHNO };
    int k = xt.k[XW_GROOVE][3], g;
    if (k > 0) return k - 1;
    g = xt_genre();
    if (g) return AUTO[g];
    return xt.wp.kit ? KF_909 : KF_808;
}
static int xt_kit(void) { int f = xt_flavour(); return XT_KF_LANE[f][L_SNARE] >= DI_BD9; }   /* 0 808, 1 909 snare */
int xtra_kit(void) { return xt_kit(); }
int xtra_flavour(void) { return xt_flavour(); }
int xtra_genre(void) { return xt_genre(); }
static int xt_locked(void) { return xt.k[XW_GEN][3] != 0; }
int xtra_locked(void) { return xt_locked(); }
/* MUTE / SOLO: the lanes that sound (any SOLO: only the soloed groups) */
static uint32_t xt_audible(void)
{
    uint32_t solo = 0, mute = 0, m = 0, l;
    int g;
    for (g = 0; g < G_N; g++) { if (xt.lane_st[g] == 2u) solo |= 1u << g; if (xt.lane_st[g] == 1u) mute |= 1u << g; }
    for (l = 0; l < 17u; l++) {
        uint32_t gb = 1u << XT_GROUP[l];
        if (solo ? (solo & gb) != 0u : !(mute & gb)) m |= 1u << l;
    }
    return m;
}
int xtra_lane_state(int g) { return (g < 0 || g >= G_N) ? 0 : xt.lane_st[g]; }
void xtra_set_lane_state(int g, int s)
{
    if (g < 0 || g >= G_N || s < 0 || s > 2) return;
    xt.lane_st[g] = (uint8_t)s;
    if (xt.k[XW_PERF][2] == g) xt.k[XW_PERF][3] = (int16_t)s;
    xt.gen++;
}
static void xt_hit(uint32_t inst, uint32_t vel, uint32_t pm)
{
    uint32_t w = xf_par.trw;
    xf_par.trq[w & (XF_TRQ - 1u)] = inst | vel << 8 | pm << 16;
    xf_par.trw = w + 1u;
}
/* the lanes that sound at step st: the playing copy, then the fill / build / break and HAT BUSY */
static uint32_t xt_step_lanes(uint32_t st, uint8_t *roll)
{
    uint32_t b = 1u << st, m = 0, l;
    int busy = xt.k[XW_GROOVE][1];
    xt.fill_now = 0;
    for (l = 0; l < 17u; l++) if (xt.wp.l[l] & b) m |= 1u << l;
    *roll = (uint8_t)((xt.wp.roll >> (2u * st)) & 3u);
    if (xt.brk == 1) { *roll = 0; return 0; }    /* BREAK: the drums drop out */
    if (xt.brk >= 2) {                           /* BUILD 1..4: no kick, the hats come in, the snare roll speeds up */
        static const uint16_t BS[4] = { 0x0000, 0x0000, 0x5555, 0xFFFF }, BG[4] = { 0x1111, 0x5555, 0xAAAA, 0x0000 },
                              BH[4] = { 0x4444, 0x5555, 0xFFFF, 0xFFFF };
        int k = xt.brk - 2;
        *roll = 0;
        return (BS[k] & b ? 1u << L_SNARE : 0) | (BG[k] & b ? 1u << L_GHOST : 0) | (BH[k] & b ? 1u << L_CH : 0);
    }
    if (xt.fill_hold || (xt.fill_from && xt.fill_kind == 1 && st >= 8u)) {   /* ROLL: snare + closed hat every 16th */
        m &= (1u << L_KICK) | (1u << L_CLAP);
        m |= (1u << L_SNARE) | (1u << L_CH);
        *roll = (uint8_t)(st >= 14u ? 1u : 0u);   /* the last two: 32nds */
        xt.fill_now = 1;
        return m;
    }
    if (xt.fill_from && st >= 8u) {              /* TOMS: snare and toms roll in (the Wander-Drift fill) */
        uint32_t sn = 0x1500u, gh = 0x0A00u, ch = 0x1F00u;
        m &= 1u << L_KICK;
        if (sn & b) m |= 1u << L_SNARE;
        if (gh & b) m |= 1u << L_GHOST;
        if (ch & b) m |= 1u << L_CH;
        if (st == 13u) m |= 1u << L_HT;
        if (st == 14u) m |= 1u << L_MT;
        if (st == 15u) m |= 1u << L_LT;
        *roll = 0;
        return m;
    }
    if (st == 0u && xt.crash_next) m |= 1u << L_CRASH;
    if (busy != 2) {                             /* HAT BUSY: thin out or fill in the closed hat, the pattern stays */
        uint32_t ch = 1u << L_CH, oh = (m >> L_OH) & 1u;
        if (busy == 0) m &= ~ch;
        else if (busy == 1) { if (st & 1u) m &= ~ch; }
        else if (!oh && (busy == 4 || !(st & 1u))) m |= ch;
        if (busy < 2) *roll = 0;
    }
    return m;
}
static void xt_drum_fire(uint32_t m, uint32_t st)
{
    int f = xt_flavour();
    const uint8_t *map = XT_KF_LANE[f];
    uint32_t l;
    m &= xt_audible();
    if (m & 1u) xf_par.drm_trig[0]++;            /* (the tests' counts: kick, snare, closed hat lanes) */
    if (m & 2u) xf_par.drm_trig[1]++;
    if (m & (1u << L_CH)) xf_par.drm_trig[2]++;
    for (l = 0; l < 17u; l++)
        if ((m >> l) & 1u) {
#if POLI_PADREC
            if (xtra_padrec_skip_lane((int)l, (int)st)) continue;   /* (a take: the hit just played live) */
#endif
            uint32_t in = map[l], pm = 4096u, vel = XT_LANE_VEL[l];
            if (l == L_KICK) { if (xt.wp.lng && f == KF_808) in = DI_BDL8; pm = XT_KF_KPM[f]; }
            if (l == L_COWB) pm = XT_SEMI_Q12[(xt.wp.bell[st >> 3] >> (4u * (st & 7u))) & 15u];
            if (l == L_SNARE && xt.fill_now) vel = xt.fill_hold ? 60u + st * 4u : 40u + (st - 8u) * 12u;   /* rising */
            xt_hit(in, vel, pm);
        }
}
static void xt_load_wp(void)
{
    xt.wp = *xt_pat(xt.k[XW_DRUM][0]);
    xt.wp_src = (uint8_t)xt.k[XW_DRUM][0];
    xt.reload = 0;
    xt.wp_gen = 0;                       /* PLAY GEN: a real load replaces the temporary beat */
}
/* the patterns RANDOM / NEXT choose from: POOL 0..6 = 1-2 .. 1-8, 7 = 1-9 (with USER), 8 STYLE (the current
 * pattern's own group: 1-8, or its genre pack's 12), 9 ALL */
static void xt_pool(int *lo, int *hi)
{
    int p = xt.k[XW_DRIFT][3], c = xt.k[XW_DRUM][0];
    if (p <= 6) { *lo = 0; *hi = p + 1; }
    else if (p == 7) { *lo = 0; *hi = XT_PAT_USER; }
    else if (p == 9) { *lo = 0; *hi = XT_PAT_N - 1; }
    else if (c < XT_PAT_USER) { *lo = 0; *hi = 7; }
    else if (c == XT_PAT_USER) { *lo = 0; *hi = XT_PAT_USER; }
    else { *lo = 9 + (c - 9) / XT_PACK * XT_PACK; *hi = *lo + XT_PACK - 1; }
}
static void xt_change(void)
{
    int lo, hi, c = xt.k[XW_DRUM][0], n;
    xt_pool(&lo, &hi);
    if (xt.k[XW_DRIFT][0] == 1) n = (c >= lo && c < hi) ? c + 1 : lo;
    else if (hi == lo) n = lo;
    else if (c >= lo && c <= hi) { n = lo + xt_drndn(hi - lo); if (n >= c) n++; }
    else n = lo + xt_drndn(hi - lo + 1);
    xt.k[XW_DRUM][0] = (int16_t)n;
    xt.changes++;
}
/* ---- Drums12 (21 Nov 2026): an OLD-layout PATTERN index (0..28 of the Nov 17 build: 1-8, USER, HOUSE1-4,
 * DUBST1-4, DNB1-4, TRAP1-4, PHONK1-4) on the new knob. HOUSE n, DUBST n, DNB n keep their pattern; TRAP n and
 * PHONK n land on DUBST n (the same 140 BPM, the same half-time 808 kernel: snare / clap on beat 3). Only for an
 * old index that is read in (a settings record marked with the old layout), never for a live knob turn. */
int xt_pat_from_old(int p)
{
    if (p < 0 || p > 28) return 0;
    if (p <= 12) return p;                       /* 1-8, USER, HOUSE1-4: same knob */
    if (p <= 16) return 9 + XT_PACK + (p - 13);  /* DUBST n */
    if (p <= 20) return 9 + 2 * XT_PACK + (p - 17);   /* DNB n */
    return 9 + XT_PACK + ((p - 21) & 3);         /* TRAP n, PHONK n -> DUBST n */
}
/* Drums12 persistence (Hans, 21 Nov 2026): the PATTERN knob is kept in the settings record (poli_preset.c, rsv[1]
 * = PATTERN + 1, rsv[2] = the knob layout). XT_PAT_LAYOUT 2 = this 12-per-pack knob; 1 = the Nov 17 0..28 knob. */
#define XT_PAT_LAYOUT 2
int xtra_pattern(void) { return xt.k[XW_DRUM][0]; }
/* at boot: v = the stored PATTERN + 1 (0 = none), lay = its layout. Anything unknown: pattern 1 */
void xtra_pattern_restore(int v, int lay)
{
    int p = v - 1, np;
    if (v <= 0) p = 0;
    else if (lay == 1) p = xt_pat_from_old(p);
    else if (lay != XT_PAT_LAYOUT || p >= XT_PAT_N) p = 0;
    xt.k[XW_DRUM][0] = (int16_t)p;
    np = xt_pack(p);
    if (np >= 0) xt.k[XW_SEQ][0] = XT_PACK_BPM[np];   /* a pack brings its tempo, as when the knob enters it */
    xt_load_wp();
    xt.gen++;
}

/* ---- Drift-Full: the mutation rules (our own; applied to the playing copy, which keeps them) */
static int xt_gcount(int g)
{
    int n = 0;
    uint32_t l;
    for (l = 0; l < 17u; l++) if (XT_GROUP[l] == (uint32_t)g && l != L_RIDE && l != L_CRASH) n += xt_pop(xt.wp.l[l]);
    return n;
}
static int xt_cap(int g) { static const uint8_t CI[G_N] = { 0, 0, 0, 1, 2, 3 }; return XT_CAP[xt_genre()][CI[g]]; }
static uint32_t xt_bell(uint32_t st) { return (xt.wp.bell[st >> 3] >> (4u * (st & 7u))) & 15u; }
static void xt_set_bell(uint32_t st, uint32_t v)
{
    uint32_t sh = 4u * (st & 7u);
    xt.wp.bell[st >> 3] = (xt.wp.bell[st >> 3] & ~(15u << sh)) | (v & 15u) << sh;
}
static int xt_pick(uint32_t m) { return m ? xt_nth(m, xt_drndn(xt_pop(m))) : -1; }
#define XT_PROT 0x1110u                          /* snare hits on beats 2, 3, 4 (steps 5, 9, 13): never moved */
static int xt_mv(int lane, int st, int to)
{
    if (st < 0 || to < 0 || to > 15 || ((xt.wp.l[lane] >> to) & 1u)) return 0;
    xt.wp.l[lane] = (uint16_t)((xt.wp.l[lane] & ~(1u << st)) | 1u << to);
    if (lane == L_CH) xt.wp.roll &= ~(3u << (2 * st));
    if (lane == L_COWB) { xt_set_bell((uint32_t)to, xt_bell((uint32_t)st)); xt_set_bell((uint32_t)st, 0); }
    return 1;
}
static int xt_mut_once(int morph)
{
    static const uint8_t PICK[10] = { G_CH, G_CH, G_CH, G_CH, G_SNARE, G_SNARE, G_SNARE, G_OH, G_PERC, G_PERC };
    int gen = xt_genre(), g = PICK[xt_drndn(10)], op = morph ? xt_drndn(3) : 0, st, to, d = (xt_drnd() & 1u) ? 1 : -1;
    uint32_t occ;
    switch (g) {
    case G_SNARE: {
        uint32_t sn = xt.wp.l[L_SNARE], gh = xt.wp.l[L_GHOST];
        occ = sn | gh | xt.wp.l[L_CLAP];
        if (op == 1 && xt_gcount(G_SNARE) < xt_cap(G_SNARE)) {
            st = xt_pick(0xFFFFu & ~occ & ~XT_PROT);
            if (st < 0) return 0;
            xt.wp.l[L_GHOST] = (uint16_t)(gh | 1u << st);
            return 1;
        }
        if (op == 2 && gh) { st = xt_pick(gh); xt.wp.l[L_GHOST] = (uint16_t)(gh & ~(1u << st)); return 1; }
        if ((xt_drnd() & 1u) && gh) { st = xt_pick(gh); to = st + d; return !((occ >> (to & 15)) & 1u) && xt_mv(L_GHOST, st, to); }
        st = xt_pick(sn & ~XT_PROT);
        if (st < 0) { st = xt_pick(gh); to = st + d; return st >= 0 && !((occ >> (to & 15)) & 1u) && xt_mv(L_GHOST, st, to); }
        to = st + d;
        return to >= 0 && to < 16 && !((occ >> to) & 1u) && xt_mv(L_SNARE, st, to);
    }
    case G_CH: {
        uint32_t ch = xt.wp.l[L_CH], oh = xt.wp.l[L_OH];
        if (op == 1) {
            if (XT_ROLLOK[gen] && ch && (xt_drnd() % 3u) == 0u) {   /* a roll on a hat that is there */
                st = xt_pick(ch);
                xt.wp.roll = (xt.wp.roll & ~(3u << (2 * st))) | (1u + (xt_drnd() & 1u)) << (2 * st);
                return 1;
            }
            if (xt_gcount(G_CH) >= xt_cap(G_CH)) return 0;
            st = xt_pick(0xFFFFu & ~ch & ~oh);
            if (st < 0) return 0;
            xt.wp.l[L_CH] = (uint16_t)(ch | 1u << st);
            return 1;
        }
        if (op == 2) {
            if (xt_pop(ch) <= 2) return 0;
            st = xt_pick(ch);
            xt.wp.l[L_CH] = (uint16_t)(ch & ~(1u << st)); xt.wp.roll &= ~(3u << (2 * st));
            return 1;
        }
        st = xt_pick(ch);
        to = st + d;
        return st >= 0 && to >= 0 && to < 16 && !((oh >> to) & 1u) && xt_mv(L_CH, st, to);
    }
    case G_OH: {
        uint32_t oh = xt.wp.l[L_OH];
        if (op == 1) {
            if (xt_gcount(G_OH) >= xt_cap(G_OH)) return 0;
            st = xt_pick(0x4444u & ~oh);
            if (st < 0) return 0;
            xt.wp.l[L_OH] = (uint16_t)(oh | 1u << st);
            xt.wp.l[L_CH] &= (uint16_t)~(1u << st);   /* (a closed hat there would cut it at once) */
            return 1;
        }
        if (op == 2) { st = xt_pick(oh); if (st < 0) return 0; xt.wp.l[L_OH] = (uint16_t)(oh & ~(1u << st)); return 1; }
        st = xt_pick(oh);                        /* an open hat moves to another off-beat 8th */
        to = xt_pick(0x4444u & ~oh);
        if (st < 0 || to < 0) return 0;
        xt.wp.l[L_CH] &= (uint16_t)~(1u << to);
        return xt_mv(L_OH, st, to);
    }
    default: {                                   /* percussion: a lane that plays (else the genre's own) */
        uint32_t l, have = 0;
        int lane;
        for (l = L_LT; l <= L_HC; l++) if (xt.wp.l[l]) have |= 1u << l;
        lane = have ? xt_pick(have) : gen == 1 ? L_LC : L_RIM;
        if (op == 1 || !have) {
            if (xt_gcount(G_PERC) >= xt_cap(G_PERC)) return 0;
            st = xt_pick(0xFFFFu & ~(uint32_t)xt.wp.l[lane] & ~(uint32_t)xt.wp.l[L_KICK]);
            if (st < 0) return 0;
            xt.wp.l[lane] = (uint16_t)(xt.wp.l[lane] | 1u << st);
            if (lane == L_COWB) xt_set_bell((uint32_t)st, (uint32_t)(xt_drnd() % 3u) * 5u / 2u);   /* 0, 2, 5 */
            return 1;
        }
        st = xt_pick(xt.wp.l[lane]);
        if (op == 2) {
            xt.wp.l[lane] = (uint16_t)(xt.wp.l[lane] & ~(1u << st));
            if (lane == L_COWB) xt_set_bell((uint32_t)st, 0);
            return 1;
        }
        to = st + d * (1 + (int)(xt_drnd() & 1u));
        return to >= 0 && to < 16 && xt_mv(lane, st, to);
    }
    }
}
/* over a genre's caps (after a surprise): take hits away, ghosts / extra hats / percussion first */
static void xt_trim(void)
{
    int g, guard;
    for (g = G_SNARE; g < G_N; g++) {
        if (g == G_CLAP) continue;
        for (guard = 0; guard < 32 && xt_gcount(g) > xt_cap(g); guard++) {
            uint32_t l;
            int st;
            if (g == G_SNARE) { l = xt.wp.l[L_GHOST] ? L_GHOST : L_SNARE; st = xt_pick(xt.wp.l[l] & (l == L_SNARE ? ~XT_PROT : 0xFFFFu)); }
            else if (g == G_CH) { l = L_CH; st = xt_pick(xt.wp.l[l]); }
            else if (g == G_OH) { l = L_OH; st = xt_pick(xt.wp.l[l]); }
            else { uint32_t have = 0, k; for (k = L_LT; k <= L_HC; k++) if (xt.wp.l[k]) have |= 1u << k; l = (uint32_t)xt_pick(have); st = xt_pick(xt.wp.l[l]); }
            if (st < 0) break;
            xt.wp.l[l] = (uint16_t)(xt.wp.l[l] & ~(1u << st));
        }
    }
}
/* SURPRISE: a rarer, bigger move, still inside the genre: house / 1-8 an open-hat lift on every off-beat;
 * dubstep a tom pickup into the bar line; DnB a ghost-snare run; techno (Drums12) a closed-hat 16th run on the
 * last beat into the bar line, no rolls. Kick and clap untouched; the caps still hold. */
static void xt_surprise(void)
{
    xt_pat_t *p = &xt.wp;
    switch (xt_genre()) {
    case 2: p->l[L_HT] |= 0x2000; p->l[L_MT] |= 0x4000; p->l[L_LT] |= 0x8000; p->l[L_GHOST] &= 0x1FFF; break;
    case 3: p->l[L_GHOST] = (uint16_t)((p->l[L_GHOST] | 0x4A40u) & ~(uint32_t)p->l[L_SNARE]); break;
    case 4: p->l[L_CH] |= 0xF000; p->l[L_OH] &= 0x0FFF; break;
    default: p->l[L_OH] |= 0x4444; p->l[L_CH] &= (uint16_t)~0x4444u; break;
    }
    xt_trim();
}
/* the Drift's moment (every N bars, or the bar after a chord change's fill): CHANGE (if on), then MUTATE's
 * mutations on the arrived pattern, then, by SURPRISE's chance, a surprise. LOCK: nothing moves. */
static void xt_generate_into_user(int style);
static void xt_moment(void)
{
    int c = xt.k[XW_DRIFT][0], mu = xt.k[XW_DRIFT][2], n, i;
    if (xt_locked()) return;
    if (c == 3) { xt_generate_into_user(-1); xt.k[XW_DRUM][0] = XT_PAT_USER; xt_load_wp(); xt.changes++; xt.crash_next = 1; }
    else if (c) { xt_change(); xt_load_wp(); xt.crash_next = 1; }
    if (mu) { n = XT_MUTN[xt_genre()]; for (i = 0; i < n; i++) if (xt_mut_once(mu == 2)) xt.mutations++; }
    if (xt.k[XW_GEN][2] && xt_drndn(255) < xt.k[XW_GEN][2]) { xt_surprise(); xt.surprises++; }
}
static uint32_t xt_every_n(void) { int e = xt.k[XW_DRIFT][1]; return e == 0 ? 0u : 4u << (e - 1); }   /* 0 = CHORD */
static uint32_t xt_every(void) { uint32_t n = xt_every_n(); return n ? n : 2u; }
/* a chord change (the first key of a new set: keybed, MIDI, the arp's fresh chord): with EVERY = CHORD the next
 * bar is a fill and the Drift moves the bar after */
static void xt_chord(void)
{
    if (xt.on[XW_DRIFT] && xt_every_n() == 0u && xt.run && !xt.chord_cnt && !xt_locked()) { xt.chord_cnt = 2; xt.gen++; }
}
/* the start of each bar (step 1 of 16) */
static void xt_bar_start(uint32_t bar)
{
    int drift = xt.on[XW_DRIFT], was = xt.brk, moment = 0, fill = 0;
    uint32_t N = xt_every_n();
    xt.bar = bar;
    xt.crash_next = 0;
    if (xt.brk >= 2) xt.brk = (uint8_t)(xt.brk >= 5 ? 0 : xt.brk + 1);   /* BUILD 1..4, then the drop */
    if (xt.brk_req == 1 && xt.brk == 0) xt.brk = 1;
    else if (xt.brk_req == 2 && xt.brk == 1) xt.brk = 2;
    else if (xt.brk_req == 3) xt.brk = 0;
    xt.brk_req = 0;
    if (was && !xt.brk) xt.crash_next = 1;      /* the drop lands with a crash */
    if (drift && !xt.brk && !was) {
        if (N == 0u) {
            if (xt.chord_cnt == 2u) { fill = 1; xt.chord_cnt = 1; }
            else if (xt.chord_cnt == 1u) { moment = 1; xt.chord_cnt = 0; }
        } else {
            if (bar > 0u && bar % N == 0u) moment = 1;
            if (bar % N == N - 1u) fill = 1;
        }
    }
    if (moment) xt_moment();
    xt.fill_from = 0; xt.fill_kind = 0;
    if (fill && xt.k[XW_DRIFT][0] && xt.k[XW_GROOVE][2] && !xt_locked()) {
        xt.fill_from = 9; xt.fill_kind = (uint8_t)xt.k[XW_GROOVE][2];
        xt.fills++;
    }
    xt.gen++;
}
/* Drum-Step: the chance of the USER pattern's own hits at step st (the fill / build / HAT BUSY additions and the
 * drop's crash always play). A hit at 75 / 50 / 25 % draws from the drums' own random numbers and stays if the
 * draw (0..3) is below 4 - level; a 100 % hit draws nothing, so a pattern with no chance set plays as before. A
 * closed hat left out takes its roll with it. */
static uint32_t xt_chance(uint32_t m, uint32_t st, uint8_t *roll)
{
    uint32_t l, b = 1u << st, sh = 2u * st;
    if (xt.wp_src != XT_PAT_USER || xt.brk) return m;
    if (xt.wp_gen) return m;             /* PLAY GEN: the temp beat is 100 %; USER chance stays on USER */
    for (l = 0; l < 17u; l++) {
        uint32_t c = (xt.chance[l] >> sh) & 3u;
        if (!c || !((m >> l) & 1u) || !(xt.wp.l[l] & b) || (l == L_CRASH && st == 0u && xt.crash_next)) continue;
        if ((uint32_t)xt_drndn(4) >= 4u - c) {
            m &= ~(1u << l);
            if (l == L_CH) *roll = 0;
        }
    }
    return m;
}
static void xt_drum_step(uint32_t st, uint32_t tick, uint32_t now)
{
    uint8_t roll;
    uint32_t m;
    if (xt.sw_d_pend) { xt.sw_d_pend = 0; if (xt.on[XW_DRUM] && xt.c2 == 3) xt_drum_fire(xt.sw_d_mask, xt.sw_d_st); }
    if (st == 0u) xt_bar_start(tick / 96u);
    if ((st & 3u) == 0u && (xt.reload || xt.wp_src != (uint8_t)xt.k[XW_DRUM][0])) xt_load_wp();   /* on the beat */
    xt.drum_pos = (uint8_t)st;
    if (!(xt.on[XW_DRUM] && xt.c2 == 3)) { xt.roll_cur = 0; return; }
    m = xt_step_lanes(st, &roll);
    m = xt_chance(m, st, &roll);                 /* Drum-Step: USER hits below 100 % (none set: no draw, no change) */
    if (xt_swung_tick(tick) && m) {              /* swing: this 16th waits; its roll plays as one hit */
        xt.sw_d_mask = m; xt.sw_d_st = (uint8_t)st; xt.sw_d_due = now + xt_swing_ms(); xt.sw_d_pend = 1;
        xt.roll_cur = 0;
        return;
    }
    xt.roll_cur = roll;
    xt_drum_fire(m, st);
}
/* held-back (swung) steps, due now (or flushed before the next clock) */
static void xt_swing_due(uint32_t now, int flush)
{
    if (xt.sw_d_pend && (flush || (int32_t)(now - xt.sw_d_due) >= 0)) { xt.sw_d_pend = 0; if (xt.run && xt.on[XW_DRUM] && xt.c2 == 3) xt_drum_fire(xt.sw_d_mask, xt.sw_d_st); }
    if (xt.sw_s_pend && (flush || (int32_t)(now - xt.sw_s_due) >= 0)) { xt.sw_s_pend = 0; if (xt.on[XW_SEQ]) xt_seq_fire(xt.sw_s_st, now); }
    if (xt.sw_a_pend && (flush || (int32_t)(now - xt.sw_a_due) >= 0)) { xt.sw_a_pend = 0; if (xt_arp_live()) xt_arp_step(now); }
}
int xtra_bar(void) { return (int)xt.bar; }
int xtra_bar_in(void) { return (int)(xt.bar % xt_every()); }
int xtra_every(void) { return (int)xt_every_n(); }
int xtra_fill(void) { return xt.fill_from != 0 || xt.fill_hold; }
int xtra_brk(void) { return xt.brk; }
int xtra_brk_req(void) { return xt.brk_req; }
int xtra_chord_cnt(void) { return xt.chord_cnt; }
uint32_t xtra_changes(void) { return xt.changes; }
uint32_t xtra_fills(void) { return xt.fills; }
uint32_t xtra_mutations(void) { return xt.mutations; }
uint32_t xtra_surprises(void) { return xt.surprises; }
uint32_t xtra_generated(void) { return xt.gens; }
int xtra_wp_lane(int l) { return (l < 0 || l > 16) ? 0 : xt.wp.l[l]; }
uint32_t xtra_wp_roll(void) { return xt.wp.roll; }
int xtra_user_lane(int l) { return (l < 0 || l > 16) ? 0 : xt.user.l[l]; }
int xtra_wp_src(void) { return xt.wp_src; }
int xtra_pend_pat(void) { return (xt.reload || xt.wp_src != (uint8_t)xt.k[XW_DRUM][0]) ? xt.k[XW_DRUM][0] : -1; }
/* RESTORE: the factory (or USER) pattern back into the playing copy, from the next beat (at once when stopped) */
void xtra_restore(void)
{
    if (xt.run) xt.reload = 1; else xt_load_wp();
    xt.gen++;
}

/* GENERATE (SEL on GENERATE or GROOVE): a beat from our own genre rules into the 9th slot (USER; 1-8 and the packs
 * never change), heard from the next beat (at once when stopped). STYLE AUTO = the playing pattern's genre (house
 * for 1-8). DENSITY sets the hats and the extras; the genre's caps still hold. LOCK refuses (returns 0). */
static int xt_generate_into(xt_pat_t *dst, int style)
{
    static const uint16_t HAT[5] = { 0x1111, 0x4444, 0x5555, 0xDDDD, 0xFFFF };
    int d = xt.k[XW_GEN][1], i, into_user = (dst == &xt.user);
    if (style < 0) style = xt.k[XW_GEN][0];
    if (style == 0) { style = xt_genre(); if (style == 0) style = 1; }
    memset(dst, 0, sizeof *dst);
    if (into_user) memset(xt.chance, 0, sizeof xt.chance);      /* Drum-Step: a new USER beat, every hit 100 % */
    switch (style) {
    case 2:                                      /* dubstep: half time, snare + clap on beat 3 */
        dst->l[L_KICK] = (uint16_t)(0x0001u | 1u << (6 + 4 * xt_drndn(2)) | ((xt_drnd() & 1u) ? 0x0800u : 0u));
        dst->l[L_SNARE] = 0x0100; dst->l[L_CLAP] = 0x0100;
        dst->l[L_CH] = (uint16_t)(d >= 3 ? 0x5555u : d >= 1 ? 0x1111u | 0x4040u : 0x1111u);
        if (xt_drnd() & 1u) dst->l[L_GHOST] = 0x4000;
        if (d >= 2 && (xt_drnd() & 1u)) dst->l[L_OH] = 0x0040;
        if (d >= 3 && (xt_drnd() & 1u)) { dst->l[L_HT] = 0x2000; dst->l[L_LT] = 0x8000; }
        dst->kit = 1;
        break;
    case 3:                                      /* DnB: two-step kick, snare 2 and 4 */
        dst->l[L_KICK] = (uint16_t)(0x0401u | ((xt_drnd() & 1u) ? 1u << (xt_drnd() & 1u ? 7 : 9) : 0u));
        dst->l[L_SNARE] = 0x1010;
        dst->l[L_CH] = HAT[d < 2 ? 2 : d];
        for (i = 0; i < 1 + d / 2; i++) { static const uint8_t GH[6] = { 2, 7, 9, 11, 14, 15 }; dst->l[L_GHOST] |= (uint16_t)(1u << GH[xt_drndn(6)]); }
        if (xt_drnd() & 1u) { dst->l[L_RIDE] = 0x5555; dst->l[L_CH] = 0; }
        dst->kit = 1;
        break;
    case 4:                                      /* techno (Drums12): four on the floor, clap 2 and 4, off-beat open hats */
        dst->l[L_KICK] = 0x1111; dst->l[L_CLAP] = 0x1010;
        dst->l[L_OH] = 0x4444;
        dst->l[L_CH] = (uint16_t)(HAT[d] & ~0x4444u);
        if (d >= 2) { if (xt_drnd() & 1u) dst->l[L_RIDE] = 0x5555; else dst->l[L_RIM] = (uint16_t)(1u << (3 + 4 * xt_drndn(4))); }
        dst->kit = 1;
        break;
    default:                                     /* house: four on the floor, clap 2 and 4, open hats on the off-beats */
        dst->l[L_KICK] = 0x1111; dst->l[L_CLAP] = 0x1010;
        dst->l[L_OH] = 0x4444;
        dst->l[L_CH] = (uint16_t)(HAT[d] & ~0x4444u);
        if (xt_drnd() & 1u) { static const uint8_t GH[4] = { 3, 7, 11, 15 }; dst->l[L_GHOST] = (uint16_t)(1u << GH[xt_drndn(4)]); }
        if (d >= 2) for (i = 0; i < d; i++) { static const uint8_t PL[4] = { L_LC, L_MC, L_HC, L_RIM }; dst->l[PL[xt_drndn(4)]] |= (uint16_t)(1u << xt_drndn(16)); }
        if ((xt_drnd() & 3u) == 0u) dst->l[L_RIDE] = 0x4444 & ~dst->l[L_OH];
        dst->kit = 1;
        style = 1;
        break;
    }
    if (into_user) xt.user_style = (uint8_t)style;
    {   /* the caps of its genre */
        xt_pat_t keep = xt.wp;
        uint8_t ks = xt.wp_src;
        uint8_t gkeep = xt.wp_gen, skeep = xt.wp_style;
        xt.wp = *dst;
        if (into_user) xt.wp_src = XT_PAT_USER;
        else { xt.wp_gen = 1; xt.wp_style = (uint8_t)style; }
        xt_trim();
        *dst = xt.wp;
        xt.wp = keep; xt.wp_src = ks;
        xt.wp_gen = gkeep; xt.wp_style = skeep;
    }
    if (into_user) xt.gens++;
    return style;
}
static void xt_generate_into_user(int style)
{
    (void)xt_generate_into(&xt.user, style);
}

int xtra_generate(void)
{
    if (xt_locked()) return 0;
    xt_generate_into_user(-1);
    xt.k[XW_DRUM][0] = XT_PAT_USER;
    if (xt.run) xt.reload = 1; else xt_load_wp();
    xt.gen++;
    return 1;
}
int xtra_user_style(void) { return xt.user_style; }
void xtra_new_beat(void) { (void)xtra_generate(); }

/* REC on the GROOVE window: BREAK (from the next bar line), REC again: the 4-bar BUILD, then the drop; REC held
 * 0.6 s during a break or build: drop back in at the next bar line. Returns 0 when the transport is stopped. */
int xtra_break_press(uint32_t now)
{
    if (!xt.run) return 0;
    xt.rec_down = 1; xt.rec_used = 0; xt.rec_t0 = now;
    if (xt.brk == 0 && xt.brk_req != 1) xt.brk_req = 1;
    else if (xt.brk == 1) xt.brk_req = 2;
    else xt.rec_used = 2;                        /* (the press only counts as a hold now) */
    xt.gen++;
    return 1;
}
void xtra_rec_held(int down, uint32_t now)
{
    if (!xt.rec_down) return;
    if (!down) { xt.rec_down = 0; return; }
    if (!xt.rec_used || xt.rec_used == 2) {
        if (now - xt.rec_t0 >= 600u && (xt.brk || xt.brk_req)) { xt.brk_req = 3; xt.rec_used = 1; xt.gen++; }
    }
}

/* ---- one clock (24 per quarter) */
static void xt_clock(uint32_t now)
{
    xt_swing_due(now, 0);
    xt.ticks_total++;
    if (!xt.run && xt.ticks_total % 6u == 0u) {  /* Drift-Full: 16ths for the gates and REPEAT, transport stopped */
        xf_par.s16 = xt.ticks_total / 6u;
        if (xt.rep_pend) { xt.rep_pend = 0; xt.perf_bits |= 1u; }
#if POLI_PADREC
        xtra_padrec_sixteenth(now, (xt.ticks_total / 6u) % 16u, 0);
#endif
    }
    if (xt_arp_live()) {
        if (++xt.arp_cnt >= XT_RATE_CLK[xt.k[XW_ARP][1]]) {
            xt.arp_cnt = 0;
            if (xt.k[XW_GROOVE][0] > 50 && xt.k[XW_ARP][1] == 4 && (xt.arp_i & 1u)) {   /* 1/16: every 2nd waits */
                xt.sw_a_pend = 1; xt.sw_a_due = now + xt_swing_ms();
            } else xt_arp_step(now);
        }
    }
    if (xt.run) {
        uint32_t rc = XT_RATE_CLK[xt.k[XW_SEQ][2]], ph = xt.tick_n % 6u;
        if (xt.on[XW_SEQ] && xt.tick_n % rc == 0u) {
            uint32_t st = (xt.tick_n / rc) % (uint32_t)xt.k[XW_SEQ][1];
            if (xt.sw_s_pend) { xt.sw_s_pend = 0; xt_seq_fire(xt.sw_s_st, now); }
            if (xt_swung_tick(xt.tick_n)) { xt.sw_s_pend = 1; xt.sw_s_st = (uint8_t)st; xt.sw_s_due = now + xt_swing_ms(); }
            else xt_seq_fire(st, now);
        }
        if (ph == 0u) {
            xf_par.s16 = xt.tick_n / 6u;         /* Drift-Full: the 16th for the gates; a REPEAT starts on it */
            if (xt.rep_pend) { xt.rep_pend = 0; xt.perf_bits |= 1u; }
            xt_drum_step((xt.tick_n / 6u) % 16u, xt.tick_n, now);
#if POLI_PADREC
            xtra_padrec_sixteenth(now, (xt.tick_n / 6u) % 16u, 1);
#endif
        } else if (xt.roll_cur && xt.on[XW_DRUM] && xt.c2 == 3 && (xt_audible() >> L_CH & 1u) &&
                 (xt.roll_cur == 1 ? ph == 3u : (ph == 2u || ph == 4u)))   /* hat rolls: 32nds, or a triplet */
            xt_hit(XT_KF_LANE[xt_flavour()][L_CH], 90u, 4096u);
        xt.tick_n++;
    }
}

void xtra_all_off(void)
{
    xt_off(&xt.arp_note);
    xt_off(&xt.seq_note);
    xt_off(&xt.prev_note);
    xt.arp_off_pend = xt.seq_off_pend = 0;
    xt.sw_a_pend = xt.sw_s_pend = 0;
}

void xtra_transport(int run)
{
    run = run ? 1 : 0;
    if (run == xt.run) return;
    xt.run = (uint8_t)run;
    xt.sw_d_pend = xt.sw_s_pend = 0;
    xt.brk = xt.brk_req = 0;
    xt.fill_from = 0;
    xt.roll_cur = 0;
    xt.chord_cnt = 0;                            /* Drift-Full */
    if (xt.reload || xt.wp_src != (uint8_t)xt.k[XW_DRUM][0]) xt_load_wp();
    if (run) {
        xt.tick_n = 0;
        xt.bar = 0;
        xt.acc_q8 = xtra_tick_q8();               /* the first clock comes at the next pass: beat 1 at once */
        if (xt.on[XW_ARP] && xt.k[XW_ARP][0] == 6 && !xt.nheld) {   /* FROM SEQ: the walk starts with the beat */
            xt.arp_cnt = (uint32_t)XT_RATE_CLK[xt.k[XW_ARP][1]] - 1u;
            xt.arp_i = 0;
        }
        /* PLAY GEN (DRUM STEP i=2: K3 on the FM-1, K7 on the Elite): a fresh loop into the playing copy only.
         * USER, chance, PATTERN and PAD REC stay put. LOCK and an armed PAD REC skip it. */
        if (xt.k[XW_STEP][2] && !xt_locked() && !xtra_padrec_armed()) {
            int st;
            xt.wp_gen = 0;                       /* AUTO STYLE follows the programmed pattern, not a leftover beat */
            st = xt_generate_into(&xt.wp, -1);
            xt.wp_gen = 1;
            xt.wp_style = (uint8_t)st;
            xt.reload = 0;
            xt.wp_src = (uint8_t)xt.k[XW_DRUM][0];
        }
    } else {
        if (xt.wp_gen) { xt.wp_gen = 0; xt_load_wp(); }   /* STOP: the programmed pattern back at once */
        xt_off(&xt.seq_note);
        xt.seq_off_pend = 0;
        xt_tr_reset();                           /* Drums12-Transpose: STOP */
        if (xt.on[XW_ARP] && !xt.nheld) { xt_off(&xt.arp_note); xt.arp_off_pend = 0; xt.sw_a_pend = 0; }
        else if (xt.on[XW_ARP] && xt.hold) xt_unlatch(xt.last_ms);   /* Arp-Hold: STOP lets the latched notes go */
    }
    xt.gen++;
}

/* main program, every pass and from the LCD idle hook: clocks, gate ends, the settings for core 1 */
void xtra_tick(uint32_t now)
{
    uint32_t dt = now - xt.last_ms, tq, n = 0;
    xt.last_ms = now;
    xt.ext = xt.ext_ms != 0u && now - xt.ext_ms < 500u;
    if (!xt.ext) {
        tq = xtra_tick_q8();
        if (dt > 200u) dt = 200u;                /* a long stall: no burst of clocks after it */
        xt.acc_q8 += dt * 256u;
        while (xt.acc_q8 >= tq && n < 8u) {
            xt.acc_q8 -= tq;
            xt_clock(now);
            n++;
        }
        if (xt.acc_q8 >= tq) xt.acc_q8 = 0;
    }
    xt_swing_due(now, 0);
    if (xt.arp_off_pend && (int32_t)(now - xt.arp_off_ms) >= 0) { xt_off(&xt.arp_note); xt.arp_off_pend = 0; }
    if (xt.seq_off_pend && (int32_t)(now - xt.seq_off_ms) >= 0) { xt_off(&xt.seq_note); xt.seq_off_pend = 0; }
#if POLI_PADREC
    xtra_padrec_poll(now);
#endif
    xtra_push();
}

/* MIDI realtime (any channel): F8 clock, FA start, FB continue, FC stop. poli_nav_midi still sees them (LFO sync) */
void xtra_rt(uint8_t st, uint32_t now)
{
    if (st == 0xF8u) {
        xt.ext_ms = now ? now : 1u;
        xt.ext = 1;
        xt.acc_q8 = 0;
        if (xt.ext_start) { xt.ext_start = 0; xt.tick_n = 0; xtra_transport(1); }
        xt_clock(now);
    } else if (st == 0xFAu) {
        xtra_transport(0);
        xt.ext_start = 1;                         /* the next clock is beat 1 */
    } else if (st == 0xFBu) {
        xt.run = 1; xt.gen++;
    } else if (st == 0xFCu) {
        xt.ext_start = 0;
        xtra_transport(0);
        if (xt.on[XW_ARP] && xt.hold) xt_unlatch(now);   /* Arp-Hold: MIDI Stop, even when already stopped */
    }
}

/* panic (CC 120) and all-notes-off (CC 123): the held notes go, the added sound fades out and empties */
void xtra_panic(void)
{
    xt.nheld = 0;
    xt.ndn = 0;                                   /* Arp-Hold: the latch goes too */
    xt.sw_a_pend = xt.sw_s_pend = xt.sw_d_pend = 0;
    xt.roll_cur = 0;
    xt.arp_note = xt.seq_note = xt.prev_note = 255u;   /* (the voice was silenced by polivoks_midi) */
    xt.arp_off_pend = xt.seq_off_pend = 0;
    xt.perf_bits = 0; xt.rep_pend = 0; xt.fill_hold = 0; xt.mdn = 0;   /* Drift-Full: held effects let go */
    xt_tr_reset();                                /* Drums12-Transpose */
    xt.tr_mid[0] = xt.tr_mid[1] = xt.tr_mid[2] = xt.tr_mid[3] = 0;
    xf_par.clear_req++;
}

/* MIDI channel messages already filtered to the receive channel: 1 = taken (do not pass to the voice) */
int xtra_midi(uint8_t status, uint8_t d1, uint8_t d2, uint32_t now)
{
    uint8_t st = (uint8_t)(status & 0xF0u);
    int on = st == 0x90u && d2, off = st == 0x80u || (st == 0x90u && !d2);
    if (st == 0xB0u && (d1 == 120u || d1 == 123u)) {
        xtra_panic();
        return 0;                                 /* the voice takes its own panic */
    }
    if (xt.rec && on) { xt_rec_write(d1); return 0; }   /* recorded, and heard */
    if (!xt.on[XW_ARP]) {                         /* Drift-Full: chord changes from MIDI notes with the arp off */
        if (on) { if (!xt.mdn) xt_chord(); xt.mdn++; }
        else if (off && xt.mdn) xt.mdn--;
    }
    if ((on || off) && d1 < 128u) {               /* Drums12-Transpose: a key sets the offset; its note-off is taken too */
        uint32_t *w = &xt.tr_mid[d1 >> 5], b = 1u << (d1 & 31u);
        if (on && xt_tr_live()) { *w |= b; xt_tr_set(d1); return 1; }
        if (off && (*w & b)) { *w &= ~b; return 1; }
    }
    if (!xt.on[XW_ARP] || !(on || off)) return 0;
    {
        int was = xt.nheld;
        if (on) xt_key_on(d1, d2);
        else xt_key_off(d1);
        xt_arp_held_changed(now, was);
    }
    return 1;
}

/* Drift-Full: PERFORM armed (SEL on PERFORM): the keybed plays the drum and mix effects instead of notes. Key 1 =
 * the lowest (F): 1 FILL (held), 2 REPEAT (held; from the next 16th), 3 REVERSE, 4 TAPE STOP, 5 LP SWEEP, 6 HP
 * SWEEP (held), 7..12 MUTE kick / snare / clap / closed hat / open hat / perc (press: on / off), 13..18 SOLO the
 * same, 19 GENERATE, 20 RESTORE, 21 LOCK, 22 DUCK on / off. The arp keeps its latched notes. */
static void xt_fx_keys(uint32_t pressed, uint32_t released)
{
    static const uint8_t HB[6] = { 0, 0, 2, 4, 8, 16 };   /* keys 1..6: FILL, REPEAT, REVERSE, TAPE STOP, LP, HP */
    uint32_t k;
    for (k = 0; k < 6u; k++) {
        if (released & (1u << k)) {
            if (k == 0u) xt.fill_hold = 0;
            else if (k == 1u) { xt.rep_pend = 0; xt.perf_bits &= ~1u; }
            else xt.perf_bits &= ~(uint32_t)HB[k];
            xt.gen++;
        }
        if (pressed & (1u << k)) {
            if (k == 0u) xt.fill_hold = 1;
            else if (k == 1u) xt.rep_pend = 1;
            else xt.perf_bits |= HB[k];
            xt.gen++;
        }
    }
    for (k = 6; k < 18u; k++)
        if (pressed & (1u << k)) {
            int g = (int)(k - 6u) % 6, s = k < 12u ? 1 : 2;
            xtra_set_lane_state(g, xt.lane_st[g] == (uint8_t)s ? 0 : s);
        }
    if (pressed & (1u << 18)) (void)xtra_generate();
    if (pressed & (1u << 19)) xtra_restore();
    if (pressed & (1u << 20)) { xt.k[XW_GEN][3] = (int16_t)!xt.k[XW_GEN][3]; xt.gen++; }
    if (pressed & (1u << 21)) { xt.on[XW_DFX] = (uint8_t)!xt.on[XW_DFX]; xt.gen++; }
    xtra_push();
}
uint32_t xtra_perf_bits(void) { return xt.perf_bits | (xt.rep_pend ? 1u : 0u); }
int xtra_fill_hold(void) { return xt.fill_hold; }
/* REC held on DRUMS or PERFORM: FILL while it is down */
void xtra_fill_button(int down) { uint8_t v = (uint8_t)(down ? 2u : 0u); if (v != xt.fill_hold) { xt.fill_hold = v; xt.gen++; } }   /* (2: the button) */
static void xt_step_keys(uint32_t pressed, uint32_t released, int kb_oct, uint32_t now);   /* Drum-Step (below) */
/* the keybed, every pass: returns the keys the voice should see (none while ARP takes them or REC writes) */
#if POLI_PADREC
static void xt_step_keys(uint32_t pressed, uint32_t released, int kb_oct, uint32_t now);
int xtra_step_white(int k);
int xtra_step_lane(int v);
/* Parity (23 Nov 2026): the FM-1's pads are DRUM STEP's 16 white keys, F3 up (kick, snare, clap, closed hat, open
 * hat, low / mid / high tom, rim, cowbell, clave, low / mid / high conga, ride, crash); a press is a pad hit, a
 * release lets the pad go (hold 500 ms = erase the lane). Black keys do nothing. Keys let go here that were down
 * before the page opened end as on DRUM STEP. */
static void xt_padrec_keys(uint32_t pressed, uint32_t released, int kb_oct, uint32_t now)
{
    uint32_t k;
    for (k = 0; k < 27u; k++) {
        int v = xtra_step_white((int)k), pad;
        if (v < 0 || !((pressed | released) & (1u << k))) continue;
        pad = xtra_padrec_pad_of_lane(xtra_step_lane(v));
        if (pad < 0) continue;
        if (released & (1u << k)) xtra_padrec_pad(pad, 0, now);
        if (pressed & (1u << k)) xtra_padrec_pad(pad, 127, now);
    }
    xt_step_keys(0u, released, kb_oct, now);
}
#endif
uint32_t xtra_keys(uint32_t keys, int kb_oct, uint32_t now)
{
    uint32_t pressed = keys & ~xt.prev_keys, released = xt.prev_keys & ~keys, k, old = xt.prev_keys;
    int was = xt.nheld;
    xt.prev_keys = keys;
    if (poli_nav_xtra() == XW_STEP) {             /* Drum-Step: the white keys toggle the voices; nothing reaches the voice */
        xt_step_keys(pressed, released, kb_oct, now);
        return 0;
    }
#if POLI_PADREC
    if (poli_nav_xtra() == XW_PADREC) {           /* Parity: PAD REC, the white keys are the pads; nothing reaches the voice */
        xt_padrec_keys(pressed, released, kb_oct, now);
        return 0;
    }
#endif
    if (xt.on[XW_PERF] && !xt.rec) { xt_fx_keys(pressed, released); return 0; }   /* Drift-Full: KEYS = PERFORM */
    if (xt.rec) {
        for (k = 0; k < 27u; k++) {
            int note = 65 + (int)k + kb_oct * 12;
            if (note < 0) note = 0;
            if (note > 126) note = 126;
            if (pressed & (1u << k)) {
                xt_rec_write((uint8_t)note);
                if (!xt.run) { xt_off(&xt.prev_note); xt_on(&xt.prev_note, (uint8_t)note, 127); }
            }
        }
        if (!keys) xt_off(&xt.prev_note);
        return 0;
    }
    if (!xt.on[XW_ARP] && xt_tr_live()) {        /* Drums12-Transpose: the keys transpose, the voice does not see them */
        if (pressed && !old) xt_chord();
        for (k = 27u; k-- > 0u; )
            if (pressed & (1u << k)) {           /* (two in one pass: the higher one) */
                int note = 65 + (int)k + kb_oct * 12;
                xt_tr_set(note < 0 ? 0 : note > 126 ? 126 : note);
                break;
            }
        return 0;
    }
    if (!xt.on[XW_ARP]) { if (pressed && !old) xt_chord(); return keys; }
    for (k = 0; k < 27u; k++) {
        int note = 65 + (int)k + kb_oct * 12;
        if (note < 0) note = 0;
        if (note > 126) note = 126;
        if (released & (1u << k)) xt_key_off((uint8_t)note);   /* (releases first: a key moved in one pass) */
    }
    for (k = 0; k < 27u; k++) {
        int note = 65 + (int)k + kb_oct * 12;
        if (note < 0) note = 0;
        if (note > 126) note = 126;
        if (pressed & (1u << k)) xt_key_on((uint8_t)note, 127);
    }
    xt_arp_held_changed(now, was);
    return 0;
}

/* ---- Drum-Step (22 Nov 2026): the DRUM STEP page. K1 STEP = the cursor (1..16), K2 CHANCE = the chance of the
 * last-touched voice's hit at the cursor (100 / 75 / 50 / 25 %). The 16 white keys, fixed (OCT does not move them),
 * left to right: kick, snare, clap, closed hat, open hat, low / mid / high tom, rim, cowbell, clave, low / mid /
 * high conga, ride, crash (no ghost snare). A press toggles that voice at the cursor. The first edit of a pattern
 * other than USER copies it (its genre too) into USER and PATTERN goes to USER. An edit goes into USER and into the
 * playing copy at once; the Drift, fills and HAT BUSY still only touch the playing copy, RESTORE / LOCK as before. */
static const uint8_t XT_STEP_LANE[16] = { L_KICK, L_SNARE, L_CLAP, L_CH, L_OH, L_LT, L_MT, L_HT, L_RIM, L_COWB,
                                          L_CLAVE, L_LC, L_MC, L_HC, L_RIDE, L_CRASH };
/* keybed key k (0 = the lowest F) -> voice 0..15, or -1 for a black key or the key past the 16th white */
int xtra_step_white(int k)
{
    static const int8_t W[12] = { 0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6 };   /* F G A B C D E */
    int v;
    if (k < 0 || k > 26 || W[k % 12] < 0) return -1;
    v = (k / 12) * 7 + W[k % 12];
    return v < 16 ? v : -1;
}
int xtra_step_lane(int v) { return (v < 0 || v > 15) ? -1 : XT_STEP_LANE[v]; }
int xtra_step_voice(void) { return xt.st_voice; }
int xtra_chance(int l, int st) { return (l < 0 || l > 16 || st < 0 || st > 15) ? 0 : (int)((xt.chance[l] >> (2 * st)) & 3u); }
/* the pattern the page shows and edits: the programmed one (PATTERN), not the Drift's playing copy */
static const xt_pat_t *xt_step_src(void) { return xt_pat(xt.k[XW_DRUM][0]); }
/* voice v at step st: bit 0 = a hit, bits 1-2 = its chance level (USER only) */
int xtra_step_cell(int v, int st)
{
    int l = xtra_step_lane(v);
    if (l < 0 || st < 0 || st > 15 || !((xt_step_src()->l[l] >> st) & 1u)) return 0;
    return 1 | (xt.k[XW_DRUM][0] == XT_PAT_USER ? xtra_chance(l, st) << 1 : 0);
}
/* the first edit: the programmed pattern (and its genre) into USER, chance cleared, PATTERN = USER */
static void xt_step_user(void)
{
    int p = xt.k[XW_DRUM][0], fresh;
    if (xt.wp_gen) {                     /* PLAY GEN: drop the temp beat and edit the real USER from here */
        xt.wp_gen = 0;
        xt.wp = xt.user;
        xt.wp_src = XT_PAT_USER;
        xt.reload = 0;
    }
    fresh = xt.reload || xt.wp_src != (uint8_t)p;
    if (p != XT_PAT_USER) {
        xt.user = *xt_pat(p);
        xt.user_style = (uint8_t)(p < XT_PAT_USER ? 0 : 1 + (p - 9) / XT_PACK);
        memset(xt.chance, 0, sizeof xt.chance);
        xt.k[XW_DRUM][0] = XT_PAT_USER;
    }
    if (fresh) { xt.wp = xt.user; xt.reload = 0; }   /* (a pattern change was still waiting for the beat) */
    xt.wp_src = XT_PAT_USER;
}
static void xt_step_audition(uint32_t l, uint32_t st)
{
    int f = xt_flavour();
    uint32_t in = XT_KF_LANE[f][l], pm = 4096u;
    if (l == L_KICK) { if (xt.wp.lng && f == KF_808) in = DI_BDL8; pm = XT_KF_KPM[f]; }
    if (l == L_COWB) pm = XT_SEMI_Q12[xt_bell(st)];
    xt_hit(in, XT_LANE_VEL[l], pm);
}
void xtra_step_toggle(int v)
{
    int l = xtra_step_lane(v), st = xt.k[XW_STEP][0];
    uint32_t b, on;
    if (l < 0 || st < 0 || st > 15) return;
    b = 1u << st;
    xt_step_user();
    xt.user.l[l] = (uint16_t)(xt.user.l[l] ^ b);
    on = xt.user.l[l] & b;
    xt.wp.l[l] = (uint16_t)((xt.wp.l[l] & ~b) | on);
    xt.chance[l] &= ~(3u << (2 * st));          /* a new or removed hit: 100 % */
    if (l == L_CH && !on) { xt.user.roll &= ~(3u << (2 * st)); xt.wp.roll &= ~(3u << (2 * st)); }
    xt.st_voice = (uint8_t)v;
    xt.k[XW_STEP][1] = 0;
    if (on && !xt.run && xt.on[XW_DRUM] && xt.c2 == 3) xt_step_audition((uint32_t)l, (uint32_t)st);   /* heard */
#if POLI_PADREC
    xtra_padrec_follow_user();                   /* Parity (as the Elite): PAD REC's working groove follows */
#endif
    xt.gen++;
}
static int xt_step_chance_now(void) { return xtra_step_cell(xt.st_voice, xt.k[XW_STEP][0]) >> 1; }
static void xt_step_knob(int i, int v)
{
    int l = XT_STEP_LANE[xt.st_voice & 15u], st = xt.k[XW_STEP][0];
    if (i == 0) { xt.k[XW_STEP][1] = (int16_t)(xtra_step_cell(xt.st_voice, v) >> 1); return; }
    if (i == 2) {                        /* PLAY GEN: leaving ON restores the programmed pattern */
        if (v == 0 && xt.wp_gen) {
            if (xt.run) xt.reload = 1;
            else xt_load_wp();
        }
        return;
    }
    if (i != 1) return;
    if (!(xtra_step_cell(xt.st_voice, st) & 1)) { xt.k[XW_STEP][1] = 0; return; }   /* no hit there: stays 100 % */
    xt_step_user();
    xt.chance[l] = (xt.chance[l] & ~(3u << (2 * st))) | (uint32_t)v << (2 * st);
#if POLI_PADREC
    xtra_padrec_follow_user();
#endif
}
static void xt_step_keys(uint32_t pressed, uint32_t released, int kb_oct, uint32_t now)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (pressed & (1u << k)) xtra_step_toggle(xtra_step_white((int)k));
    if (!released) return;                       /* keys let go here that were down before the page opened: */
    if (xt.rec) { if (!xt.prev_keys) xt_off(&xt.prev_note); }   /* the REC preview ends, */
    else if (xt.on[XW_PERF]) xt_fx_keys(0, released);           /* a held PERFORM effect ends, */
    else if (xt.on[XW_ARP]) {                                   /* or the arp lets the note go */
        int was = xt.nheld;
        for (k = 0; k < 27u; k++)
            if (released & (1u << k)) {
                int note = 65 + (int)k + kb_oct * 12;
                xt_key_off((uint8_t)(note < 0 ? 0 : note > 126 ? 126 : note));
            }
        xt_arp_held_changed(now, was);
    }
}

/* ---- the panel: SEL (on/off), K1..K4, the buttons */
void xtra_toggle(int w)
{
    if (w < 0 || w >= XW_N) return;
    if (w == XW_STEP) w = XW_DRUM;               /* Drum-Step: SEL on DRUM STEP = DRUMS on / off */
    if (w == XW_GROOVE || w == XW_GEN) { xtra_new_beat(); return; }   /* SEL on GROOVE / GENERATE = GENERATE */
    xt.on[w] = (uint8_t)!xt.on[w];
#if POLI_ARP
    if ((w == XW_ARP || w == XW_SEQ) && xt.on[w] && poli_arp_on()) poli_arp_set_on(0);   /* Parity: one arp at a time */
#endif
    if (w == XW_PERF && !xt.on[w]) { xt.perf_bits = 0; xt.rep_pend = 0; xt.fill_hold = 0; xt.prev_keys = 0; }
    if (w == XW_PERF && xt.on[w]) xt.prev_keys = 0;
    if (w == XW_WAND) { xt.w_lock = xt.on[w]; xt.w_lpos = 0; }   /* SEL on WANDER = LOCK */
    if (w == XW_ARP) {
        if (xt.on[w]) { xt.on[XW_SEQ] = 0; xt_off(&xt.seq_note); xt.seq_off_pend = 0; xt.nheld = 0; xt_tr_reset(); }
        else { xt.nheld = 0; xt_off(&xt.arp_note); xt.arp_off_pend = 0; xt.sw_a_pend = 0; }
        xt.ndn = 0;                               /* Arp-Hold: ARP off lets the latched notes go */
        xt.prev_keys = 0;                         /* keys already down join the arp (or go back to the voice) */
    }
    if (w == XW_SEQ) {
        if (xt.on[w]) { xt.on[XW_ARP] = 0; xt.nheld = 0; xt.ndn = 0; xt_off(&xt.arp_note); xt.arp_off_pend = 0; }
        else { xt_off(&xt.seq_note); xt.seq_off_pend = 0; xt.sw_s_pend = 0; xt_tr_reset(); }
    }
    xtra_push();
    xt.gen++;
}
void xtra_knob(int w, int i, int steps)
{
    int v;
#if POLI_PADREC
    if (w == XW_PADREC) { xtra_padrec_knob(i, steps); return; }
#endif
#if POLI_ARP
    if (w == XW_CARP) { if (i >= 0 && i <= 3 && steps) { poli_arp_knob(i, steps); xt.gen++; } return; }
#endif
    if (w < 0 || w >= XW_N || i < 0 || i > 3 || steps == 0) return;
    if (XT_HI[w][i] <= XT_LO[w][i]) return;      /* an empty knob */
    if (w == XW_STEP) xt.k[XW_STEP][1] = (int16_t)xt_step_chance_now();
    v = xt.k[w][i] + steps;
    if (v < XT_LO[w][i]) v = XT_LO[w][i];
    if (v > XT_HI[w][i]) v = XT_HI[w][i];
    xt.k[w][i] = (int16_t)v;
    if (w == XW_SEQ && i == 1 && xt.rec_cur >= v) xt.rec_cur = 0;
    if (w == XW_DRUM && i == 0) {                 /* Drift-Full: heard from the next beat; a pack brings its tempo */
        int np = xt_pack(v);
        if (np >= 0 && np != xt_pack(xt.wp_src)) xt.k[XW_SEQ][0] = XT_PACK_BPM[np];
        if (!xt.run) xt_load_wp();
    }
    if (w == XW_STEP) xt_step_knob(i, v);       /* Drum-Step: STEP moves the cursor, CHANCE sets the cell's */
    if (w == XW_PERF && i == 2) xt.k[XW_PERF][3] = xt.lane_st[v];   /* LANE: STATE shows that lane's */
    if (w == XW_PERF && i == 3) xt.lane_st[xt.k[XW_PERF][2]] = (uint8_t)v;
    xtra_push();
    xt.gen++;
}
void xtra_rec_toggle(void)
{
    xt.rec = (uint8_t)!xt.rec;
    xt.rec_cur = 0;
    xt_off(&xt.prev_note);
    if (xt.rec && xt.on[XW_ARP]) { xt.on[XW_ARP] = 0; xt.nheld = 0; xt.ndn = 0; xt_off(&xt.arp_note); xt.arp_off_pend = 0; }
    xt.gen++;
}
void xtra_rest(void) { if (xt.rec) xt_rec_write(255u); }

void xtra_init(void)
{
    int w, i;
    memset(&xt, 0, sizeof xt);
    for (w = 0; w < XW_N; w++)
        for (i = 0; i < 4; i++) xt.k[w][i] = XT_DEF[w][i];
    for (i = 0; i < XT_SEQ_N; i++) xt.seq[i] = i < XT_SEQ_DEF_N ? XT_SEQ_DEF[i] : 255u;   /* Seq64: 17..64 rests */
    xt.arp_note = xt.seq_note = xt.prev_note = 255u;
    xt.rng = 0x1234567u;
    xt.drng = 0x89ABCDEu;                         /* Arp-Hold: the drums' own sequence */
    xt.hold = XT_HOLD_DEFAULT;                    /* Arp-Hold: ARP HOLD starts ON at power-on (REC on ARPEGGIO) */
    xt.pend_pat = 255u;
    xt.on[XW_DFX] = 1;                            /* Drift-Full: DUCK on (it acts only while the drums play) */
    xt.user_style = 0;                            /* (USER starts as pattern 1: generic rules) */
    xt.user = XT_PAT[0];                          /* Wander-Drift: the 9th slot starts as pattern 1 */
    xt_load_wp();
    xtra_push();
#if POLI_PADREC
    xtra_padrec_init();
#endif
}

#if POLI_PADREC
void xtra_padrec_follow_user(void);              /* poli_padrec.inc */
#include "poli_padrec.inc"
#endif

#if POLI_ARP
/* Parity (23 Nov 2026): the CLASSIC ARP page (window 13). K1 MODE, K2 RATE, K3 OCTAVES, K4 GATE (5 % a click);
 * SEL or ARP there = on / off; REC there = LATCH. The classic arp and the EXTRAS ARP / SEQ exclude each other. */
static const int16_t XT_CARP_LO[4] = { 0, 0, 1, ARP_GATE_MIN };
static const int16_t XT_CARP_HI[4] = { ARP_NMODE - 1, ARP_NRATE - 1, 4, ARP_GATE_MAX };
static int xt_carp_lo(int i) { return (i < 0 || i > 3) ? 0 : XT_CARP_LO[i]; }
static int xt_carp_hi(int i) { return (i < 0 || i > 3) ? 0 : XT_CARP_HI[i]; }
static int xt_carp_get(int i)
{
    poli_arp_view_t a;
    poli_arp_view(&a);
    return i == 0 ? a.mode : i == 1 ? a.rate : i == 2 ? a.oct : i == 3 ? a.gate : 0;
}
void xtra_carp_toggle(void)
{
    if (!poli_arp_on()) {                        /* on: the EXTRAS ARP and SEQ go off first (one note path) */
        if (xt.on[XW_ARP]) xtra_toggle(XW_ARP);
        if (xt.on[XW_SEQ]) xtra_toggle(XW_SEQ);
    }
    poli_arp_set_on(!poli_arp_on());
    xt.gen++;
}
#endif
