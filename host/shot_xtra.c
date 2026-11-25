/* Host frames of the ARP/SEQ/DRUMS/... windows (the former EXTRAS section). Not firmware. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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

int main(int argc, char **argv)
{
    /* Visit order matches SELECT walk (XW_ORDER). */
    static const struct { int id; const char *name; } win[12] = {
        {0, "arpeggio"}, {5, "wander"}, {1, "sequencer"}, {2, "drums"}, {11, "drum-step"},
        {8, "generate"}, {6, "drum-drift"}, {7, "groove"}, {9, "drum-fx"},
        {10, "perform"}, {3, "delay"}, {4, "reverb"}
    };
    const char *outdir = argc > 1 ? argv[1] : "build/xtra-shots";
    int lang, i;
    char path[256];
    poli_nav_init();
    poli_preset_init(0);
    for (lang = 0; lang < 2; lang++) {
        if (lang) poli_nav_lang_step(1);
        else poli_nav_home();
        for (i = 0; i < 12; i++) {
            poli_nav_xtra_open(win[i].id);
            poli_ui_draw();
            snprintf(path, sizeof path, "%s/%s-%02d-%s.ppm",
                     outdir, lang ? "ru" : "en", i + 1, win[i].name);
            write_ppm(path);
        }
        /* Drum-Step: the page with an edited USER beat (the first pass does the edits; RU shows the same state):
         * a few hits toggled with the white keys, chances 75 / 50 / 25 %, the transport running at step 7 */
        if (!lang) {
            static const uint8_t KEYS[][2] = { {0, 2}, {0, 10}, {6, 0}, {6, 1}, {12, 6}, {12, 7}, {16, 9},
                                               {24, 15}, {26, 0}, {7, 14}, {9, 3} };   /* (keybed key, step) */
            static const uint8_t CH[3][3] = { {4, 2, 1}, {12, 2, 2}, {6, 6, 3} };   /* (step, key, chance): */
            int j;                                   /* snare 5 75 %, snare 13 50 %, closed hat 7 25 % */
            xtra_set_c2(3, 0);
            poli_nav_xtra_open(11);
            for (j = 0; j < (int)(sizeof KEYS / sizeof KEYS[0]); j++) {
                xtra_knob(11, 0, KEYS[j][1] - xtra_get(11, 0));
                xtra_keys(1u << KEYS[j][0], 0, 0);
                xtra_keys(0, 0, 0);
            }
            for (j = 0; j < 3; j++) {                /* touch the hit (off, on again), then K2 */
                xtra_knob(11, 0, CH[j][0] - xtra_get(11, 0));
                xtra_keys(1u << CH[j][1], 0, 0); xtra_keys(0, 0, 0);
                xtra_keys(1u << CH[j][1], 0, 0); xtra_keys(0, 0, 0);
                xtra_knob(11, 1, CH[j][2]);
            }
            xtra_toggle(11);                         /* SEL: DRUMS on */
            xtra_transport(1);                       /* PLAY; 120 BPM: a 16th is 125 ms, so 830 ms is step 7 */
            for (j = 1; j <= 830; j++) xtra_tick((uint32_t)j);
        }
        poli_nav_xtra_open(11);
        poli_ui_draw();
        snprintf(path, sizeof path, "%s/%s-13-drum-step-edit.ppm", outdir, lang ? "ru" : "en");
        write_ppm(path);
        /* Control: a synth window must stay unchanged. */
        poli_nav_home();
        poli_ui_draw();
        snprintf(path, sizeof path, "%s/%s-00-generator.ppm",
                 outdir, lang ? "ru" : "en");
        write_ppm(path);
    }
    return 0;
}
