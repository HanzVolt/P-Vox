/* SPDX-License-Identifier: GPL-3.0-only */
/* Seq64 (22 Nov 2026): host frames of the Elite, from the real draw path. Not firmware. Built with -DBOARD_SMK37=1
 * -DSMK_SPEC_HOST_FEED. Mode "all": every ADDED window 0..12 at power-on (EN, RU), for the parent-vs-new identity
 * (public API only, so it builds on the parent too). Mode "seq": the SEQUENCER window at STEPS 1..64. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"
#include "poli_preset.c"

static uint16_t fb[240 * 240];
static int16_t host_spec[128];

int smk_spectrum_host_snap(int16_t *dst, int n)
{
    int i;
    if (!dst || n < 1)
        return 0;
    if (n > 128)
        n = 128;
    for (i = 0; i < n; i++)
        dst[i] = host_spec[i];
    return n;
}

static void lcd_sync(void) {}

static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t row, col;
    if (!w || !h || x >= 240u || y >= 240u) return;
    if (x + w > 240u) w = 240u - x;
    if (y + h > 240u) h = 240u - y;
    for (row = 0; row < h; row++)
        for (col = 0; col < w; col++)
            fb[(y + row) * 240u + x + col] = c;
}

static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t row, col;
    if (!w || !h || x >= 240u || y >= 240u) return;
    if (x + w > 240u) w = 240u - x;
    if (y + h > 240u) h = 240u - y;
    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            uint16_t s = px[row * w + col];
            fb[(y + row) * 240u + x + col] = (uint16_t)((s >> 8) | (s << 8));
        }
    }
}

#include "gfx.c"
#include "poli_ui.c"

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    uint32_t i;
    if (!f) {
        perror(path);
        return;
    }
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3];
        rgb[0] = (uint8_t)(((c >> 11) & 31u) * 255u / 31u);
        rgb[1] = (uint8_t)(((c >> 5) & 63u) * 255u / 63u);
        rgb[2] = (uint8_t)((c & 31u) * 255u / 31u);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}


static uint32_t now;
static void set(int w, int i, int v) { xtra_knob(w, i, v - xtra_get(w, i)); }
static void run(uint32_t ms) { uint32_t e = now + ms; while (now < e) xtra_tick(++now); }
static void fresh(int lang)
{
    poli_nav_init();
    poli_preset_init(0);
    if (lang) poli_nav_lang_step(1); else poli_nav_home();
    now = 0;
    xtra_set_c2(3, 0);
}
int main(int argc, char **argv)
{
    static const char *NAME[6] = { "default-16", "single-note-3", "steps-37-stopped", "steps-64-playing", "steps-64-rec",
                                   "steps-17-playing" };
    const char *mode = argc > 1 ? argv[1] : "seq", *outdir = argc > 2 ? argv[2] : "build/seq64-shots";
    int lang, f, i;
    char path[256];
    for (lang = 0; lang < 2; lang++) {
        if (!strcmp(mode, "all")) {
            for (f = 0; f < NAV_XWIN; f++) {
                fresh(lang);
                poli_nav_xtra_open(f);
                poli_ui_draw();
                snprintf(path, sizeof path, "%s/%s-win%02d.ppm", outdir, lang ? "ru" : "en", f);
                write_ppm(path);
            }
            continue;
        }
        for (f = 0; f < 6; f++) {
            fresh(lang);
            for (i = 16; i < 64; i++) xtra_set_seq_note(i, 41 + (i * 5) % 17);   /* a longer line after the first 16 */
            switch (f) {
            case 1: xtra_set_seq_note(0, 53); xtra_set_seq_note(1, 255); xtra_set_seq_note(2, 255); set(1, 1, 3);
                    xtra_toggle(1); xtra_transport(1); run(130); break;          /* one note in 3 steps, playing */
            case 2: set(1, 1, 37); break;                                       /* stopped: the page with the last step */
            case 3: set(1, 1, 64); xtra_toggle(1); xtra_transport(1); run(125 * 20 + 10); break;   /* step 21 */
            case 4: set(1, 1, 64); xtra_rec_toggle(); for (i = 0; i < 49; i++) xtra_rest(); break; /* REC at step 50 */
            case 5: set(1, 1, 17); xtra_toggle(1); xtra_transport(1); run(125 * 16 + 10); break;   /* step 17 */
            default: break;
            }
            poli_nav_xtra_open(1);
            poli_ui_draw();
            snprintf(path, sizeof path, "%s/%s-%02d-seq-%s.ppm", outdir, lang ? "ru" : "en", f + 1, NAME[f]);
            write_ppm(path);
        }
    }
    return 0;
}
