/* SPDX-License-Identifier: GPL-3.0-only */
/* Polivoks voice, two oscillators, contours, and the events the parent
 * used to open-code in volt.c. Not a finished schematic-level emulation.
 * Not stock FM. Does not call Altair, Aelita, RMIF, or a shared dispatcher.
 *
 * Include note_inc.c first (12-TET phase increment at an assumed 44100 Hz,
 * 2^32. Not a factory tuning table). Then poli_fcore.c, then this file.
 * If poli_fcore.c was not included yet, it is included below.
 *
 * What the historical voice already took from sources, kept here:
 * Reid, SOS July 2010: two VCOs; VCO 2 detune and VCO 2 into VCO 1;
 * five waveforms; mixer of both oscillators (noise and the external input
 * exist on the instrument and are omitted); 12 dB/oct filter of two
 * op-amps, low-pass and band-pass; each contour can repeat as an AD
 * triangle; LFO with four shapes; glide on VCO 1; duophonic assignment
 * into one filter and one VCA. Hard sync is not implemented.
 * Monosounds duty medians 45%, 20%, 8.8% are measurements, not a factory
 * figure. No PWM. Footage is the MIDI note (no 32'–2' switch). LFO rate
 * knob: 0.084..9.34 Hz on 0..110, then 14.7 Hz..20 kHz on 111..127
 * (assumed 44100). Those limits are labeled, not filled in.
 *
 * Filter: poli_fcore.c. Bass support is computed inside that tick, after the
 * wet states. The voice blends the wet tap toward that supported tap with a
 * gain that defaults to fully supported, then multiplies by the amp envelope.
 * The blend is the only enable. The core tick is not a toggle.
 */
#include "polivoks.h"
/* Envelope step B (Hans, 2026-11-12): both envelope levels and their
 * attack/decay/release steps carry POLI_EGK = 15 fraction bits (Q15), so a
 * stage can move less than one level per sample. Level 26000 is 26000 << 15
 * (max 28000 << 15 = 917,504,000 < 2^31). The two multiplies that use a level
 * shift by 15 + POLI_EGK, so the sound at an integer level is unchanged.
 * polivoks_read reports the old units. Spec: polivoks-envB-spec SPEC-envB.md. */
#define POLI_EGK 15
/* Curved envelope (option V3, Hans 2026-11-12): attack/decay/release are
 * one-pole RC-style steps. poli_atk/dec/rel_* hold Q24 coefficients
 * (1..POLI_EG_CMAX_*), sustain stays a Q15 level. Tables, defaults and the
 * definition of "time": poli_env_rc.h / tools/gen_poli_env_rc.py. */
#include "poli_env_rc.h"
#include "poli_fcore.h"
#ifndef POLI_FCORE_C
#include "poli_fcore.c"
#endif

/* Monosounds medians, not a factory drawing: 45%, 20%, 8.8% of the phase. */
static const uint32_t POLI_DUTY[3] = { 1932735283u, 858993459u, 377957122u };

volatile uint8_t poli_note = 60;
volatile uint8_t poli_note2 = 255;       /* second duophonic note, or 255 */
volatile uint8_t poli_gate;
volatile uint8_t poli_key_on, poli_midi_on;
volatile uint8_t poli_shape;             /* 0 tri, 1 saw, 2..4 rectangles */
volatile uint8_t poli_shape2;
volatile uint8_t poli_link_shape = 1;    /* FM-1 has one shape encoder: both VCOs */
volatile int8_t poli_oct1, poli_oct2;    /* -2..+2, 8' at 0 */
volatile uint8_t poli_bp;                /* 0 low-pass, 1 band-pass */
volatile uint8_t poli_rep_f, poli_rep_a; /* repeating AD on filter, on VCA */
volatile uint8_t poli_glide;
volatile uint8_t poli_duo = 1;           /* 1: two held keys give VCO2 the high key. 0, one key, or MIDI: VCO2 plays the same note as VCO1 (unison, then DETUNE). */
volatile uint8_t poli_lfo_shape;         /* 0 tri, 1 square, 2 noise, 3 S&H */

static uint32_t poli_ph1, poli_ph2, poli_lfo_ph, poli_noise = 0xA5A5u;
static int32_t poli_lp, poli_bp_st, poli_eg_f, poli_eg_a, poli_sh, poli_glide_inc;
static uint8_t poli_up_f, poli_up_a, poli_fell_f, poli_fell_a;
int32_t poli_cut = 11556;   /* panel cutoff at knob 59 on the 100 Hz..10 kHz map */
int32_t poli_res = 6000;    /* below the bass-support knee until the knob rises */
int32_t poli_drv = 14000;   /* input scale into the filter, not a post clip */
/* Manual (eng-base specs, from the passport): common tune at least ±12%
 * (±1 tone); generator II detune at least ±25% (±2 tones). Linear in the
 * control word. Not a measured pot taper.
 * 3932/32768 = 0.1200. detune >> 2 at ±32768 is ±25%. */
static int32_t poli_tune;
static int32_t poli_bend;              /* MIDI pitch bend, 1/4096 semitone, 0 centre, +-8192 = +-2 st */
static int32_t poli_detune;
static int32_t poli_fm;                 /* VCO2→VCO1 depth. 0 at boot, as Рис.1 (depth knobs at 0). Was 7000. */
static int32_t poli_lv1 = 32768;
static int32_t poli_lv2 = 32768;
static int32_t poli_lnz;                /* noise mixer. 0 = previous voice */
static int32_t poli_lex;                /* external mixer. jack is UNKNOWN */
static const int32_t *poli_ext_buf;
static uint32_t poli_lfo_inc = 486977u; /* ~5 Hz at the assumed 44100. Hz law unread. */
static int32_t poli_mod_pitch;          /* generator I LFO depth. 0 at boot, as Рис.1. Was 2048. */
static int32_t poli_mod_pitch2;         /* generator II, separate amount. Same default. */
static uint8_t poli_keyhold;            /* ВКЛ. switch. Deliberate hold, not release-rate 0. */
static uint8_t poli_held_note;          /* last note kept gated while the switch is on */
static int32_t poli_mod_cut = 1024;     /* == lfo >> 5 */
static int32_t poli_mod_amp;            /* 0: no tremolo, silence stays exact 0 */
static int32_t poli_eg_amt = 8192;      /* (eg * 8192) >> 15 == eg >> 2. The depth in use: knob x velocity scale. */
/* Velocity -> filter contour and amp level (same SLOT_VEL amount; VelAmp-Bend, 14 Nov 2026, from the
   SMK-37 port). Worked out at note-on and on a contour turn in the main program, never in the sample loop:
   vs = 32768 - amt*(127-vel)*32768/127^2. poli_eg_amt = knob * vs >> 15 (filter contour, as before).
   Amp level: vs is folded with poli_out_lvl once per render block into amp_g = (vs * out_lvl) >> 15;
   the sample loop keeps its single output multiply, skipped when amp_g is 32768. vel 127 (and every
   keybed note) or amt 0 gives vs = 32768: the exact old voice. Latched only on a new gate. */
static int32_t poli_eg_knob = 8192;     /* the contour knob's amount */
static int32_t poli_vel_amt;            /* 0..127, 0 = velocity off */
static int32_t poli_vel_scale = 32768;  /* of the sounding note: contour and amp level */
static uint8_t poli_vel_next = 127u;    /* velocity of the note that starts the gate next */
static uint8_t poli_lfo_restart;        /* KEY RESTART switch */
static volatile uint8_t poli_lfo_rst;   /* request: restart the LFO cycle at the next block */
static int32_t poli_kt_pct = 62;        /* 0 / 62 / 100 % keyboard tracking, pivot MIDI 60 */
static int32_t poli_atk_f = POLI_EG_CA0, poli_dec_f = POLI_EG_CD0, poli_sus_f = 14000<<POLI_EGK, poli_rel_f = POLI_EG_CR0;
static int32_t poli_atk_a = POLI_EG_CA0, poli_dec_a = POLI_EG_CD0, poli_sus_a = 14000<<POLI_EGK, poli_rel_a = POLI_EG_CR0;
static int32_t poli_glide_rate = 8000;
static int32_t poli_out_lvl = 32768;    /* unity. Not the 0.025 V / 0.25 V jacks. */
static uint8_t poli_bass_on = 1;         /* default ON: blend rests on the supported tap */
static int32_t poli_bass_g = 32768;      /* 0 = wet, 32768 = supported. Slews. */
static uint8_t poli_arm_block;           /* panic: keys already down are not a new note */
static int8_t poli_kb_oct;               /* keybed transpose -3..+2. Base note stays 65 (lowest key at -3 = note 29). */
static poli_fcore_t poli_fc;

static int32_t poli_clamp(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint32_t poli_mul_pm(uint32_t inc, int32_t det, int32_t span)
{
    int32_t m = 32768 + (int32_t)(((int64_t)det * span) >> 15);
    if (m < 4096) m = 4096;
    if (m > 65536) m = 65536;
    return (uint32_t)(((uint64_t)inc * (uint32_t)m) >> 15);
}

static uint32_t poli_note_inc_oct(int32_t note, int oct)
{
    int32_t n = note + oct * 12;
    if (n < 0) n = 0;
    if (n > 126) n = 126;
    return NOTE_INC[n];
}

void polivoks_init(void)
{
    poli_note = 60;
    poli_note2 = 255;
    poli_gate = 0;
    poli_key_on = 0;
    poli_midi_on = 0;
    poli_shape = 0;
    poli_shape2 = 0;
    poli_link_shape = 1;
    poli_oct1 = 0;
    poli_oct2 = 0;
    poli_bp = 0;
    poli_rep_f = 0;
    poli_rep_a = 0;
    poli_glide = 0;
    poli_duo = 1;
    poli_lfo_shape = 0;
    poli_tune = 0;
    poli_bend = 0;
    poli_detune = 0;
    poli_fm = 0;
    poli_lv1 = 32768;
    poli_lv2 = 32768;
    poli_lnz = 0;
    poli_lex = 0;
    poli_ext_buf = 0;
    poli_lfo_inc = 486977u;
    poli_mod_pitch = 0;
    poli_mod_pitch2 = 0;
    poli_keyhold = 0;
    poli_held_note = 0;
    poli_mod_cut = 1024;
    poli_mod_amp = 0;
    poli_eg_amt = 8192;
    poli_eg_knob = 8192;
    poli_vel_amt = 0;
    poli_vel_scale = 32768;
    poli_vel_next = 127u;
    poli_lfo_restart = 0;
    poli_lfo_rst = 0;
    poli_kt_pct = 62;
    poli_atk_f = POLI_EG_CA0;
    poli_dec_f = POLI_EG_CD0;
    poli_sus_f = 14000<<POLI_EGK;
    poli_rel_f = POLI_EG_CR0;
    poli_atk_a = POLI_EG_CA0;
    poli_dec_a = POLI_EG_CD0;
    poli_sus_a = 14000<<POLI_EGK;
    poli_rel_a = POLI_EG_CR0;
    poli_glide_rate = 8000;
    poli_out_lvl = 32768;
    poli_ph1 = 0;
    poli_ph2 = 0;
    poli_lfo_ph = 0;
    poli_noise = 0xA5A5u;
    poli_lp = 0;
    poli_bp_st = 0;
    poli_eg_f = 0;
    poli_eg_a = 0;
    poli_sh = 0;
    poli_glide_inc = 0;
    poli_up_f = 0;
    poli_up_a = 0;
    poli_fell_f = 0;
    poli_fell_a = 0;
    poli_cut = 11556;
    poli_res = 6000;
    poli_drv = 14000;
    poli_bass_on = 1;
    poli_bass_g = 32768;
    poli_arm_block = 0;
    poli_kb_oct = 0;
    poli_fcore_reset(&poli_fc);
}

void poli_suspend(void)
{
    poli_fcore_quiet(&poli_fc);
    poli_eg_a = 0;
    poli_eg_f = 0;
    poli_gate = 0;
    poli_key_on = 0;
    poli_midi_on = 0;
    poli_lp = 0;
    poli_bp_st = 0;
    poli_fell_f = 0;
    poli_fell_a = 0;
    poli_up_f = 0;
    poli_up_a = 0;
    poli_held_note = 0;
}

static void poli_eg_amt_push(void)
{
    poli_eg_amt = (int32_t)(((int64_t)poli_eg_knob * poli_vel_scale) >> 15);
}

/* A new note: the gate goes from off to on (first key, or the first MIDI note-on, after all were up).
   Legato notes (another key or note-on while the gate is held, key hold, duo second key) are not new. */
static void poli_new_note(void)
{
    int32_t v = poli_vel_next;
    if (v < 1) v = 1;
    if (v > 127) v = 127;
    poli_vel_scale = 32768 - (int32_t)(((int64_t)poli_vel_amt * (127 - v) * 32768) / (127 * 127));
    poli_eg_amt_push();
    if (poli_lfo_restart) poli_lfo_rst = 1u;
}

static void poli_recompute_gate(void)
{
    uint8_t was = poli_gate;
    /* The panic block forces key_on off before this runs, so a key that is
       still down does not arm ВКЛ. A MIDI note-on is a new note and may. */
    if ((poli_key_on || poli_midi_on) && poli_keyhold)
        poli_held_note = 1u;
    if (poli_key_on || poli_midi_on)
        poli_gate = 1u;
    else if (poli_keyhold && poli_held_note)
        poli_gate = 1u;
    else {
        poli_held_note = 0u;
        poli_gate = 0u;
    }
    if (!was && poli_gate) poli_new_note();
}

void polivoks_panic(void)
{
    /* All-sound-off. Clear the repeat latches so the next render cannot
       restart them. A key that is still held is not a new note. */
    poli_rep_f = 0;
    poli_rep_a = 0;
    poli_held_note = 0;
    poli_arm_block = 1;
    poli_bend = 0;                      /* pitch back to centre */
    poli_suspend();
}

void polivoks_midi(uint8_t status, uint8_t data1, uint8_t data2)
{
    uint8_t st = (uint8_t)(status & 0xF0u);
    /* Standalone has no other instrument. A program change silences the tail
       and does not select anything else. CC 120 is all-sound-off (panic):
       it clears repeat latches. CC 123 is all-notes-off: the MIDI gate
       releases; a held panel key remains; repeat latches stay. Both, and panic, put pitch bend
       back to centre. 0xE0 is 14-bit pitch bend, +-2 semitones on both generators. */
    if (st == 0xC0u) {
        poli_suspend();
    } else if (st == 0xB0u && (data1 == 120u || data1 == 123u)) {
        if (data1 == 120u)
            polivoks_panic();
        else {
            poli_midi_on = 0u;
            poli_bend = 0;
            poli_recompute_gate();
        }
    } else if (st == 0x90u && data2) {
        poli_note = data1 > 126u ? 126u : data1; /* merge: MIDI 127 plays 126 (keybed rule), not middle C */
        poli_vel_next = data2;
        poli_midi_on = 1u;
        poli_recompute_gate();
    } else if (st == 0x80u || (st == 0x90u && data2 == 0u)) {
        poli_midi_on = 0u;
        poli_recompute_gate();
    } else if (st == 0xE0u) {
        /* 14-bit pitch bend, centre 8192 = 0. data1 = LSB, data2 = MSB. */
        int32_t v = ((int32_t)(data2 & 0x7Fu) << 7) | (int32_t)(data1 & 0x7Fu);
        polivoks_set_pitch_bend(v - 8192);
    }
}

void polivoks_set_shape2(uint8_t shape)
{
    poli_link_shape = 0;
    poli_shape2 = shape;
}

void polivoks_set_link_shapes(uint8_t on) { poli_link_shape = on ? 1u : 0u; }

void polivoks_set_footage(int vco, int oct)
{
    if (oct < -2) oct = -2;
    if (oct > 2) oct = 2;
    if (vco == 0) poli_oct1 = (int8_t)oct;
    else poli_oct2 = (int8_t)oct;
}

void polivoks_set_tune(int32_t tune) { poli_tune = poli_clamp(tune, -32768, 32767); }
void polivoks_set_detune(int32_t detune) { poli_detune = poli_clamp(detune, -32768, 32767); }
void polivoks_set_pitch_bend(int32_t units) { poli_bend = poli_clamp(units, -8192, 8192); }
int32_t polivoks_pitch_bend(void) { return poli_bend; }

/* Q15 frequency multiplier for a bend in 1/4096-semitone units (from the SMK-37 port).
 * 65-entry table, 6.25-cent steps from -200 to +200 cents, linear interpolation.
 * Once per block; no 64-bit divide, no float, nothing in the sample loop.
 * Centre entry is exactly 32768. */
static const uint16_t POLI_BEND_Q15[65] = {
    29193, 29299, 29405, 29511, 29618, 29725, 29832, 29940,
    30048, 30157, 30266, 30376, 30485, 30596, 30706, 30817,
    30929, 31041, 31153, 31266, 31379, 31492, 31606, 31720,
    31835, 31950, 32066, 32182, 32298, 32415, 32532, 32650,
    32768, 32887, 33005, 33125, 33245, 33365, 33486, 33607,
    33728, 33850, 33973, 34095, 34219, 34343, 34467, 34591,
    34716, 34842, 34968, 35095, 35221, 35349, 35477, 35605,
    35734, 35863, 35993, 36123, 36254, 36385, 36516, 36648,
    36781
};
static uint32_t poli_bend_mul(int32_t u)
{
    int32_t idx, frac, a, b;
    u = poli_clamp(u, -8192, 8192);
    idx = (u + 8192) >> 8;              /* 0..64 */
    frac = (u + 8192) & 255;
    a = (int32_t)POLI_BEND_Q15[idx];
    if (idx >= 64)
        return (uint32_t)a;
    b = (int32_t)POLI_BEND_Q15[idx + 1];
    return (uint32_t)(a + (((b - a) * frac) >> 8));
}
void polivoks_set_fm_depth(int32_t depth) { poli_fm = poli_clamp(depth, 0, 32767); }

void polivoks_set_level(int which, int32_t level)
{
    level = poli_clamp(level, 0, 32768);
    if (which == 0) poli_lv1 = level;
    else if (which == 1) poli_lv2 = level;
    else if (which == 2) poli_lnz = level;
    else if (which == 3) poli_lex = level;
}

void polivoks_set_ext_buf(const int32_t *mono) { poli_ext_buf = mono; }
void polivoks_set_lfo_inc(uint32_t inc) { poli_lfo_inc = inc; }
void polivoks_set_mod_pitch(int32_t depth)
{
    /* The old single amount. Tests that silence pitch modulation still silence both. */
    depth = poli_clamp(depth, 0, 32767);
    poli_mod_pitch = depth;
    poli_mod_pitch2 = depth;
}
void polivoks_set_mod_pitch_vco(int vco, int32_t depth)
{
    depth = poli_clamp(depth, 0, 32767);
    if (vco == 0) poli_mod_pitch = depth;
    else poli_mod_pitch2 = depth;
}
int32_t polivoks_mod_pitch_vco(int vco) { return vco == 0 ? poli_mod_pitch : poli_mod_pitch2; }
void polivoks_set_keyhold(uint8_t on)
{
    poli_keyhold = on ? 1u : 0u;
    if (!poli_keyhold)
        poli_held_note = 0;
    poli_recompute_gate();
}
uint8_t polivoks_keyhold(void) { return poli_keyhold; }
void polivoks_set_mod_cut(int32_t depth) { poli_mod_cut = poli_clamp(depth, 0, 32767); }
void polivoks_set_mod_amp(int32_t depth) { poli_mod_amp = poli_clamp(depth, 0, 32767); }
void polivoks_set_contour_amount(int32_t amount) { poli_eg_knob = poli_clamp(amount, 0, 32768); poli_eg_amt_push(); }
void polivoks_set_vel_amount(int amt)
{
    poli_vel_amt = poli_clamp(amt, 0, 127);
    if (!poli_vel_amt) { poli_vel_scale = 32768; poli_eg_amt_push(); }   /* off: full contour and amp level at once */
}
int32_t polivoks_contour_eff(void) { return poli_eg_amt; }
void polivoks_set_lfo_restart(uint8_t on) { poli_lfo_restart = on ? 1u : 0u; }
void polivoks_lfo_reset(void) { poli_lfo_rst = 1u; }
void polivoks_set_key_track(int pct)
{
    if (pct >= 100) poli_kt_pct = 100;
    else if (pct <= 0) poli_kt_pct = 0;
    else poli_kt_pct = 62;
}
int polivoks_key_track(void) { return (int)poli_kt_pct; }

void polivoks_set_adsr(int which, int32_t atk, int32_t dec, int32_t sus, int32_t rel)
{
    /* Old units (linear levels per sample, test/host API). Mapped to the
       coefficient with the same full-sweep time for slow rates (c ~ rate *
       K / 26000), capped at the 8000-level step. 0 stays 0 (no attack /
       no decay), release is at least 1. */
    atk = poli_clamp(atk, 0, 8000);
    dec = poli_clamp(dec, 0, 8000);
    rel = poli_clamp(rel, 1, 8000);
    sus = poli_clamp(sus, 0, 28000);
    atk = poli_clamp(atk * POLI_EG_LIN_A, 0, POLI_EG_CMAX_A);
    dec = poli_clamp(dec * POLI_EG_LIN_DR, 0, POLI_EG_CMAX_DR);
    rel = poli_clamp(rel * POLI_EG_LIN_DR, 1, POLI_EG_CMAX_DR);
    sus <<= POLI_EGK;
    if (which == 0) {
        poli_atk_f = atk; poli_dec_f = dec; poli_sus_f = sus; poli_rel_f = rel;
    } else {
        poli_atk_a = atk; poli_dec_a = dec; poli_sus_a = sus; poli_rel_a = rel;
    }
}

/* Knob path (poli_nav.c ENV_C_ATK / ENV_C_DR): atk/dec/rel are Q24
   coefficients; sustain is a plain level 0..28000. */
void polivoks_set_adsr_q(int which, int32_t atk, int32_t dec, int32_t sus, int32_t rel)
{
    atk = poli_clamp(atk, 1, POLI_EG_CMAX_A);
    dec = poli_clamp(dec, 1, POLI_EG_CMAX_DR);
    rel = poli_clamp(rel, 1, POLI_EG_CMAX_DR);
    sus = poli_clamp(sus, 0, 28000) << POLI_EGK;
    if (which == 0) {
        poli_atk_f = atk; poli_dec_f = dec; poli_sus_f = sus; poli_rel_f = rel;
    } else {
        poli_atk_a = atk; poli_dec_a = dec; poli_sus_a = sus; poli_rel_a = rel;
    }
}

void polivoks_set_glide_rate(int32_t step) { poli_glide_rate = poli_clamp(step, 0, 200000); }
void polivoks_set_duo(uint8_t on) { poli_duo = on ? 1u : 0u; }
void polivoks_set_output(int32_t level) { poli_out_lvl = poli_clamp(level, 0, 32768); }

void polivoks_set_bass_support(uint8_t on) { poli_bass_on = on ? 1u : 0u; }
uint8_t polivoks_bass_support(void) { return poli_bass_on; }
int32_t polivoks_bass_gain(void) { return poli_bass_g; }

void polivoks_set_kb_oct(int oct)
{
    if (oct < -3) oct = -3;            /* one octave lower than before (Hans, 2026-11-12) */
    if (oct > 2) oct = 2;
    poli_kb_oct = (int8_t)oct;
}
int polivoks_kb_oct(void) { return poli_kb_oct; }
uint8_t polivoks_link_shapes(void) { return poli_link_shape; }
uint8_t polivoks_shape2(void) { return poli_shape2; }
uint8_t polivoks_duo_on(void) { return poli_duo; }
int polivoks_footage(int vco) { return vco == 0 ? poli_oct1 : poli_oct2; }

uint32_t polivoks_lfo_phase(void) { return poli_lfo_ph; }

void polivoks_panel(uint32_t keys, int shape_enc, int bp_edge, int rep_f_edge,
                    int rep_a_edge, int glide_edge, int lfo_edge,
                    int32_t cut_enc, int32_t res_enc, int32_t drv_enc)
{
    uint32_t k, nlow = 255u, nhigh = 255u;
    if (shape_enc) {
        int32_t sh = (int32_t)poli_shape + (shape_enc > 0 ? 1 : -1);
        if (sh < 0) sh = 4;
        if (sh > 4) sh = 0;
        poli_shape = (uint8_t)sh;
    }
    if (bp_edge) poli_bp = (uint8_t)(poli_bp ^ 1u);
    if (rep_f_edge) poli_rep_f = (uint8_t)(poli_rep_f ^ 1u);
    if (rep_a_edge) poli_rep_a = (uint8_t)(poli_rep_a ^ 1u);
    if (glide_edge) poli_glide = (uint8_t)(poli_glide ^ 1u);
    if (lfo_edge) {
        poli_lfo_shape = (uint8_t)(poli_lfo_shape + 1u);
        if (poli_lfo_shape > 3u) poli_lfo_shape = 0u;
    }
    if (cut_enc) {
        int32_t v = poli_cut + cut_enc * 800;
        if (v < 0) v = 0;
        if (v > 30000) v = 30000;
        poli_cut = v;
    }
    if (res_enc) {
        int32_t v = poli_res + res_enc * 800;
        if (v < 0) v = 0;
        if (v > 30000) v = 30000;
        poli_res = v;
    }
    if (drv_enc) {
        int32_t v = poli_drv + drv_enc * 800;
        if (v < 2000) v = 2000;
        if (v > 30000) v = 30000;
        poli_drv = v;
    }
    /* Panic blocks the keybed until every key is up. Releasing the mask is
       not itself a note. A MIDI note-on during the block is a new note and
       is left in poli_midi_on by polivoks_midi. */
    if (poli_arm_block) {
        if (keys == 0u)
            poli_arm_block = 0u;
    }
    if (poli_arm_block) {
        poli_key_on = 0u;
        poli_note2 = 255u;
    } else {
        for (k = 0; k < 27u; k++) {
            if (keys & (1u << k)) {
                int32_t note = 65 + (int32_t)k + (int32_t)poli_kb_oct * 12;
                if (note < 0) note = 0;
                if (note > 126) note = 126;
                if (nlow == 255u) nlow = (uint32_t)note;
                nhigh = (uint32_t)note;
            }
        }
        if (nlow != 255u) {
            if (!poli_key_on) poli_vel_next = 127u;   /* the keybed has no velocity: full, the old depth */
            poli_note = (uint8_t)nlow;
            poli_note2 = (nhigh != nlow) ? (uint8_t)nhigh : 255u;
            poli_key_on = 1u;
        } else {
            poli_key_on = 0u;
            poli_note2 = 255u;
        }
    }
    poli_recompute_gate();
}

void polivoks_read(int32_t *a2, int32_t *a3, int32_t *bass,
                   int32_t *cut, int32_t *res, int32_t *drv,
                   uint32_t *ph1, uint32_t *ph2, int32_t *eg_amp, int32_t *eg_filt)
{
    if (a2) *a2 = poli_fc.a2;
    if (a3) *a3 = poli_fc.a3;
    if (bass) *bass = poli_fc.bass;
    if (cut) *cut = poli_fc.cut;
    if (res) *res = poli_fc.res;
    if (drv) *drv = poli_fc.drv;
    if (ph1) *ph1 = poli_ph1;
    if (ph2) *ph2 = poli_ph2;
    if (eg_amp) *eg_amp = poli_eg_a >> POLI_EGK;
    if (eg_filt) *eg_filt = poli_eg_f >> POLI_EGK;
}

static int32_t poli_clip(int32_t x)
{
    if (x > 16000) return 16000;
    if (x < -16000) return -16000;
    return x;
}

__attribute__((noinline)) static int32_t poli_wave(uint32_t ph, uint32_t shape)
{
    int32_t saw = (int32_t)(ph >> 17) - 16384;
    if (shape == 0u) {
        int32_t t = saw < 0 ? -saw : saw;
        return (t << 1) - 16384;
    }
    if (shape == 1u)
        return saw;
    return ph < POLI_DUTY[(shape - 2u) > 2u ? 2u : (shape - 2u)] ? 12000 : -12000;
}

static int32_t poli_eg(int32_t env, uint8_t repeat, uint8_t *up, uint8_t *fell,
                       int32_t atk, int32_t dec, int32_t sus, int32_t rel)
{
    /* Repeat is the panel AD triangle, not a stuck gate. Rates 180/90 are
       the previous stand-in: У4 does not give a repeat time. The triangle
       runs only while the gate is held. Otherwise this is the ADSR: peak
       26000, then decay to the sustain knob. Default times (1.4 ms attack,
       5.4 ms decay, 9 ms release, sustain 14000) are not timed from the
       sheet. A stored release of 0 is not a hold: the release coefficient is
       at least 1 and aims below 0, so the level reaches exact 0. That is
       a model (old API: about 0.58 s from the 26000 peak at the assumed
       44100 Hz; the curved knob path reaches 10.0 s for the full sweep),
       not a factory RC time. The real Послезвучание pot's short end is a
       short release; this stand-in keeps 0 at the slow end of the same
       curve and only removes the infinite hold. Panic still cuts at once. */
    /* Repeat cycles only while the gate is held. Key-off takes the release
       path below even if the panel latch is still set, so a latched repeat
       does not keep sounding after the key. The latch itself stays, and the
       next held note cycles again. Panic clears the latch separately. */
    if (repeat && poli_gate) {
        if (*up) {
            env += 180 << POLI_EGK;
            if (env >= (28000 << POLI_EGK)) { env = 28000 << POLI_EGK; *up = 0u; }
        } else {
            env -= 90 << POLI_EGK;
            if (env <= 0) { env = 0; *up = 1u; }
        }
        *fell = 1u;
        return env;
    }
    /* Curved envelope (V3): one-pole RC-style step toward a target,
       env += (tgt - env) * c / 2^24, c = Q24 coefficient. atk/dec/rel are
       stored inside 1..POLI_EG_CMAX_A / POLI_EG_CMAX_DR (largest step 8000
       levels, as before) and sus inside 0..28000 << POLI_EGK by
       polivoks_set_adsr(_q) and polivoks_init. No other writer.
       Attack charges toward 34000 like an RC toward a higher rail and stops
       at the 26000 peak (only the attack has a target above 28000, so the
       clip never touches a sustain up to 28000 or a release). Decay and the
       move toward a changed sustain glide to sus. Release aims at -26 so it
       crosses 0 in finite time and the clamp below stops it at exact 0: no
       stall above 0 and no -1 floor. New note from any level: attack starts
       from that level (no jump). |tgt - env| < 2^31 and c < 2^24, so the
       product fits int64 and the step never passes the target. */
    {
        int32_t tgt, c;
        if (poli_gate) {
            if (!*fell) { tgt = POLI_EG_OVER << POLI_EGK; c = atk; }
            else { tgt = sus; c = dec; }
        } else { *fell = 0u; tgt = -(POLI_EG_REL_T << POLI_EGK); c = rel; }
        env += (int32_t)(((int64_t)(tgt - env) * c) >> 24);
        if (tgt > (28000 << POLI_EGK) && env >= (26000 << POLI_EGK)) { env = 26000 << POLI_EGK; *fell = 1u; }
    }
    if (env < 0) env = 0;
    return env;
}

/* Out of polivoks_render so the sample loop keeps its register set. Once per block. */
static __attribute__((noinline)) void poli_apply_bend(uint32_t *b1, uint32_t *b2)
{
    uint32_t bm;
    if (!poli_bend)
        return;                         /* centre: bit-identical */
    bm = poli_bend_mul(poli_bend);
    *b1 = (uint32_t)(((uint64_t)*b1 * bm) >> 15);
    *b2 = (uint32_t)(((uint64_t)*b2 * bm) >> 15);
}

__attribute__((noinline)) void polivoks_render(int32_t *out, uint32_t n)
{
    uint32_t i, n1 = poli_note, n2, shape = poli_shape, shape2;
    uint32_t ph1 = poli_ph1, ph2 = poli_ph2, lp = poli_lfo_ph;
    uint32_t nz = poli_noise;
    int32_t egf = poli_eg_f, ega = poli_eg_a;
    int32_t sh = poli_sh, ginc = poli_glide_inc;
    int32_t lfo, s1, s2, x, y, scale, kt_c;
    int oct1 = poli_oct1, oct2 = poli_oct2;
    uint32_t inc1, inc2, step1, base1, base2;
    uint8_t up_f = poli_up_f, up_a = poli_up_a;
    uint8_t fell_f = poli_fell_f, fell_a = poli_fell_a;
    if (n1 > 126u) n1 = 126u; /* merge: clip, never middle C */
    n2 = poli_note2;
    if (shape > 4u) shape = 1u;
    shape2 = poli_link_shape ? shape : poli_shape2;
    if (shape2 > 4u) shape2 = 1u;
    if (oct1 < -2) oct1 = -2;
    if (oct1 > 2) oct1 = 2;
    if (oct2 < -2) oct2 = -2;
    if (oct2 > 2) oct2 = 2;
    /* Note, footage, tune, detune, and duo are latched for this call, the
       same moment the note number is latched. LFO, glide, and generator
       cross-FM stay inside the sample loop. */
    {
        int32_t tune = poli_tune;
        int32_t detune = poli_detune;
        /* One key or MIDI: both generators on the same note (1-ГОЛ. unison).
           Two held keys with duo on: VCO2 takes the high key. */
        int32_t n2n = (poli_duo && n2 <= 126u) ? (int32_t)n2 : (int32_t)n1;
        base1 = poli_mul_pm(poli_note_inc_oct((int32_t)n1, oct1), tune, 3932);
        base2 = poli_mul_pm(poli_note_inc_oct(n2n, oct2), tune, 3932);
        base2 = poli_mul_pm(base2, detune, 8192);
        /* MIDI pitch bend, once per block, both generators. */
        poli_apply_bend(&base1, &base2);
    }
    /* Key tracking is a per-block offset, not an inner-loop product.
       62% is (note-60)*212 cut units per semitone (4096 units = 1 octave of a).
       100% is *341. Pivot is MIDI C4 = 60. Duo uses n1 (the low key). */
    {
        int32_t dn = (int32_t)n1 - 60;
        if (poli_kt_pct >= 100) kt_c = dn * 341;
        else if (poli_kt_pct <= 0) kt_c = 0;
        else kt_c = dn * 212;
    }
    /* LFO restart (KEY RESTART on a new note, or MIDI Start while synced): once per block, before the
       loop, at the block the new note starts in. Phase 0 = start of the cycle (triangle at its top,
       square high). S&H takes a new value. Noise ignores the phase. */
    if (poli_lfo_rst) {
        poli_lfo_rst = 0u;
        lp = 0u;
        nz = nz * 1664525u + 1013904223u;
        sh = (int32_t)(nz >> 16) - 32768;
    }
    /* Amp VCA: fold velocity into the output-level scale once per block (depth 0),
       so the sample loop has no extra velocity work. */
    int32_t amp_g = (int32_t)(((int64_t)poli_vel_scale * poli_out_lvl) >> 15);
    for (i = 0; i < n; i++) {
        uint32_t lfo_inc = poli_lfo_inc;
        int32_t ns, ext, lfo_c, eg_c;
        lp += lfo_inc;
        nz = nz * 1664525u + 1013904223u;
        if (lp < lfo_inc)
            sh = (int32_t)(nz >> 16) - 32768;
        if (poli_lfo_shape == 0u) {
            int32_t t = (int32_t)(lp >> 16) - 32768;
            lfo = t < 0 ? -t : t;
            lfo = (lfo << 1) - 32768;
        } else if (poli_lfo_shape == 1u)
            lfo = lp < 0x80000000u ? 20000 : -20000;
        else if (poli_lfo_shape == 2u)
            lfo = (int32_t)(nz >> 17) - 16384;
        else
            lfo = sh >> 1;
        egf = poli_eg(egf, poli_rep_f, &up_f, &fell_f, poli_atk_f, poli_dec_f, poli_sus_f, poli_rel_f);
        ega = poli_eg(ega, poli_rep_a, &up_a, &fell_a, poli_atk_a, poli_dec_a, poli_sus_a, poli_rel_a);
        inc1 = base1;
        inc2 = base2;
        scale = 32768 + (int32_t)(((int64_t)lfo * poli_mod_pitch) >> 15);
        if (scale < 16384) scale = 16384;
        if (scale > 49152) scale = 49152;
        inc1 = (uint32_t)(((uint64_t)inc1 * (uint32_t)scale) >> 15);
        scale = 32768 + (int32_t)(((int64_t)lfo * poli_mod_pitch2) >> 15);
        if (scale < 16384) scale = 16384;
        if (scale > 49152) scale = 49152;
        inc2 = (uint32_t)(((uint64_t)inc2 * (uint32_t)scale) >> 15);
        if (poli_glide) {
            int32_t gr = poli_glide_rate;
            if (gr < 0) gr = 0;
            if (ginc <= 0) ginc = (int32_t)inc1;
            if ((uint32_t)ginc < inc1) ginc += gr;
            else if ((uint32_t)ginc > inc1) ginc -= gr;
            inc1 = (uint32_t)ginc;
        } else
            ginc = (int32_t)inc1;
        s2 = poli_wave(ph2, shape2);
        ph2 += inc2;
        scale = 32768 + (int32_t)(((int64_t)s2 * poli_fm) >> 15);   /* VCO 2 -> VCO 1 */
        if (scale < 8192) scale = 8192;
        if (scale > 49152) scale = 49152;
        step1 = (uint32_t)(((uint64_t)inc1 * (uint32_t)scale) >> 15);
        s1 = poli_wave(ph1, shape);
        ph1 += step1;
        ns = (int32_t)(nz >> 17) - 16384;
        ext = poli_ext_buf ? poli_ext_buf[i] : 0;
        x = (int32_t)(((int64_t)s1 * poli_lv1) >> 15)
          + (int32_t)(((int64_t)s2 * poli_lv2) >> 15)
          + (int32_t)(((int64_t)ns * poli_lnz) >> 15)
          + (int32_t)(((int64_t)ext * poli_lex) >> 15);
        if (x > 14000) x = 14000 + ((x - 14000) >> 2);          /* mixer can slam the filter */
        if (x < -14000) x = -14000 + ((x + 14000) >> 2);
        x = poli_clip(x);
        {
            int32_t cut, lpo, bpo;
            lfo_c = (int32_t)(((int64_t)lfo * poli_mod_cut) >> 15);
            eg_c = (int32_t)(((int64_t)egf * poli_eg_amt) >> (15 + POLI_EGK));
            cut = poli_cut + eg_c + lfo_c + kt_c;
            if (cut < 0) cut = 0;
            if (cut > 32767) cut = 32767;
            /* Bass is already in lpo/bpo. The amp envelope is the multiply below. */
            /* x is within ±16000, cut within 0..32767, and the firmware
               writes of resonance and drive stay inside 0..30000. */
            poli_fcore_run(&poli_fc, x, cut, poli_res, poli_drv, &lpo, &bpo);
            {
                /* Wet is the filter state. Supported is the core's bass
                   sidechain, already blended and clamped in poli_fcore_tick.
                   Gain 32768 selects that tap exactly (the ON default).
                   Gain 0 selects the wet tap. In between, a convex mix.
                   The follower inside the core keeps running either way, so
                   re-enabling does not restart its phase. No extra octave. */
                int32_t y_wet = poli_bp ? poli_fc.a2 : poli_fc.a3;
                int32_t y_sup = poli_bp ? bpo : lpo;
                /* One arithmetic shift of the signed error. The previous
                   law used sign(err) * max(1, |err| >> 5). Those differ
                   whenever |err| is not a multiple of 32, because a
                   negative arithmetic shift rounds away from zero and a
                   positive shift that lands on 0 snaps to the target.
                   A full-scale step is still 1024, and both endpoints
                   are still reached. The filter's own integrator slew
                   is not this gain. */
                int32_t tgt = poli_bass_on ? 32768 : 0;
                int32_t err = tgt - poli_bass_g;
                int32_t step = err >> 5;
                if (step == 0)
                    poli_bass_g = tgt;
                else
                    poli_bass_g += step;
                if (poli_bass_g >= 32768)
                    y = y_sup;
                else if (poli_bass_g <= 0)
                    y = y_wet;
                else
                    y = y_wet + (int32_t)(((int64_t)(y_sup - y_wet) * poli_bass_g) >> 15);
            }
        }
        y = (int32_t)(((int64_t)y * ega) >> (15 + POLI_EGK));
        if (poli_mod_amp) {
            int32_t vm = 32768 + (int32_t)(((int64_t)lfo * poli_mod_amp) >> 15);
            if (vm < 0) vm = 0;
            if (vm > 65536) vm = 65536;
            y = (int32_t)(((int64_t)y * vm) >> 15);
        }
        /* Combined vs * out_lvl, computed once per block. Skipped at 32768. */
        if (amp_g != 32768)
            y = (int32_t)(((int64_t)y * amp_g) >> 15);
        if (y > 30000) y = 30000;
        if (y < -30000) y = -30000;
        out[2u * i] += y;
        out[2u * i + 1u] += y;
    }
    poli_ph1 = ph1;
    poli_ph2 = ph2;
    poli_lfo_ph = lp;
    poli_noise = nz;
    poli_eg_f = egf;
    poli_eg_a = ega;
    poli_lp = poli_fc.a3;
    poli_bp_st = poli_fc.a2;
    poli_sh = sh;
    poli_glide_inc = ginc;
    poli_up_f = up_f;
    poli_up_a = up_a;
    poli_fell_f = fell_f;
    poli_fell_a = fell_a;
}
