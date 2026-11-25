/* SPDX-License-Identifier: GPL-3.0-only */
/* Load screen host checks (firmware/src/poli_boot.c, compiled unchanged with -DPOLI_BOOT_TEST). Not firmware.
 * The FM-1 is modelled: the screen (240x240 framebuffer, the 240x64 canvas path), the ALNK0 DMA (two halves of
 * 256 frames, the half-free flag, the half being played), the IRQ mask, the watchdog, fm1_ms. The model's
 * timings are ASSUMPTIONS (a fade frame takes FRAME_MS; the main loop polls the flag every POLL_FR frames), not
 * FM-1 measurements. The synth after the hand-over is the real engine: polivoks_render, out_gain_block, << 7,
 * exactly as platform.c's ISR.
 *   test_boot OUTDIR [decoded.wav encoder-input.wav]   (the two WAVs: tools/gen_poli_boot.py --wav) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"
#include "poli_preset.c"

static uint16_t fb[240 * 240];
static uint32_t blit_rows_outside;               /* rows blitted outside 0..239 */
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++) fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++) {
        if (y + j >= 240u) { blit_rows_outside++; continue; }
        for (i = 0; i < w && x + i < 240u; i++) { uint16_t c = px[j * w + i]; fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8); }
    }
}
#include "gfx.c"
#include "poli_ui.c"

/* ---- the FM-1 model ---- */
#define FS 44100u
#define HALF_FRAMES 256u
#define HALF_WORDS (HALF_FRAMES * 2u)
#define FM1_IRQ_ALNK0 11u
#define FM1_AUDIO_HALF 0x80u
static volatile uint32_t fm1_ms;
static int32_t abuf[2u * HALF_WORDS];
static int32_t master_q12 = 4096;
#include "out_mute.h"
static int irq_enabled = 1, irq_mask_calls, irq_unmask_calls, irq_attach_calls, irq_cli;
static void isr_alnk0(void) {}
static void fm1_irq_off(void) { irq_cli = 1; }
static void fm1_irq_on(void) { irq_cli = 0; }
static void fm1_irq_mask(uint32_t n) { if (n == FM1_IRQ_ALNK0) { irq_enabled = 0; irq_mask_calls++; } }
static void fm1_irq_unmask(uint32_t n) { if (n == FM1_IRQ_ALNK0) { irq_enabled = 1; irq_unmask_calls++; } }
static void fm1_irq_attach(uint32_t n, void (*h)(void), uint32_t prio)
{ (void)n; (void)h; (void)prio; irq_attach_calls++; }   /* must stay 0: no vector write after lock_top */
/* watchdog: the longest model time between two feeds */
static uint64_t now_fr;                          /* model time in 44.1 kHz frames */
static uint64_t wdt_last, wdt_maxgap;
static uint32_t wdt_feeds;
static void fm1_wdt_feed(void) { if (now_fr - wdt_last > wdt_maxgap) wdt_maxgap = now_fr - wdt_last; wdt_last = now_fr; wdt_feeds++; }
static void model_advance(uint32_t frames);
/* DMA: plays half `playing`; at its end it switches and sets the flag (the finished half is free) */
static uint32_t playing, pos_in_half, flag, flag_never;
static uint32_t late;                            /* halves played again without a refill (underrun) */
static uint8_t filled[2];
static int32_t *spk; static uint64_t spk_n, spk_cap;   /* what the speaker got, left channel, 24-bit words */
static uint32_t POLL_FR = 8;                     /* ASSUMED main-loop poll period in frames */
static uint8_t fm1_audio_pending(void) { model_advance(POLL_FR); return (uint8_t)(flag ? FM1_AUDIO_HALF : 0u); }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return playing ^ 1u; }
static void fm1_audio_ack_half(void) { flag = 0; filled[playing ^ 1u] = 1; }
/* the ISR, as platform.c fm1_alnk0_irq (no USB tap on the host) */
static long isr_runs, isr_runs_masked;
static int32_t *rec_out; static uint32_t rec_blocks;   /* test 8: keys by ISR half, outputs recorded */
static void isr_model(void)
{
    int32_t *o = &abuf[fm1_audio_free_half() * HALF_WORDS];
    uint32_t i;
    isr_runs++;
    if (rec_out) {
        uint32_t h = rec_blocks;
        polivoks_panel((h >= 20u && h < 200u) ? (1u << 12) : (h >= 220u && h < 260u) ? (1u << 5 | 1u << 9) : 0u,
                       0, 0, 0, 0, 0, 0, 0, 0, 0);
    }
    for (i = 0; i < HALF_WORDS; i++) o[i] = 0;
    polivoks_render(o, HALF_FRAMES);
    out_gain_block(o, HALF_WORDS, master_q12);
    for (i = 0; i < HALF_WORDS; i++) o[i] = (int32_t)((uint32_t)o[i] << 7);   /* platform.c's <<= 7, bitwise the same */
    if (rec_out && rec_blocks < 400u) { memcpy(rec_out + rec_blocks * HALF_WORDS, o, sizeof(int32_t) * HALF_WORDS); rec_blocks++; }
    fm1_audio_ack_half();
}
static void model_advance(uint32_t frames)
{
    while (frames--) {
        if (spk && spk_n < spk_cap) spk[spk_n++] = abuf[playing * HALF_WORDS + 2u * pos_in_half];
        now_fr++;
        if (now_fr % 441u == 0) fm1_ms += 10u;   /* 441 frames = 10 ms */
        if (++pos_in_half == HALF_FRAMES) {
            pos_in_half = 0;
            filled[playing] = 0;
            playing ^= 1u;
            if (!filled[playing] && spk) late++;
            if (!flag_never) flag = 1;
            if (flag && irq_enabled && !irq_cli) isr_model();
            else if (flag && !irq_enabled) isr_runs_masked += 0;
        }
    }
}
/* platform_poll: the fade calls it once a frame. ASSUMED frame time (4 strips of 240x60 RGB565 at 12 MHz SPI:
   115,200 B = 76.8 ms, plus the RLE decode): FRAME_MS */
static uint32_t FRAME_MS = 80;
static uint32_t polls;
static void platform_poll(void) { polls++; fm1_wdt_feed(); model_advance(FRAME_MS * 441u / 10u); }
static uint32_t edges_taken, encs_taken;
static uint32_t fm1_input_edges(uint32_t *rel) { *rel = 0; edges_taken++; return 0; }
static int32_t fm1_enc_take(uint32_t e) { (void)e; encs_taken++; return 0; }

#define POLI_BOOT_TEST 1
/* record every fade frame */
static uint16_t frames_fb[16][240 * 240];
static uint32_t frame_k[16];
static uint32_t nframes_rec;
#define cv_blit(x, y) do { cv_blit_rec((x), (y)); } while (0)
static void cv_blit_rec(uint32_t x, uint32_t y);
#include "poli_boot.c"
#undef cv_blit
static void cv_blit_rec(uint32_t x, uint32_t y)
{
    lcd_blit(x, y, cv_w, cv_h, cv_px);
    if (y + cv_h >= 240u && nframes_rec < 16u) { memcpy(frames_fb[nframes_rec], fb, sizeof fb); nframes_rec++; }
}

/* ---- tests ---- */
static int fails, passes;
#define CHECK(name, cond, ...) do { if (cond) { passes++; printf("PASS %s: ", name); } else { fails++; printf("FAIL %s: ", name); } printf(__VA_ARGS__); printf("\n"); } while (0)
static const char *outdir = ".";

static void write_ppm(const char *name, const uint16_t *f)
{
    char p[600]; FILE *o; uint32_t i;
    snprintf(p, sizeof p, "%s/%s", outdir, name);
    if (!(o = fopen(p, "wb"))) return;
    fprintf(o, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = f[i]; uint8_t rgb[3];
        rgb[0] = (uint8_t)(((c >> 11) & 31u) * 255u / 31u); rgb[1] = (uint8_t)(((c >> 5) & 63u) * 255u / 63u); rgb[2] = (uint8_t)((c & 31u) * 255u / 31u);
        fwrite(rgb, 1, 3, o);
    }
    fclose(o);
}
static void write_wav16(const char *name, const int16_t *s, uint32_t n, uint32_t rate)
{
    char p[600]; FILE *o; uint32_t v;
    snprintf(p, sizeof p, "%s/%s", outdir, name);
    if (!(o = fopen(p, "wb"))) return;
    fwrite("RIFF", 1, 4, o); v = 36u + 2u * n; fwrite(&v, 4, 1, o); fwrite("WAVEfmt ", 1, 8, o);
    v = 16; fwrite(&v, 4, 1, o); { uint16_t a = 1, ch = 1; fwrite(&a, 2, 1, o); fwrite(&ch, 2, 1, o); }
    fwrite(&rate, 4, 1, o); v = rate * 2u; fwrite(&v, 4, 1, o); { uint16_t ba = 2, bits = 16; fwrite(&ba, 2, 1, o); fwrite(&bits, 2, 1, o); }
    fwrite("data", 1, 4, o); v = 2u * n; fwrite(&v, 4, 1, o); fwrite(s, 2, n, o); fclose(o);
}
static int16_t *read_wav16(const char *path, uint32_t *n)
{
    FILE *f = fopen(path, "rb"); long sz; int16_t *d; uint8_t *raw; uint32_t i, off = 12;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    raw = malloc((size_t)sz); if (fread(raw, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(raw); return 0; } fclose(f);
    while (off + 8 <= (uint32_t)sz) {
        uint32_t len = (uint32_t)raw[off + 4] | (uint32_t)raw[off + 5] << 8 | (uint32_t)raw[off + 6] << 16 | (uint32_t)raw[off + 7] << 24;
        if (!memcmp(raw + off, "data", 4)) {
            *n = len / 2u; d = malloc(len);
            for (i = 0; i < *n; i++) d[i] = (int16_t)(raw[off + 8 + 2 * i] | raw[off + 9 + 2 * i] << 8);
            free(raw); return d;
        }
        off += 8 + len;
    }
    free(raw); return 0;
}

/* the whole voice through boot_voice_next (no DMA): the 44.1 kHz stream */
static int32_t *voice_stream(uint32_t *n)
{
    boot_voice_t v; uint32_t cap = 200000, k = 0; int32_t *s = malloc(cap * 4);
    boot_voice_init(&v);
    while (!v.done && k < cap) s[k++] = boot_voice_next(&v);
    *n = k; return s;
}

static void reset_model(void)
{
    memset(abuf, 0, sizeof abuf); playing = 0; pos_in_half = 0; flag = 0; flag_never = 0; late = 0;
    filled[0] = filled[1] = 1; irq_enabled = 1; irq_mask_calls = irq_unmask_calls = irq_attach_calls = 0; irq_cli = 0;
    isr_runs = isr_runs_masked = 0; boot_halves = boot_timeout = 0; boot_frames = 0; nframes_rec = 0;
    wdt_last = now_fr; wdt_maxgap = 0; wdt_feeds = 0; polls = 0; edges_taken = encs_taken = 0;
}

int main(int argc, char **argv)
{
    uint32_t i, n;
    if (argc > 1) outdir = argv[1];
    poli_nav_init();
    poli_preset_init(0);

    /* 1. glyphs: every letter of ПОЛИВОКС is in TB20 */
    {
        static const uint16_t CP[8] = {0x041F, 0x041E, 0x041B, 0x0418, 0x0412, 0x041E, 0x041A, 0x0421};
        int miss = 0; for (i = 0; i < 8u; i++) miss += glyph_at(F_T20, CP[i]) < 0;
        CHECK("word_glyphs", miss == 0, "missing %d of 8", miss);
    }

    /* 2. the fade with the full model, then the voice, then the synth */
    reset_model();
    {
        static int32_t sp[400000]; uint64_t t_fade0 = now_fr, t_voice0, t_end; long isr_before;
        spk = 0;
        fm1_ms = 0;
        boot_fade_in();
        CHECK("fade_frames", boot_frames == nframes_rec && boot_frames >= 2u && boot_frames <= 16u,
              "%u frames drawn over %u ms of model time (ASSUMED %u ms a frame)", boot_frames,
              (unsigned)((now_fr - t_fade0) * 1000u / FS), FRAME_MS);
        CHECK("fade_watchdog", polls == boot_frames && wdt_maxgap <= FRAME_MS * 441u / 10u,
              "platform_poll %u times (once a frame); longest gap between feeds %.1f ms", polls, wdt_maxgap * 1000.0 / FS);
        {   /* frame 0 all black; last frame == a k=256 draw; brightness never decreases */
            uint32_t nz0 = 0, mono = 1, f, sumprev = 0;
            for (i = 0; i < 240u * 240u; i++) nz0 += frames_fb[0][i] != 0;
            for (f = 0; f < nframes_rec; f++) {
                uint32_t s = 0; for (i = 0; i < 240u * 240u; i++) s += (frames_fb[f][i] >> 11) + ((frames_fb[f][i] >> 5) & 63u) + (frames_fb[f][i] & 31u);
                if (s < sumprev) { mono = 0; } sumprev = s; frame_k[f] = s;
            }
            CHECK("fade_first_black", nz0 == 0, "frame 0 (k=0): %u non-black pixels", nz0);
            CHECK("fade_monotonic", mono, "summed brightness never falls over %u frames", nframes_rec);
            {
                uint16_t keep[240 * 240]; uint32_t d = 0;
                memcpy(keep, fb, sizeof fb); memset(fb, 0, sizeof fb);
                boot_draw(256u);
                for (i = 0; i < 240u * 240u; i++) d += fb[i] != keep[i];
                CHECK("fade_ends_full", d == 0, "last fade frame vs a k=256 draw: %u pixels differ", d);
                write_ppm("boot-final.ppm", fb);
            }
            for (f = 0; f < nframes_rec; f++) { char nm[64]; snprintf(nm, sizeof nm, "boot-fade-%02u.ppm", f); write_ppm(nm, frames_fb[f]); }
        }
        /* layout of the final frame */
        {
            int wy0 = 999, wy1 = -1, wx0 = 999, wx1 = -1, ly0 = 999, ly1 = -1, x, y, bad = 0, cream = 0, outside = 0;
            uint16_t crm = COL_CREAM;
            for (y = 0; y < 240; y++) for (x = 0; x < 240; x++) {
                uint16_t c = fb[y * 240 + x]; int j, inpal = 0;
                if (!c) continue;
                for (j = 1; j < 16; j++) inpal |= c == POLI_LOGO_PAL[j];
                if (c == crm) { cream++; if (y < wy0) wy0 = y; if (y > wy1) wy1 = y; if (x < wx0) wx0 = x; if (x > wx1) wx1 = x; }
                else if (inpal) { if (y < ly0) ly0 = y; if (y > ly1) ly1 = y; }
                else bad++;
                if (y >= BOOT_LOGO_Y + POLI_LOGO_H || (y < BOOT_LOGO_Y && c != crm)) outside++;
            }
            CHECK("layout_colours", bad == 0, "%d pixels neither cream nor a logo palette colour", bad);
            CHECK("layout_word", wy0 >= BOOT_WORD_Y && wy1 < BOOT_LOGO_Y && wx0 >= 40 && wx1 <= 199 && wx0 + wx1 == 239,
                  "ПОЛИВОКС ink rows %d..%d, columns %d..%d (cell 40..199, centred), %d px", wy0, wy1, wx0, wx1, cream);
            CHECK("layout_logo", ly0 >= BOOT_LOGO_Y && ly1 <= BOOT_LOGO_Y + POLI_LOGO_H - 1 && outside == 0,
                  "logo pixels rows %d..%d (box %d..%d); gap letters to logo box %d rows; stray %d", ly0, ly1, BOOT_LOGO_Y,
                  BOOT_LOGO_Y + POLI_LOGO_H - 1, BOOT_LOGO_Y - 1 - wy1, outside);
            CHECK("layout_blit_inside", blit_rows_outside == 0, "%u rows blitted past row 239", blit_rows_outside);
        }
        {   /* the logo pixels equal the FORM picture decoded straight */
            uint32_t pos = 0, d = 0, k;
            for (i = 0; i < sizeof POLI_LOGO_RLE; i++) {
                uint32_t run = (POLI_LOGO_RLE[i] >> 4) + 1u, c = POLI_LOGO_RLE[i] & 15u;
                for (k = 0; k < run; k++, pos++) {
                    uint32_t yy = BOOT_LOGO_Y + pos / 240u, xx = pos % 240u;
                    uint16_t want = c ? POLI_LOGO_PAL[c] : 0;
                    if (yy < 240u && fb[yy * 240u + xx] != want) d++;
                }
            }
            CHECK("logo_exact", pos == 240u * 188u && d == 0, "RLE covers %u px (want 45120); %u px differ from the picture", pos, d);
        }

        /* the voice: mask, feed, zeros, unmask */
        spk = sp; spk_n = 0; spk_cap = sizeof sp / sizeof sp[0]; late = 0;
        isr_before = isr_runs; t_voice0 = now_fr;
        wdt_maxgap = 0; wdt_last = now_fr;
        boot_voice_play();
        t_end = now_fr;
        CHECK("irq_masked_then_unmasked", irq_mask_calls == 1 && irq_unmask_calls == 1 && irq_attach_calls == 0 && irq_enabled,
              "mask %d, unmask %d, attach %d, enabled after %d", irq_mask_calls, irq_unmask_calls, irq_attach_calls, irq_enabled);
        CHECK("isr_silent_while_masked", isr_runs == isr_before, "ISR runs during the voice: %ld", isr_runs - isr_before);
        CHECK("no_underrun", late == 0, "%u halves replayed without a refill (poll every %u frames)", late, POLL_FR);
        CHECK("no_timeout", boot_timeout == 0, "gave up: %u", boot_timeout);
        CHECK("voice_watchdog", wdt_maxgap <= POLL_FR, "longest gap between feeds %.3f ms", wdt_maxgap * 1000.0 / FS);
        {
            uint32_t nv; int32_t *vs = voice_stream(&nv); uint64_t first = 0, k; int ok = 1;
            uint32_t want_n = (uint32_t)(((uint64_t)(POLI_VOICE_N - 1u) * FS + POLI_VOICE_RATE - 1u) / POLI_VOICE_RATE);
            int32_t pk = 0, maxstep_edge = 0, maxstep = 0;
            static int16_t out16[200000];
            /* the speaker stream: voice starts at the first filled half; locate by the first non-zero */
            while (first < spk_n && sp[first] == 0) first++;
            /* the voice stream has leading zeros of its own (s0 = 0): align on its first non-zero */
            { uint32_t f2 = 0; while (f2 < nv && vs[f2] == 0) f2++;
              for (k = 0; k + f2 < nv && first + k < spk_n; k++) if (sp[first + k] != ((vs[f2 + k] * 4096) >> 12) * 128) ok = 0;
              CHECK("speaker_equals_decoder", ok && k == nv - f2, "%llu of %u voice frames reach the speaker bit-exact", (unsigned long long)k, nv - f2); }
            CHECK("clip_length", nv == want_n, "%u frames at 44.1 kHz = %.4f s (want %u); 16 kHz samples %u = %.4f s; halves fed %u",
                  nv, nv / 44100.0, want_n, POLI_VOICE_N, POLI_VOICE_N / 16000.0, boot_halves);
            for (k = 0; k < nv; k++) { int32_t a = vs[k] < 0 ? -vs[k] : vs[k]; if (a > pk) pk = a; out16[k] = (int16_t)vs[k];
                if (k) { int32_t d = vs[k] - vs[k - 1]; d = d < 0 ? -d : d; if (d > maxstep) maxstep = d; if ((k < 221u || k + 221u > nv) && d > maxstep_edge) maxstep_edge = d; } }
            CHECK("voice_peak", pk <= 18000 && pk >= 12000, "peak %d of 32767 (%.2f dBFS Q15), no clamp", pk, 20.0 * __builtin_log10(pk / 32767.0));
            CHECK("voice_edges", vs[0] == 0 && vs[nv - 1] == 0 && maxstep_edge <= 64,
                  "first %d last %d; largest step in the first/last 5 ms %d (largest anywhere %d)", vs[0], vs[nv - 1], maxstep_edge, maxstep);
            write_wav16("boot-voice-decoded-44k.wav", out16, nv, 44100u);
            printf("INFO first non-zero voice frame at the speaker %.2f ms after the IRQ mask (model)\n", first * 1000.0 / FS);
            free(vs);
        }
        /* hand-over: the last voice frames, the zero halves, then the synth; continue the speaker 0.5 s */
        {
            uint64_t k, nz_tail = 0, vend = spk_n; int32_t worst = 0;
            spk_cap = spk_n + 22050u;
            model_advance(22050u);
            for (k = vend - HALF_FRAMES; k < spk_n; k++) { if (sp[k] != 0) nz_tail++; }
            for (k = vend - 882u - HALF_FRAMES; k < spk_n; k++) { int32_t d = (sp[k] - sp[k - 1]) >> 7; d = d < 0 ? -d : d; if (d > worst) worst = d; }
            CHECK("handover_silence", nz_tail == 0 && isr_runs > isr_before, "%llu non-zero frames from the last zero half to 0.5 s of synth (ISR ran %ld halves)",
                  (unsigned long long)nz_tail, isr_runs - isr_before);
            CHECK("handover_no_click", worst <= 64, "largest frame-to-frame step from 20 ms before the end of the voice to 0.5 s of synth %d (Q15)", worst);
            printf("INFO model: voice start to synth %.4f s; speaker frames %llu\n", (t_end - t_voice0) / 44100.0, (unsigned long long)spk_n);
        }
        spk = 0;
    }
    /* 3. boot_run as app_main calls it: fade, voice, drain, in that order */
    reset_model();
    {
        uint32_t fr0, h0;
        boot_run();
        fr0 = boot_frames; h0 = boot_halves;
        CHECK("boot_run_sequence", fr0 >= 2u && h0 > 0u && irq_mask_calls == 1 && irq_unmask_calls == 1 && irq_attach_calls == 0 && irq_enabled &&
              edges_taken == 1 && encs_taken == 7 && boot_timeout == 0 && wdt_maxgap <= FRAME_MS * 441u / 10u,
              "%u fade frames, %u halves fed, mask %d unmask %d attach %d, drain edges %u encoders %u, longest watchdog gap %.1f ms",
              fr0, h0, irq_mask_calls, irq_unmask_calls, irq_attach_calls, edges_taken, encs_taken, wdt_maxgap * 1000.0 / FS);
    }

    /* 4. ADPCM: the C decoder vs the generator's Python decoder, and vs the encoder input */
    if (argc > 3) {
        uint32_t nd, ni; int16_t *pd = read_wav16(argv[2], &nd), *pin = read_wav16(argv[3], &ni);
        if (pd && pin) {
            boot_voice_t v; uint32_t d = 0; double se = 0, ss = 0; int32_t me = 0;
            v.pred = v.idx = 0; v.n = 0;
            for (i = 0; i < nd; i++) { int32_t s = boot_adpcm(&v); if (s != pd[i]) d++; { int32_t e = s - pin[i]; se += (double)e * e; ss += (double)pin[i] * pin[i]; if ((e < 0 ? -e : e) > me) me = e < 0 ? -e : e; } }
            CHECK("adpcm_bitexact", nd == POLI_VOICE_N && d == 0, "%u samples, %u differ from the generator's decoder", nd, d);
            CHECK("adpcm_error", 10.0 * __builtin_log10(ss / se) > 20.0, "decoded vs encoder input: SNR %.2f dB, max |err| %d", 10.0 * __builtin_log10(ss / se), me);
        } else CHECK("adpcm_files", 0, "could not read %s / %s", argv[2], argv[3]);
    }

    /* 5. MASTER: half gain halves the voice (as out_gain_block) */
    {
        boot_voice_t a, b; static int32_t oa[2 * 256], ob[2 * 256]; uint32_t h, d = 0;
        boot_voice_init(&a); boot_voice_init(&b);
        for (h = 0; h < 40u; h++) { boot_fill(oa, 256, &a, 4096); boot_fill(ob, 256, &b, 2048);
            for (i = 0; i < 512u; i++) if ((ob[i] >> 7) != (((oa[i] >> 7) * 2048) >> 12)) d++; }
        CHECK("master_scales", d == 0, "%u of %u words differ from v*2048>>12", d, 40u * 512u);
    }

    /* 6. no half flag ever (masked IRQ might not set it: UNKNOWN on hardware): the wait gives up, the synth starts */
    reset_model();
    {
        uint64_t t0 = now_fr;
        flag_never = 1;
        boot_voice_play();
        CHECK("flag_never_timeout", boot_timeout == 1 && irq_unmask_calls == 1 && irq_attach_calls == 0 && irq_enabled,
              "gave up after %.0f ms (limit %u ms), IRQ unmasked again", (now_fr - t0) * 1000.0 / FS,
              POLI_VOICE_N * 1000u / POLI_VOICE_RATE + 500u);
        flag_never = 0;
    }

    /* 7. a slow main loop (poll every 300 frames > a half) is caught by the model (negative control) */
    reset_model();
    {
        static int32_t sp2[200000];
        spk = sp2; spk_n = 0; spk_cap = 200000; POLL_FR = 300; late = 0;
        boot_voice_play();
        CHECK("slow_poll_detected", late > 0, "poll every 300 frames: %u underrun halves (the model sees them)", late);
        POLL_FR = 8; spk = 0;
    }

    /* 8. the synth after the load screen is the same synth: the ISR model renders 400 halves of a key script
          (keys by ISR half number), once straight and once with boot_voice_play after 10 halves */
    {
        static int32_t ra[2 * 256 * 400], rb[2 * 256 * 400]; uint32_t d = 0;
        for (int pass = 0; pass < 2; pass++) {
            reset_model();
            polivoks_init(); poli_nav_init(); poli_preset_init(0);
            rec_out = pass ? rb : ra; rec_blocks = 0;
            if (pass) { model_advance(10u * 256u); boot_voice_play(); }
            while (rec_blocks < 400u) model_advance(64u);
            rec_out = 0;
        }
        for (i = 0; i < 2u * 256u * 400u; i++) d += ra[i] != rb[i];
        CHECK("synth_after_boot_same", d == 0, "%u of %u words differ (400 ISR halves, key script, voice after half 10)", d, 2u * 256u * 400u);
    }
    /* idle synth is exactly silent before any key (what the fade's live ISR renders) */
    {
        int32_t o[512]; uint32_t h, nz = 0;
        polivoks_init(); poli_nav_init(); poli_preset_init(0);
        for (h = 0; h < 200u; h++) { memset(o, 0, sizeof o); polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0); polivoks_render(o, 256); out_gain_block(o, 512, 4096); for (i = 0; i < 512u; i++) nz += o[i] != 0; }
        CHECK("idle_synth_silent", nz == 0, "%u non-zero words in 200 idle halves (1.16 s)", nz);
    }
    (void)n;
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
