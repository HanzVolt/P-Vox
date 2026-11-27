/* SPDX-License-Identifier: GPL-3.0-only */
/* Core2-FxSeq (15 Nov 2026): the ADDED features of this build. NOT Polivoks features: the original
 * instrument has no arpeggiator, sequencer, drum track, delay or reverb. Ground-up code, no stock FM code.
 *
 *   poli_xtra.c   main program (core 0): the clock (internal tempo or MIDI clock), the arpeggiator, the
 *                 step sequencer, the drum track's pattern, the settings of all five, and the screens' data.
 *                 Notes reach the voice only through polivoks_midi, as a MIDI note would.
 *   poli_c2fx.c   second core (core 1), code in RAM (.c1_text): delay, reverb and the drum voices. It reads
 *                 the voice's finished output one half buffer (256 frames, 5.8 ms) late and gives back a
 *                 stereo "added" signal that the audio interrupt adds to the next half (xf_isr_mix).
 *
 * The hand-off (one job at a time, hal/fm1_cpu1.h): the audio interrupt, after polivoks_render, checks that
 * core 1 finished the job it was given at the previous half (no waiting: if not, this half stays dry and
 * xf_late counts it), adds xf_wet to the half, copies the dry half to xf_dry and hands core 1 the next job.
 * Core 1 then has a whole half (5.8 ms) to turn xf_dry into the next xf_wet. Each buffer has one writer at
 * a time: xf_dry core 0 between jobs, xf_wet core 1 during a job. With every added feature off and its
 * tail gone, xf_run is 0 and the interrupt does exactly what the base build did. */
#ifndef POLI_XTRA_H
#define POLI_XTRA_H
#include <stdint.h>
/* new state is kept out of the compiler's global merging (attribute used), so the voice's own code is laid out
 * as in the base build (its weighted score depends on it) */
#define XT_KEEP __attribute__((used))

#define XF_TRQ 16u                             /* Wander-Drift: drum hit ring */
#define XF_DV 6u                               /* drum voices sounding at once (a pool; the oldest quiet one is taken) */
#define XF_DI 26u                              /* drum instruments: 808 family 0..14, 909 family 15..25 */
/* settings the main program writes and core 1 reads once per half (aligned words: no torn value) */
typedef struct {
    volatile int32_t dly_on, dly_len, dly_fb, dly_tone, dly_lvl;   /* len: samples, the echo spacing */
    volatile int32_t rev_on, rev_fb, rev_damp, rev_lvl;            /* fb, damp, lvl: Q15 */
    volatile int32_t drm_lvl;                                      /* Q15 */
    volatile uint32_t drm_trig[3];                                 /* hit counters: kick, snare, hat lanes (tests only) */
    volatile uint32_t clear_req;                                   /* counter: fade the added signal out, empty all lines */
    /* Wander-Drift: the drum machine. Hits go through a ring (main writes an entry, then trw; core 1 reads up to
     * trw): bits 0..7 instrument, 8..15 velocity 1..127, 16..31 pitch factor Q12 (4096 = as patched) */
    volatile uint32_t trq[XF_TRQ];
    volatile uint32_t trw;
    /* Drift-Full (17 Nov 2026): MIX mode. mix_on 1 = core 1 makes the WHOLE output (voice + drums + echo /
     * reverb + the PERFORM effects + a final soft clip) and the interrupt plays it instead of the dry (xf_keep 0).
     * duck_on / duck_depth (Q15) / duck_k (Q15 decay per 16 samples): a kick hit ducks the voice and the echo /
     * reverb returns (never the dry drums). gate 0 off, 1 = 1/16, 2 = 1/8 (the voice only), timed from s16
     * (16ths since PLAY, main bumps it) and step_smp (samples per 16th). perf: held effects, bit 0 REPEAT,
     * 1 REVERSE, 2 TAPE STOP, 3 LP sweep, 4 HP sweep; rep_len: the repeat length (samples). kdrive: the kick's
     * drive, Q12 (4096 = clean). crush 0 none, 1 BREAK, 2 HARD (the dry drums only). d_echo, d_rev: the drums'
     * send levels into the echo and the reverb (Q15; each instrument's own send weight in its patch) */
    volatile int32_t mix_on, duck_on, duck_depth, duck_k, gate, step_smp, perf, rep_len, kdrive, crush, d_echo, d_rev;
    volatile uint32_t s16;
    /* TUNE / DECAY for sampled drum playback (same mapping as xt_patches: 128+k*2, 32+k*3/2) */
    volatile int32_t smp_tq, smp_dq;
} xf_par_t;

/* Wander-Drift: one drum instrument as core 1 plays it (main program computes it from poli_xtra.c's voicing table,
 * the TUNE and DECAY knobs; in RAM, as core 1 may not read the flash). Two paths, mixed:
 *   tone   parabolic sine, its phase step falling from inc1 + dinc to inc1 (pitch envelope pdec), level tamp, decay tdec
 *   n/m    noise (namp) plus mn square oscillators (mamp each, steps minc[]), through a state-variable filter
 *          (f, q; mode 0 low, 1 band, 2 high pass), decay ndec; bursts > 0: that many re-attacks bgap blocks of 16
 *          samples apart, decaying at bdec (the hand clap), then the tail at ndec.
 * choke: an instrument whose voice this hit silences (the closed hat stops the open hat), -1 none.
 * Decays are per 16 samples, Q15. */
typedef struct {
    int32_t tamp, inc1, dinc, tdec, pdec;
    int32_t namp, mamp, ndec, bdec, f, q, mode, bursts, bgap, choke, mn;
    int32_t minc[6];
    int32_t send, kick;                         /* Drift-Full: echo / reverb send weight Q15 (kick 0); 1 = a kick (drive, duck) */
} xf_patch_t;
extern xf_patch_t xf_patch[XF_DI];             /* poli_xtra.c writes, core 1 reads at a hit */

/* what core 1 reports (core 1 writes, the main program reads) */
typedef struct {
    volatile uint32_t magic;                    /* "XFC2" for a memory read */
    volatile uint32_t jobs;                     /* jobs done */
    volatile uint32_t busy_last, busy_max;      /* ticks (24 MHz TIMER4) core 1 spent on one job */
    volatile uint32_t idle;                     /* 1: every send off, the tails gone, the lines empty, xf_wet zero */
    volatile uint32_t clear_ack;                /* the clear_req it last carried out */
    volatile int32_t wet_peak, dry_peak;        /* last half, for the tests */
    volatile uint32_t limited;                  /* halves where the added signal was turned down for headroom */
} xf_stat_t;

#define XF_N 256u                               /* frames per half (platform.c HALF_FRAMES) */
#define XF_DLY_N 65536u                         /* delay line: 65536 samples, 1.486 s at 44.1 kHz (int16) */
#define XF_DLY_MAX (XF_DLY_N - 2u * XF_N)       /* longest echo spacing it gives */
#define XF_DLY_MIN (XF_N + 1u)                  /* shortest (the half of latency is taken off the read) */
#define XF_MR_N 32768u                          /* Drift-Full: the PERFORM ring, stereo int16 frames (0.743 s, 128 KiB) */
#define XF_MR_MAX (XF_MR_N - 2u * XF_N)         /* the longest REPEAT it holds */
extern xf_par_t xf_par;                         /* poli_xtra.c */
extern xf_stat_t xf_st;

uint32_t xtra_keys(uint32_t keys, int kb_oct, uint32_t now);
#if BOARD_SMK37
uint64_t xtra_keys_smk(uint64_t keys, const volatile uint8_t *vel, int kb_oct, uint32_t now);
/* SMK-only pad-record (22 Nov 2026): 16 pads into user drum slots. Not compiled on the FM-1. */
enum { XW_PADREC = 12, XT_PADREC_SLOTS = 8, XT_PADREC_BLOB = 408 };   /* (Drum-Step: PAD REC 11 -> 12; 11 = DRUM STEP) */
int xtra_padrec_armed(void);
int xtra_padrec_countin(void);
int xtra_padrec_waiting(void);
int xtra_padrec_mode(void);
int xtra_padrec_quant(void);
int xtra_padrec_slot(void);
int xtra_padrec_step(void);
int xtra_padrec_dirty(void);
int xtra_padrec_can_undo(void);
int xtra_padrec_slot_used(int slot);
int xtra_padrec_lane_of_pad(int pad);
int xtra_padrec_work_lane(int l);
int xtra_padrec_lo(int i);
int xtra_padrec_hi(int i);
int xtra_padrec_get(int i);
void xtra_padrec_knob(int i, int steps);
void xtra_padrec_hit(int pad);
void xtra_padrec_pad(int pad, int vel, uint32_t now); /* vel 0 = release; hold-erase */
int xtra_padrec_rec(uint32_t now);                    /* 0 = refused (no core 2) */
void xtra_padrec_undo(void);
void xtra_padrec_clear(void);
int xtra_padrec_save(void);                           /* working copy -> flash slot + RAM USER */
int xtra_padrec_load(int slot);                       /* 0 = empty / bad slot */
void xtra_padrec_step_move(int dir);
int xtra_padrec_skip_lane(int l, int st);
void xtra_padrec_sixteenth(uint32_t now, uint32_t st, int running);
void xtra_padrec_init(void);
void xtra_padrec_poll(uint32_t now);
int xtra_padrec_export(void *p, uint32_t n);
int xtra_padrec_import(const void *p, uint32_t n);
#endif
#endif
