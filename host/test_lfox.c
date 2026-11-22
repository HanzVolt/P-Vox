/* SPDX-License-Identifier: GPL-3.0-only */
/* LFOTail-Sync-Vel host checks: LFO key restart, LFO sync to MIDI clock, velocity -> contour, preset extras. Not firmware. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"
#include "poli_nav.c"

/* ---- flash model: 0 bank A, 1 bank B, 2 backup, 3 settings */
#define SIM_SECT 4
#define SIM_MAGIC 0x544C4F56u
static uint8_t simf[SIM_SECT][4096];
static long sim_ops, sim_cut_at = -1, sim_writes[SIM_SECT], sim_erases;
static int sim_dead;
static uint32_t sim_crc(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) { c ^= *p++; for (k = 0; k < 8u; k++) c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1; }
    return ~c;
}
static int sim_step(void) { if (sim_dead) return 0; if (sim_cut_at >= 0 && sim_ops >= sim_cut_at) { sim_dead = 1; return 0; } sim_ops++; return 1; }
static void sim_reset(void) { memset(simf, 0xFF, sizeof simf); sim_ops = 0; sim_cut_at = -1; sim_dead = 0; memset(sim_writes, 0, sizeof sim_writes); sim_erases = 0; }
#define PV_STORE 1
static int pv_store_load(int w, void *p, uint32_t n)
{
    uint32_t h[3];
    if (n > 4096u - 12u) return -1;
    memcpy(h, simf[w], 12);
    if (h[0] != SIM_MAGIC) return -1;
    if (h[1] == n) {
        if (sim_crc(simf[w] + 12, n) != h[2]) return -1;
        memcpy(p, simf[w] + 12, n);
        return 0;
    }
    if (h[1] < n && h[1] >= 12u && h[1] <= 4096u - 12u) {
        if (sim_crc(simf[w] + 12, h[1]) != h[2]) return -1;
        memcpy(p, simf[w] + 12, h[1]);
        memset((uint8_t *)p + h[1], 0, n - h[1]);
        return 0;
    }
    return -1;
}
static int pv_store_save(int w, const void *p, uint32_t n)
{
    uint32_t h[3];
    if (n > 4096u - 12u) return -1;
    sim_writes[w]++;
    h[0] = SIM_MAGIC; h[1] = n; h[2] = sim_crc((const uint8_t *)p, n);
    if (!sim_step()) return -1;
    memset(simf[w], 0xFF, 4096); sim_erases++;
    if (!sim_step()) { memcpy(simf[w] + 12, p, n / 2); return -1; }   /* cut halfway through the bytes */
    memcpy(simf[w] + 12, p, n);
    if (!sim_step()) return -1;
    memcpy(simf[w], h, 12);
    return 0;
}
#include "poli_preset.c"
#define FS 44100
static int npass, nfail;
static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail && detail[0] ? "  " : "", detail ? detail : "");
    if (ok) npass++; else nfail++;
}
static int32_t buf[2 * 256];
static void render(uint32_t n) { while (n) { uint32_t k = n > 256u ? 256u : n; memset(buf, 0, sizeof buf); polivoks_render(buf, k); n -= k; } }
static double inc_hz(uint32_t inc) { return inc * (double)FS / 4294967296.0; }
static void boot(void) { sim_reset(); poli_nav_init(); poli_preset_init(0); }
/* set a slot by knob on its window (flat index, knob 0..3) to an absolute value */
static void set_knob(int fl, int k, int val)
{
    place(fl, 0);
    pos[WIN_SLOT[fl][k]] = (int16_t)val;
    apply_kind(WIN_SLOT[fl][k], 0);
}
/* n MIDI clocks at bpm, from time *t (ms, double), with optional +-jit ms jitter */
static void clocks(double *t, double bpm, int n, int jit)
{
    int i;
    double per = 60000.0 / (bpm * 24.0);
    for (i = 0; i < n; i++) {
        int j = jit ? (rand() % (2 * jit + 1)) - jit : 0;
        double tt = *t + j;
        poli_nav_tick_ms((uint32_t)(tt < 0 ? 0 : tt + 0.5));
        poli_nav_midi(0xF8, 0, 0);
        *t += per;
    }
}
static double wraps_hz2(double secs)
{
    /* exact: accumulate the increment per block via the phase difference */
    long n, N = (long)(secs * FS) / 256 * 256;
    uint32_t prev = polivoks_lfo_phase(), ph;
    double cyc = 0;
    for (n = 0; n < N; n += 256) { render(256); ph = polivoks_lfo_phase(); cyc += (double)(uint32_t)(ph - prev) / 4294967296.0; prev = ph; }
    return cyc / ((double)N / FS);
}

int main(void)
{
    char d[256];
    /* ---------------- defaults ---------------- */
    boot();
    expect("defaults_off_free_0", pos[SLOT_LFO_RST] == 0 && pos[SLOT_LFO_SYNC] == 0 && pos[SLOT_VEL] == 0 && !poli_lfo_restart && poli_vel_amt == 0, "");
    expect("lfo_window_k3_k4", WIN_SLOT[2][2] == SLOT_LFO_RST && WIN_SLOT[2][3] == SLOT_LFO_SYNC, "K3 RESTART, K4 SYNC on MODULATOR");
    expect("filtermod_k2_vel", WIN_SLOT[6][1] == SLOT_VEL, "K2 VELOC. on FILTER MOD");
    {   /* all 63 factory presets load with the extras at their defaults */
        int i, bad = 0;
        for (i = 0; i < PV_NSLOT; i++) { pv_load(i, 0); if (pos[SLOT_LFO_RST] || pos[SLOT_LFO_SYNC] || pos[SLOT_VEL] || pv_dirty()) bad++; }
        snprintf(d, sizeof d, "%d bad", bad); expect("factory_extras_default", bad == 0, d);
    }
    /* ---------------- KEY RESTART ---------------- */
    boot();
    set_knob(2, 0, 58);
    set_knob(2, 2, 1);                                   /* RESTART ON */
    expect("restart_knob_on", poli_lfo_restart == 1, "");
    polivoks_midi(0x90, 60, 100); render(1000); polivoks_midi(0x80, 60, 0); render(3333);
    {
        uint32_t inc = poli_lfo_inc, ph;
        polivoks_midi(0x90, 62, 100);                    /* new note: gate off -> on */
        render(256);
        ph = polivoks_lfo_phase();
        snprintf(d, sizeof d, "phase %u after one 256-frame block, expect %u", ph, inc * 256u);
        expect("restart_new_note_phase0", ph == inc * 256u, d);
        render(1111);
        ph = polivoks_lfo_phase();
        polivoks_midi(0x90, 64, 100);                    /* legato: gate already on */
        render(256);
        snprintf(d, sizeof d, "phase %u, continued %u", polivoks_lfo_phase(), ph + inc * 256u);
        expect("restart_legato_no_reset", polivoks_lfo_phase() == ph + inc * 256u, d);
        polivoks_midi(0x80, 64, 0); polivoks_midi(0x80, 62, 0); render(500);
        /* keybed note restarts too */
        polivoks_panel(1u << 3, 0, 0, 0, 0, 0, 0, 0, 0, 0); render(256);
        expect("restart_keybed", polivoks_lfo_phase() == inc * 256u, "");
        polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0); render(300);
    }
    {   /* S&H: a new value on restart */
        int32_t s0;
        set_knob(2, 1, 3);
        set_knob(2, 0, 10);                              /* slow, so no natural wrap inside one block */
        polivoks_midi(0x80, 60, 0); render(256);
        s0 = poli_sh;
        polivoks_midi(0x90, 60, 100); render(256);
        snprintf(d, sizeof d, "sh %d -> %d", s0, poli_sh);
        expect("restart_sh_new_value", poli_sh != s0, d);
        polivoks_midi(0x80, 60, 0); render(256);
    }
    {   /* OFF: no reset */
        uint32_t ph, inc;
        set_knob(2, 1, 0); set_knob(2, 2, 0); set_knob(2, 0, 58);
        inc = poli_lfo_inc; render(777);
        ph = polivoks_lfo_phase();
        polivoks_midi(0x90, 60, 100); render(256);
        expect("restart_off_continues", polivoks_lfo_phase() == ph + inc * 256u, "");
        polivoks_midi(0x80, 60, 0);
    }
    /* ---------------- SYNC ---------------- */
    {
        static const char *nm[10] = { "FREE", "1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
        static const double beats[10] = { 0, 4, 2, 1, 2.0 / 3, 0.5, 1.0 / 3, 0.25, 1.0 / 6, 0.125 };
        double bpms[3] = { 120, 90, 174 };
        int b, s;
        for (b = 0; b < 3; b++) {
            double t = 1000;
            boot();
            set_knob(2, 3, 3);
            expect(b == 0 ? "sync_no_clock_plays_free" : "sync_no_clock_plays_free_again", poli_lfo_inc == lfo_inc_of(pos[SLOT_LFO_RATE]), "no tempo yet: the RATE knob law");
            clocks(&t, bpms[b], 96, 0);
            for (s = 1; s <= 9; s++) {
                double want = bpms[b] / 60.0 / beats[s], got, meas;
                set_knob(2, 3, s);
                got = inc_hz(poli_lfo_inc);
                meas = wraps_hz2(2.0);
                snprintf(d, sizeof d, "%.0f BPM %-5s want %.4f Hz, increment %.4f Hz, rendered %.4f Hz (%.3f %%)", bpms[b], nm[s], want, got, meas, 100 * (meas - want) / want);
                expect("sync_rate", fabs(meas - want) / want < 0.002, d);
            }
        }
        {   /* jitter +-1 ms (USB poll / main loop), 120 BPM 1/4 */
            double t = 1000, hz, worst = 0; int k;
            boot(); set_knob(2, 3, 3); srand(7);
            clocks(&t, 120, 48, 1);
            for (k = 0; k < 200; k++) { clocks(&t, 120, 1, 1); hz = inc_hz(poli_lfo_inc); if (fabs(hz - 2.0) > worst) worst = fabs(hz - 2.0); }
            snprintf(d, sizeof d, "worst over 200 clocks %.4f Hz from 2.0000 (%.2f %%)", worst, 50 * worst);
            expect("sync_jitter_1ms", worst < 0.01, d);
        }
        {   /* dropout: tempo held; resume at a new tempo */
            double t = 1000, h0, h1; uint32_t inc0;
            boot(); set_knob(2, 3, 3);
            clocks(&t, 120, 96, 0);
            inc0 = poli_lfo_inc; h0 = inc_hz(inc0);
            t += 3000; render(3 * FS);
            snprintf(d, sizeof d, "after 3 s without clock: %.4f Hz (was %.4f)", inc_hz(poli_lfo_inc), h0);
            expect("sync_dropout_holds", poli_lfo_inc == inc0, d);
            clocks(&t, 140, 1, 0);
            expect("sync_resume_first_clock_no_jump", poli_lfo_inc == inc0, "one clock after the gap: no interval yet, still held");
            clocks(&t, 140, 48, 0);
            h1 = inc_hz(poli_lfo_inc);
            snprintf(d, sizeof d, "after 48 clocks at 140 BPM: %.4f Hz (want %.4f)", h1, 140 / 60.0);
            expect("sync_resume_new_tempo", fabs(h1 - 140 / 60.0) < 0.005, d);
            set_knob(2, 3, 0);
            expect("sync_free_back_to_knob", poli_lfo_inc == lfo_inc_of(pos[SLOT_LFO_RATE]), "");
        }
        {   /* tempo change 120 -> 100 without a gap: settles */
            double t = 1000; int k, settle = -1;
            boot(); set_knob(2, 3, 3);
            clocks(&t, 120, 96, 0);
            for (k = 1; k <= 96; k++) { clocks(&t, 100, 1, 0); if (settle < 0 && fabs(inc_hz(poli_lfo_inc) - 100 / 60.0) < 0.005) settle = k; }
            snprintf(d, sizeof d, "within 0.3 %% after %d clocks (%.2f beats)", settle, settle / 24.0);
            expect("sync_tempo_change_settles", settle > 0 && settle <= 72, d);
        }
        {   /* burst: 20 queued clocks at one ms (a stall) do not move the tempo by more than 12.5 % */
            double t = 1000; uint32_t inc0; int k;
            boot(); set_knob(2, 3, 3);
            clocks(&t, 120, 96, 0);
            inc0 = poli_lfo_inc;
            t += 150;   /* a 150 ms stall (under the 500 ms gap) */
            for (k = 0; k < 8; k++) { poli_nav_tick_ms((uint32_t)t); poli_nav_midi(0xF8, 0, 0); }
            t += 60000.0 / (120 * 24);
            clocks(&t, 120, 2, 0);
            snprintf(d, sizeof d, "%.4f Hz after the burst (was %.4f)", inc_hz(poli_lfo_inc), inc_hz(inc0));
            expect("sync_burst_guard", fabs(inc_hz(poli_lfo_inc) - inc_hz(inc0)) / inc_hz(inc0) < 0.125, d);
        }
        {   /* MIDI Start: phase 0 on the next clock (synced only) */
            double t = 1000; uint32_t inc;
            boot(); set_knob(2, 3, 3);
            clocks(&t, 120, 48, 0);
            render(5000);
            poli_nav_midi(0xFA, 0, 0);
            render(256);
            expect("start_waits_for_clock", polivoks_lfo_phase() != 0u && poli_lfo_rst == 0, "");
            clocks(&t, 120, 1, 0);
            inc = poli_lfo_inc;
            render(256);
            snprintf(d, sizeof d, "phase %u expect %u", polivoks_lfo_phase(), inc * 256u);
            expect("start_resets_phase", polivoks_lfo_phase() == inc * 256u, d);
            set_knob(2, 3, 0); render(1000);
            poli_nav_midi(0xFA, 0, 0); clocks(&t, 120, 1, 0);
            expect("start_free_no_reset", poli_lfo_rst == 0, "FREE: Start does not touch the LFO");
        }
        {   /* clock and transport are not sound edits and do not reach the CC map */
            double t = 1000; uint32_t e0; int32_t c0;
            boot(); e0 = poli_nav_edits(); c0 = poli_cut;
            clocks(&t, 120, 30, 0); poli_nav_midi(0xFA, 0, 0); poli_nav_midi(0xFB, 0, 0); poli_nav_midi(0xFC, 0, 0);
            expect("realtime_no_edit", poli_nav_edits() == e0 && poli_cut == c0 && !pv_dirty(), "");
        }
    }
    /* ---------------- VELOCITY -> CONTOUR ---------------- */
    {
        int32_t knob, e127, e64, e1, ek;
        boot();
        set_knob(5, 2, 100);                               /* contour */
        knob = poli_eg_knob;
        set_knob(6, 1, 127);                               /* VELOC. 127 */
        polivoks_midi(0x90, 60, 127); e127 = polivoks_contour_eff(); polivoks_midi(0x80, 60, 0);
        polivoks_midi(0x90, 60, 64);  e64 = polivoks_contour_eff();  polivoks_midi(0x80, 60, 0);
        polivoks_midi(0x90, 60, 1);   e1 = polivoks_contour_eff();   polivoks_midi(0x80, 60, 0);
        polivoks_panel(1u << 5, 0, 0, 0, 0, 0, 0, 0, 0, 0); ek = polivoks_contour_eff(); polivoks_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        snprintf(d, sizeof d, "knob %d: vel127 %d (%.1f %%), vel64 %d (%.1f %%), vel1 %d (%.1f %%), keybed %d; peak contour shift (26000 peak) %d / %d / %d cut units",
                 knob, e127, 100.0 * e127 / knob, e64, 100.0 * e64 / knob, e1, 100.0 * e1 / knob, ek,
                 (int)((26000LL * e127) >> 15), (int)((26000LL * e64) >> 15), (int)((26000LL * e1) >> 15));
        expect("vel_depth_127_64_1", e127 == knob && e64 < knob && e64 > knob * 49 / 100 && e64 < knob * 51 / 100 && e1 < knob / 100 && ek == knob, d);
        polivoks_midi(0x90, 60, 30); e64 = polivoks_contour_eff();
        polivoks_midi(0x90, 62, 127);
        snprintf(d, sizeof d, "first %d, after legato vel127 %d", e64, polivoks_contour_eff());
        expect("vel_legato_keeps_first", polivoks_contour_eff() == e64, d);
        polivoks_midi(0x90, 62, 0);
        expect("vel0_is_note_off", poli_midi_on == 0, "note-on velocity 0 = note-off (both MIDI notes counted as one gate in this engine)");
        polivoks_midi(0x80, 60, 0);
        set_knob(5, 2, 50);
        polivoks_midi(0x90, 60, 64);
        snprintf(d, sizeof d, "contour turned to 50 during the note: %d of %d", polivoks_contour_eff(), poli_eg_knob);
        expect("vel_contour_turn_keeps_scale", polivoks_contour_eff() < poli_eg_knob && polivoks_contour_eff() > poli_eg_knob * 49 / 100, d);
        polivoks_midi(0x80, 60, 0);
        set_knob(6, 1, 0);
        expect("vel_off_full_depth", polivoks_contour_eff() == poli_eg_knob, "");
    }
    {   /* amount 0: any velocity renders bit-identical to the old engine (velocity 127) */
        static int32_t a[2 * 44100], b2[2 * 44100];
        int v, same = 1, n;
        for (v = 0; v < 2; v++) {
            int32_t *o = v ? b2 : a;
            boot(); set_knob(5, 2, 127); set_knob(5, 0, 30);
            polivoks_midi(0x90, 48, v ? 1 : 127);
            memset(o, 0, sizeof a);
            for (n = 0; n < 44100; n += 256) polivoks_render(o + 2 * n, 44100 - n < 256 ? 44100 - n : 256);
        }
        same = !memcmp(a, b2, sizeof a);
        expect("vel_amt0_bit_identical", same, "1 s, contour 127, cutoff 30: velocity 1 vs 127 with VELOC. 0");
    }
    {   /* velocity lowers the filter sweep: rendered peak cutoff */
        int v, vs[3] = { 127, 64, 1 }; int32_t pk[3];
        for (v = 0; v < 3; v++) {
            int n; int32_t m = 0;
            boot(); set_knob(5, 2, 127); set_knob(5, 0, 20); set_knob(6, 1, 127);
            polivoks_midi(0x90, 48, (uint8_t)vs[v]);
            for (n = 0; n < 8820; n++) { memset(buf, 0, 8); polivoks_render(buf, 1); if (poli_fc.cut > m) m = poli_fc.cut; }
            pk[v] = m;
        }
        snprintf(d, sizeof d, "peak smoothed filter cut (cutoff knob 20, contour 127, VELOC. 127): vel127 %d, vel64 %d, vel1 %d", pk[0], pk[1], pk[2]);
        expect("vel_filter_sweep", pk[0] > pk[1] && pk[1] > pk[2], d);
    }
    /* ---------------- preset extras ---------------- */
    {
        pv_rec_t r; uint8_t k[31], sw, x[3] = { 1, 7, 99 }, x2[3];
        boot();
        poli_nav_patch_get(k, &sw);
        pv_rec_make_x(&r, PV_FACTORY[0].name, sw, k, x);
        expect("rec_x_bytes", r.b[49] == 1 && r.b[50] == 7 && r.b[51] == 99 && r.b[3] == 31 && r.b[1] == PV_LAYOUT && r.b[2] == PV_LAW && pv_rec_ok(&r), "bytes 49..51, nknob 31, layout and law unchanged, CRC ok");
        pv_bank.rec[5] = r; pv_load(5, 0);
        poli_nav_patch_get_x(x2);
        expect("rec_x_load", x2[0] == 1 && x2[1] == 7 && x2[2] == 99 && poli_lfo_restart == 1 && poli_vel_amt == 99 && !pv_dirty(), "");
        set_knob(2, 3, 3);
        expect("rec_x_dirty", pv_dirty(), "a SYNC change marks the preset edited");
        r.b[50] = 200; r.b[49] = 9;
        { uint32_t c = pv_crc32(r.b, 60u); r.b[60] = (uint8_t)c; r.b[61] = (uint8_t)(c >> 8); r.b[62] = (uint8_t)(c >> 16); r.b[63] = (uint8_t)(c >> 24); }
        pv_bank.rec[6] = r; pv_load(6, 0);
        poli_nav_patch_get_x(x2);
        expect("rec_x_unknown_default", pv_rec_ok(&r) && x2[0] == 0 && x2[1] == 0 && x2[2] == 99, "unknown values load as the default; the record is not rejected");
        memset(&r.b[49], 0, 3);
        { uint32_t c = pv_crc32(r.b, 60u); r.b[60] = (uint8_t)c; r.b[61] = (uint8_t)(c >> 8); r.b[62] = (uint8_t)(c >> 16); r.b[63] = (uint8_t)(c >> 24); }
        pv_bank.rec[7] = r; pv_load(7, 0); poli_nav_patch_get_x(x2);
        expect("rec_old_zero_defaults", x2[0] == 0 && x2[1] == 0 && x2[2] == 0 && pv_rec_ok(&r), "an older record (reserved bytes 0) loads OFF / FREE / 0");
    }
    {   /* save + reboot through the flash model keeps the extras */
        uint8_t x2[3];
        boot();
        set_knob(2, 2, 1); set_knob(2, 3, 8); set_knob(6, 1, 64);
        poli_preset_save_btn(0); poli_preset_save_btn(0);
        poli_nav_init(); poli_preset_init(0);
        poli_nav_patch_get_x(x2);
        snprintf(d, sizeof d, "after reboot: restart %d sync %d vel %d", x2[0], x2[1], x2[2]);
        expect("save_reboot_extras", x2[0] == 1 && x2[1] == 8 && x2[2] == 64, d);
    }
    printf("HOST-LFOX %s %d pass %d fail\n", nfail ? "FAIL" : "PASS", npass, nfail);
    return nfail ? 1 : 0;
}
