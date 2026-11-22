/* SPDX-License-Identifier: GPL-3.0-only */
/* Host audio render of the 50 factory presets THROUGH the preset-load mute. Not firmware.
 * As render_presets.c (A3 held 1.5 s, released, 2.0 s more), but each slot is loaded with the
 * PRESETS bar + SEL (muted load, PV_MUTE model as test_mute.c) and the note starts at once, so the
 * first half (5.8 ms) of the note is inside the fade-up. ISR block model, MASTER at unity (4096).
 * Peak against the engine's +-30000 clamp, as render_presets.c. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"
#include "out_mute.h"
#define FS 44100
#define BLK 256
#define NHOLD (FS * 3 / 2)
#define NTOT (FS * 7 / 2)
static int32_t L[NTOT + BLK]; static long n_out;
static void irq_block(void)
{
    static int32_t o[2 * BLK];
    int k;
    memset(o, 0, sizeof o);
    polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    polivoks_render(o, BLK);
    out_gain_block(o, 2u * BLK, OUT_UNITY);
    for (k = 0; k < BLK; k++) if (n_out < NTOT + BLK) L[n_out++] = o[2 * k];
}
#ifndef NO_MUTE                                    /* -DNO_MUTE: same path, load not muted (A/B) */
#define PV_MUTE 1
static void pv_mute_on(void) { int n = 0; out_mute_req = 1u; while (out_mute_g != 0 && n < 4) { irq_block(); n++; } }
static void pv_mute_off(void) { out_mute_req = 0u; }
#endif
#include "poli_preset.c"
static int view_cur(void) { poli_preset_view_t v; poli_preset_view(&v); return v.cur; }
int main(void)
{
    int i; double hmin = 1e9; int worst = 0;
    printf("slot\tpeak\theadroom_dB\tclamped\n");
    for (i = 0; i < PV_NSLOT; i++) {
        long k, n0; int32_t pk = 0; int clamps = 0, d;
        poli_nav_init(); poli_preset_init(0);
        n_out = 0;
#ifdef NO_MUTE
        irq_block();                                  /* same engine input as the muted path's fade-down half */
#endif
        d = i - view_cur(); poli_preset_turn(d ? d : PV_NSLOT, 0); poli_preset_sel();   /* muted load */
        n0 = n_out;                                   /* the fade-down half (silence before the note) */
        polivoks_midi(0x90, 57, 100);
        while (n_out - n0 < NTOT) {
            if (n_out - n0 >= NHOLD && n_out - n0 - BLK < NHOLD) polivoks_midi(0x80, 57, 0);
            irq_block();
        }
        for (k = 0; k < n_out; k++) { int32_t a = L[k] < 0 ? -L[k] : L[k]; if (a > pk) pk = a; if (a >= 30000) clamps++; }
        {
            double hr = pk ? 20.0 * log10(30000.0 / pk) : 99.0;
            if (hr < hmin) { hmin = hr; worst = i; }
            printf("%02d\t%d\t%.2f\t%d\n", i + 1, pk, hr, clamps);
        }
    }
    printf("# lowest headroom %.2f dB at %02d\n", hmin, worst + 1);
    return 0;
}
