/* SPDX-License-Identifier: GPL-3.0-only */
/* Polivoks voice / control / events. Drop-in for the unity file described
 * in sv_integrate.h. Include order in the parent, unchanged except that the
 * Polivoks branch may call polivoks_midi and polivoks_panel:
 *
 *   #include "note_inc.c"
 *   #include "poli_fcore.c"
 *   #include "polivoks_limited.c"
 *
 * polivoks_render adds into out[2*i], out[2*i+1]. It does not clear the
 * buffer. The platform zeros it. Leaving Polivoks is the parent's job:
 * call poli_suspend() on program change, PRESETS, or SEL away from inst 0.
 * This file does not select Altair, Aelita, or RMIF and does not call them.
 *
 * polivoks_panel still accepts the historical direct edges (shape step,
 * LP/BP, filter repeat, amp repeat, glide, LFO shape, cutoff, resonance,
 * drive) so host tests can drive them without the board headers. The FM-1
 * app does not pass those edges: section windows call the setters and the
 * panel volatiles, and polivoks_panel is used for the keybed. Encoder steps
 * are whatever the caller passes. The direct cutoff/resonance/drive path
 * still scales by 800.
 *
 * Two oscillators, one filter, one amp envelope. There is no third VCO.
 * Duophony is the lowest and highest held keys of the keybed. On the FM-1
 * (and host tests without BOARD_SMK37) that is the 27-key bed, base MIDI
 * note 65 (bit 0 = F3). On the SMK-37 (BOARD_SMK37) it is all 37 keys,
 * base MIDI note 60 (bit 0 = C3, bit 5 = F3 = 65). polivoks_set_kb_oct
 * adds -3..+2 octaves on the keybed only. The keys argument is 64-bit so
 * bit 36 fits. A single sounding note leaves
 * oscillator 2 one semitone above oscillator 1. MIDI is one held note.
 * Repeating contours run only while a key or the MIDI gate is held. Key-off
 * releases even if the latch is still set. Panic and CC 120 clear both
 * latches and ignore keys that are already down until that mask goes up.
 * A release rate of 0 still holds the level (the previous law). With no
 * gate and no repeating-while-held contour, suspend and a finished release
 * contribute exact 0.
 */
#ifndef POLIVOKS_H
#define POLIVOKS_H
#include <stdint.h>

void poli_suspend(void);
void polivoks_init(void);
void polivoks_render(int32_t *out, uint32_t n);
void polivoks_midi(uint8_t status, uint8_t data1, uint8_t data2);
void polivoks_panic(void);
void polivoks_arp_gate(uint8_t note, uint8_t on, uint8_t vel); /* arpeggiator gate source (main loop) */
void polivoks_panel(uint64_t keys, int shape_enc, int bp_edge, int rep_f_edge,
                    int rep_a_edge, int glide_edge, int lfo_edge,
                    int32_t cut_enc, int32_t res_enc, int32_t drv_enc);
/* Front-panel amounts. Defaults reproduce the previous fixed voice.
 * Laws that are not a traced pot curve are named in polivoks_limited.c.
 * Footage: -2..+2 is 32', 16', 8', 4', 2'. 8' (0) is the MIDI-note default.
 * Service trimmers under the lid (Shift/Pitch/Registers) are not these knobs.
 */
void polivoks_set_shape2(uint8_t shape);          /* clears the shared-shape link */
void polivoks_set_link_shapes(uint8_t on);
void polivoks_set_footage(int vco, int oct);
void polivoks_set_tune(int32_t tune);             /* both VCOs, ±12% at full scale */
void polivoks_set_detune(int32_t detune);         /* VCO 2 only, ±25% at full scale */
/* Pitch bend, 1/4096 semitone units, 0 at centre, clamped to ±2 semitones
 * (±8192). Wheel and MIDI both call this. Last source that changes the
 * value wins (the board layer writes only when the wheel bend changes).
 * Applied once per render block to both generators; not in the sample loop.
 * Filter key tracking still uses the MIDI/keybed note, as tune/detune/LFO do. */
void polivoks_set_pitch_bend(int32_t units);
int32_t polivoks_pitch_bend(void);
void polivoks_set_fm_depth(int32_t depth);
void polivoks_set_level(int which, int32_t level); /* 0 VCO1, 1 VCO2, 2 noise, 3 ext */
void polivoks_set_ext_buf(const int32_t *mono);   /* NULL = no external sample. Jack UNKNOWN. */
void polivoks_set_lfo_inc(uint32_t inc);          /* phase increment, not a Hz calibration */
void polivoks_set_mod_pitch(int32_t depth);     /* legacy: both generators, one amount */
void polivoks_set_mod_pitch_vco(int vco, int32_t depth); /* 0 gen I, 1 gen II */
int32_t polivoks_mod_pitch_vco(int vco);
void polivoks_set_mod_cut(int32_t depth);
void polivoks_set_mod_amp(int32_t depth);
void polivoks_set_contour_amount(int32_t amount);
void polivoks_set_vel_amount(int amt);          /* 0..127: how much a soft note-on lowers filter contour and amp level (0 = off) */
void polivoks_set_panel_vel(uint8_t v);         /* 1..127; next keybed gate-on uses this instead of 127. Default 127. */
int32_t polivoks_contour_eff(void);              /* the contour depth the engine uses now (after velocity) */
void polivoks_set_lfo_restart(uint8_t on);       /* 1: a new note (gate off -> on) restarts the LFO cycle */
void polivoks_lfo_reset(void);                  /* restart the LFO cycle at the next audio block (MIDI Start) */
void polivoks_set_key_track(int pct);          /* 0 off, 62, or 100; other values become 62 */
int polivoks_key_track(void);
void polivoks_set_adsr(int which, int32_t atk, int32_t dec, int32_t sus, int32_t rel);
/* same, but atk/dec/rel in Q15 steps (level << 15 per sample); sus in plain 0..28000 */
void polivoks_set_adsr_q(int which, int32_t atk, int32_t dec, int32_t sus, int32_t rel);
/* One stage, one aligned word (TIMER5-safe). which 0=filter, 1=amp; stage 0..3 = A/D/S/R.
 * atk/dec/rel are Q24 coefficients; sustain is a plain 0..28000 level (same as set_adsr_q). */
void polivoks_set_adsr_stage_q(int which, int stage, int32_t v);
int32_t polivoks_adsr_stage_q(int which, int stage);
void polivoks_set_glide_rate(int32_t step);
void polivoks_set_duo(uint8_t on);
void polivoks_set_output(int32_t level);
/* Bass support defaults ON. The filter core always computes the sidechain.
 * This flag slews a blend, in the voice, from the wet tap to that supported
 * tap. It does not add an oscillator or an octave divider. */
void polivoks_set_bass_support(uint8_t on);
uint8_t polivoks_bass_support(void);
int32_t polivoks_bass_gain(void);             /* 0 wet .. 32768 supported */
void polivoks_set_kb_oct(int oct);            /* -3..+2, keybed only */
int polivoks_kb_oct(void);
/* ВКЛ. key-imitation. On: the last note stays gated after key-off.
 * Panic and CC 120 clear that latch. This is not the release-rate control. */
void polivoks_set_keyhold(uint8_t on);
uint8_t polivoks_keyhold(void);
uint8_t polivoks_link_shapes(void);
uint8_t polivoks_shape2(void);
uint8_t polivoks_duo_on(void);
int polivoks_footage(int vco);
uint32_t polivoks_lfo_phase(void);
void polivoks_read(int32_t *a2, int32_t *a3, int32_t *bass,
                   int32_t *cut, int32_t *res, int32_t *drv,
                   uint32_t *ph1, uint32_t *ph2, int32_t *eg_amp, int32_t *eg_filt);

/* Amp-envelope snapshot for the AMP ENV window. Main-loop / host only.
 * stage is IDLE/ATK/DEC/SUS/REL from gate, fell, level vs sustain. */
#ifndef POLI_AMPENV_STAGES
#define POLI_AMPENV_STAGES
enum {
    POLI_AMPENV_IDLE = 0,
    POLI_AMPENV_ATK  = 1,
    POLI_AMPENV_DEC  = 2,
    POLI_AMPENV_SUS  = 3,
    POLI_AMPENV_REL  = 4
};
#endif
typedef struct {
    int32_t atk, dec, sus, rel; /* Q24 coeffs; sus is 0..28000 */
    int32_t level;              /* poli_eg_a >> 15, typically 0..26000 */
    int stage;
    int gate;
    int rpt;
} poli_ampenv_view_t;
void polivoks_ampenv_view(poli_ampenv_view_t *o);
void polivoks_filtenv_view(poli_ampenv_view_t *o); /* same shape, filter EG (which=0) */

extern volatile uint8_t poli_note;
extern volatile uint8_t poli_note2;
extern volatile uint8_t poli_gate;
extern volatile uint8_t poli_key_on;
extern volatile uint8_t poli_midi_on;
extern volatile uint8_t poli_shape;
extern volatile uint8_t poli_bp;
extern volatile uint8_t poli_rep_f;
extern volatile uint8_t poli_rep_a;
extern volatile uint8_t poli_glide;
extern volatile uint8_t poli_lfo_shape;
extern int32_t poli_cut;
extern int32_t poli_res;
extern int32_t poli_drv;
#endif
