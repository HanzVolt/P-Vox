/* SPDX-License-Identifier: GPL-3.0-only */
/* AMP / FILTER EG plot sweeps from the real draw path.
 * Built with -DBOARD_SMK37=1 -DSMK_SPEC_HOST_FEED. */
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
    if (!dst || n < 1) return 0;
    if (n > 128) n = 128;
    for (i = 0; i < n; i++) dst[i] = host_spec[i];
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
    if (!f) { perror(path); return; }
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
static void dummy_spec(void)
{
    int i;
    for (i = 0; i < 128; i++) {
        int v = 16000 - i * 90;
        if (v < 300) v = 300;
        host_spec[i] = (int16_t)(((i * 17) & 1) ? v : -v);
    }
}
static void set_adsr(int a, int d, int s, int r)
{
    poli_nav_reflect_slot(SLOT_ATK_A, a);
    poli_nav_reflect_slot(SLOT_DEC_A, d);
    poli_nav_reflect_slot(SLOT_SUS_A, s);
    poli_nav_reflect_slot(SLOT_REL_A, r);
    poli_nav_reflect_slot(SLOT_ATK_F, a);
    poli_nav_reflect_slot(SLOT_DEC_F, d);
    poli_nav_reflect_slot(SLOT_SUS_F, s);
    poli_nav_reflect_slot(SLOT_REL_F, r);
}
static void shot(const char *path)
{
    int i;
    for (i = 0; i < 4; i++) {
        dummy_spec();
        memset(fb, 0, sizeof fb);
        poli_ui_draw();
    }
    write_ppm(path);
}

/* Hold the other three at a mid setting so the swept stage is obvious. */
static const int SWP[] = { 0, 16, 32, 48, 64, 80, 96, 127 };

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/tmp/natural-shots";
    const char *tag = argc > 2 ? argv[2] : "run";
    char path[256];
    int i, k, page;
    const char *sides[] = { "amp", "filter" };
    const int secs[] = { NAV_SEC_AMP, NAV_SEC_FX };

    polivoks_init();
    poli_nav_init();
    poli_preset_init(0);

    /* Characteristic before/after frames */
    struct { const char *name; int a,d,s,r; } frames[] = {
        { "init", 0, 0, 64, 4 },
        { "mid", 64, 64, 64, 64 },
        { "square", 0, 0, 127, 0 },
        { "rel8", 0, 32, 64, 8 },
        { "rel16", 0, 32, 64, 16 },
        { "rel64", 0, 32, 64, 64 },
        { "rel127", 0, 32, 64, 127 },
        { "sus0", 0, 48, 0, 48 },
        { "sus32", 0, 48, 32, 48 },
        { "sus64", 0, 48, 64, 48 },
        { "sus96", 0, 48, 96, 48 },
        { "sus127", 0, 48, 127, 48 },
    };

    for (page = 0; page < 2; page++) {
        poli_nav_section(secs[page]);
        for (k = 0; k < (int)(sizeof frames / sizeof frames[0]); k++) {
            set_adsr(frames[k].a, frames[k].d, frames[k].s, frames[k].r);
            snprintf(path, sizeof path, "%s/%s-%s-%s.ppm", dir, sides[page], tag, frames[k].name);
            shot(path);
        }
        /* 0..127 sweeps of each stage */
        for (i = 0; i < 8; i++) {
            int v = SWP[i];
            set_adsr(v, 64, 64, 64);
            snprintf(path, sizeof path, "%s/%s-%s-A%d.ppm", dir, sides[page], tag, v);
            shot(path);
            set_adsr(32, v, 64, 32);
            snprintf(path, sizeof path, "%s/%s-%s-D%d.ppm", dir, sides[page], tag, v);
            shot(path);
            set_adsr(0, 48, v, 48);
            snprintf(path, sizeof path, "%s/%s-%s-S%d.ppm", dir, sides[page], tag, v);
            shot(path);
            set_adsr(0, 32, 64, v);
            snprintf(path, sizeof path, "%s/%s-%s-R%d.ppm", dir, sides[page], tag, v);
            shot(path);
        }
    }
    return 0;
}
