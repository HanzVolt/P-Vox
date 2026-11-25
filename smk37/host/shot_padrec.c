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


int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "padrec.ppm";
    polivoks_init();
    poli_nav_init();
    poli_preset_init(0);
    xtra_set_c2(3, 0);
    xtra_toggle(2);                  /* drums on, so the page is live */
    poli_nav_xtra_open(XW_PADREC);
    xtra_padrec_knob(1, 1);          /* STEP, so a cursor shows */
    xtra_padrec_pad(8, 100, 1);      /* kick on step 1 */
    xtra_padrec_pad(8, 0, 1);
    xtra_padrec_step_move(4);
    xtra_padrec_pad(9, 100, 1);      /* snare */
    xtra_padrec_pad(9, 0, 1);
    xtra_padrec_pad(11, 100, 1);     /* closed hat */
    xtra_padrec_pad(11, 0, 1);
    memset(fb, 0, sizeof fb);
    poli_ui_draw();
    write_ppm(path);
    fprintf(stderr, "wrote %s (PAD REC, xtra=%d)\n", path, poli_nav_xtra());
    return poli_nav_xtra() == XW_PADREC ? 0 : 1;
}
