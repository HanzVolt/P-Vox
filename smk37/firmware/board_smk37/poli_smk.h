/* SMK-37 Elite board-layer hook: outer keys, pads, faders, wheels, and the
 * four transport buttons the FM-1 path does not consume. BOARD_SMK37=0 is a
 * no-op (FM-1 builds). Called from app_main after poli_nav_init and from the
 * 1 kHz loop after service_controls().
 *
 * Approved plumbing (Hans, 13 Nov 2026) plus keybed velocity (Hans, later
 * that day). Maps existing hardware to existing engine parameters / UI
 * actions. The mod wheel still drives FROM MOD (SLOT_MOD_CUT / CC 1). The
 * pitch wheel and MIDI pitch-bend both slide both generators by up to
 * ±2 semitones (last source that changes the value wins). Measured keybed
 * velocity is passed through: the
 * existing VELOCITY (SLOT_VEL) amount scales filter contour and amp level.
 * Bank-A cutoff fader uses the 10-bit median (fad_med, not the slower
 * fad_acc IIR) plus a 1 ms exponential smoother (err>>1, τ ≈ 1.44 ms) so
 * 7-bit steps do not zipper. The ramp and the 10-bit target tick from
 * TIMER5 when fm1_ms increments, and the first tick is applied on the
 * poll that sees the move, so a UI redraw cannot insert a dead time.
 * Existing first-movement soft takeover is unchanged (no new arming rule).
 * Encoder and MIDI CC cutoff paths still snap. All 63 preset slots are
 * used. The signed octave fix (int oct) is kept.
 * Bank-A fader 1 also drives a lights-only pad spectrum meter
 * (smk_pad_spectrum.h): 8 columns left to right in a blue-to-deep-red
 * hue wash, both pads of a column the same colour, fill from poli_cut.
 * Bank-A fader 2 (resonance) does the same with a green-to-deep-red
 * wash, fill from poli_res. Whichever of those two faders moved last
 * owns the pads; preset colours return 1.5 s after the last move.
 * Encoder, CC 74, CC 71 and bank B do not trigger it. Pad taps still
 * load presets. WS2812 updates stay in the main loop at 40 Hz, never
 * in TIMER5 or the audio path.
 * A real touch of a fader, the mod wheel, or the pitch wheel jumps the
 * screen to that control's slot window (poli_nav_show_slot), the same
 * way a section button does. Jump only past soft-takeover or a 7-bit
 * move of at least 2, so ADC noise does not flip pages. MIDI CC does
 * not jump (the call is at this board site, not inside poli_nav_cc).
 * K1-K4 still edit the current window. A real pitch-wheel move still
 * opens GLOBAL (glide / tune), the closest page, and also bends pitch.
 * Resonance (bank-A F2) and the four bank-B amp ADSR faders use the
 * same 10-bit median + 1 ms err>>1 glide as cutoff. The engine target
 * stays the raw 10-bit median. A display-only hysteresis (H=12 counts,
 * ends snap to 0/127) feeds the on-screen number, pad-meter move
 * detection, page-jump / soft-takeover arming, and AMP/filter plot
 * geometry, so ADC jitter does not flicker those. UI reflects are
 * value-only (nav_val_gen), not a full 240x240 redraw. Soft takeover
 * is still first-movement of the steadied value; engine writes from
 * TIMER5 are single aligned words. */
#pragma once
#include "polivoks.h"
#include "poli_nav.h"
#include "poli_preset.h"
#include "smk_cut_smooth.h"
#include "smk_fader_steady.h"
#include "smk_pad_spectrum.h"

#if BOARD_SMK37
#include "smk_input.h"
#include "smk_wheel.h"

enum {
    SMK_FADER_BANK_A = 0,               /* cutoff / res / contour / drive */
    SMK_FADER_BANK_B = 1,               /* amp A / D / S / R */
    SMK_PAD_PAGES = 4,                  /* 1-16 / 17-32 / 33-48 / 49-63 (15 on the last page) */
    SMK_MOD_DEAD = 12,                  /* raw ADC counts at the bottom of travel */
    SMK_MOD_MINSPAN = 80                /* until the wheel has shown a real span */
};

/* Fader CCs (poli_nav_cc / CC_SLOT): bank A FILTER, bank B AMP EG. */
static const uint8_t SMK_FADER_CC[2][4] = {
    { 74, 71, 70, 16 },                 /* SLOT_CUT, SLOT_RES, SLOT_CONT, SLOT_DRIVE */
    { 73, 75, 79, 72 }                  /* SLOT_ATK_A, SLOT_DEC_A, SLOT_SUS_A, SLOT_REL_A */
};

static uint8_t smk_prev_buttons;
static uint8_t smk_fader_bank;          /* 0 = A, 1 = B; FADER BANK LED on when B */
static uint8_t smk_pad_page;            /* 0..3; KNOB BANK LED on when not page 0 */
static uint8_t smk_fad_last[4];
static uint8_t smk_fad_shown[4];         /* display-only 0-127, hysteresis H=12 */
static uint8_t smk_fad_arm[4];          /* soft takeover: armed after the fader moves */
static uint8_t smk_fad_have;
static uint8_t smk_cut_run;             /* 1: bank-A F1 is ramping poli_cut */
static uint32_t smk_cut_ui_ms;          /* last cutoff screen dirty, for 25 Hz throttle */
static int32_t smk_cut_tgt;
static uint8_t smk_live_run[4];         /* 1: that live fader is ramping the engine */
static int32_t smk_live_tgt[4];
static uint32_t smk_live_ui_ms[4];      /* last value-only screen dirty, ~25 Hz */
static uint16_t smk_spec_med;           /* last fad_med[0] that counted as a cutoff meter move */
static uint16_t smk_spec_res_med;       /* last fad_med[1] that counted as a resonance meter move */
static uint32_t smk_spec_draw_ms;       /* last spectrum padled_flush */
static uint8_t smk_mod_have;
static uint8_t smk_mod_arm;
static uint8_t smk_mod_last;
static uint8_t smk_mod_primed;
static int16_t smk_mod_rest;            /* highest raw seen (wheel down); up = lower raw */
static int16_t smk_mod_lo;              /* lowest raw seen (wheel up) */
static uint8_t smk_pw_last;             /* pitch wheel, 10-bit raw >> 3 */
static uint8_t smk_pw_primed;
static smk_pw_t smk_pw;                 /* self-calibrating ±2 st, deadzone at rest */
static int32_t smk_pw_sent;             /* last wheel bend written; change-only */
static uint8_t smk_leds_dirty = 1;
static int smk_led_cur = -1;

static int smk_val7_med(int k);
static int smk_fader_is_live(int k);

static void smk_soft_reset_faders(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        /* Live faders arm from the steadied 10-bit median so a bottom
         * 0->1 step is not hidden by the 7-bit IIR deadband, and ADC
         * jitter does not arm them. Others keep the lagged 7-bit reading. */
        if (smk_fader_is_live(i)) {
            smk_fad_shown[i] = (uint8_t)smk_val7_of_med(fm1_in.fad_med[i]);
            smk_fad_last[i] = smk_fad_shown[i];
        } else {
            smk_fad_shown[i] = fm1_in.fader[i];
            smk_fad_last[i] = fm1_in.fader[i];
        }
        smk_fad_arm[i] = 0;
        smk_live_run[i] = 0;            /* leave engine values; do not slam on bank switch */
        smk_live_ui_ms[i] = 0;
        smk_live_tgt[i] = 0;
    }
    smk_fad_have = 1;
    smk_cut_run = 0;                    /* leave poli_cut; bank switch must not slam cutoff */
    smk_cut_ui_ms = 0;
    smk_spec_med = fm1_in.fad_med[0];
    smk_spec_res_med = fm1_in.fad_med[1];
}

/* Screen-only: close a preset bar/save so the slot page can show, then
 * open the window that holds kind. Main loop only. */
static void smk_jump_slot(int kind)
{
    if (poli_preset_mode() != PV_MODE_NONE)
        poli_preset_cancel();
    poli_nav_show_slot(kind);
}

/* Real touch: first soft-takeover arm, or a 7-bit move of at least 2. */
static int smk_jump_touch(int just_armed, int prev, int val)
{
    int d = val - prev;
    if (d < 0)
        d = -d;
    return just_armed || d >= 2;
}

static int smk_cc_slot(int cc)
{
    switch (cc) {
    case 74: return SLOT_CUT;
    case 71: return SLOT_RES;
    case 70: return SLOT_CONT;
    case 16: return SLOT_DRIVE;
    case 73: return SLOT_ATK_A;
    case 75: return SLOT_DEC_A;
    case 79: return SLOT_SUS_A;
    case 72: return SLOT_REL_A;
    case 1:  return SLOT_MOD_CUT;
    default: return SLOT_EMPTY;
    }
}

/* Bank-A F1 (cutoff) only. Soft takeover is the same first-movement rule
 * the other faders already use (arm when the 7-bit reading leaves its
 * boot / bank-switch value). No extra pickup or acc-threshold arming.
 * Once armed, the target is the 10-bit median (fad_med), not the lagged
 * fad_acc IIR, and a 1 ms smoother ticks from TIMER5 — including a
 * retarget, so a UI redraw cannot freeze the reading. The first tick is
 * applied on this poll. Does not call poli_nav_cc, so encoder / MIDI CC
 * still snap poli_cut. */

/* 7-bit of the 10-bit median. 0 when med is 0..7, 1 at 8..15, no IIR hole. */
static int smk_val7_med(int k)
{
    uint32_t m = fm1_in.fad_med[k];
    if (m > 1016u)
        m = 1016u;
    return (int)(m / 8u);
}

/* Bank-A F1 (cutoff) and F2 (res), or all four bank-B amp ADSR faders. */
static int smk_fader_is_live(int k)
{
    if (smk_fader_bank == SMK_FADER_BANK_A)
        return k == 0 || k == 1;
    return 1;
}

static int smk_live_kind(int k)
{
    if (smk_fader_bank == SMK_FADER_BANK_A)
        return k == 0 ? SLOT_CUT : SLOT_RES;
    return SLOT_ATK_A + k;
}

static int32_t smk_live_tgt_now(int k)
{
    uint16_t med = fm1_in.fad_med[k];
    if (smk_fader_bank == SMK_FADER_BANK_A)
        return k == 0 ? smk_cut_of_fader_adc(med) : poli_res_of_fader_adc(med);
    return poli_env_of_fader_adc(k, med);
}

static int32_t smk_live_cur(int k)
{
    if (smk_fader_bank == SMK_FADER_BANK_A)
        return k == 0 ? poli_cut : poli_res;
    return polivoks_adsr_stage_q(1, k);
}

/* Single aligned word into the engine; keep nav's ADSR shadow in step. */
static void smk_live_write(int k, int32_t v)
{
    if (smk_fader_bank == SMK_FADER_BANK_A) {
        if (k == 0)
            poli_cut = v;
        else
            poli_res = v;
        return;
    }
    polivoks_set_adsr_stage_q(1, k, v);
    poli_nav_adsr_shadow(1, k, v);
}

static void smk_live_start_or_retarget(int k, int32_t tgt)
{
    int was = smk_live_run[k];
    smk_live_tgt[k] = tgt;
    if (smk_live_cur(k) == tgt) {
        smk_live_run[k] = 0;
        return;
    }
    smk_live_run[k] = 1;
    if (!was) {
        smk_live_write(k, smk_cut_smooth_tick(smk_live_cur(k), tgt));
        if (smk_live_cur(k) == tgt)
            smk_live_run[k] = 0;
    }
}

static int32_t smk_cut_tgt_now(void)
{
    return smk_cut_of_fader_adc(fm1_in.fad_med[0]);
}

static void smk_cut_start_or_retarget(int32_t tgt)
{
    int was = smk_cut_run;
    smk_cut_tgt = tgt;
    if (poli_cut == tgt) {
        smk_cut_run = 0;
        return;
    }
    smk_cut_run = 1;
    if (!was) {
        /* first change this poll, not after the next 1 ms interrupt */
        poli_cut = smk_cut_smooth_tick(poli_cut, smk_cut_tgt);
        if (poli_cut == smk_cut_tgt)
            smk_cut_run = 0;
    }
}

/* One real millisecond, from TIMER5 when fm1_ms increments. Retarget from
 * the latest 10-bit median if a live fader is already armed. No catch-up
 * burst: a stalled main loop must not slam the engine. Cutoff still uses
 * its own run/tgt so the existing start_or_retarget path stays intact. */
static void smk_fader_ms_tick(void)
{
    int k;

    if (smk_fader_bank == SMK_FADER_BANK_A && smk_fad_arm[0]) {
        smk_cut_tgt = smk_cut_tgt_now();
        if (poli_cut != smk_cut_tgt)
            smk_cut_run = 1;
    }
    if (smk_cut_run) {
        poli_cut = smk_cut_smooth_tick(poli_cut, smk_cut_tgt);
        if (poli_cut == smk_cut_tgt)
            smk_cut_run = 0;
    }

    for (k = 0; k < 4; k++) {
        if (k == 0 && smk_fader_bank == SMK_FADER_BANK_A)
            continue;                   /* cutoff is ticked above */
        if (!smk_fader_is_live(k) || !smk_fad_arm[k])
            continue;
        smk_live_tgt[k] = smk_live_tgt_now(k);
        if (smk_live_cur(k) != smk_live_tgt[k])
            smk_live_run[k] = 1;
        if (!smk_live_run[k])
            continue;
        smk_live_write(k, smk_cut_smooth_tick(smk_live_cur(k), smk_live_tgt[k]));
        if (smk_live_cur(k) == smk_live_tgt[k])
            smk_live_run[k] = 0;
    }
}

static void smk_fader_cut_take(int val, uint32_t now)
{
    int shown = (int)smk_fad_shown[0];
    int changed, prev, just_armed = 0;
    (void)val;                      /* display uses the steadied shown value */

    changed = smk_steady_update(&shown, fm1_in.fad_med[0], SMK_STEADY_H);
    smk_fad_shown[0] = (uint8_t)shown;
    prev = (int)smk_fad_last[0];

    if (!smk_fad_arm[0]) {
        if (!changed)
            return;
        smk_fad_arm[0] = 1;
        just_armed = 1;
    }
    if (changed) {
        smk_fad_last[0] = (uint8_t)shown;
        /* Dirty the cutoff value at most ~25 Hz (value-only, not a
         * full 240x240 paint). Audio does not wait for this. */
        if (smk_cut_ui_ms == 0u || now - smk_cut_ui_ms >= 40u) {
            poli_nav_reflect_cut(shown);
            smk_cut_ui_ms = now ? now : 1u;
        }
        if (smk_jump_touch(just_armed, prev, shown))
            smk_jump_slot(SLOT_CUT);
        /* Meter trigger: steadied move only, not raw ADC jitter. */
        smk_spec_med = fm1_in.fad_med[0];
        smk_spec_note_move(now, SMK_SPEC_SRC_CUT);
    }
    /* Engine always follows the raw 10-bit median once armed. */
    smk_cut_start_or_retarget(smk_cut_tgt_now());
}

/* Bank-A F2 (resonance) only. Same first-movement soft takeover. Once
 * armed, the target is the 10-bit median with the 1 ms smoother, not
 * poli_nav_cc. Meter trigger is a real physical fader move only (7-bit
 * step or fad_med delta >= 8), never encoder or MIDI CC 71. UI is a
 * value-only reflect, throttled at ~25 Hz. */
static void smk_fader_res_take(int val, uint32_t now)
{
    int shown = (int)smk_fad_shown[1];
    int changed, prev, just_armed = 0;
    (void)val;

    changed = smk_steady_update(&shown, fm1_in.fad_med[1], SMK_STEADY_H);
    smk_fad_shown[1] = (uint8_t)shown;
    prev = (int)smk_fad_last[1];

    if (!smk_fad_arm[1]) {
        if (!changed)
            return;
        smk_fad_arm[1] = 1;
        just_armed = 1;
    }
    if (changed) {
        smk_fad_last[1] = (uint8_t)shown;
        if (smk_live_ui_ms[1] == 0u || now - smk_live_ui_ms[1] >= 40u) {
            poli_nav_reflect_slot(SLOT_RES, shown);
            smk_live_ui_ms[1] = now ? now : 1u;
        }
        if (smk_jump_touch(just_armed, prev, shown))
            smk_jump_slot(SLOT_RES);
        smk_spec_res_med = fm1_in.fad_med[1];
        smk_spec_note_move(now, SMK_SPEC_SRC_RES);
    }
    smk_live_start_or_retarget(1, smk_live_tgt_now(1));
}

/* Bank-B amp ADSR fader k=0..3. Same first-movement arm, 10-bit median
 * target, 1 ms glide, value-only 25 Hz UI reflect, and a page jump on
 * a real touch. */
static void smk_fader_env_take(int k, int val, uint32_t now)
{
    int shown = (int)smk_fad_shown[k];
    int changed, prev, just_armed = 0;
    (void)val;

    changed = smk_steady_update(&shown, fm1_in.fad_med[k], SMK_STEADY_H);
    smk_fad_shown[k] = (uint8_t)shown;
    prev = (int)smk_fad_last[k];

    if (!smk_fad_arm[k]) {
        if (!changed)
            return;
        smk_fad_arm[k] = 1;
        just_armed = 1;
    }
    if (changed) {
        smk_fad_last[k] = (uint8_t)shown;
        if (smk_live_ui_ms[k] == 0u || now - smk_live_ui_ms[k] >= 40u) {
            poli_nav_reflect_slot(smk_live_kind(k), shown);
            smk_live_ui_ms[k] = now ? now : 1u;
        }
        if (smk_jump_touch(just_armed, prev, shown))
            smk_jump_slot(smk_live_kind(k));
    }
    smk_live_start_or_retarget(k, smk_live_tgt_now(k));
}

/* Keep the 10-bit target current between 7-bit steps (main loop). The
 * ISR also retargets, so this is a backup when the loop is running. */
static void smk_cut_follow_poll(void)
{
    int k;
    if (smk_fader_bank == SMK_FADER_BANK_A && smk_fad_arm[0])
        smk_cut_start_or_retarget(smk_cut_tgt_now());
    for (k = 0; k < 4; k++) {
        if (k == 0 && smk_fader_bank == SMK_FADER_BANK_A)
            continue;
        if (smk_fader_is_live(k) && smk_fad_arm[k])
            smk_live_start_or_retarget(k, smk_live_tgt_now(k));
    }
}

/* Mod wheel: existing SLOT_MOD_CUT amount via CC 1, not a new source.
 * Travel is short and inverted (up = lower raw); rest self-calibrates to the
 * highest raw seen. Soft takeover so power-on rest does not zero the amount. */
static int smk_mod_cc(int32_t raw)
{
    int32_t span, d, v;

    if (!smk_mod_have) {
        if (raw < 50)
            return -1;                  /* ADC not settled (BSS starts at 0) */
        smk_mod_rest = (int16_t)raw;
        smk_mod_lo = (int16_t)raw;
        smk_mod_have = 1;
        return 0;
    }
    if (raw > smk_mod_rest)
        smk_mod_rest = (int16_t)raw;
    if (raw < smk_mod_lo)
        smk_mod_lo = (int16_t)raw;
    span = (int32_t)smk_mod_rest - (int32_t)smk_mod_lo;
    if (span < SMK_MOD_MINSPAN)
        span = SMK_MOD_MINSPAN;
    d = (int32_t)smk_mod_rest - raw;
    if (d <= SMK_MOD_DEAD)
        return 0;
    v = (d - SMK_MOD_DEAD) * 127 / (span - SMK_MOD_DEAD);
    if (v > 127)
        v = 127;
    return (int)v;
}

static void smk_soft_take(uint8_t *last, uint8_t *arm, int cc, int val)
{
    int prev, just_armed = 0;
    if (val < 0)
        return;
    prev = (int)*last;
    if (!*arm) {
        if (prev == val)
            return;
        *arm = 1;
        just_armed = 1;
    }
    if (prev == val)
        return;
    *last = (uint8_t)val;
    poli_nav_cc_live(cc, val);
    if (smk_jump_touch(just_armed, prev, val))
        smk_jump_slot(smk_cc_slot(cc));
}

static int smk_pad_page_len(int page)
{
    int n = PV_NSLOT - page * 16;
    if (n > 16)
        n = 16;
    if (n < 0)
        n = 0;
    return n;
}

static void smk_refresh_btn_leds(void)
{
    fm1_led_key(BTN_FADER_BANK, smk_fader_bank != 0);
    fm1_led_key(BTN_KNOB_BANK, smk_pad_page != 0);
}

/* Preset colours. Not used while the spectrum meter is up. */
static void smk_refresh_leds(void)
{
    poli_preset_view_t v;
    int i, n, cur, lit;
    uint8_t r, g, b;

    smk_refresh_btn_leds();
    poli_preset_view(&v);
    cur = v.cur;
    smk_led_cur = cur;
    n = smk_pad_page_len((int)smk_pad_page);
    for (i = 0; i < 16; i++) {
        if (i >= n) {
            padled_set((uint32_t)i, 0, 0, 0);          /* last page: pad 16 unused */
            continue;
        }
        lit = (cur == smk_pad_page * 16 + i);
        /* cream for the current slot, dim warm for the other slots on the page */
        r = lit ? 200 : 28;
        g = lit ? 140 : 16;
        b = lit ? 48 : 6;
        padled_set((uint32_t)i, r, g, b);
    }
    padled_flush();
    smk_leds_dirty = 0;
}

/* PAD REC colours: dim if the lane is empty, bright if it has a step, flare on the playhead / a hit. */
static const uint8_t SMK_PR_RGB[16][3] = {
    { 80, 200, 40 },  { 40, 180, 60 },  { 140, 200, 40 }, { 140, 60, 200 },
    { 160, 50, 180 }, { 180, 40, 160 }, { 220, 220, 220 }, { 220, 40, 160 },
    { 220, 30, 30 },  { 220, 200, 30 }, { 220, 120, 20 }, { 20, 200, 200 },
    { 20, 180, 140 }, { 220, 140, 30 }, { 220, 160, 40 }, { 220, 180, 50 }
};
static void smk_padrec_leds(uint32_t now)
{
    int i, l, st = xtra_padrec_step(), cin = xtra_padrec_countin();
    uint8_t r, g, b, lit, flare;
    uint16_t bits;
    smk_refresh_btn_leds();
    fm1_led_key(BTN_KNOB_BANK, 0);
    for (i = 0; i < 16; i++) {
        l = xtra_padrec_lane_of_pad(i);
        bits = (uint16_t)(l >= 0 ? xtra_padrec_work_lane(l) : 0);
        lit = bits != 0;
        flare = (uint8_t)((bits >> (st & 15)) & 1u);
        if (cin && (cin % 4u) == 0u) {           /* dim white flash on count-in beats */
            r = 80; g = 80; b = 80;
        } else {
            uint8_t sc = flare ? 255u : lit ? 160u : 36u;
            r = (uint8_t)((SMK_PR_RGB[i][0] * sc) / 255u);
            g = (uint8_t)((SMK_PR_RGB[i][1] * sc) / 255u);
            b = (uint8_t)((SMK_PR_RGB[i][2] * sc) / 255u);
        }
        padled_set((uint32_t)i, r, g, b);
    }
    padled_flush();
    smk_leds_dirty = 0;
    (void)now;
}

/* Spectrum meter from the last-moved fader (poli_cut or poli_res).
 * Main loop only; never the audio ISR or TIMER5. */
static void smk_spec_paint(void)
{
    int i;
    uint8_t r, g, b;
    int res = (smk_spec_owner() == SMK_SPEC_SRC_RES);

    smk_refresh_btn_leds();
    for (i = 0; i < SMK_SPEC_PADS; i++) {
        if (res)
            smk_spec_res_colour(i, poli_res, &r, &g, &b);
        else
            smk_spec_colour(i, poli_cut, &r, &g, &b);
        padled_set((uint32_t)i, r, g, b);
    }
    padled_flush();
}

static void smk_update_pad_leds(uint32_t now, int cur)
{
    if (poli_nav_xtra() == XW_PADREC) {
        smk_padrec_leds(now);
        return;
    }
    if (smk_spec_active(now)) {
        if (smk_spec_draw_ms == 0u || now - smk_spec_draw_ms >= (uint32_t)SMK_SPEC_PERIOD_MS) {
            smk_spec_paint();
            smk_spec_draw_ms = now ? now : 1u;
        } else {
            smk_refresh_btn_leds();
        }
        return;
    }
    /* Hold just expired, or a normal dirty/preset change. */
    if (smk_spec_draw_ms) {
        smk_spec_draw_ms = 0;
        smk_refresh_leds();
        return;
    }
    if (smk_leds_dirty || cur != smk_led_cur)
        smk_refresh_leds();
}

static void poli_smk_init(void)
{
    smk_pad_events = 1;                 /* pads load presets; they must not also play notes */
    smk_prev_buttons = fm1_in.buttons;
    smk_fader_bank = SMK_FADER_BANK_A;
    smk_pad_page = 0;
    smk_soft_reset_faders();
    smk_mod_have = 0;
    smk_mod_arm = 0;
    smk_mod_primed = 0;
    smk_pw_primed = 0;
    smk_pw.pc = 0;
    smk_pw.plo = 0;
    smk_pw.phi = 0;
    smk_pw.drift_ms = 0;
    smk_pw_sent = 0;
    smk_leds_dirty = 1;
    smk_led_cur = -1;
    smk_spec_reset();
    smk_spec_draw_ms = 0;
    smk_spec_med = fm1_in.fad_med[0];
    smk_spec_res_med = fm1_in.fad_med[1];
}

static void poli_smk_poll(void)
{
    int ev, k, val, pad, n, dir, cur;
    uint32_t now = fm1_ms;
    uint8_t btns = fm1_in.buttons;
    uint8_t pressed = (uint8_t)(btns & (uint8_t)~smk_prev_buttons);
    poli_preset_view_t view;

    /* Own button edges: service_controls() already called fm1_input_edges()
     * and cleared fm1_in.pressed. Do not consume the FM-1 button bits. */

    /* All 37 keys are already in fm1_in.keys and are voiced by
     * polivoks_panel (lowest held = GEN I; highest = GEN II if VOICES=2).
     * Outer keys are not sent through polivoks_midi; that path stays
     * for external USB/DIN MIDI only. */

    if (pressed & (1u << BTN_FADER_BANK)) {
        smk_fader_bank ^= 1u;
        smk_soft_reset_faders();        /* physical positions belong to the other bank */
        smk_leds_dirty = 1;
    }
    if (pressed & (1u << BTN_KNOB_BANK)) {
        smk_pad_page = (uint8_t)((smk_pad_page + 1u) % SMK_PAD_PAGES);
        smk_leds_dirty = 1;
    }
    if (pressed & ((1u << BTN_LEFT) | (1u << BTN_RIGHT))) {
        dir = (pressed & (1u << BTN_RIGHT)) ? 1 : -1;
        if (poli_nav_xtra() == XW_PADREC && xtra_padrec_mode())
            xtra_padrec_step_move(dir);
        else if (poli_preset_mode() == PV_MODE_BAR)
            poli_preset_turn(dir, now);
        else if (poli_preset_mode() != PV_MODE_SAVE)
            poli_nav_select_turn(dir);
        smk_leds_dirty = 1;
    }

    if (poli_nav_xtra() == XW_PADREC) {
        for (;;) {
            ev = (int)smk_pad_take();
            if (ev < 0)
                break;
            pad = (ev >> 8) & 0xFF;
            xtra_padrec_pad(pad, ev & 0xFF, now);   /* velocity is ignored for level; 0 = release (hold-erase ends) */
            smk_leds_dirty = 1;
        }
    } else {
        n = smk_pad_page_len((int)smk_pad_page);
        for (;;) {
            ev = (int)smk_pad_take();
            if (ev < 0)
                break;
            if ((ev & 0xFF) == 0)
                continue;                   /* release: load only on the hit */
            pad = (ev >> 8) & 0xFF;
            if (pad < n)
                poli_preset_load_slot(smk_pad_page * 16 + pad, now);
            smk_leds_dirty = 1;
        }
    }

    if (!smk_fad_have)
        smk_soft_reset_faders();
    for (k = 0; k < 4; k++) {
        if (k == 0 && smk_fader_bank == SMK_FADER_BANK_A) {
            val = smk_val7_med(k);
            smk_fader_cut_take(val, now);
        } else if (k == 1 && smk_fader_bank == SMK_FADER_BANK_A) {
            val = smk_val7_med(k);
            smk_fader_res_take(val, now);
        } else if (smk_fader_bank == SMK_FADER_BANK_B) {
            val = smk_val7_med(k);
            smk_fader_env_take(k, val, now);
        } else {
            val = (int)fm1_in.fader[k];
            smk_soft_take(&smk_fad_last[k], &smk_fad_arm[k],
                          (int)SMK_FADER_CC[smk_fader_bank][k], val);
        }
    }
    smk_cut_follow_poll();

    /* Pitch wheel (fm1_in.wheel[0]): a real 7-bit move (at least 2 steps
     * of raw>>3) still opens GLOBAL, the closest page (tune / level /
     * glide / voices). The same raw also bends both generators by up to
     * ±2 semitones. Rest is self-calibrated (first settled reading;
     * board-layer note: one Pro rested at ~535, travel ~445..640, unit-
     * dependent). A dead band of SMK_PW_DEAD raw counts around the
     * centre is exactly 0. Last-wins with MIDI: this path writes the
     * engine only when the computed wheel bend changes, so a resting
     * wheel does not cancel an incoming MIDI bend. Returning to the
     * deadzone does write 0. No bend-range slot. */
    {
        int32_t raw = (int32_t)fm1_in.wheel[0];
        int pw7 = (int)(raw >> 3);
        if (pw7 > 127)
            pw7 = 127;
        if (!smk_pw_primed) {
            smk_pw_last = (uint8_t)pw7;
            smk_pw_primed = 1;
        } else if (pw7 != (int)smk_pw_last) {
            int d = pw7 - (int)smk_pw_last;
            if (d < 0)
                d = -d;
            smk_pw_last = (uint8_t)pw7;
            if (d >= 2)
                smk_jump_slot(SLOT_GLIDE_RATE);
        }
        /* Do not calibrate on a power-on 0 (ADC not settled). The helper
         * treats pc==0 as uncalibrated; a later rest around 535 would
         * then look like a full throw. */
        if (smk_pw.pc || (raw > 50 && raw < 1000)) {
            int32_t b = smk_pw_bend(&smk_pw, raw, now);
            if (b != smk_pw_sent) {
                smk_pw_sent = b;
                polivoks_set_pitch_bend(b);
            }
        }
    }
    val = smk_mod_cc((int32_t)fm1_in.wheel[1]);
    if (val >= 0 && smk_mod_have) {
        if (!smk_mod_primed) {
            smk_mod_last = (uint8_t)val;
            smk_mod_primed = 1;
        } else {
            smk_soft_take(&smk_mod_last, &smk_mod_arm, 1, val);
        }
    }

    smk_prev_buttons = btns;

    /* Lowest held key of all 37: give the engine its measured velocity.
     * polivoks_panel latches it only on a new gate-on. The engine itself
     * does not include the HAL; this is the only feed. */
    if (fm1_in.keys) {
        uint64_t ks = fm1_in.keys;
        uint32_t kk = 0;
        while ((ks & 1u) == 0u) {
            ks >>= 1;
            kk++;
        }
        polivoks_set_panel_vel(fm1_in.key_vel[kk]);
    }

    poli_preset_view(&view);
    cur = view.cur;
    smk_update_pad_leds(now, cur);
}

#else
static void poli_smk_init(void) {}
static void poli_smk_poll(void) {}
#endif
