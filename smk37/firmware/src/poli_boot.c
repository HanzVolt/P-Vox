/* SPDX-License-Identifier: GPL-3.0-only */
/* Polivoks load screen (Hans, 2026-11-12, locked design): black, then ПОЛИВОКС (Terminus Bold 10x18 wordmark
 * face at 2x, cream) at the top and the FORM / HanzVolt HV logo directly underneath; both fade in together;
 * then the voice "Polivoks by Hans Volt" (Dmitry); then straight to the synth page (no hold, no fade-out).
 *
 * This code is GPL-3.0-only. The HV logo data it draws (poli_boot_logo.h) is NOT: (C) Hans Volt (HanzVolt),
 * all rights reserved, see ATTRIBUTION.txt. The voice data (poli_boot_voice.h) is TTS output whose licence is
 * UNKNOWN (NOTES.md).
 *
 * Runs once, in app_main, before the main loop. Nothing here is reachable from the audio ISR
 * (fm1_alnk0_irq): the ISR source is unchanged. The fade runs with the ISR live (the synth renders silence: no
 * key or MIDI is read yet). The voice is fed from the main thread with the ALNK0 IRQ masked (route c2 of
 * polivoks-bootscreen-score-readonly-20261112/REPORT.md): poll the half-free flag, fill the free half,
 * ack it; after the clip two whole halves of zeros, then the IRQ is unmasked again (ICFG enable bit only;
 * the vector was already set in fm1_audio_init, before fm1_guard_lock_top) and the synth renders.
 * UNTESTED ON HARDWARE: whether CON2's half flag sets while the IRQ is masked, refill timing (5.8 ms a half),
 * a pending IRQ firing on unmask (harmless: the ISR renders the synth's silence) are unknown. If the flag
 * never comes, the wait gives up after the clip length + 500 ms and the synth starts (the DMA then replays the
 * last synth half: silence). */
#include "poli_boot_logo.h"
#include "poli_boot_voice.h"

#define BOOT_WORD_Y 4          /* ПОЛИВОКС cell 36 rows: 4..39 (ink 10..35) */
#define BOOT_LOGO_Y 48         /* HV logo 188 rows: 48..235; 12 rows between the letters' ink and the logo */
#define BOOT_FADE_MS 600u
#define BOOT_STRIP_H 60        /* 4 strips of 240 x 60 (the device canvas is 240 x 64) */
#define BOOT_ISR_PRIO 3u       /* as platform.c fm1_audio_init */
_Static_assert(BOOT_LOGO_Y >= BOOT_WORD_Y + 2 * TB20_H, "the logo overlaps the wordmark cell");
_Static_assert(BOOT_LOGO_Y + POLI_LOGO_H <= 240, "the logo runs off the screen");
_Static_assert(POLI_LOGO_W == 240, "the logo is drawn as whole canvas rows");
_Static_assert(240u * BOOT_STRIP_H <= CV_MAX, "boot strip exceeds the canvas");

/* Host-test counters (host/test_boot.c defines POLI_BOOT_TEST). The device build keeps no boot state in RAM,
 * so no static of the synth moves (the ISR path's code stays instruction-for-instruction the same). */
#ifdef POLI_BOOT_TEST
static uint32_t boot_frames, boot_halves, boot_timeout;
#define BOOT_COUNT(x) ((x)++)
#define BOOT_FLAG(x) ((x) = 1u)
#else
#define BOOT_COUNT(x) ((void)0)
#define BOOT_FLAG(x) ((void)0)
#endif

static uint16_t boot_dim(uint16_t c, uint32_t k)   /* brightness k 0..256, as FORM splash_dim */
{
    uint32_t r = (uint32_t)(c >> 11), g = (uint32_t)((c >> 5) & 63u), b = (uint32_t)(c & 31u);
    if (k >= 256u)
        return c;
    return (uint16_t)((r * k >> 8) << 11 | (g * k >> 8) << 5 | (b * k >> 8));
}

/* ПОЛИВОКС at 2x from the TB20 bitmaps, centred, into the canvas strip that starts at screen row origin */
static void boot_word(int origin, uint16_t c)
{
    static const uint16_t CP[8] = {0x041F, 0x041E, 0x041B, 0x0418, 0x0412, 0x041E, 0x041A, 0x0421};
    int i, row, col, x0 = 120 - (7 * 2 * TB20_W + 2 * TB20_W) / 2;
    for (i = 0; i < 8; i++) {
        int gi = glyph_at(F_T20, CP[i]);
        if (gi < 0)
            continue;
        for (row = 0; row < TB20_H; row++) {
            unsigned m = TB20_BITS[gi][row];
            int y = BOOT_WORD_Y + 2 * row - origin;
            if (!m || y + 1 < 0 || y >= BOOT_STRIP_H)
                continue;
            for (col = 0; col < TB20_W; col++)
                if (m & (1u << (TB20_W - 1 - col))) {
                    int x = x0 + i * 2 * TB20_W + 2 * col;
                    cv_pset(x, y, c); cv_pset(x + 1, y, c); cv_pset(x, y + 1, c); cv_pset(x + 1, y + 1, c);
                }
        }
    }
}

/* the logo rows inside the strip [origin, origin + BOOT_STRIP_H), palette dimmed once a frame */
static void boot_logo(int origin, const uint16_t *pal)
{
    int32_t y0 = origin > BOOT_LOGO_Y ? origin : BOOT_LOGO_Y;
    int32_t y1 = origin + BOOT_STRIP_H < BOOT_LOGO_Y + POLI_LOGO_H ? origin + BOOT_STRIP_H : BOOT_LOGO_Y + POLI_LOGO_H;
    uint32_t lo, hi, pos = 0, i, at;
    if (y0 >= y1)
        return;
    lo = (uint32_t)(y0 - BOOT_LOGO_Y) * POLI_LOGO_W;
    hi = (uint32_t)(y1 - BOOT_LOGO_Y) * POLI_LOGO_W;
    at = (uint32_t)(y0 - origin) * POLI_LOGO_W;   /* canvas index of logo pixel lo */
    for (i = 0; i < sizeof POLI_LOGO_RLE && pos < hi; i++) {
        uint32_t run = (POLI_LOGO_RLE[i] >> 4) + 1u, c = POLI_LOGO_RLE[i] & 15u, p;
        if (c && pos + run > lo)
            for (p = pos < lo ? lo : pos; p < pos + run && p < hi; p++)
                cv_px[p - lo + at] = pal[c];
        pos += run;
    }
}

static void boot_draw(uint32_t k)
{
    uint16_t pal[16];
    uint32_t ci;
    int origin;
    for (ci = 0; ci < 16u; ci++)
        pal[ci] = swap16(boot_dim(POLI_LOGO_PAL[ci], k));
    for (origin = 0; origin < 240; origin += BOOT_STRIP_H) {
        cv_begin(240, BOOT_STRIP_H, C_BLACK);
        if (origin < BOOT_WORD_Y + 2 * TB20_H)
            boot_word(origin, boot_dim(COL_CREAM, k));
        boot_logo(origin, pal);
        cv_blit(0, (uint32_t)origin);
    }
    BOOT_COUNT(boot_frames);
}

/* ---- the voice: IMA-ADPCM 16 kHz, linear interpolation to 44.1 kHz (exact rational step 16000 / 44100) */
static const int16_t BOOT_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
    97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
    4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t BOOT_IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

typedef struct {
    int32_t pred, idx;         /* decoder state */
    uint32_t n;                /* 16 kHz samples decoded so far */
    uint32_t acc;              /* 44.1 kHz phase: 0..44099 between s0 and s1 */
    int32_t s0, s1;
    uint32_t out;              /* 44.1 kHz frames produced (voice, before the zeros) */
    uint8_t done;
} boot_voice_t;

static int32_t boot_adpcm(boot_voice_t *v)
{
    uint32_t c, st;
    int32_t d;
    if (v->n >= POLI_VOICE_N)
        return 0;
    c = (uint32_t)(POLI_VOICE_ADPCM[v->n >> 1] >> ((v->n & 1u) * 4u)) & 15u;
    v->n++;
    st = (uint32_t)BOOT_STEP[v->idx];
    d = (int32_t)(st >> 3);
    if (c & 4u) d += (int32_t)st;
    if (c & 2u) d += (int32_t)(st >> 1);
    if (c & 1u) d += (int32_t)(st >> 2);
    v->pred += c & 8u ? -d : d;
    if (v->pred > 32767) v->pred = 32767;
    if (v->pred < -32768) v->pred = -32768;
    v->idx += BOOT_IDX[c & 7u];
    if (v->idx < 0) v->idx = 0;
    if (v->idx > 88) v->idx = 88;
    return v->pred;
}

static void boot_voice_init(boot_voice_t *v)
{
    v->pred = v->idx = 0;
    v->n = 0;
    v->acc = 0;
    v->out = 0;
    v->done = 0;
    v->s0 = boot_adpcm(v);
    v->s1 = boot_adpcm(v);
}

/* the next 44.1 kHz sample (Q15); 0 and done once the last 16 kHz sample is passed */
static int32_t boot_voice_next(boot_voice_t *v)
{
    int32_t s;
    uint32_t f;
    if (v->done)
        return 0;
    f = (v->acc << 15) / 44100u;                     /* 0..32767 */
    s = v->s0 + (((v->s1 - v->s0) * (int32_t)f) >> 15);
    v->out++;
    v->acc += POLI_VOICE_RATE;
    if (v->acc >= 44100u) {
        v->acc -= 44100u;
        if (v->n >= POLI_VOICE_N) {
            v->done = 1;                             /* s1 was the last sample (0: the clip ends in zeros) */
        } else {
            v->s0 = v->s1;
            v->s1 = boot_adpcm(v);
        }
    }
    return s;
}

/* one half: L = R = voice x MASTER (as out_gain_block's unmuted gain: v * m >> 12, clamped), Q15 -> 24 bit as
 * the ISR's << 7. Returns 1 when the half was all zeros (past the end of the clip) */
static int boot_fill(int32_t *o, uint32_t frames, boot_voice_t *v, int32_t master)
{
    uint32_t i;
    int was_done = v->done;
    for (i = 0; i < frames; i++) {
        int32_t s = (boot_voice_next(v) * master) >> 12;
        s = s > 32767 ? 32767 : s < -32767 ? -32767 : s;
        o[2u * i] = s * 128;                         /* (= the ISR's << 7, defined for negative s) */
        o[2u * i + 1u] = s * 128;
    }
    return was_done;
}

static void boot_fade_in(void)
{
    uint32_t t0 = fm1_ms, e;
    do {
        e = fm1_ms - t0;
        if (e > BOOT_FADE_MS)
            e = BOOT_FADE_MS;
        boot_draw(256u * e / BOOT_FADE_MS);
        platform_poll();                          /* watchdog, USB, MASTER knob: the ISR is live here */
    } while (e < BOOT_FADE_MS);                   /* the last frame is drawn at exactly 256 */
}

static void boot_voice_play(void)
{
    boot_voice_t v;
    uint32_t t0, zeros = 0;
    const uint32_t limit = POLI_VOICE_N * 1000u / POLI_VOICE_RATE + 500u;
    int32_t m = master_q12;                       /* MASTER as the fade left it; not re-read for the 2 s */
    boot_voice_init(&v);
    fm1_irq_off();
    fm1_irq_mask(FM1_IRQ_ALNK0);                  /* the synth stops rendering; the DMA keeps running */
    fm1_irq_on();
    t0 = fm1_ms;
    while (zeros < 2u) {
        uint8_t p;
        fm1_wdt_feed();
        if (fm1_ms - t0 > limit) {
            BOOT_FLAG(boot_timeout);
            break;
        }
        p = fm1_audio_pending();
        fm1_audio_ack_aux(p);
        if (p & FM1_AUDIO_HALF) {
            zeros += (uint32_t)boot_fill(&abuf[fm1_audio_free_half() * HALF_WORDS], HALF_FRAMES, &v, m);
            fm1_audio_ack_half();
            BOOT_COUNT(boot_halves);
        }
    }
    fm1_irq_off();
    fm1_irq_unmask(FM1_IRQ_ALNK0);           /* the synth again; ICFG only, vector already set */
    fm1_irq_on();
}

static void boot_run(void)
{
    uint32_t rel;
    boot_fade_in();
    boot_voice_play();
    (void)fm1_input_edges(&rel);                  /* presses and turns during the load screen do nothing */
    for (rel = 0; rel < 7u; rel++)
        (void)fm1_enc_take(rel);
}
