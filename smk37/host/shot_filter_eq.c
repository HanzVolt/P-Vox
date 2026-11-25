/* SPDX-License-Identifier: GPL-3.0-only */
/* Host frames of the FILTER page with the 32-band graphic EQ footer.
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

static void shot(const char *path, int cut, int res, int bp)
{
    memset(fb, 0, sizeof fb);
    poli_nav_cc(74, (uint8_t)cut);
    poli_nav_cc(71, (uint8_t)res);
    poli_nav_cc(85, bp ? 127 : 0);
    poli_ui_draw();
    write_ppm(path);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/eq-shots";
    char path[256];
    poli_nav_init();
    poli_preset_init(0);
    poli_nav_section(NAV_SEC_FX);
    snprintf(path, sizeof path, "%s/lp-res0.ppm", dir);
    shot(path, 64, 0, 0);
    snprintf(path, sizeof path, "%s/lp-res100.ppm", dir);
    shot(path, 64, 100, 0);
    snprintf(path, sizeof path, "%s/bp-res20.ppm", dir);
    shot(path, 64, 20, 1);
    snprintf(path, sizeof path, "%s/bp-res100.ppm", dir);
    shot(path, 64, 100, 1);
    return 0;
}
