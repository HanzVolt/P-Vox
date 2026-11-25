/* SPDX-License-Identifier: GPL-3.0-only */
/* Host frames of the AMP EG window with the animated ADSR plot.
 * Not firmware. Built with -DBOARD_SMK37=1. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"
#include "poli_preset.c"

static uint16_t fb[240 * 240];

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
    if (!f) return;
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

static int32_t abuf[512];

static void render_n(uint32_t n)
{
    while (n) {
        uint32_t k = n > 256u ? 256u : n;
        memset(abuf, 0, sizeof abuf);
        polivoks_render(abuf, k);
        n -= k;
    }
}

static void shot(const char *path)
{
    memset(fb, 0, sizeof fb);
    poli_ui_draw();
    write_ppm(path);
}

static int wait_stage(int want, uint32_t max_samp)
{
    poli_ampenv_view_t ev;
    uint32_t n;
    polivoks_ampenv_view(&ev);
    if (ev.stage == want)
        return 1;
    for (n = 0; n < max_samp; n += 32) {
        render_n(32);
        polivoks_ampenv_view(&ev);
        if (ev.stage == want)
            return 1;
    }
    return 0;
}

static int wait_atk_mid(uint32_t max_samp)
{
    poli_ampenv_view_t ev;
    uint32_t n;
    for (n = 0; n < max_samp; n += 16) {
        render_n(16);
        polivoks_ampenv_view(&ev);
        if (ev.stage == POLI_AMPENV_ATK && ev.level >= 8000 && ev.level <= 20000)
            return 1;
        if (ev.stage != POLI_AMPENV_ATK && ev.stage != POLI_AMPENV_IDLE)
            return ev.level > 0;
    }
    return 0;
}

static int wait_rel_mid(uint32_t max_samp)
{
    poli_ampenv_view_t ev;
    uint32_t n;
    for (n = 0; n < max_samp; n += 16) {
        render_n(16);
        polivoks_ampenv_view(&ev);
        if (ev.stage == POLI_AMPENV_REL && ev.level >= 4000 && ev.level <= ev.sus - 1000)
            return 1;
        if (ev.stage == POLI_AMPENV_IDLE)
            return 0;
    }
    return ev.stage == POLI_AMPENV_REL;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/ampenv-shots";
    char path[256];
    poli_ampenv_view_t ev;

    polivoks_init();
    poli_nav_init();
    poli_preset_init(0);
    poli_nav_section(NAV_SEC_AMP);

    /* Long A, mid D, mid S, long R — idle shape, no dot. */
    polivoks_set_adsr_q(1, 40000, 200000, 16000, 40000);
    snprintf(path, sizeof path, "%s/idle-longA.ppm", dir);
    shot(path);

    polivoks_midi(0x90, 60, 100);
    wait_atk_mid(200000);
    snprintf(path, sizeof path, "%s/attack.ppm", dir);
    shot(path);

    wait_stage(POLI_AMPENV_DEC, 200000);
    polivoks_ampenv_view(&ev);
    /* Mid decay if we can catch it; otherwise first decay frame. */
    if (ev.stage == POLI_AMPENV_DEC && ev.level > ev.sus + 2000) {
        uint32_t n;
        for (n = 0; n < 80000 && ev.stage == POLI_AMPENV_DEC && ev.level > (ev.sus + SMK_AMPENV_PEAK) / 2; n += 16) {
            render_n(16);
            polivoks_ampenv_view(&ev);
        }
    }
    snprintf(path, sizeof path, "%s/decay.ppm", dir);
    shot(path);

    wait_stage(POLI_AMPENV_SUS, 200000);
    snprintf(path, sizeof path, "%s/sustain.ppm", dir);
    shot(path);

    polivoks_midi(0x80, 60, 0);
    wait_rel_mid(200000);
    snprintf(path, sizeof path, "%s/release.ppm", dir);
    shot(path);

    /* Drain, then a high-sustain idle shape for contrast. */
    wait_stage(POLI_AMPENV_IDLE, 200000);
    polivoks_set_adsr_q(1, 200000, 200000, 25000, 80000);
    snprintf(path, sizeof path, "%s/idle-highS.ppm", dir);
    shot(path);

    return 0;
}
