/* SPDX-License-Identifier: GPL-3.0-only */
/* Section-window navigation for the FM-1 Polivoks app.
 * Recommended tables in design/NAV-WINDOWS-v2.md. Tentative choices are
 * named in ASSIGNMENTS.md: bass on FILTER MOD's empty SELECT, language on
 * the unused ALGORITHM encoder, key-hold left unimplemented, keyboard base
 * left at note 65, no third oscillator, external level not mapped.
 */
#ifndef POLI_NAV_H
#define POLI_NAV_H
#include <stdint.h>
#include "poli_feat.h"        /* Parity (23 Nov 2026): the feature switches */

/* Logical voice order is the HOME roll, not this enum.
 * EDIT = OSC, LFO = modulator, FX = filter, ENV = amp, GLO = global. */
enum {
    NAV_SEC_OSC = 0,
    NAV_SEC_LFO = 1,
    NAV_SEC_FX = 2,
    NAV_SEC_AMP = 3,
    NAV_SEC_GLOBAL = 4,
    NAV_SEC_XTRA = 5         /* Core2-FxSeq: the ADDED features (ARP, SEQ, DRUMS, DELAY, REVERB), not Polivoks */
};
enum { NAV_XWIN = 12 + POLI_PADREC + POLI_ARP };   /* Drift-Full: + 8 GENERATE, 9 DRUM FX, 10 PERFORM; Drum-Step: + 11 DRUM STEP;
                                                     Parity: + 12 PAD REC, 13 CLASSIC ARP */
/* was: enum { NAV_XWIN = 12 }; */
/* was: enum { NAV_XWIN = 11 }; */
/* was: enum { NAV_XWIN = 8 }; */   /* Wander-Drift: 8 (ids 5 WANDER, 6 DRUM DRIFT, 7 GROOVE; poli_nav.c XW_ORDER) */
enum { NAV_XWIN_C2 = 5 };       /* their windows: ARP, SEQ, DRUMS, DELAY, REVERB (flat 12..16, outside the HOME roll) */

enum { NAV_NWIN = 12 };  /* 11 voice windows + MIDI channel in GLOBAL */

enum {
    SLOT_EMPTY = 0,
    SLOT_EXT,
    SLOT_CUT, SLOT_RES, SLOT_CONT, SLOT_DRIVE,
    SLOT_ATK_F, SLOT_DEC_F, SLOT_SUS_F, SLOT_REL_F,
    SLOT_ATK_A, SLOT_DEC_A, SLOT_SUS_A, SLOT_REL_A,
    SLOT_MOD_CUT, SLOT_MOD_AMP, SLOT_MOD_PITCH, SLOT_MOD_PITCH2,
    SLOT_FOOT1, SLOT_FOOT2, SLOT_SHAPE1, SLOT_SHAPE2,
    SLOT_FM, SLOT_DETUNE, SLOT_LV1, SLOT_LV2, SLOT_NOISE,
    SLOT_LFO_RATE, SLOT_LFO_SHAPE,
    SLOT_TUNE, SLOT_LEVEL, SLOT_GLIDE_RATE, SLOT_DUO,
    SLOT_LFO_RST, SLOT_LFO_SYNC, SLOT_VEL,   /* preset extras (record bytes 49..51), not in the 31-knob block */
    SLOT_COUNT
};

enum {
    SW_NONE = 0,
    SW_LPBP, SW_REP_F, SW_REP_A, SW_LINK, SW_GLIDE, SW_BASS, SW_KEYHOLD,
    SW_MODOSC
};

enum {
    MSG_NONE = 0,
    MSG_OCTAVE,
    MSG_NO_SWITCH,
    MSG_NO_FX,
    MSG_KEYHOLD,
    MSG_EMPTY,
    MSG_EXT,
    MSG_LANG,
    MSG_ARP, MSG_SEQ, MSG_PLAY, MSG_REC, MSG_SAVE, MSG_PRESETS,
    MSG_STOP, MSG_NO_C2, MSG_NEW_BEAT, MSG_NEED_PLAY, MSG_BREAK, MSG_BUILD, MSG_DROP, MSG_ARP_HOLD, MSG_ARP_FREE, MSG_LOCKED, MSG_GENERATED, MSG_RESTORE, MSG_LOCK_ON, MSG_LOCK_OFF      /* Core2-FxSeq: transport stopped; an effect needs the second core, which is not running */
};
#if POLI_PADREC
enum { MSG_PAD_SAVED = MSG_LOCK_OFF + 1, MSG_PAD_EMPTY, MSG_PAD_UNDO, MSG_PAD_CLEARED, MSG_PAD_FAILED };
#endif
#if POLI_ARP
enum { MSG_CARP_LATCH = MSG_LOCK_OFF + 6, MSG_CARP_FREE };   /* Parity: REC on CLASSIC ARP */
#endif

typedef struct {
    int section;              /* owner of the current window */
    int window;               /* index inside the section, or the roll index */
    int nwindows;
    int roll;                 /* 1: HOME is walking the whole voice list */
    int flat;                 /* 0..11 in logical voice order (11 is MIDI channel) */
    int lang;                 /* 0 EN, 1 RU */
    int kb_oct;
    int slot_kind[4];
    int slot_pos[4];
    int switch_kind;
    int switch_on;
    int msg;
    int midi_ch;              /* receive channel 1..16, not a preset parameter */
    int key_track;            /* 0 / 62 / 100 %; not a preset parameter */
    int sync_ok;              /* a MIDI clock tempo is known (LFO SYNC follows it) */
    int xtra;                 /* Core2-FxSeq: -1, or the ADDED window shown (0 ARP .. 4 REVERB; section NAV_SEC_XTRA) */
} poli_nav_view_t;

void poli_nav_init(void);
void poli_nav_section(int section);       /* opens that section's first window */
void poli_nav_home(void);                 /* rolling mode, starts at Generator I */
void poli_nav_select_turn(int steps);     /* section wrap, or the 12-window roll */
void poli_nav_select_press(void);         /* SEL: the window's one switch, if any */
void poli_nav_knob(int index, int steps); /* K1..K4, index 0..3 */
void poli_nav_tick_ms(uint32_t ms);      /* main-loop time for encoder acceleration */
void poli_nav_octave(int dir);            /* -1, +1, or 0 to reset */
void poli_nav_lang_step(int steps);       /* any nonzero step toggles once */
void poli_nav_fx(void);                  /* FX opens the filter section */
void poli_nav_reserved(int msg);
void poli_nav_xtra_open(int w);           /* Core2-FxSeq: show ADDED window w (0 ARP, 1 SEQ, 2 DRUMS, 3 DELAY, 4 REVERB) */
int poli_nav_xtra(void);                  /* -1 or the ADDED window shown */
void poli_nav_view(poli_nav_view_t *v);
int poli_nav_lang(void);
uint32_t poli_nav_generation(void);
#if POLI_VALREPAINT
uint32_t poli_nav_value_generation(void); /* Parity: a knob moved a value only; the panel repaints, not the screen */
#endif
#if POLI_EGPLOT
int poli_nav_pos(int kind);              /* current 7-bit pos[kind]; 0 if kind is out of range (the EG plots) */
#endif
/* preset hooks: 31 knob bytes (SLOT_CUT..SLOT_DUO, offset to 0..) + 1 switch byte */
void poli_nav_patch_get(uint8_t k[31], uint8_t *sw);
void poli_nav_patch_set(const uint8_t k[31], uint8_t sw);
int poli_nav_patch_max(int i);
/* preset extras, record bytes 49..51 (0 in every older record): LFO restart 0/1, LFO sync 0..9, velocity 0..127.
   poli_nav_patch_set resets them to 0 (OFF / FREE / 0); poli_nav_patch_set_x then applies stored ones. */
enum { NAV_NX = 3 };
void poli_nav_patch_get_x(uint8_t x[NAV_NX]);
void poli_nav_patch_set_x(const uint8_t x[NAV_NX]);
int poli_nav_patch_max_x(int i);
/* MIDI clock sync (state for tests and the screen) */
int poli_nav_sync_valid(void);            /* 1: a tempo has been measured (held after the clock stops) */
uint32_t poli_nav_sync_q8(void);          /* smoothed 24-clock span, ms x 256 */
uint32_t poli_nav_lfo_inc_now(void);      /* the LFO increment last pushed */
uint32_t poli_nav_edits(void);
void poli_nav_touch(void);
void poli_nav_cc(uint8_t cc, uint8_t val); /* absolute CC 0-127 -> slot or switch; unmapped ignored */
void poli_nav_midi(uint8_t status, uint8_t data1, uint8_t data2); /* notes/CCs/pitch-bend/panic; filtered to the receive channel */
int poli_nav_midi_ch(void);               /* receive channel 1..16 */
void poli_nav_set_midi_ch(int ch);        /* invalid values become 1; not a preset edit */
int poli_nav_key_track(void);             /* 0, 62, or 100 */
void poli_nav_set_key_track(int pct);     /* 0 / 62 / 100; other values become 62 */

/* Law 5 (Hans 2026-11-12): audible-range envelope remap and octave cutoff.
 * Formulas, not tables. Used by env_knob and by preset law-4 -> law-5 conversion. */
#define POLI_CUT_TOP 24874          /* knob 127: measured LP -3 dB ~10 kHz at res 0 */
#define POLI_CUT_DEF_POS 59         /* mid-travel; cut 11556, about 800 Hz at res 0 */
#define ENV_KNOB_ATK 0              /* remapped boot / Init attack (old 27 cannot be kept) */
#define ENV_KNOB_DEC 0              /* remapped boot / Init decay  (old 31 cannot be kept) */
#define ENV_KNOB_REL 4              /* remapped boot / Init release (old 39 -> closest old 36) */
int poli_env_old_atk(int n);
int poli_env_old_dec(int n);
int poli_env_old_rel(int n);
int32_t poli_cut_of_knob(int p);
uint8_t poli_law5_env(uint8_t old, int stage); /* 0 attack, 1 decay, 2 release */
uint8_t poli_law5_cut(uint8_t old);
#endif
