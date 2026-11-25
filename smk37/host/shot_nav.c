/* Host frames of the navigation screens. Not firmware. */
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

int main(void)
{
    static const char *name[12] = {
        "gen1", "gen2", "modulator", "mod-osc", "mixer",
        "filter", "filter-mod", "filter-env", "amp-env", "amp-mod", "global", "midi"
    };
    int lang, i;
    char path[160];
    poli_nav_init();
    poli_preset_init(0);
    for (lang = 0; lang < 2; lang++) {
        if (lang) poli_nav_lang_step(1);
        poli_nav_home();
        for (i = 0; i < 12; i++) {
            poli_ui_draw();
            snprintf(path, sizeof path, "build/nav-shots/%s-%02d-%s.ppm",
                     lang ? "ru" : "en", i + 1, name[i]);
            write_ppm(path);
            poli_nav_select_turn(1);
        }
        /* preset views on window 1 of the roll: bar on slot 2, save screen, then edited (asterisk) */
        poli_preset_turn(1, 0);
        poli_ui_draw();
        snprintf(path, sizeof path, "build/nav-shots/%s-p1-bar.ppm", lang ? "ru" : "en");
        write_ppm(path);
        poli_preset_sel();
        poli_nav_knob(0, 1);
        poli_ui_draw();
        snprintf(path, sizeof path, "build/nav-shots/%s-p2-edited.ppm", lang ? "ru" : "en");
        write_ppm(path);
        poli_preset_save_btn(0);
        poli_preset_knob(0, 3); poli_preset_knob(1, 30);
        poli_ui_draw();
        snprintf(path, sizeof path, "build/nav-shots/%s-p3-save.ppm", lang ? "ru" : "en");
        write_ppm(path);
        poli_preset_sel();
        poli_ui_draw();
        snprintf(path, sizeof path, "build/nav-shots/%s-p4-compare.ppm", lang ? "ru" : "en");
        write_ppm(path);
        poli_preset_cancel();
        poli_preset_init(0);
    }
    return 0;
}
