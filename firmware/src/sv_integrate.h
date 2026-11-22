/* SPDX-License-Identifier: GPL-3.0-only */
/* Stable outline for lanes that build models separately.
 * This header is the contract. It is not a sound engine.
 * The app is still one unity file: firmware/src/volt.c includes the
 * instrument sources. app_audio adds into a buffer the platform already
 * zeroed. Integers only. No float. No 64-bit division. No libfm1_engine.a.
 *
 * Slot: app.ld XIP LENGTH 0x8DFBC (581564). Spare must stay >= 512.
 * Select: inst_sel 0 Polivoks, 1 Altair 231, 2 Aelita, 3 RMIF chorus,
 * 4 RMIF phaser. PRESETS encoder, SEL button, MIDI program 0..4.
 *
 * Each instrument owns its globals and one render function. The render
 * adds into out[2*i], out[2*i+1]. It does not clear the buffer.
 * Note events are applied from app_idle / midi_in only while that
 * instrument is selected. Leaving an instrument must silence its tail
 * (a suspend that clears envelopes and filter states).
 *
 * Controls are read only inside that instrument's branch, so one lane
 * does not move another's knobs. FM-1 map: ENC_KNOB1..4 are ids 2..5,
 * ENC_PRESETS is 6. Buttons: FX 2, ENV 4, LFO 5, GLO 7, ARP 10.
 *
 * Who owns which file in this handoff:
 *   polivoks_limited.c, poli_fcore.c  — Polivoks v2 filter (already in tree)
 *   altair231.c in the combined tree  — early model; the later 56208
 *     direction is handoff/altair-later/altair231.c, not this file
 *   aelita_div.c                      — current Aelita source
 *   rmif_opus.c                       — RMIF divider/keyer, plus the
 *     existing chorus and phaser wrappers left for the effects lane
 *
 * RMIF voice entry used by those wrappers: int32_t rmif_keyer(uint32_t n);
 * One call is one sample. The effects lane replaces the wrappers.
 * Do not add a chorus rate control: manual page 12 figure 12 has
 * DEPTH, FEEDBACK, and MIX marked 0–6, and no rate. MIX 0 and
 * FEEDBACK 0 both remove the effect. Feedback is the flanger-like path.
 * opscxem2 does not identify the delay chips as MN3007 or MN3101.
 * opsxem7 is the phaser: four FET stages A3, A5, A7, A8, triangle at
 * KT2. Routing and the rate law are not verified. The wrappers still
 * in rmif_opus.c are the old stand-ins, not those circuits.
 */
#ifndef SV_INTEGRATE_H
#define SV_INTEGRATE_H
#endif
