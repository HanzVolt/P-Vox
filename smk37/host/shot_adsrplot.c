/* SPDX-License-Identifier: GPL-3.0-only */
/* Host frames of AMP and FILTER EG plots from the real draw path, at
 * fixed A/D/S/R shapes. Not firmware.
 * Built with -DBOARD_SMK37=1 -DSMK_SPEC_HOST_FEED. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"
#include "poli_preset.c"
#include "smk_amp_env.h"

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

static void capture_spec(void)
{
    /* Idle page shots have no hanging note (a live dot would sit on
     * the engine envelope, not the displayed knobs). A falling dummy
     * spectrum still fills the 52 px footer. */
    int i;
    for (i = 0; i < 128; i++) {
        int v = 18000 - i * 110;
        if (v < 400) v = 400;
        host_spec[i] = (int16_t)(((i * 17) & 1) ? v : -v);
    }
}

static void shot(const char *path)
{
    int i;
    for (i = 0; i < 8; i++) {
        capture_spec();
        memset(fb, 0, sizeof fb);
        poli_ui_draw();
    }
    write_ppm(path);
}

static void report_geom(const char *tag, int which)
{
    smk_ampenv_geom_t g;
    int w = which ? 216 : 130;
    env_plot_geom(which, 0, 0, w, 100, &g);
    printf("%s which=%d xa=%d xd=%d xs=%d xr=%d y_peak=%d y_sus=%d y_zero=%d\n",
           tag, which,
           g.x_peak - g.x0, g.x_sus0 - g.x_peak,
           g.x_sus1 - g.x_sus0, g.x_end - g.x_sus1,
           g.y_peak, g.y_sus, g.y_zero);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/adsrplot-shots";
    char path[256];
    int i, decs[] = { 0, 32, 64, 96, 127 };
    const char *decn[] = { "d0", "d32", "d64", "d96", "d127" };

    polivoks_init();
    poli_nav_init();
    poli_preset_init(0);

    /* square: 0/0/127/0 */
    set_adsr(0, 0, 127, 0);
    report_geom("square", 1);
    report_geom("square", 0);
    poli_nav_section(NAV_SEC_AMP);
    snprintf(path, sizeof path, "%s/amp-square.ppm", dir);
    shot(path);
    poli_nav_section(NAV_SEC_FX);
    snprintf(path, sizeof path, "%s/filter-square.ppm", dir);
    shot(path);

    /* all max */
    set_adsr(127, 127, 127, 127);
    report_geom("allmax", 1);
    report_geom("allmax", 0);
    poli_nav_section(NAV_SEC_AMP);
    snprintf(path, sizeof path, "%s/amp-allmax.ppm", dir);
    shot(path);
    poli_nav_section(NAV_SEC_FX);
    snprintf(path, sizeof path, "%s/filter-allmax.ppm", dir);
    shot(path);

    /* Init defaults (nav init is 0/0/64/4). Re-init to be sure, then
     * leave the positions alone. */
    poli_nav_init();
    poli_preset_init(0);
    report_geom("init", 1);
    report_geom("init", 0);
    poli_nav_section(NAV_SEC_AMP);
    snprintf(path, sizeof path, "%s/amp-init.ppm", dir);
    shot(path);
    poli_nav_section(NAV_SEC_FX);
    snprintf(path, sizeof path, "%s/filter-init.ppm", dir);
    shot(path);

    /* Decay sweep: A=0, S=64, R=0 so only decay width changes. */
    for (i = 0; i < 5; i++) {
        set_adsr(0, decs[i], 64, 0);
        report_geom(decn[i], 1);
        report_geom(decn[i], 0);
        poli_nav_section(NAV_SEC_AMP);
        snprintf(path, sizeof path, "%s/amp-%s.ppm", dir, decn[i]);
        shot(path);
        poli_nav_section(NAV_SEC_FX);
        snprintf(path, sizeof path, "%s/filter-%s.ppm", dir, decn[i]);
        shot(path);
    }

    return 0;
}
