/* SPDX-License-Identifier: GPL-3.0-only */
/* Host audio sanity render of the 50 factory presets. Not firmware.
 * Each: load the slot from boot, A3 (MIDI 57) held 1.5 s, released, 2.0 s more.
 * Peak against the engine's own +-30000 output clamp (polivoks_render). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"
#include "poli_preset.c"
#include "wav_write.h"
#define FS 44100
#define NHOLD (FS * 3 / 2)
#define NTOT (FS * 7 / 2)
static int32_t L[NTOT], st[2 * 256];
int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    int i, worst = 0;
    double hmin = 1e9;
    printf("slot\tname\tpeak\theadroom_dB\tclamped\trms_dB\tend_silent\tsilent_after_ms\n");
    for (i = 0; i < PV_NSLOT; i++) {
        int n = 0, k, clamps = 0; int32_t pk = 0; double ss = 0; long last_nz = -1;
        char nb[40], path[300];
        poli_nav_init(); poli_preset_init(0);
        poli_nav_patch_set(&pv_bank.rec[i].b[18], pv_bank.rec[i].b[16]);
        polivoks_midi(0x90, 57, 100);
        while (n < NTOT) {
            int m = NTOT - n > 256 ? 256 : NTOT - n;
            if (n >= NHOLD && n - m < NHOLD) polivoks_midi(0x80, 57, 0);
            memset(st, 0, sizeof st);
            polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
            polivoks_render(st, (uint32_t)m);
            for (k = 0; k < m; k++) {
                int32_t y = st[2 * k], a = y < 0 ? -y : y;
                L[n + k] = y;
                if (a > pk) pk = a;
                if (a >= 30000) clamps++;
                if (y) last_nz = n + k;
                ss += (double)y * y;
            }
            n += m;
        }
        poli_preset_name_utf8(pv_bank.rec[i].b + 4, nb, 1);
        snprintf(path, sizeof path, "%s/preset-%02d.wav", dir, i + 1);
        poli_wav_write(path, L, L, NTOT);
        {
            double hr = pk ? 20.0 * log10(30000.0 / pk) : 99.0, rms = 10.0 * log10(ss / NTOT + 1e-9) - 20.0 * log10(30000.0);
            if (hr < hmin) { hmin = hr; worst = i; }
            printf("%02d\t%s\t%d\t%.2f\t%d\t%.1f\t%s\t%.0f\n", i + 1, nb, pk, hr, clamps, rms,
                   last_nz < NTOT - FS / 2 ? "yes" : "NO", last_nz < 0 ? 0.0 : (last_nz - NHOLD) * 1000.0 / FS);
        }
    }
    printf("# lowest headroom %.2f dB at %02d\n", hmin, worst + 1);
    return 0;
}
