/* SPDX-License-Identifier: GPL-3.0-only */
/* Host frames of FILTER, AMP EG, and generator pages from the real
 * draw path. Not firmware. Built with -DBOARD_SMK37=1 -DSMK_SPEC_HOST_FEED. */
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

static int wait_amp(int want, uint32_t max_samp)
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
    return ev.stage == want;
}

static int wait_filt(int want, uint32_t max_samp)
{
    poli_ampenv_view_t ev;
    uint32_t n;
    polivoks_filtenv_view(&ev);
    if (ev.stage == want)
        return 1;
    for (n = 0; n < max_samp; n += 32) {
        render_n(32);
        polivoks_filtenv_view(&ev);
        if (ev.stage == want)
            return 1;
    }
    return ev.stage == want;
}

static void capture_spec(void)
{
    int i;
    memset(abuf, 0, sizeof abuf);
    polivoks_render(abuf, 128);
    for (i = 0; i < 128; i++) {
        int32_t v = abuf[2 * i];
        host_spec[i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
    }
}

static void shot(const char *path)
{
    int i;
    /* Several capture+draw passes so smk_sa_smooth and a full footer of
     * 24 bars are settled before the file is written. The first pass
     * alone can leave the 52 px band looking almost empty. */
    for (i = 0; i < 8; i++) {
        capture_spec();
        memset(fb, 0, sizeof fb);
        poli_ui_draw();
    }
    write_ppm(path);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/fadersteady-shots";
    char path[256];

    polivoks_init();
    poli_nav_init();
    poli_preset_init(0);

    /* A fairly open saw, mid resonance, mid amp sustain — enough
     * harmonic energy for the 24-bar footer on every page. */
    poli_shape = 1;
    poli_cut = 20000;
    poli_res = 10000;
    polivoks_set_level(0, 30000);
    polivoks_set_level(1, 0);
    polivoks_set_output(32767);
    polivoks_set_adsr_q(1, 80000, 200000, 18000, 80000);
    polivoks_set_adsr_q(0, 60000, 150000, 12000, 60000);

    polivoks_midi(0x90, 60, 120);
    wait_amp(POLI_AMPENV_SUS, 300000);
    wait_filt(POLI_AMPENV_SUS, 300000);

    poli_nav_section(NAV_SEC_FX);
    snprintf(path, sizeof path, "%s/filter.ppm", dir);
    shot(path);

    poli_nav_section(NAV_SEC_AMP);
    snprintf(path, sizeof path, "%s/amp.ppm", dir);
    shot(path);

    poli_nav_section(NAV_SEC_OSC);
    snprintf(path, sizeof path, "%s/generator.ppm", dir);
    shot(path);

    return 0;
}
