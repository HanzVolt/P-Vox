/* SPDX-License-Identifier: GPL-3.0-only */
/* Parity (23 Nov 2026): host frames of the FM-1 parity screens, EN and RU: PAD REC, CLASSIC ARP, FILTER (EG plot),
 * AMP EG (plot and dot), and a synth page with the analyser band fed by the engine's own output. Not firmware.
 *   shot_parity OUTDIR   -> OUTDIR/{en,ru}-{padrec,carp,filter,amp,spectrum}.ppm */
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
    for (row = 0; row < h; row++) for (col = 0; col < w; col++) fb[(y + row) * 240u + x + col] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t row, col;
    if (!w || !h || x >= 240u || y >= 240u) return;
    if (x + w > 240u) w = 240u - x;
    if (y + h > 240u) h = 240u - y;
    for (row = 0; row < h; row++)
        for (col = 0; col < w; col++) {
            uint16_t s = px[row * w + col];
            fb[(y + row) * 240u + x + col] = (uint16_t)((s >> 8) | (s << 8));
        }
}
/* the analyser hears the engine: the last 128 rendered left samples */
static int16_t last_out[128];
int smk_spectrum_host_snap(int16_t *dst, int n)
{
    int i;
    for (i = 0; i < n && i < 128; i++) dst[i] = last_out[i];
    return i;
}
#include "gfx.c"
#include "poli_ui.c"

static uint32_t now_ms;
static void run_ms(uint32_t ms)
{
    static int32_t buf[2 * 256];
    while (ms--) {
        uint32_t k = 47u, i;
        now_ms++;
        poli_nav_tick_ms(now_ms);
        xtra_tick(now_ms);
        memset(buf, 0, sizeof buf);
        polivoks_render(buf, k);
        memmove(last_out, last_out + k, (128 - k) * sizeof last_out[0]);
        for (i = 0; i < k; i++) {
            int32_t v = buf[2 * i];
            last_out[128 - k + i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
}
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
static int white_key(int v) { int k; for (k = 0; k < 27; k++) if (xtra_step_white(k) == v) return k; return -1; }

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "build/parity-shots";
    char path[256];
    int lang, i;
    for (lang = 0; lang < 2; lang++) {
        const char *L = lang ? "ru" : "en";
        poli_nav_init();
        poli_preset_init(0);
        xtra_init();
        xtra_set_c2(3, 0);
        now_ms = 0;
        if (lang) poli_nav_lang_step(1);
        /* PAD REC: a groove entered in STEP mode from the keys, cursor on step 5 */
        poli_nav_xtra_open(XW_STEP);
        poli_nav_select_turn(1);
        xtra_padrec_clear();
        xtra_knob(XW_PADREC, 1, 1);
        {
            static const struct { int key, step; } G[] = { { 0, 0 }, { 0, 4 }, { 0, 8 }, { 0, 12 }, { 1, 4 }, { 1, 12 },
                                                          { 3, 2 }, { 3, 6 }, { 3, 10 }, { 3, 14 }, { 4, 15 }, { 9, 7 } };
            for (i = 0; i < (int)(sizeof G / sizeof G[0]); i++) {
                uint32_t b = 1u << white_key(G[i].key);
                xtra_knob(XW_PADREC, 3, G[i].step - xtra_padrec_step());
                (void)xtra_keys(b, 0, now_ms); (void)xtra_keys(0, 0, now_ms);
            }
            xtra_knob(XW_PADREC, 3, 4 - xtra_padrec_step());
        }
        poli_ui_draw();
        snprintf(path, sizeof path, "%s/%s-padrec.ppm", out, L); write_ppm(path);
        /* CLASSIC ARP: on, UP-DN, 1/16, 2 octaves, LATCH, three keys */
        poli_nav_xtra_open(0);
        poli_nav_select_turn(1);
        poli_arp_set_on(1);
        poli_arp_set(ARP_UPDOWN, 4, 2, 60, 1);
        for (i = 0; i < 400; i++) { (void)poli_arp_keys((1u << 0) | (1u << 4) | (1u << 7), 0, now_ms); run_ms(1); }
        poli_arp_page_show(1);
        poli_ui_draw();
        snprintf(path, sizeof path, "%s/%s-carp.ppm", out, L); write_ppm(path);
        poli_arp_clear();
        poli_arp_set_on(0);
        polivoks_midi(0xB0, 123, 0);
        run_ms(500);
        /* FILTER: a held note, the filter EG partway */
        poli_nav_home();
        for (i = 0; i < 5; i++) poli_nav_select_turn(1);
        poli_ui_draw();
        polivoks_panel(1u << 5, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        run_ms(60);
        for (i = 0; i < 20; i++) { run_ms(40); poli_ui_filtenv_poll(now_ms); poli_ui_spectrum_poll(now_ms); }
        snprintf(path, sizeof path, "%s/%s-filter.ppm", out, L); write_ppm(path);
        polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        run_ms(1500);
        /* AMP EG: a slower attack and release; the dot partway up the attack */
        poli_nav_home();
        for (i = 0; i < 8; i++) poli_nav_select_turn(1);
        poli_nav_knob(0, 60);
        poli_nav_knob(1, 30);
        poli_nav_knob(3, 50);
        poli_ui_draw();
        polivoks_panel(1u << 5, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        for (i = 0; i < 6; i++) { run_ms(40); poli_ui_ampenv_poll(now_ms); poli_ui_spectrum_poll(now_ms); }
        snprintf(path, sizeof path, "%s/%s-amp.ppm", out, L); write_ppm(path);
        polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        run_ms(4000);
        /* the analyser band under GENERATOR I, a held saw */
        poli_nav_home();
        poli_ui_draw();
        polivoks_panel(1u << 3, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        for (i = 0; i < 25; i++) { run_ms(40); poli_ui_spectrum_poll(now_ms); }
        snprintf(path, sizeof path, "%s/%s-spectrum.ppm", out, L); write_ppm(path);
        polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }
    return 0;
}
