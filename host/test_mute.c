/* SPDX-License-Identifier: GPL-3.0-only */
/* Preset-load mute host checks (out_mute.h + poli_preset.c with PV_MUTE). Not firmware.
 * The FM-1 audio ISR is modelled block by block: polivoks_panel (main loop), polivoks_render
 * (256 frames), then out_gain_block, the SAME function the device ISR runs (platform.c). The
 * main loop's wait for silence (volt.c pv_mute_on) is modelled by running ISR blocks until the
 * fade has reached 0. MASTER is modelled at unity (4096) so samples are on the engine's
 * +-30000 scale, as in test_preset. Host model only: no FM-1 ran this.
 *   -DNO_MUTE: the same harness with the mute never requested (A/B reference). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"
#include "out_mute.h"

#define BLK 256
static int32_t master = OUT_UNITY;
#ifndef NO_MUTE
static int irq_stopped;                 /* model a stopped audio stream: the wait must give up */
static long blocks_in_wait, waits, wait_gaveup;
#endif
static int32_t prev;
static int32_t *rec; static long rec_n, rec_cap;   /* recorded output (left), when rec != 0 */

static void irq_block(void)
{
    static int32_t o[2 * BLK];
    int k;
    memset(o, 0, sizeof o);
    polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    polivoks_render(o, BLK);
    out_gain_block(o, 2u * BLK, master);
    for (k = 0; k < BLK; k++) {
        if (rec && rec_n < rec_cap) rec[rec_n++] = o[2 * k];
        prev = o[2 * k];
    }
}
static void run(int n) { while (n > 0) { irq_block(); n -= BLK; } }

#ifndef NO_MUTE
#define PV_MUTE 1
static void pv_mute_on(void)            /* volt.c: request, then wait (<= 20 ms) for the ISR to reach 0 */
{
    int n = 0;
    out_mute_req = 1u;
    waits++;
    while (out_mute_g != 0 && n < 4) {  /* 4 halves = 23 ms > the 20 ms device timeout */
        if (!irq_stopped) irq_block();
        n++;
    }
    if (out_mute_g != 0) wait_gaveup++;
    blocks_in_wait += n;
}
static void pv_mute_off(void) { out_mute_req = 0u; }
#endif
#include "poli_preset.c"

static int fail, pass;
static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (ok) pass++; else fail++;
}
static int view_cur(void) { poli_preset_view_t v; poli_preset_view(&v); return v.cur; }
static void load_slot(int s) { int d = s - view_cur(); poli_preset_turn(d ? d : PV_NSLOT, 0); poli_preset_sel(); }
static long maxstep_at;
static int32_t maxstep(const int32_t *x, long from, long to)   /* steps x[i]-x[i-1], i in [from, to) */
{
    int32_t m = 0; long i;
    maxstep_at = from;
    for (i = from; i < to; i++) { int32_t d = x[i] - x[i - 1]; if (d < 0) d = -d; if (d > m) { m = d; maxstep_at = i; } }
    return m;
}

int main(void)
{
    char d[700];
    int i, ok;
    static int32_t w[1 + 2048 + 2 * BLK + 2048];
    long X = 1 + 2048, Y = X + BLK, POST = Y + BLK, END = POST + 2048;

#ifndef NO_MUTE
    /* ---- unmuted gain is the old MASTER loop, bit for bit */
    {
        long bad = 0, n = 0; int32_t k10, x;
        for (k10 = 0; k10 < 1024; k10++) {
            int32_t m = (int32_t)(((uint32_t)k10 * (uint32_t)k10) >> 8);
            for (x = -32768; x <= 32767; x += 1) {
                int32_t o[1] = { x }, v = (x * m) >> 12;
                v = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
                out_mute_req = 0; out_mute_g = OUT_UNITY;
                out_gain_block(o, 1, m);
                if (o[0] != v) bad++;
                n++;
            }
        }
        snprintf(d, sizeof d, "%ld (MASTER value, sample) pairs: all 1024 knob values x every 16-bit input, %ld differ", n, bad);
        expect("no mute: out_gain_block equals the old o*master>>12 clamp", bad == 0, d);
    }
    /* ---- fade shape */
    {
        int32_t o[512], m = 4088, dfirst, dlast; int j, mono = 1, z = 0, over = 0;
        for (j = 0; j < 512; j++) o[j] = 30000;
        out_mute_g = OUT_UNITY; out_mute_req = 1; out_gain_block(o, 512, m);
        for (j = 1; j < 512; j++) if (o[j] > o[j - 1]) mono = 0;
        z += o[511] == 0; dfirst = o[0]; dlast = o[511];
        for (j = 0; j < 512; j++) o[j] = 30000;
        out_gain_block(o, 512, m);
        for (j = 0; j < 512; j++) if (o[j]) over++;
        for (j = 0; j < 512; j++) o[j] = 30000;
        out_mute_req = 0; out_gain_block(o, 512, m);
        for (j = 1; j < 512; j++) if (o[j] < o[j - 1]) mono = 0;
        snprintf(d, sizeof d, "down: first %d last %d; held half nonzero samples %d; up: first %d last %d; then gain back %d (MASTER 4088, input 30000, words L,R interleaved)",
                 dfirst, dlast, over, o[0], o[511], (int)out_mute_g);
        ok = mono && over == 0 && o[0] == 0 && out_mute_g == OUT_UNITY;
        for (j = 0; j < 512; j++) o[j] = 30000;
        out_gain_block(o, 512, m);
        for (j = 0; j < 512; j++) if (o[j] != (30000 * 4088) >> 12) ok = 0;
        expect("fade: down over one half, held at 0, up over one half, then exactly MASTER again", ok, d);
        (void)z;
    }
#endif

    /* ---- held note across every factory preset change (test_preset's sequence, ISR block model) */
    {
        int same_big = 0, strict_big = 0, notsound = 0, stuck = 0, worst_i = -1, worst_si = -1, stuck_req = 0;
        int32_t worst_sx = 0, worst_T = 0; double worst_r = 0, worst_sr = 0;
        poli_nav_init(); poli_preset_init(0);
        for (i = 0; i < PV_NSLOT; i++) {
            int32_t sb, sa_same, sx, T, sa_post, own; long Tat;
            long j;
            load_slot(i);
            polivoks_midi(0x90, 57, 100);
            run(4096);
            rec = w; rec_n = 0; rec_cap = (long)(sizeof w / sizeof w[0]);
            w[rec_n++] = prev;                         /* the sample before the window */
            run(2048);                                 /* steady, preset i: [1, X) */
#ifdef NO_MUTE
            run(BLK);                                  /* the old sound's last half, full gain: [X, Y) */
            load_slot((i + 1) % PV_NSLOT);
#else
            load_slot((i + 1) % PV_NSLOT);             /* the fade-down half is rendered inside: [X, Y) */
            if (rec_n != Y) { printf("FAIL wait rendered %ld samples, not one half\n", rec_n - X); fail++; }
#endif
            run(BLK);                                  /* first half of the new sound: [Y, POST) */
            run(2048);                                 /* [POST, END) */
            rec = 0;
            if (!poli_gate) notsound++;
            sb = maxstep(w, 1, X);
            sx = maxstep(w, Y, Y + 1);                 /* same method: the first sample after the change */
            sa_same = maxstep(w, Y + 1, Y + 1 + 2048);
            T = maxstep(w, X, POST);                   /* strict: anywhere in the two halves around the change */
            Tat = maxstep_at - X;
            sa_post = maxstep(w, POST + 1, END);
            if (sx > (sb > sa_same ? sb : sa_same)) { same_big++; if ((double)sx / (sb > sa_same ? sb : sa_same) > worst_r) { worst_r = (double)sx / (sb > sa_same ? sb : sa_same); worst_i = i; } }
            if (sx > worst_sx) worst_sx = sx;
            own = sb > sa_post ? sb : sa_post;
            if (T > own) { strict_big++; printf("INFO strict %02d->%02d: largest step in the 2 halves %d at sample %ld of 512 (%s half), own largest %d / %d\n", i + 1, (i + 1) % PV_NSLOT + 1, T, Tat, Tat < BLK ? "old-sound" : "new-sound", sb, sa_post);
                           if ((double)T / own > worst_sr) { worst_sr = (double)T / own; worst_si = i; } }
            if (T > worst_T) worst_T = T;
            if (out_mute_req) stuck_req++;
            polivoks_midi(0x80, 57, 0);
            run(44100 * 3);
            { int32_t ega; polivoks_read(0, 0, 0, 0, 0, 0, 0, 0, &ega, 0);
              rec = w; rec_n = 0; rec_cap = 2048; run(2048); rec = 0;
              for (j = 0, ok = 1; j < 2048; j++) if (w[j]) ok = 0;
              if (poli_gate || ega != 0 || !ok) stuck++; }
        }
        snprintf(d, sizeof d, "%d changes, key held: gate kept %d/%d, still sounding 3 s after key up %d, mute left on %d", PV_NSLOT, PV_NSLOT - notsound, PV_NSLOT, stuck, stuck_req);
        expect("held note across all slots: note continues, then stops; mute always released", !notsound && !stuck && !stuck_req, d);
        printf("MEASURE same-method (first sample after the change vs both sounds' own largest step): %d of %d above; largest first-sample step %d of 30000 (worst x%.2f at %02d->%02d)\n",
               same_big, PV_NSLOT, worst_sx, worst_r, worst_i + 1, worst_i + 2);
        if (worst_i < 0) printf("MEASURE (same-method: none above, so no worst pair)\n");
        printf("MEASURE strict (largest step anywhere in the 2 halves around the change vs both own largest): %d of %d above; largest step there %d of 30000 (worst x%.2f at %02d->%02d)\n",
               strict_big, PV_NSLOT, worst_T, worst_sr, worst_si + 1, worst_si + 2);
#ifndef NO_MUTE
        printf("MEASURE waits %ld, ISR halves rendered inside waits %ld (%.2f per wait), waits that gave up %ld\n",
               waits, blocks_in_wait, waits ? (double)blocks_in_wait / waits : 0.0, wait_gaveup);
#endif
    }

#ifndef NO_MUTE
    /* ---- loads still give the stored sound */
    {
        int bad = 0; uint8_t k[31], sw;
        poli_nav_init(); poli_preset_init(0);
        for (i = 0; i < PV_NSLOT; i++) {
            load_slot(i);
            poli_nav_patch_get(k, &sw);
            if (memcmp(k, PV_FACTORY[i].k, 31) || sw != PV_FACTORY[i].sw || out_mute_req || out_mute_g != 0) bad++;
            run(BLK);
            if (out_mute_g != OUT_UNITY) bad++;
        }
        snprintf(d, sizeof d, "%d muted loads: wrong patch / mute left on / not silent at the switch / not back to unity after a half: %d", PV_NSLOT, bad);
        expect("muted loads: right patch, silent at the switch, full gain one half later", !bad, d);
    }
    /* ---- compare (save screen SEL) also mutes, and cancel back to the edit */
    {
        long w0 = waits; uint8_t ke[31], swe, k[31], sw;
        poli_nav_init(); poli_preset_init(0);
        load_slot(12); poli_nav_section(NAV_SEC_FX); poli_nav_knob(0, 15);
        poli_nav_patch_get(ke, &swe);
        w0 = waits;
        poli_preset_save_btn(0); poli_preset_sel();          /* stored */
        poli_nav_patch_get(k, &sw); ok = !memcmp(k, PV_FACTORY[12].k, 31);
        poli_preset_sel();                                    /* edit */
        poli_nav_patch_get(k, &sw); ok = ok && !memcmp(k, ke, 31) && sw == swe;
        poli_preset_sel(); poli_preset_cancel();              /* stored, then cancel -> edit */
        poli_nav_patch_get(k, &sw); ok = ok && !memcmp(k, ke, 31);
        snprintf(d, sizeof d, "4 sound switches, %ld muted waits, mute left on %d", waits - w0, (int)out_mute_req);
        expect("compare switches are muted too, edit restored", ok && waits - w0 == 4 && !out_mute_req, d);
    }
    /* ---- audio stream stopped: the wait gives up and the load still happens */
    {
        uint8_t k[31], sw; long g0 = wait_gaveup;
        poli_nav_init(); poli_preset_init(0);
        out_mute_g = OUT_UNITY; out_mute_req = 0;
        irq_stopped = 1; load_slot(7); irq_stopped = 0;
        poli_nav_patch_get(k, &sw);
        snprintf(d, sizeof d, "gave up %ld time(s), patch %s, mute request after %d", wait_gaveup - g0, memcmp(k, PV_FACTORY[7].k, 31) ? "WRONG" : "slot 08", (int)out_mute_req);
        expect("no audio running: load still applied, no hang, mute released", wait_gaveup - g0 == 1 && !memcmp(k, PV_FACTORY[7].k, 31) && !out_mute_req, d);
        run(BLK); run(BLK);
        expect("after the stream resumes: silent half then back to unity", out_mute_g == OUT_UNITY, "out_mute_g");
    }
    /* ---- boot load is not muted (nothing sounds yet) */
    {
        long w0 = waits;
        poli_nav_init(); poli_preset_init(0);
        expect("boot load does not wait for a mute", waits == w0 && !out_mute_req, "poli_preset_init");
    }
#endif
    printf("%s %d pass %d fail\n", fail ? "HOST-MUTE FAIL" : "HOST-MUTE PASS", pass, fail);
    return fail ? 1 : 0;
}
