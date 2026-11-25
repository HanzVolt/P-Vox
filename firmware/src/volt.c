/* SPDX-License-Identifier: GPL-3.0-only */
/* Standalone Polivoks for the FM-1 measurement platform.
 * One instrument. Altair, Aelita, and RMIF are not included.
 * Seq, arp, and the separate drum track are not in this image.
 * Two generators plus the modulator. No third VCO. No external input level.
 *
 * Section buttons: EDIT generators, LFO modulator, FX filter, ENV amp,
 * GLO global. Each opens that section's first window. SELECT turn cycles
 * that section and wraps. HOME opens the 11-window voice roll and wraps.
 * SEL is the button, not an encoder press. K1-K4 edit the window.
 * No hold-secondary gesture.
 * PRESETS turn shows the preset bar, SEL loads it; SAVE opens the save
 * screen and a second SAVE does its action (poli_preset.c).
 * ALGORITHM turn toggles EN/RU (provisional: that encoder is otherwise unused).
 * OCT- / OCT+ shift the keybed by one octave, clamped to -3..+2; both
 * together reset it. The keybed base stays note 65.
 *
 * BLOCK TIMER test build (14 Nov 2026): the audio ISR times each 256-frame
 * half (platform.c, bt); the preset row shows the longest fill, its % of
 * 5805 us and the late count where the grey FM-1 tag was (poli_ui.c
 * bt_draw). OCT- + OCT+ together also resets them. Not a sound change.
 *
 * CORE2-FXSEQ build (15 Nov 2026): ADDED features, NOT Polivoks ones (the
 * original has none of them), all OFF at power-on and outside the presets:
 * arpeggiator, 16-step sequencer and a synthesized drum track (main program,
 * poli_xtra.c), delay and reverb plus the drum voices on the second core
 * (poli_c2fx.c, code in RAM). ARP / SEQ open their windows (pressed again:
 * on/off), PLAY starts/stops, REC records steps; SELECT turn walks ARP, SEQ,
 * DRUMS, DELAY, REVERB. With all of them off the voice is the base build's.
 *
 * Platform sources are the measurement platform plus crt0.S. This file is
 * the only application translation unit the link compiles.
 */
#include "poli_feat.h"                          /* Parity (23 Nov 2026): PAD REC, classic arp, plots, analyser */
#define APP_NAME "VOLT"
#define STORE_MAGIC 0x544C4F56u
static void app_idle(void);
#define LCD_IDLE app_idle
void xf_core2_boot(void);
#define APP_CORE2_BOOT xf_core2_boot            /* platform.c fm1_cstart: before the audio (poli_c2fx.c) */
#include "platform.c"

static volatile uint32_t hook_tag;

#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"
#ifndef DINGHY_HOST
#include "fm1_cpu1.h"                           /* the second core (FORM 1.3's HAL, after X0X and Melodee) */
#endif
#include "poli_c2fx.c"                          /* Core2-FxSeq: delay, reverb, drum voices on core 1 */

/* Preset store in flash (poli_preset.c). Blobs 5..7 and the settings sector
 * are inside store.h's FM-1 allow-list (0xF4000-0xFBFFF, 0xFC000).
 * UNTESTED ON HARDWARE: run only against a host flash model. */
#define PV_STORE 1
#define PV_BLOB_BACKUP 5u                       /* 0xF9000 */
#define PV_BLOB_A 6u                            /* 0xFA000 */
#define PV_BLOB_B 7u                            /* 0xFB000 */
_Static_assert(BLOBS_AT + PV_BLOB_A * 0x1000u == 0xFA000u && BLOBS_AT + PV_BLOB_B * 0x1000u == 0xFB000u &&
               BLOBS_AT + PV_BLOB_BACKUP * 0x1000u == 0xF9000u, "preset sectors moved");
static uint32_t pv_blob_of(int which) { return which == 0 ? PV_BLOB_A : which == 1 ? PV_BLOB_B : PV_BLOB_BACKUP; }
/* 3212 = 12 + 50*64, the payload of a 50-slot bank. A current load asks for
 * sizeof pv_bank (4044). area_load requires an exact length, so a short bank
 * is retried at the old size and the rest of the buffer is cleared. */
#define PV_BANK_V1_BYTES (12u + 50u * 64u)
_Static_assert(PV_BANK_V1_BYTES == 3212u, "50-slot payload");
static int pv_store_load(int which, void *p, uint32_t n)
{
    if (which == 3)
        return settings_load(p, n);
    if (blob_load(pv_blob_of(which), p, n) == 0)
        return 0;
    if (n > PV_BANK_V1_BYTES && blob_load(pv_blob_of(which), p, PV_BANK_V1_BYTES) == 0) {
        memset((uint8_t *)p + PV_BANK_V1_BYTES, 0, n - PV_BANK_V1_BYTES);
        return 0;
    }
    return -1;
}
static int pv_store_save(int which, const void *p, uint32_t n)
{
    fm1_wdt_feed();
    return which == 3 ? settings_save(p, n) : blob_save(pv_blob_of(which), p, n);
}
#if POLI_PADREC
/* Parity (23 Nov 2026): PAD REC's 8 grooves in blob 0 (0xF4000), as on the Elite. Blob 0 is inside store.h's FM-1
 * allow-list (0xF4000-0xFBFFF) and no other P-Vox code uses it (presets: 5..7). A load needs the store magic, the
 * exact 408-byte length, the CRC and PAD REC's own "PVDR" v1 header, so anything else there is left alone. */
#define PV_PADREC_BLOB 0u
_Static_assert(BLOBS_AT + PV_PADREC_BLOB * 0x1000u == 0xF4000u && PV_PADREC_BLOB != PV_BLOB_A &&
               PV_PADREC_BLOB != PV_BLOB_B && PV_PADREC_BLOB != PV_BLOB_BACKUP, "PAD REC blob");
static int pv_padrec_flush(void)
{
    uint8_t buf[XT_PADREC_BLOB];
    if (xtra_padrec_export(buf, sizeof buf) < 0) return -1;
    fm1_wdt_feed();
    return blob_save(PV_PADREC_BLOB, buf, sizeof buf);
}
static void pv_padrec_boot(void)
{
    uint8_t buf[XT_PADREC_BLOB];
    if (blob_load(PV_PADREC_BLOB, buf, sizeof buf) == 0)
        (void)xtra_padrec_import(buf, sizeof buf);
}
#endif
/* Preset-load mute (out_mute.h, in platform.c's output stage): ask for
 * silence, wait until the audio ISR has faded a whole half to 0 (at most the
 * half in flight plus the fade half, 2 x 5.8 ms), then change the sound. The
 * timeout covers a stopped audio stream: the load goes ahead anyway.
 * Not on the host model (tests/host.c renders audio from platform_poll, which
 * a wait here would never reach): there a load stays immediate. */
#ifndef DINGHY_HOST
#define PV_MUTE 1
#define PV_MUTE_WAIT_MS 20u
static void pv_mute_on(void)
{
    uint32_t t0 = fm1_ms;
    out_mute_req = 1u;
    while (out_mute_g != 0 && fm1_ms - t0 < PV_MUTE_WAIT_MS)
        fm1_wdt_feed();
}
static void pv_mute_off(void) { out_mute_req = 0u; }
#endif
#include "poli_preset.c"
#include "poli_ui.c"
/* The load screen (poli_boot.c): ПОЛИВОКС + the HV logo fade in, the voice, then the synth page. Device only:
 * the host model (tests/host.c) has no masked-IRQ audio path; host/test_boot.c checks it with a model.
 * The HV logo is (C) Hans Volt, all rights reserved, NOT GPL (ATTRIBUTION.txt). 0: build without it. */
#ifndef POLI_BOOT_SCREEN
#define POLI_BOOT_SCREEN 1
#endif
#if POLI_BOOT_SCREEN && !defined(DINGHY_HOST)
#include "poli_boot.c"
#endif

__attribute__((noinline)) static void inst_polivoks(int32_t *out, uint32_t n)
{
    hook_tag = 1u;
    polivoks_render(out, n);
}

static volatile uint8_t inst_sel;

static void app_audio(int32_t *out, uint32_t n)
{
    inst_polivoks(out, n);
    xf_isr_mix(out);                            /* Core2-FxSeq: nothing while xf_run is 0 (every added feature off) */
}

static uint32_t idle_ok;

static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2) != 0) {
        poli_nav_tick_ms(fm1_ms);   /* MIDI clock sync timestamps (and the encoder clock) */
        if (st >= 0xF8u)
            xtra_rt(st, fm1_ms);    /* Core2-FxSeq: clock / start / stop for the arp, seq, drums (LFO sync still below) */
        else if (st < 0xF0u && (int)(st & 0x0Fu) + 1 == poli_nav_midi_ch() && xtra_midi(st, d1, d2, fm1_ms))
            continue;               /* a note the arpeggiator took */
        poli_nav_midi(st, d1, d2);
    }
}

static void app_idle(void)
{
    if (idle_ok) {
        midi_in();
        xtra_tick(fm1_ms);          /* Core2-FxSeq: the clock keeps time while the screen is sent */
    }
}

static void light_section(const poli_nav_view_t *v)
{
    fm1_led_key(BTN_HOME, v->roll);
    fm1_led_key(BTN_EDIT, !v->roll && v->section == NAV_SEC_OSC);
    fm1_led_key(BTN_LFO, !v->roll && v->section == NAV_SEC_LFO);
    fm1_led_key(BTN_FX, !v->roll && v->section == NAV_SEC_FX);
    fm1_led_key(BTN_ENV, !v->roll && v->section == NAV_SEC_AMP);
    fm1_led_key(BTN_GLO, !v->roll && v->section == NAV_SEC_GLOBAL);
#if POLI_ARP
    fm1_led_key(BTN_ARP, xtra_on(0) || poli_arp_on());   /* Core2-FxSeq: on/off and transport lamps (Parity: either arp) */
#else
    fm1_led_key(BTN_ARP, xtra_on(0));           /* Core2-FxSeq: on/off and transport lamps */
#endif
    fm1_led_key(BTN_SEQ, xtra_on(1));
    fm1_led_key(BTN_PLAY, xtra_running());
#if POLI_PADREC
    fm1_led_key(BTN_REC, xtra_rec() || xtra_padrec_armed());
#else
    fm1_led_key(BTN_REC, xtra_rec());
#endif
}

static void service_controls(void)
{
    poli_nav_tick_ms(fm1_ms);
    uint32_t b, rel;
    int32_t e;
    int down, up, held_both;
    static const int knobs[4] = { ENC_KNOB1, ENC_KNOB2, ENC_KNOB3, ENC_KNOB4 };
    int k;

    int pm;
    e = fm1_enc_take(ENC_PRESETS);
    if (e) poli_preset_turn(e, fm1_ms);
    b = fm1_input_edges(&rel);
    (void)rel;
    down = (b & (1u << BTN_OCT_DOWN)) != 0;
    up = (b & (1u << BTN_OCT_UP)) != 0;
    held_both = (fm1_in.buttons & ((1u << BTN_OCT_DOWN) | (1u << BTN_OCT_UP)))
        == ((1u << BTN_OCT_DOWN) | (1u << BTN_OCT_UP));
    if (down || up) {
        if ((down && up) || held_both) {
            poli_nav_octave(0);
            bt_reset();             /* block-timer test build: the same gesture clears the max and the late count */
            xf_st.busy_max = 0;     /* (and core 2's) */
            xf_late = 0;
        }
        else if (down)
            poli_nav_octave(-1);
        else
            poli_nav_octave(1);
    }
    pm = poli_preset_mode();
    if (pm != PV_MODE_NONE) {
        /* A preset view is up: SEL loads (bar) or compares (save screen);
           HOME, a section button or a SELECT turn closes it, nothing else. */
        if (b & ((1u << BTN_HOME) | (1u << BTN_EDIT) | (1u << BTN_LFO) | (1u << BTN_FX) | (1u << BTN_ENV) |
                 (1u << BTN_GLO))) poli_preset_cancel();
        if (b & (1u << BTN_SEL)) poli_preset_sel();
        if (b & (1u << BTN_SAVE)) poli_preset_save_btn(fm1_ms);
        e = fm1_enc_take(ENC_SELECT);
        if (e) poli_preset_cancel();
    } else {
        if (b & (1u << BTN_HOME)) poli_nav_home();
        if (b & (1u << BTN_EDIT)) poli_nav_section(NAV_SEC_OSC);
        if (b & (1u << BTN_LFO)) poli_nav_section(NAV_SEC_LFO);
        if (b & (1u << BTN_FX)) poli_nav_fx();
        if (b & (1u << BTN_ENV)) poli_nav_section(NAV_SEC_AMP);
        if (b & (1u << BTN_GLO)) poli_nav_section(NAV_SEC_GLOBAL);
        if (b & (1u << BTN_SEL)) poli_nav_select_press();
#if POLI_PADREC
        if (b & (1u << BTN_SAVE)) {
            if (poli_nav_xtra() == XW_PADREC) {   /* Parity: SAVE on PAD REC stores the groove in its slot (blob 0) */
                if (xtra_padrec_save()) poli_nav_reserved(pv_padrec_flush() == 0 ? MSG_PAD_SAVED : MSG_PAD_FAILED);
            } else
                poli_preset_save_btn(fm1_ms);
        }
#else
        if (b & (1u << BTN_SAVE)) poli_preset_save_btn(fm1_ms);
#endif
        e = fm1_enc_take(ENC_SELECT);
        if (e) poli_nav_select_turn(e);
    }
    /* Core2-FxSeq: ARP / SEQ open their ADDED window; pressed again there, switch it on or off (SEQ during REC
       writes a rest). PLAY starts / stops the transport. REC arms step recording and shows SEQ. */
    if (b & ((1u << BTN_ARP) | (1u << BTN_SEQ) | (1u << BTN_REC)) && pm != PV_MODE_NONE) poli_preset_cancel();
    /* Parity (23 Nov 2026): ARP acts on the arp whose page is shown. On CLASSIC ARP it switches the classic arp on or
       off; anywhere else it is as before (opens ARPEGGIO, there switches the EXTRAS arp). The Elite's long press is
       not used: no timing, and the EXTRAS ARP never sees a press meant for the classic one. */
    if (b & (1u << BTN_ARP)) {
#if POLI_ARP
        if (poli_nav_xtra() == XW_CARP) poli_nav_select_press();
        else
#endif
        if (poli_nav_xtra() == 0) poli_nav_select_press();
        else poli_nav_xtra_open(0);
    }
    if (b & (1u << BTN_SEQ)) {
#if POLI_PADREC
        if (poli_nav_xtra() == XW_PADREC) {   /* Parity: SEQ on PAD REC clears the groove (SEL undoes it) */
            xtra_padrec_clear();
            poli_nav_reserved(MSG_PAD_CLEARED);
        } else
#endif
        if (xtra_rec()) xtra_rest();
        else if (poli_nav_xtra() == 1) poli_nav_select_press();
        else poli_nav_xtra_open(1);
    }
    if (b & (1u << BTN_PLAY)) {
        xtra_transport(!xtra_running());
        poli_nav_reserved(xtra_running() ? MSG_PLAY : MSG_STOP);
    }
    if (b & (1u << BTN_REC)) {
#if POLI_PADREC
        if (poli_nav_xtra() == XW_PADREC) {   /* Parity (as the Elite): REC on PAD REC arms a take (count-in / next bar) */
            if (!xtra_padrec_rec(fm1_ms)) poli_nav_reserved(MSG_NO_C2);
        } else
#endif
#if POLI_ARP
        if (poli_nav_xtra() == XW_CARP) {     /* Parity: REC on CLASSIC ARP = LATCH on / off (as REC = HOLD on ARPEGGIO) */
            poli_arp_latch_toggle();
            poli_nav_reserved(poli_arp_latched() ? MSG_CARP_LATCH : MSG_CARP_FREE);
        } else
#endif
        if (poli_nav_xtra() == 0) {   /* Arp-Hold: REC on ARPEGGIO = ARP HOLD on / off */
            xtra_hold_toggle(fm1_ms);
            poli_nav_reserved(xtra_hold() ? MSG_ARP_HOLD : MSG_ARP_FREE);
        } else if (poli_nav_xtra() == 2 || poli_nav_xtra() == 10) {   /* Drift-Full: REC on DRUMS / PERFORM = FILL, held */
            xtra_fill_button(1);
        } else if (poli_nav_xtra() == 6) {   /* Drift-Full: REC on DRUM DRIFT = RESTORE the factory pattern */
            xtra_restore();
            poli_nav_reserved(MSG_RESTORE);
        } else if (poli_nav_xtra() == 8) {   /* Drift-Full: REC on GENERATE = LOCK on / off */
            xtra_knob(8, 3, xtra_locked() ? -1 : 1);
            poli_nav_reserved(xtra_locked() ? MSG_LOCK_ON : MSG_LOCK_OFF);
        } else if (poli_nav_xtra() == 7) {   /* Wander-Drift: REC on GROOVE = breakdown / build (held: drop back in) */
            int was = xtra_brk();
            if (!xtra_break_press(fm1_ms)) poli_nav_reserved(MSG_NEED_PLAY);
            else poli_nav_reserved(was == 1 ? MSG_BUILD : was ? MSG_DROP : MSG_BREAK);
        } else {
            xtra_rec_toggle();
            poli_nav_xtra_open(1);
        }
    }
    xtra_rec_held((fm1_in.buttons >> BTN_REC) & 1u, fm1_ms);
    if (xtra_fill_hold() == 2 && !((fm1_in.buttons >> BTN_REC) & 1u)) xtra_fill_button(0);   /* Drift-Full */
    /* Provisional language control: ALGORITHM is unused by NAV v2.
       One turn either way toggles EN/RU. No hold. */
    e = fm1_enc_take(ENC_ALGORITHM);
    if (e) poli_nav_lang_step(e);
    for (k = 0; k < 4; k++) {
        e = fm1_enc_take(knobs[k]);
        if (!e) continue;
        if (poli_preset_mode() == PV_MODE_SAVE) poli_preset_knob(k, e);   /* name, letter, group, action */
        else poli_nav_knob(k, e);
    }
}

static void app_main(void)
{
    uint32_t shown = 0, bt_ms = 0;
#if POLI_VALREPAINT
    uint32_t shown_val = 0;
#endif
    poli_nav_view_t view;
#ifdef DINGHY_HOST
    uint32_t announced = 0;
#endif
    inst_sel = 0;
#ifdef DINGHY_HOST
    xf_core2_boot();            /* (the device ran it in fm1_cstart, before the audio) */
#endif
    poli_nav_init();
    poli_preset_init(fm1_ms);   /* boots into the last used slot (factory first boot: 01 Init) */
#if POLI_PADREC
    pv_padrec_boot();           /* Parity: PAD REC's grooves from blob 0; missing or foreign data stays empty */
#endif
#if POLI_BOOT_SCREEN && !defined(DINGHY_HOST)
    boot_run();                 /* the load screen; returns with the synth's audio IRQ unmasked again */
#endif
    bt_reset();                 /* block-timer test build: the load screen's halves are not counted */
    poli_ui_draw();
    shown = poli_nav_generation();

    for (;;) {
        platform_poll();
        /* MIDI before the key scan so CC 120 can block keys still held. */
        midi_in();
        service_controls();
        /* Core2-FxSeq: the arpeggiator / step record take the keys; with both off this is fm1_in.notes */
#if POLI_ARP
        /* Parity: while the classic arp is on it takes the keys (as on the Elite), except where the keys have another
           job: DRUM STEP, PAD REC and step record. Its clock runs every pass. */
        poli_arp_page_show(poli_nav_xtra() == XW_CARP);
        if (poli_arp_on() && !xtra_rec() && poli_nav_xtra() != 11
#if POLI_PADREC
            && poli_nav_xtra() != XW_PADREC
#endif
            ) {
            (void)poli_arp_keys(fm1_in.notes, 0, fm1_ms);
            polivoks_panel(xtra_keys(0u, polivoks_kb_oct(), fm1_ms), 0, 0, 0, 0, 0, 0, 0, 0, 0);
        } else {
            poli_arp_poll(fm1_ms);
            polivoks_panel(xtra_keys(fm1_in.notes, polivoks_kb_oct(), fm1_ms), 0, 0, 0, 0, 0, 0, 0, 0, 0);
        }
#else
        polivoks_panel(xtra_keys(fm1_in.notes, polivoks_kb_oct(), fm1_ms), 0, 0, 0, 0, 0, 0, 0, 0, 0);
#endif
        xtra_tick(fm1_ms);
        xf_service();
        poli_nav_view(&view);
        light_section(&view);
#ifdef DINGHY_HOST
        if (announced != poli_nav_generation()) {
            printf("%6u ms  sec %d win %d/%d lang %d oct %d\n",
                   fm1_ms, view.section, view.window + 1, view.nwindows,
                   view.lang, view.kb_oct);
            announced = poli_nav_generation();
        }
#endif
        idle_ok = 1;
        if (shown != poli_nav_generation()) {
            shown = poli_nav_generation();
#if POLI_VALREPAINT
            shown_val = poli_nav_value_generation();
#endif
            poli_ui_draw();
        }
#if POLI_VALREPAINT
        else if (shown_val != poli_nav_value_generation()) {
            /* Parity: a knob moved a value only. The panel repaints, unless the header must change too (the '*'
               appears, or a preset message is up, which a turn clears, as before): then the whole screen */
            shown_val = poli_nav_value_generation();
            if (poli_ui_values_ok()) poli_ui_draw_values();
            else {
                poli_nav_touch();
                shown = poli_nav_generation();
                poli_ui_draw();
            }
        }
#endif
        else if (fm1_ms - bt_ms >= 250u) {   /* block-timer test build: the readout, at most 4 times a second */
            bt_ms = fm1_ms;
            if (poli_ui_bt_stale())
                poli_ui_draw_bt();
        }
#if POLI_EGPLOT
        /* Parity (as the Elite): the AMP / filter envelope plots at ~25 Hz, main loop only */
        poli_ui_ampenv_poll(fm1_ms);
        poli_ui_filtenv_poll(fm1_ms);
#endif
#if POLI_SPECTRUM
        poli_ui_spectrum_poll(fm1_ms);   /* the 24-bar footer at ~25 Hz from the main-loop audio snapshot */
#endif
        idle_ok = 0;
        poli_preset_tick(fm1_ms);
        (void)inst_sel;
        (void)hook_tag;
    }
}
