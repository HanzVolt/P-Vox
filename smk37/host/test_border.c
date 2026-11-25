/* SPDX-License-Identifier: GPL-3.0-only */
/* Host check: AMP / FILTER orange panel frame stays intact across
 * value-only and envelope-plot partial updates. Also writes PPM
 * previews. Built with -DBOARD_SMK37=1 -DSMK_SPEC_HOST_FEED. */
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

#define COL_OR_HOST RGB(204, 96, 48)

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

/* After a full draw, the AMP/FILTER window is panel(4, L_py0, 232, L_py1-L_py0).
 * Record the orange frame pixels so later partial updates can be checked. */
typedef struct {
    int y0, y1;          /* inclusive frame rows */
    int x0, x1;          /* 4 and 235 */
    uint16_t expect;
    int n;
} frame_t;

static uint16_t pix(int x, int y) { return fb[y * 240 + x]; }

static int find_frame(frame_t *fr)
{
    int y, run0 = 0, run1 = 0, a0 = -1, b0 = -1, a1 = -1, b1 = -1;
    int best0 = 0, best1 = 0, s0 = -1, s1 = -1;
    fr->x0 = 4;
    fr->x1 = 235;
    fr->expect = COL_OR_HOST;
    for (y = 0; y < 240; y++) {
        if (pix(4, y) == COL_OR_HOST) {
            if (!run0) { s0 = y; run0 = 1; }
        } else if (run0) {
            if (y - s0 > best0) { best0 = y - s0; a0 = s0; b0 = y - 1; }
            run0 = 0;
        }
        if (pix(235, y) == COL_OR_HOST) {
            if (!run1) { s1 = y; run1 = 1; }
        } else if (run1) {
            if (y - s1 > best1) { best1 = y - s1; a1 = s1; b1 = y - 1; }
            run1 = 0;
        }
    }
    if (run0 && 240 - s0 > best0) { a0 = s0; b0 = 239; best0 = 240 - s0; }
    if (run1 && 240 - s1 > best1) { a1 = s1; b1 = 239; best1 = 240 - s1; }
    if (a0 < 0 || a1 < 0 || a0 != a1 || b0 != b1 || best0 < 40) {
        printf("FAIL find_frame a0=%d b0=%d a1=%d b1=%d best=%d/%d\n",
               a0, b0, a1, b1, best0, best1);
        return 0;
    }
    fr->y0 = a0;
    fr->y1 = b0;
    fr->n = 0;
    return 1;
}

static int check_frame(const frame_t *fr, const char *tag, int *bad_out)
{
    int x, y, bad = 0, tot = 0;
    /* left and right */
    for (y = fr->y0; y <= fr->y1; y++) {
        tot += 2;
        if (pix(fr->x0, y) != fr->expect) bad++;
        if (pix(fr->x1, y) != fr->expect) bad++;
    }
    /* top and bottom, inclusive of corners already counted in verticals */
    for (x = fr->x0 + 1; x <= fr->x1 - 1; x++) {
        tot += 2;
        if (pix(x, fr->y0) != fr->expect) bad++;
        if (pix(x, fr->y1) != fr->expect) bad++;
    }
    if (bad_out)
        *bad_out = bad;
    if (bad)
        printf("FAIL %s frame %d/%d pixels not orange (y0=%d y1=%d)\n",
               tag, bad, tot, fr->y0, fr->y1);
    return bad == 0;
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

static void set_contour(int p)
{
    poli_nav_reflect_slot(SLOT_CONT, p);
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

static void full_draw(void)
{
    int i;
    dummy_spec();
    for (i = 0; i < 4; i++) {
        dummy_spec();
        poli_ui_draw();
    }
}

/* Match the device main-loop order: values, then plot polls, then spectrum. */
static void partial_update(uint32_t now)
{
    dummy_spec();
    poli_ui_draw_values();
    poli_ui_ampenv_poll(now);
    poli_ui_filtenv_poll(now);
    poli_ui_spectrum_poll(now);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/tmp/border-shots";
    const char *tag = argc > 2 ? argv[2] : "run";
    char path[256];
    frame_t fr;
    int fail = 0, bad, i, r, step;
    uint32_t now;
    int sweep[] = { 0, 16, 32, 48, 64, 80, 96, 112, 127, 96, 64, 32, 0, 127, 4, 0 };

    polivoks_init();
    poli_nav_init();
    poli_preset_init(0);
    set_adsr(0, 0, 64, 4);
    set_contour(32);

    /* ---- AMP: full draw, snapshot the frame, then lots of partials ---- */
    poli_nav_section(NAV_SEC_AMP);
    full_draw();
    snprintf(path, sizeof path, "%s/amp-%s-before-updates.ppm", dir, tag);
    write_ppm(path);
    if (!find_frame(&fr)) {
        printf("FAIL amp_frame_not_found\n");
        return 1;
    }
    printf("PASS amp_frame_found  y0=%d y1=%d x=%d..%d\n", fr.y0, fr.y1, fr.x0, fr.x1);
    if (!check_frame(&fr, "amp_fresh", &bad))
        fail++;

    now = 50;
    /* Sweep release especially (graphic segment), then A/D/S and contour. */
    for (i = 0; i < (int)(sizeof sweep / sizeof sweep[0]); i++) {
        char step_tag[40];
        set_adsr(0, 32, 64, sweep[i]);
        now += 50;
        partial_update(now);
        snprintf(step_tag, sizeof step_tag, "amp_rel_%d", sweep[i]);
        if (!check_frame(&fr, step_tag, &bad))
            fail++;
    }
    for (step = 0; step <= 127; step += 7) {
        char step_tag[40];
        set_adsr(step, 127 - step, step, 127 - step);
        set_contour(step);
        now += 50;
        partial_update(now);
        snprintf(step_tag, sizeof step_tag, "amp_sweep_%d", step);
        if (!check_frame(&fr, step_tag, &bad))
            fail++;
    }
    snprintf(path, sizeof path, "%s/amp-%s-after-updates.ppm", dir, tag);
    write_ppm(path);
    if (!check_frame(&fr, "amp_after", &bad))
        fail++;
    else
        printf("PASS amp_after_updates\n");

    /* No leftover orange from a long release after shrinking to R=0. */
    {
        smk_ampenv_geom_t g;
        int x, y, w, h, xi, yi, stray = 0;
        set_adsr(0, 32, 64, 127);
        now += 50;
        partial_update(now);
        set_adsr(0, 32, 64, 0);
        now += 50;
        partial_update(now);
        ampenv_plot_rect(&x, &y, &w, &h);
        env_plot_geom(1, x, y, w, h, &g);
        for (yi = y + 1; yi < y + h - 1; yi++) {
            for (xi = g.x_end + 2; xi < x + w - 1; xi++) {
                if (pix(xi, yi) == COL_OR_HOST)
                    stray++;
            }
        }
        if (stray) {
            printf("FAIL amp_stray_release_pixels %d\n", stray);
            fail++;
        } else {
            printf("PASS amp_stray_release_pixels\n");
        }
        if (!check_frame(&fr, "amp_after_shrink", &bad))
            fail++;
    }

    /* ---- FILTER: same, including DEPTH (contour) and KEY unchanged ---- */
    poli_nav_section(NAV_SEC_FX);
    set_adsr(0, 32, 64, 4);
    set_contour(32);
    full_draw();
    snprintf(path, sizeof path, "%s/filter-%s-before-updates.ppm", dir, tag);
    write_ppm(path);
    if (!find_frame(&fr)) {
        printf("FAIL filter_frame_not_found\n");
        fail++;
    } else {
        printf("PASS filter_frame_found  y0=%d y1=%d x=%d..%d\n", fr.y0, fr.y1, fr.x0, fr.x1);
        if (!check_frame(&fr, "filter_fresh", &bad))
            fail++;
        now += 50;
        for (i = 0; i < (int)(sizeof sweep / sizeof sweep[0]); i++) {
            char step_tag[40];
            set_adsr(0, 32, 64, sweep[i]);
            set_contour(sweep[i]);
            now += 50;
            partial_update(now);
            snprintf(step_tag, sizeof step_tag, "filter_rel_%d", sweep[i]);
            if (!check_frame(&fr, step_tag, &bad))
                fail++;
        }
        for (step = 0; step <= 127; step += 7) {
            char step_tag[40];
            set_adsr(step, 64, 80, 127 - step);
            set_contour(step);
            now += 50;
            partial_update(now);
            snprintf(step_tag, sizeof step_tag, "filter_sweep_%d", step);
            if (!check_frame(&fr, step_tag, &bad))
                fail++;
        }
        snprintf(path, sizeof path, "%s/filter-%s-after-updates.ppm", dir, tag);
        write_ppm(path);
        if (!check_frame(&fr, "filter_after", &bad))
            fail++;
        else
            printf("PASS filter_after_updates\n");
    }

    (void)r;
    printf("suite_fail=%d tag=%s\n", fail, tag);
    return fail ? 1 : 0;
}
