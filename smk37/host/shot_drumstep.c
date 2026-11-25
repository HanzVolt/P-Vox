/* SPDX-License-Identifier: GPL-3.0-only */
/* Drum-Step (22 Nov 2026): host frames of the Elite's DRUM STEP page (empty and edited, EN and RU) and of PAD REC
 * after a DRUM STEP edit, from the real draw path. Not firmware. Built with -DBOARD_SMK37=1 -DSMK_SPEC_HOST_FEED. */
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


static void key(int k) { (void)xtra_keys_smk((uint64_t)1u << k, 0, 0, 0); (void)xtra_keys_smk(0, 0, 0, 0); }
int main(int argc, char **argv)
{
    /* the FM-1 shot's edits, on the Elite's keys (FM-1 key k = Elite key k + 5) */
    static const uint8_t KEYS[][2] = { {0, 2}, {0, 10}, {6, 0}, {6, 1}, {12, 6}, {12, 7}, {16, 9},
                                       {24, 15}, {26, 0}, {7, 14}, {9, 3} };   /* (FM-1 key, step) */
    static const uint8_t CH[3][3] = { {4, 2, 1}, {12, 2, 2}, {6, 6, 3} };   /* (step, FM-1 key, chance) */
    const char *outdir = argc > 1 ? argv[1] : ".";
    char path[300];
    int lang, j;
    polivoks_init();
    poli_nav_init();
    poli_preset_init(0);
    xtra_set_c2(3, 0);
    for (lang = 0; lang < 2; lang++) {
        if (lang) poli_nav_lang_step(1);
        if (!lang) {
            poli_nav_xtra_open(XW_STEP);
            memset(fb, 0, sizeof fb);
            poli_ui_draw();
            snprintf(path, sizeof path, "%s/%s-drum-step.ppm", outdir, "en");
            write_ppm(path);
            for (j = 0; j < (int)(sizeof KEYS / sizeof KEYS[0]); j++) {
                xtra_knob(XW_STEP, 0, KEYS[j][1] - xtra_get(XW_STEP, 0));
                key(KEYS[j][0] + 5);
            }
            for (j = 0; j < 3; j++) {
                xtra_knob(XW_STEP, 0, CH[j][0] - xtra_get(XW_STEP, 0));
                key(CH[j][1] + 5); key(CH[j][1] + 5);
                xtra_knob(XW_STEP, 1, CH[j][2]);
            }
            xtra_toggle(XW_STEP);                    /* SEL: DRUMS on */
            xtra_transport(1);                       /* 120 BPM: 830 ms is step 7 */
            for (j = 1; j <= 830; j++) xtra_tick((uint32_t)j);
        }
        poli_nav_xtra_open(XW_STEP);
        memset(fb, 0, sizeof fb);
        poli_ui_draw();
        snprintf(path, sizeof path, "%s/%s-drum-step-edit.ppm", outdir, lang ? "ru" : "en");
        write_ppm(path);
        poli_nav_xtra_open(XW_PADREC);               /* PAD REC shows the same groove (its working copy followed) */
        memset(fb, 0, sizeof fb);
        poli_ui_draw();
        snprintf(path, sizeof path, "%s/%s-padrec-after-drum-step.ppm", outdir, lang ? "ru" : "en");
        write_ppm(path);
    }
    xtra_transport(0);
    {   /* RU, the empty page: a fresh start */
        poli_nav_init();
        poli_nav_lang_step(1);
        poli_nav_xtra_open(XW_STEP);
        memset(fb, 0, sizeof fb);
        poli_ui_draw();
        snprintf(path, sizeof path, "%s/ru-drum-step.ppm", outdir);
        write_ppm(path);
    }
    fprintf(stderr, "wrote %s/{en,ru}-{drum-step,drum-step-edit,padrec-after-drum-step}.ppm\n", outdir);
    return 0;
}
