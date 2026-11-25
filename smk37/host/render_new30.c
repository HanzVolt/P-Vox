/* SPDX-License-Identifier: GPL-3.0-only */
/* Host safety render of the 30 proposed law-5 presets. Not firmware, not a
 * deliverable audio pack. Each preset is applied through poli_nav_patch_set,
 * then notes are held 1.5 s and released for 2.0 s at MIDI 36, 57 and 81.
 * Peak is checked against the engine clamp +-30000. Also reports DC, whether
 * the tail goes quiet, and any sample that is the int32 extreme (runaway). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"

typedef struct { uint8_t name[12]; uint8_t sw; uint8_t k[31]; } pv_fact_t;
#include "pv_new30.inc"

/* name decoder: same 12-code packing; we only need a printable name, so include
 * the font table through a tiny copy of poli_preset_name_utf8? Simpler: print
 * the comment index and also decode via a stub. We'll include the font. */
#include "poli_name_font.inc"
static void name_utf8(const uint8_t code[12], char *out)
{
    int i, n = 12;
    char *o = out;
    while (n > 0 && code[n - 1] == 0u) n--;
    for (i = 0; i < n; i++) {
        uint32_t cp = TN_CP[code[i] < TN_N ? code[i] : 0];
        if (cp < 0x80u) *o++ = (char)cp;
        else { *o++ = (char)(0xC0u | (cp >> 6)); *o++ = (char)(0x80u | (cp & 0x3Fu)); }
    }
    *o = 0;
}

#define FS 44100
#define NHOLD (FS * 3 / 2)
#define NTOT (FS * 7 / 2)
static int32_t st[2 * 256];

static void render_note(int midi, int32_t *pk, int *clamps, double *ss, double *sum,
                        long *last_nz, int *runaway, int *nanish, long *nsm)
{
    int n = 0, k;
    polivoks_midi(0x80, midi, 0);
    polivoks_midi(0x90, midi, 100);
    while (n < NTOT) {
        int m = NTOT - n > 256 ? 256 : NTOT - n;
        if (n >= NHOLD && n - m < NHOLD) polivoks_midi(0x80, midi, 0);
        memset(st, 0, sizeof st);
        polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        polivoks_render(st, (uint32_t)m);
        for (k = 0; k < m; k++) {
            int32_t y = st[2 * k], a = y < 0 ? -y : y;
            if (a > *pk) *pk = a;
            if (a >= 30000) (*clamps)++;
            if (y) *last_nz = *nsm + n + k;
            *ss += (double)y * y;
            *sum += (double)y;
            if (y == 2147483647 || y == (-2147483647 - 1)) (*runaway)++;
            /* integer engine: no IEEE NaN; treat the symmetric extreme as bad */
            (void)nanish;
        }
        n += m;
    }
    *nsm += NTOT;
}

int main(void)
{
    int i, notes[3] = {36, 57, 81};
    int worst = 0, any_fail = 0;
    double hmin = 1e9;
    printf("slot\tname\tbp\tpeak\theadroom_dB\tclamped\tdc\trms_dB\tend_silent\tsilent_after_ms\trunaway\n");
    for (i = 0; i < PV_NNEW; i++) {
        int n, clamps = 0, runaway = 0, nanish = 0;
        int32_t pk = 0;
        double ss = 0, sum = 0;
        long last_nz = -1, nsm = 0;
        char nb[40];
        int bp = (PV_NEW[i].sw & 1u) != 0;
        poli_nav_init();
        poli_nav_patch_set(PV_NEW[i].k, PV_NEW[i].sw);
        for (n = 0; n < 3; n++)
            render_note(notes[n], &pk, &clamps, &ss, &sum, &last_nz, &runaway, &nanish, &nsm);
        name_utf8(PV_NEW[i].name, nb);
        {
            double hr = pk ? 20.0 * log10(30000.0 / pk) : 99.0;
            double rms = 10.0 * log10(ss / (double)nsm + 1e-9) - 20.0 * log10(30000.0);
            double dc = sum / (double)nsm;
            int end_ok = last_nz < nsm - FS / 2;
            /* silent_after: from the last note's release (third hold ends at 2*NTOT+NHOLD) */
            double after = last_nz < 0 ? 0.0 : (last_nz - (2.0 * NTOT + NHOLD)) * 1000.0 / FS;
            if (hr < hmin) { hmin = hr; worst = i; }
            if (clamps > 0 || runaway || !end_ok || pk >= 30000)
                any_fail = 1;
            printf("%02d\t%s\t%s\t%d\t%.2f\t%d\t%.2f\t%.1f\t%s\t%.0f\t%d\n",
                   i + 51, nb, bp ? "BP" : "LP", pk, hr, clamps, dc, rms,
                   end_ok ? "yes" : "NO", after, runaway);
        }
    }
    printf("# lowest headroom %.2f dB at %02d; fail=%d\n", hmin, worst + 51, any_fail);
    return any_fail ? 1 : 0;
}
