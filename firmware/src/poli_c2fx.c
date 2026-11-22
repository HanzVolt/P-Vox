/* SPDX-License-Identifier: GPL-3.0-only */
/* Core2-FxSeq (15 Nov 2026): the ADDED sound features (NOT Polivoks: the original has no delay, reverb or
 * drum machine) and their hand-off to the second core. Ground-up code: no stock FM-1 code, no samples.
 * See poli_xtra.h for the plan of the hand-off.
 *
 *   DELAY   one mono line, 65536 int16 samples (1.486 s), tempo divisions set by the main program (poli_xtra.c);
 *           FEEDBACK up to 0.9, TONE = a one-pole low pass inside the loop (each echo darker), LEVEL.
 *           A time change crossfades the two read points over one half (no click).
 *   REVERB  Schroeder-Moorer: per side 4 combs with a one-pole damper in the loop, then 2 allpasses (g 0.5);
 *           left and right lengths differ (stereo spread). SIZE = comb feedback 0.70..0.94, DAMP, LEVEL.
 *   DRUMS   (Wander-Drift, 16 Nov 2026) a two-kit drum machine, mono into both sides: 26 synthesized
 *           instruments in an 808-style and a 909-style family (poli_xtra.c XT_VOICE), played by a pool of 6
 *           voices (xf_drum_hit, xf_drums_render). No samples. Hits come from the main program through a ring.
 *
 * All of it is the "added" signal (xf_wet). It is turned down when the dry voice plus it would pass 32600
 * (the headroom stage below), so the output clamps (polivoks_render's +-30000, out_gain_block's +-32767)
 * are not reached through it alone. Off = the send stops and the tail rings out; after 8 quiet halves
 * the effect's lines are emptied and it costs nothing. Panic / CC 120 / CC 123: one half's fade to zero,
 * then every line and drum voice is cleared.
 *
 * Core 1 code: everything named xf_c1_* and the job's helpers (always inline) sit in .c1_text, in RAM, and
 * use RAM only (tools/build.py checks it: no call or goto out of .c1_text, no flash address, no 64-bit
 * multiply). Q15 math in 32-bit words throughout. */
#include "poli_xtra.h"

#ifdef DINGHY_HOST
#define XF_C1
#define XF_POOL
#else
#define XF_C1 __attribute__((section(".c1_text"), noinline, used))
#define XF_POOL __attribute__((section(".pool")))
#endif
#define XF_IN static inline __attribute__((always_inline))

#ifdef DINGHY_HOST
/* host model of the hand-off (hal/fm1_cpu1.h on the device): a job runs at once, in line, unless a test sets
 * xf_host_defer (then it runs at xf_host_finish(): core 1 late). The wet is one half late either way. */
static uint32_t xf_host_job, xf_host_done;
static int xf_host_defer;
static void (*xf_host_fn)(uint32_t);
static inline uint32_t fm1_c1_ticks(void) { return 0; }
static inline int fm1_cpu1_idle(void) { return xf_host_done == xf_host_job; }
static inline int fm1_cpu1_run(void (*fn)(uint32_t), uint32_t arg)
{
    xf_host_fn = fn;
    xf_host_job++;
    if (!xf_host_defer) { fn(arg); xf_host_done = xf_host_job; }
    return 1;
}
static inline void fm1_cpu1_hold(void) { }
static inline void xf_host_finish(void) { if (xf_host_done != xf_host_job) { xf_host_fn(0); xf_host_done = xf_host_job; } }
#endif

/* hand-off buffers: stride 2 (L R), as the audio buffer */
static int32_t xf_dry[2u * XF_N] __attribute__((aligned(4))) XT_KEEP;   /* core 0 writes (ISR), core 1 reads; only L used */
static int32_t xf_wet[2u * XF_N] __attribute__((aligned(4))) XT_KEEP;   /* core 1 writes, core 0 adds */
static volatile uint8_t xf_run XT_KEEP;          /* main program: 1 = each half goes through core 1 */
static volatile uint32_t xf_late XT_KEEP;        /* halves when core 1 had not finished: left dry (ISR counts) */
static volatile uint32_t xf_late_run XT_KEEP;    /* in a row (ISR); the main program holds core 1 at XF_LATE_HOLD */
static volatile uint32_t xf_mixed XT_KEEP;       /* halves mixed */
/* Drift-Full: core 1 writes it at the end of each job, for the wet it just made: -1 = the interrupt ADDS the wet
 * to the dry (delay / reverb only: as before); 0 = the wet REPLACES the dry (MIX mode: core 1 made the whole mix
 * from the dry it was given, one half late, so it can duck, gate, repeat, reverse, filter and clip all of it) */
static volatile int32_t xf_keep XT_KEEP;
#define XF_LATE_HOLD 8u

/* the lines (.pool: zero at power-on) */
static int16_t xf_dl[XF_DLY_N] XF_POOL;
#define RV_C0 1093
#define RV_C1 1171
#define RV_C2 1259
#define RV_C3 1327
#define RV_C4 1117
#define RV_C5 1193
#define RV_C6 1277
#define RV_C7 1361
#define RV_A0 389
#define RV_A1 127
#define RV_A2 401
#define RV_A3 137
#define RV_N (RV_C0 + RV_C1 + RV_C2 + RV_C3 + RV_C4 + RV_C5 + RV_C6 + RV_C7 + RV_A0 + RV_A1 + RV_A2 + RV_A3)
static int16_t xf_rv[RV_N] XF_POOL;
#define RV_O0 0
#define RV_O1 (RV_O0 + RV_C0)
#define RV_O2 (RV_O1 + RV_C1)
#define RV_O3 (RV_O2 + RV_C2)
#define RV_O4 (RV_O3 + RV_C3)
#define RV_O5 (RV_O4 + RV_C4)
#define RV_O6 (RV_O5 + RV_C5)
#define RV_O7 (RV_O6 + RV_C6)
#define RV_P0 (RV_O7 + RV_C7)
#define RV_P1 (RV_P0 + RV_A0)
#define RV_P2 (RV_P1 + RV_A1)
#define RV_P3 (RV_P2 + RV_A2)

/* core 1's own state (only core 1 touches it after boot) */
static struct {
    uint32_t dw, dlen, dlen_new;         /* delay write index, the spacing in use, the one faded to */
    int32_t dlp;                         /* the TONE low pass state */
    int32_t dsend, rsend, dlvl, rlvl, drl;   /* ramped: sends 0..32767, levels Q15 */
    uint8_t dalive, ralive, dquiet, rquiet, quiet;
    int32_t ri[12];                      /* comb / allpass indexes */
    int32_t rf[8];                       /* comb dampers */
    int32_t g;                           /* headroom gain, Q15 (32768 = none) */
    int32_t dry_env;                     /* dry peak, held and decaying */
    uint32_t trr;                        /* Wander-Drift: drum hits taken from the ring */
    uint32_t rng;
    uint32_t sub;                        /* sample count for the 16-sample envelope step */
    int32_t fade;                        /* panic fade */
    /* Drift-Full: MIX mode */
    uint8_t mix, frz, pf, lpon, hpon;    /* MIX on; the ring frozen; the ring effect playing (1 REPEAT, 2 REVERSE,
                                          * 3 TAPE STOP); the LP / HP sweeps engaged */
    int32_t xin, hl, hr;                 /* the step into MIX: a crossfade from the last sample heard */
    int32_t gg, gph, gpar;               /* the gate: gain (ramped), place in the 16th, odd / even 16th */
    uint32_t s16;
    int32_t duck;                        /* the duck's depth now, Q15 */
    uint32_t mw, rp, rstart, tpos;       /* ring write frame; read frame; REPEAT start; TAPE STOP place Q16 */
    int32_t rlen, rcnt, tspd, fxg;       /* REPEAT length, its first pass; TAPE STOP speed Q16; the effect's fade */
    int32_t lpc, hpc, f1l, f2l, f1r, f2r, h1l, h2l, h1r, h2r;   /* sweep coefficients Q24 and filter states */
    int32_t crc, crv;                    /* the crush: sample-hold count and held value */
} xs XT_KEEP;
/* Drift-Full: the PERFORM ring (.pool, zero at power-on): the finished mix before the PERFORM effects, L R */
static int16_t xf_mr[2u * XF_MR_N] XF_POOL;

/* a x b / 32768 rounded toward zero (|a| < 2^16, |b| <= 2^15: fits). Toward zero, not down: a loop with feedback
 * (comb, allpass, the delay) then dies out to 0 instead of sitting at -1 for ever */
XF_IN int32_t xf_mq(int32_t a, int32_t b) { int32_t p = a * b; return (p + ((p >> 31) & 32767)) >> 15; }
XF_IN int32_t xf_half(int32_t v) { return (v + (int32_t)((uint32_t)v >> 31)) >> 1; }   /* v / 2 toward zero */
XF_IN int32_t xf_sat16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
XF_IN int32_t xf_iabs(int32_t v) { return v < 0 ? -v : v; }
XF_IN int32_t xf_ramp_to(int32_t cur, int32_t tgt, int32_t step)
{
    if (cur < tgt) { cur += step; if (cur > tgt) cur = tgt; }
    else if (cur > tgt) { cur -= step; if (cur < tgt) cur = tgt; }
    return cur;
}
/* one damped comb: y out of the line, the damper follows it, the line takes input + damper * fb */
#define COMB(o, n, k, in, fb, damp, acc) do { int32_t y_ = xf_rv[(o) + xs.ri[k]]; \
    xs.rf[k] += xf_mq(y_ - xs.rf[k], damp); \
    xf_rv[(o) + xs.ri[k]] = (int16_t)xf_sat16((in) + xf_mq(xs.rf[k], fb)); \
    if (++xs.ri[k] >= (n)) { xs.ri[k] = 0; } (acc) += y_; } while (0)
/* Schroeder allpass, g = 0.5: out = buf - x/2, buf <- x + out/2 */
#define APASS(o, n, k, x) do { int32_t b_ = xf_rv[(o) + xs.ri[k]]; int32_t o_ = b_ - xf_half(x); \
    xf_rv[(o) + xs.ri[k]] = (int16_t)xf_sat16((x) + xf_half(o_)); \
    if (++xs.ri[k] >= (n)) { xs.ri[k] = 0; } (x) = o_; } while (0)

XF_IN void xf_zero16(int16_t *p, uint32_t n)
{
    volatile int16_t *q = p;             /* (volatile: no memset call is made of it) */
    uint32_t i;
    for (i = 0; i < n; i++) q[i] = 0;
}
XF_IN void xf_dly_clear(void) { xf_zero16(xf_dl, XF_DLY_N); xs.dlp = 0; xs.dalive = 0; xs.dquiet = 0; }
XF_IN void xf_rev_clear(void)
{
    uint32_t i;
    xf_zero16(xf_rv, RV_N);
    for (i = 0; i < 8u; i++) xs.rf[i] = 0;
    xs.ralive = 0; xs.rquiet = 0;
}
/* Wander-Drift: the drum machine on core 1. XF_DV voices; a hit takes the voice already playing that instrument,
 * else a free one, else the quietest. Each voice: a tone path (parabolic sine, pitch envelope) and a noise / metal
 * path (noise + up to 6 square oscillators -> state-variable filter), envelopes every 16 samples. Rendered voice
 * by voice into xf_dbuf for the half, then mixed in the sample loop below. */
typedef struct {
    int32_t inst;                        /* -1 free */
    uint32_t ph;
    int32_t tenv, penv, nenv, nlev;
    int32_t inc1, dinc, tdec, pdec, namp, mamp, ndec, bdec, f, q, mode, bursts, bgap, bt, mn;
    int32_t lp, bp;
    uint32_t mph[6];
    int32_t minc[6];
    int32_t kick, send;                  /* Drift-Full: from the patch at the hit */
} xf_dvoice_t;
static xf_dvoice_t xf_dv[XF_DV] XT_KEEP;
static int32_t xf_dbuf[XF_N] XT_KEEP;
static int32_t xf_sbuf[XF_N] XT_KEEP;    /* Drift-Full: the drums' echo / reverb send bus */
static int32_t xf_vbuf[XF_N] XT_KEEP;    /* Drift-Full: one voice, before its drive and its send */
XF_IN void xf_drums_clear(void)
{
    uint32_t j;
    for (j = 0; j < XF_DV; j++) { xf_dv[j].inst = -1; xf_dv[j].tenv = xf_dv[j].nenv = 0; xf_dv[j].bursts = 0; }
}
XF_IN int32_t xf_drums_active(void)
{
    uint32_t j;
    for (j = 0; j < XF_DV; j++) if (xf_dv[j].inst >= 0) return 1;
    return 0;
}
XF_C1 void xf_c1_drum_hit(uint32_t e)   /* (its own function in .c1_text: the hit loop stays small) */
{
    uint32_t in = e & 255u, vel = (e >> 8) & 255u, pm = e >> 16, j, k;
    int32_t lvl = (int32_t)vel * 258, best = 0x7FFFFFFF;
    xf_dvoice_t *v = 0;
    const xf_patch_t *p;
    if (in >= XF_DI) return;
    p = &xf_patch[in];
    for (j = 0; j < XF_DV; j++) {
        if (p->choke >= 0 && xf_dv[j].inst == p->choke) { xf_dv[j].inst = -1; xf_dv[j].tenv = xf_dv[j].nenv = 0; xf_dv[j].bursts = 0; }
        if (xf_dv[j].inst == (int32_t)in) v = &xf_dv[j];
    }
    if (!v) for (j = 0; j < XF_DV; j++) if (xf_dv[j].inst < 0) { v = &xf_dv[j]; v->lp = v->bp = 0; break; }
    if (!v) for (j = 0; j < XF_DV; j++) {
        int32_t l = xf_dv[j].tenv + xf_dv[j].nenv;
        if (l < best) { best = l; v = &xf_dv[j]; }
    }
    if (p->kick && xf_par.duck_on) {     /* Drift-Full: the kick ducks the voice and the returns */
        int32_t dd = xf_par.duck_depth * (int32_t)vel / 127;
        if (dd > xs.duck) xs.duck = dd;
    }
    v->inst = (int32_t)in;
    v->kick = p->kick; v->send = p->send;
    v->lp = v->bp = 0;
    v->ph = 0;
    v->tenv = xf_mq(p->tamp, lvl);
    v->penv = 32767;
    v->nlev = (p->namp || p->mn) ? lvl : 0;
    v->nenv = v->nlev;
    v->inc1 = p->inc1; v->dinc = p->dinc;
    v->tdec = p->tdec; v->pdec = p->pdec; v->namp = p->namp; v->mamp = p->mamp; v->ndec = p->ndec; v->bdec = p->bdec;
    v->f = p->f; v->q = p->q; v->mode = p->mode; v->bursts = p->bursts; v->bgap = p->bgap; v->bt = p->bgap; v->mn = p->mn;
    for (k = 0; k < 6u; k++) v->minc[k] = p->minc[k];
    if (pm != 4096u) {                   /* a pitched hit (the cowbell lane): steps x pm / 4096, no 64-bit multiply */
        v->inc1 = (v->inc1 >> 12) * (int32_t)pm; v->dinc = (v->dinc >> 12) * (int32_t)pm;
        for (k = 0; k < 6u; k++) v->minc[k] = (v->minc[k] >> 12) * (int32_t)pm;
    }
}
#define XF_SQ(k) do { v->mph[k] += (uint32_t)v->minc[k]; src += (int32_t)v->mph[k] < 0 ? -ma : ma; } while (0)
XF_C1 void xf_c1_drums_render(void)
{
    uint32_t j, b, i;
    uint32_t rng = xs.rng;
    int32_t kd = xf_par.kdrive, cr = xf_par.crush;
    for (i = 0; i < XF_N; i++) { xf_dbuf[i] = 0; xf_sbuf[i] = 0; }
    for (j = 0; j < XF_DV; j++) {
        xf_dvoice_t *v = &xf_dv[j];
        if (v->inst < 0) continue;
        for (b = 0; b < XF_N; b += 16u) {
            int32_t te, ne, inc, ma = v->mamp, na = v->namp, f = v->f, q = v->q, lp = v->lp, bp = v->bp, mode = v->mode, mn = v->mn;
            v->tenv = xf_mq(v->tenv, v->tdec);
            v->penv = xf_mq(v->penv, v->pdec);
            if (v->bursts) {             /* the clap: quick re-attacks, then the tail */
                v->nenv = xf_mq(v->nenv, v->bdec);
                if (--v->bt <= 0) { v->bursts--; v->bt = v->bgap; v->nenv = v->nlev; }
            } else v->nenv = xf_mq(v->nenv, v->ndec);
            te = v->tenv; ne = v->nenv;
            for (i = b; i < b + 16u; i++) xf_vbuf[i] = 0;
            if (te) {
                uint32_t ph = v->ph;
                inc = v->inc1 + (((v->dinc >> 12) * v->penv) >> 3);
                for (i = b; i < b + 16u; i++) {
                    int32_t t, s;
                    ph += (uint32_t)inc;
                    t = (int32_t)ph >> 16;
                    s = (t * (32768 - xf_iabs(t))) >> 13;
                    if (s > 32767) s = 32767;
                    xf_vbuf[i] += xf_mq(s, te);
                }
                v->ph = ph;
            }
            if (ne) {
                for (i = b; i < b + 16u; i++) {
                    int32_t src = 0, hp, o;
                    if (na) {
                        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                        src = xf_mq((int32_t)(rng >> 17) - 16384, na) * 2;
                    }
                    if (mn == 6) { XF_SQ(0); XF_SQ(1); XF_SQ(2); XF_SQ(3); XF_SQ(4); XF_SQ(5); }
                    else if (mn == 2) { XF_SQ(0); XF_SQ(1); }
                    src = xf_sat16(src);
                    lp = xf_sat16(lp + xf_mq(f, bp));
                    hp = xf_sat16(src - lp - xf_mq(q, bp));
                    bp = xf_sat16(bp + xf_mq(f, hp));
                    o = mode == 2 ? hp : mode == 1 ? bp : lp;
                    xf_vbuf[i] += xf_mq(o, ne);
                }
                v->lp = lp; v->bp = bp;
            }
        }
        {                                /* Drift-Full: the kick's drive (a cubic soft clip), the send bus */
            int32_t isk = v->kick && kd > 4096, sn = v->send;
            for (i = 0; i < XF_N; i++) {
                int32_t x = xf_sat16(xf_vbuf[i]);
                if (isk) {
                    int32_t t = (x * (kd >> 4)) >> 8;
                    if (t > 32767) t = 32767;
                    if (t < -32767) t = -32767;
                    x = (3 * t - (((t * t) >> 15) * t >> 15)) >> 1;
                }
                xf_dbuf[i] += x;
                if (sn) xf_sbuf[i] += xf_mq(x, sn);
            }
        }
        if (v->tenv < 8 && v->nenv < 8 && !v->bursts) { v->inst = -1; v->tenv = v->nenv = 0; }
    }
    xs.rng = rng;
    if (cr) {                            /* BREAK / HARD: the dry drums, sample-held and coarser */
        int32_t hold = cr == 1 ? 2 : 3, msk = cr == 1 ? ~0x1FF : ~0x7FF;
        for (i = 0; i < XF_N; i++) {
            if (++xs.crc >= hold) { xs.crc = 0; xs.crv = xf_dbuf[i] & msk; }
            xf_dbuf[i] = xs.crv;
        }
    }
}

/* ---- Drift-Full: MIX mode, one half. The voice (gated, ducked), the echo / reverb returns (ducked; fed by the
 * gated voice and the drums' send bus), the dry drums (never ducked) -> the PERFORM ring -> REPEAT / REVERSE /
 * TAPE STOP -> LP / HP sweeps -> a soft clip (unity under 24576, smooth to 32767). The whole output: the interrupt
 * plays it instead of the dry (xf_keep 0). Peaks for the tails come back in xf_pk. */
static int32_t xf_pk[3] XT_KEEP;         /* delay return, reverb return, output */
#define XF_LPMAX 15099494                /* LP sweep a, Q24: 0.90 (about 16 kHz) .. 0.035 (about 250 Hz) */
#define XF_LPMIN 587202
#define XF_HPMIN 47815                   /* HP sweep a, Q24: 0.00285 (about 20 Hz) .. 0.204 (about 1.6 kHz) */
#define XF_HPMAX 3422552
XF_IN int32_t xf_soft(int32_t v)
{
    int32_t a = xf_iabs(v), e;
    if (a <= 24576) return v;
    e = a - 24576;
    if (e > 16382) e = 16382;
    a = 24576 + e - ((e * e) >> 15);
    if (a > 32767) a = 32767;
    return v < 0 ? -a : a;
}
XF_IN int32_t xf_xf(int32_t a, int32_t b, int32_t g) { return g >= 32767 ? b : a + (((b - a) >> 1) * g >> 14); }   /* a -> b by g Q15 */
XF_C1 void xf_c1_mix(uint32_t dlen_r, uint32_t dlen_n, int32_t xstep, int32_t fb, int32_t tone, int32_t rfb,
                     int32_t damp, int32_t dsend_t, int32_t rsend_t, int32_t dlvl_t, int32_t rlvl_t, int32_t drl_t,
                     int32_t drums_on)
{
    uint32_t i, M = XF_MR_N - 1u;
    int32_t xfade = 0, dopk = 0, ropk = 0, opk = 0, x;
    int32_t gm = xf_par.gate, sstep = xf_par.step_smp, dk = xf_par.duck_k, ech = xf_par.d_echo, rvs = xf_par.d_rev;
    int32_t perf = xf_par.perf, want = (perf & 4) ? 3 : (perf & 2) ? 2 : (perf & 1) ? 1 : 0, s16 = (int32_t)xf_par.s16;
    if (sstep < 64) sstep = 64;
    if ((uint32_t)s16 != xs.s16) { xs.s16 = (uint32_t)s16; xs.gph = 0; xs.gpar = s16 & 1; }   /* the gate's resync */
    if (want && want != xs.pf) {         /* a ring effect starts */
        xs.pf = want;
        if (want == 1) {
            int32_t l = xf_par.rep_len;
            xs.rlen = l < 64 ? 64 : l > (int32_t)XF_MR_MAX ? (int32_t)XF_MR_MAX : l;
            xs.rstart = xs.mw; xs.rcnt = 0; xs.rp = 0; xs.frz = 0;   /* the first pass plays and records */
        } else if (want == 2) { xs.rp = xs.mw - 1u; xs.frz = 1; }    /* backwards from the newest frame */
        else { xs.tpos = (xs.mw - 1u) << 16; xs.tspd = 65536; xs.frz = 0; }
    }
    for (i = 0; i < XF_N; i++) {
        int32_t d = xf_dry[2u * i], wl = 0, wr = 0, in, r, v, sd, dg, ds, ol, orr;
        xs.dsend = xf_ramp_to(xs.dsend, dsend_t, 128);
        xs.rsend = xf_ramp_to(xs.rsend, rsend_t, 128);
        xs.dlvl = xf_ramp_to(xs.dlvl, dlvl_t, 128);
        xs.rlvl = xf_ramp_to(xs.rlvl, rlvl_t, 128);
        xs.drl = xf_ramp_to(xs.drl, drl_t, 128);
        if (gm) {                        /* TRANCE GATE: the voice only; 1/16 = the first half of each 16th,
                                          * 1/8 = every other 16th; 64-sample slopes (no click) */
            int32_t on;
            if (++xs.gph >= sstep) { xs.gph = 0; xs.gpar ^= 1; }
            on = gm == 1 ? xs.gph < (sstep >> 1) : !xs.gpar;
            xs.gg = xf_ramp_to(xs.gg, on ? 32767 : 0, 512);
        } else xs.gg = xf_ramp_to(xs.gg, 32767, 512);
        v = xs.gg >= 32767 ? d : xf_mq(d, xs.gg);   /* (gate open: the voice exactly) */
        if ((i & 15u) == 0u && xs.duck) xs.duck = xf_mq(xs.duck, dk);
        dg = 32767 - xs.duck;
        sd = drums_on ? xf_sbuf[i] >> 2 : 0;
        if (xs.dalive) {
            r = xf_dl[(xs.dw - dlen_r) & (XF_DLY_N - 1u)];
            if (xstep) {
                int32_t r2 = xf_dl[(xs.dw - dlen_n) & (XF_DLY_N - 1u)];
                r += xf_mq(r2 - r, xfade);
                xfade += xstep;
            }
            xs.dlp += xf_mq(r - xs.dlp, tone);
            in = xf_half(xf_mq(v, xs.dsend)) + xf_mq(xf_mq(sd, ech), xs.dsend);
            xf_dl[xs.dw & (XF_DLY_N - 1u)] = (int16_t)xf_sat16(in + xf_mq(xs.dlp, fb));
            xs.dw++;
            x = xf_iabs(xs.dlp); if (x > dopk) dopk = x;
            r = xf_mq(xs.dlp, xs.dlvl) * 2;
            wl += r; wr += r;
        }
        if (xs.ralive) {
            int32_t al = 0, ar = 0;
            in = (xf_mq(v, xs.rsend) + xf_mq(xf_mq(sd, rvs), xs.rsend) * 2) / 4;
            COMB(RV_O0, RV_C0, 0, in, rfb, damp, al);
            COMB(RV_O1, RV_C1, 1, in, rfb, damp, al);
            COMB(RV_O2, RV_C2, 2, in, rfb, damp, al);
            COMB(RV_O3, RV_C3, 3, in, rfb, damp, al);
            COMB(RV_O4, RV_C4, 4, in, rfb, damp, ar);
            COMB(RV_O5, RV_C5, 5, in, rfb, damp, ar);
            COMB(RV_O6, RV_C6, 6, in, rfb, damp, ar);
            COMB(RV_O7, RV_C7, 7, in, rfb, damp, ar);
            APASS(RV_P0, RV_A0, 8, al);
            APASS(RV_P1, RV_A1, 9, al);
            APASS(RV_P2, RV_A2, 10, ar);
            APASS(RV_P3, RV_A3, 11, ar);
            al = xf_sat16(al); ar = xf_sat16(ar);
            x = xf_iabs(al); if (x > ropk) ropk = x;
            x = xf_iabs(ar); if (x > ropk) ropk = x;
            wl += xf_mq(al, xs.rlvl);
            wr += xf_mq(ar, xs.rlvl);
        }
        if (xs.duck) { v = xf_mq(v, dg); wl = (wl >> 1) * dg >> 14; wr = (wr >> 1) * dg >> 14; }   /* (|w| < 2^17) */
        ds = drums_on ? (xf_dbuf[i] >> 2) * xs.drl >> 13 : 0;
        ol = v + wl + ds; orr = v + wr + ds;
        if (ol > 49151) ol = 49151;
        if (ol < -49151) ol = -49151;
        if (orr > 49151) orr = 49151;
        if (orr < -49151) orr = -49151;
        if (!xs.frz) {                   /* the ring: the mix before the PERFORM effects */
            xf_mr[2u * (xs.mw & M)] = (int16_t)xf_sat16(ol);
            xf_mr[2u * (xs.mw & M) + 1u] = (int16_t)xf_sat16(orr);
            xs.mw++;
        }
        if (xs.pf) {                     /* REPEAT / REVERSE / TAPE STOP, faded in and out over 64 samples */
            int32_t fl = ol, fr = orr;
            uint32_t q;
            if (xs.pf == 1) {
                if (!xs.frz) { if (++xs.rcnt >= xs.rlen) { xs.frz = 1; xs.rp = 0; } }
                else {
                    int32_t e = (int32_t)xs.rp, e2 = xs.rlen - 1 - (int32_t)xs.rp;
                    q = (xs.rstart + xs.rp) & M;
                    fl = xf_mr[2u * q]; fr = xf_mr[2u * q + 1u];
                    if (e2 < e) e = e2;  /* 32-sample edges: the loop's seam does not click */
                    if (e < 32) { fl = fl * e >> 5; fr = fr * e >> 5; }
                    if ((int32_t)++xs.rp >= xs.rlen) xs.rp = 0;
                }
            } else if (xs.pf == 2) {
                q = xs.rp & M; xs.rp--;
                fl = xf_mr[2u * q]; fr = xf_mr[2u * q + 1u];
            } else {
                int32_t fr16 = (int32_t)((xs.tpos >> 1) & 32767u), a, b, g = xs.tspd >> 1;
                uint32_t q1;
                q = (xs.tpos >> 16) & M; q1 = (q + 1u) & M;
                a = xf_mr[2u * q]; b = xf_mr[2u * q1]; fl = a + ((b - a) * fr16 >> 15);
                a = xf_mr[2u * q + 1u]; b = xf_mr[2u * q1 + 1u]; fr = a + ((b - a) * fr16 >> 15);
                if (g > 32767) g = 32767;
                fl = xf_mq(fl, g); fr = xf_mq(fr, g);
                xs.tpos += (uint32_t)xs.tspd;
                xs.tspd = xs.tspd > 2 ? xs.tspd - 2 : 0;   /* 65536 -> 0 in 32768 samples (0.74 s) */
            }
            xs.fxg = xf_ramp_to(xs.fxg, want ? 32767 : 0, 512);
            ol = xf_xf(ol, fl, xs.fxg); orr = xf_xf(orr, fr, xs.fxg);
            if (!want && !xs.fxg) { xs.pf = 0; xs.frz = 0; }
        }
        if ((i & 15u) == 0u) {           /* the sweeps: every 16 samples */
            if (perf & 8) {
                if (!xs.lpon) { xs.lpon = 1; xs.lpc = XF_LPMAX; xs.f1l = xs.f2l = ol; xs.f1r = xs.f2r = orr; }
                xs.lpc -= xs.lpc >> 9; if (xs.lpc < XF_LPMIN) xs.lpc = XF_LPMIN;
            } else if (xs.lpon) { xs.lpc += xs.lpc >> 6; if (xs.lpc >= XF_LPMAX) xs.lpon = 0; }
            if (perf & 16) {
                if (!xs.hpon) { xs.hpon = 1; xs.hpc = XF_HPMIN; xs.h1l = xs.h2l = xs.h1r = xs.h2r = 0; }
                xs.hpc += xs.hpc >> 8; if (xs.hpc > XF_HPMAX) xs.hpc = XF_HPMAX;
            } else if (xs.hpon) { xs.hpc -= xs.hpc >> 6; if (xs.hpc <= XF_HPMIN) xs.hpon = 0; }
        }
        if (xs.lpon) {
            int32_t a = xs.lpc >> 9;
            xs.f1l += ((ol - xs.f1l) >> 1) * a >> 14; xs.f2l += ((xs.f1l - xs.f2l) >> 1) * a >> 14; ol = xs.f2l;
            xs.f1r += ((orr - xs.f1r) >> 1) * a >> 14; xs.f2r += ((xs.f1r - xs.f2r) >> 1) * a >> 14; orr = xs.f2r;
        }
        if (xs.hpon) {
            int32_t a = xs.hpc >> 9;
            xs.h1l += ((ol - xs.h1l) >> 1) * a >> 14; ol -= xs.h1l; xs.h2l += ((ol - xs.h2l) >> 1) * a >> 14; ol -= xs.h2l;
            xs.h1r += ((orr - xs.h1r) >> 1) * a >> 14; orr -= xs.h1r; xs.h2r += ((orr - xs.h2r) >> 1) * a >> 14; orr -= xs.h2r;
        }
        ol = xf_soft(ol); orr = xf_soft(orr);
        if (xs.xin < 32767) {            /* the step into MIX mode */
            ol = xf_xf(xs.hl, ol, xs.xin); orr = xf_xf(xs.hr, orr, xs.xin);
            xs.xin += 128; if (xs.xin > 32767) xs.xin = 32767;   /* (across one half) */
        }
        xf_wet[2u * i] = ol;
        xf_wet[2u * i + 1u] = orr;
        x = xf_iabs(ol); if (x > opk) opk = x;
        x = xf_iabs(orr); if (x > opk) opk = x;
    }
    xf_pk[0] = dopk; xf_pk[1] = ropk; xf_pk[2] = opk;
}

/* the job: xf_dry (one half of the finished voice) -> xf_wet. Core 1 (host: inline) */
XF_C1 void xf_c1_job(uint32_t arg)
{
    uint32_t t0 = fm1_c1_ticks(), i, k, req;
    int32_t dpk = 0, wpk = 0, x, dsend_t, rsend_t, dlvl_t, rlvl_t, drl_t, fb, tone, rfb, damp, allow, gt, g0, gs;
    int32_t xfade = 0, xstep = 0, drums_on, dopk = 0, ropk = 0, mixw;
    uint32_t dlen_r, dlen_n, dlen_new;
    (void)arg;
    req = xf_par.clear_req;
    if (req != xf_st.clear_ack && xs.fade == 0)
        xs.fade = 32767;                 /* a panic: this half fades out, then everything is emptied */
    dsend_t = xf_par.dly_on && !xs.fade ? 32767 : 0;
    rsend_t = xf_par.rev_on && !xs.fade ? 32767 : 0;
    dlvl_t = xf_par.dly_lvl; rlvl_t = xf_par.rev_lvl; drl_t = xf_par.drm_lvl;
    fb = xf_par.dly_fb; tone = xf_par.dly_tone; rfb = xf_par.rev_fb; damp = xf_par.rev_damp;
    k = xf_par.trw;                      /* the drum hits since the last half (at most the ring's 16) */
    if (k - xs.trr > XF_TRQ) xs.trr = k - XF_TRQ;
    for (; xs.trr != k; xs.trr++)
        if (!xs.fade) xf_c1_drum_hit(xf_par.trq[xs.trr & (XF_TRQ - 1u)]);
    if (dsend_t) xs.dalive = 1;
    if (rsend_t) xs.ralive = 1;
    drums_on = xf_drums_active();
    mixw = xf_par.mix_on;                /* (a panic fade does not leave MIX: it fades the whole mix) */
    if (!xs.dalive && !xs.ralive && !drums_on && !xs.fade && !xs.mix && !mixw) {
        if (!xf_st.idle) {
            for (i = 0; i < 2u * XF_N; i++) xf_wet[i] = 0;
            xs.dsend = xs.rsend = 0;
            xf_st.idle = 1;
        }
        xf_keep = -1;
        xf_st.jobs++;
        xf_st.busy_last = fm1_c1_ticks() - t0;
        return;
    }
    xf_st.idle = 0;
    if (drums_on) xf_c1_drums_render();
    for (i = 0; i < XF_N; i++) {         /* the dry peak (the voice is mono: L) */
        x = xf_iabs(xf_dry[2u * i]);
        if (x > dpk) dpk = x;
    }
    /* Drift-Full: into MIX mode at once (from the last sample heard: the dry plus the wet just played, crossfaded
     * over one half, 256 samples); out of it only when nothing needs it and the voice is quiet (no jump can be heard) */
    if (mixw && !xs.mix) {
        xs.mix = 1; xs.xin = 0; xs.gg = 32767; xs.gph = 0;   /* (a kick in this very half keeps its duck) */ xs.s16 = xf_par.s16;
        xs.hl = xf_sat16(xf_dry[2u * (XF_N - 1u)] + xf_wet[2u * (XF_N - 1u)]);
        xs.hr = xf_sat16(xf_dry[2u * (XF_N - 1u)] + xf_wet[2u * XF_N - 1u]);
    } else if (!mixw && xs.mix && dpk < 64 && !xs.pf && !xs.fxg && !xs.lpon && !xs.hpon) {
        xs.mix = 0; xs.frz = 0; xs.duck = 0;
    }
    /* the delay's spacing: a new one crossfades from the old read point over this half */
    dlen_n = (uint32_t)xf_par.dly_len;
    if (dlen_n < XF_DLY_MIN) dlen_n = XF_DLY_MIN;
    if (dlen_n > XF_DLY_MAX) dlen_n = XF_DLY_MAX;
    if (xs.dlen == 0u) xs.dlen = dlen_n;
    dlen_new = dlen_n;
    if (dlen_n != xs.dlen && xs.dalive) { xfade = 0; xstep = 32767 / (int32_t)XF_N; }
    else xs.dlen = dlen_n;
    dlen_r = xs.dlen - XF_N;             /* the wet comes back one half late: read that much nearer */
    dlen_n -= XF_N;
    if (xs.mix) {
        xf_c1_mix(dlen_r, dlen_n, xstep, fb, tone, rfb, damp, dsend_t, rsend_t, dlvl_t, rlvl_t, drl_t, drums_on);
        dopk = xf_pk[0]; ropk = xf_pk[1]; wpk = xf_pk[2];
    } else
    for (i = 0; i < XF_N; i++) {
        int32_t d = xf_dry[2u * i], wl = 0, wr = 0, in, r;
        xs.dsend = xf_ramp_to(xs.dsend, dsend_t, 128);
        xs.rsend = xf_ramp_to(xs.rsend, rsend_t, 128);
        xs.dlvl = xf_ramp_to(xs.dlvl, dlvl_t, 128);
        xs.rlvl = xf_ramp_to(xs.rlvl, rlvl_t, 128);
        xs.drl = xf_ramp_to(xs.drl, drl_t, 128);
        if (xs.dalive) {
            r = xf_dl[(xs.dw - dlen_r) & (XF_DLY_N - 1u)];
            if (xstep) {
                int32_t r2 = xf_dl[(xs.dw - dlen_n) & (XF_DLY_N - 1u)];
                r += xf_mq(r2 - r, xfade);
                xfade += xstep;
            }
            xs.dlp += xf_mq(r - xs.dlp, tone);
            in = xf_half(xf_mq(d, xs.dsend));   /* half level into the line: room for the feedback */
            xf_dl[xs.dw & (XF_DLY_N - 1u)] = (int16_t)xf_sat16(in + xf_mq(xs.dlp, fb));
            xs.dw++;
            x = xf_iabs(xs.dlp); if (x > dopk) dopk = x;
            r = xf_mq(xs.dlp, xs.dlvl) * 2;
            wl += r; wr += r;
        }
        if (xs.ralive) {
            int32_t al = 0, ar = 0;
            in = xf_mq(d, xs.rsend) / 4;   /* (toward zero: a shift and a sign fix, no divide) */
            COMB(RV_O0, RV_C0, 0, in, rfb, damp, al);
            COMB(RV_O1, RV_C1, 1, in, rfb, damp, al);
            COMB(RV_O2, RV_C2, 2, in, rfb, damp, al);
            COMB(RV_O3, RV_C3, 3, in, rfb, damp, al);
            COMB(RV_O4, RV_C4, 4, in, rfb, damp, ar);
            COMB(RV_O5, RV_C5, 5, in, rfb, damp, ar);
            COMB(RV_O6, RV_C6, 6, in, rfb, damp, ar);
            COMB(RV_O7, RV_C7, 7, in, rfb, damp, ar);
            APASS(RV_P0, RV_A0, 8, al);
            APASS(RV_P1, RV_A1, 9, al);
            APASS(RV_P2, RV_A2, 10, ar);
            APASS(RV_P3, RV_A3, 11, ar);
            al = xf_sat16(al); ar = xf_sat16(ar);
            x = xf_iabs(al); if (x > ropk) ropk = x;
            x = xf_iabs(ar); if (x > ropk) ropk = x;
            wl += xf_mq(al, xs.rlvl);
            wr += xf_mq(ar, xs.rlvl);
        }
        if (drums_on) {
            int32_t ds = (xf_dbuf[i] >> 2) * xs.drl >> 13;   /* (|sum| < 2^18: fits) */
            wl += ds; wr += ds;
        }
        xf_wet[2u * i] = wl;
        xf_wet[2u * i + 1u] = wr;
        x = xf_iabs(wl); if (x > wpk) wpk = x;
        x = xf_iabs(wr); if (x > wpk) wpk = x;
    }
    if (xstep) xs.dlen = dlen_new;
    /* headroom: the dry plus the wet must stay under 32600. The wet is added to the NEXT half's dry, so the dry
     * figure is a held peak (falls ~1.5 s) and never under 13000 (the 63 presets peak at 8641..12936, measured,
     * render_presets): a note that starts from silence still finds room */
    xs.dry_env -= xs.dry_env >> 8;
    if (dpk > xs.dry_env) xs.dry_env = dpk;
    allow = 32600 - (xs.dry_env > 13000 ? xs.dry_env : 13000);   /* >= 2600: the voice clamps at 30000 */
    gt = 32768;
    if (wpk > allow) gt = (int32_t)(((uint32_t)allow << 15) / (uint32_t)wpk);   /* (a 32-bit divide) */
    g0 = xs.g ? xs.g : 32768;
    if (gt < g0) { xs.g = gt; g0 = gt; } /* down: at once, from the half's first sample */
    else xs.g = g0 + ((gt - g0) >> 4);   /* up: slowly, ramped across the half */
    if (xs.mix) { xs.g = 32768; g0 = 32768; }   /* Drift-Full: MIX mode ends in its own soft clip instead */
    if (xs.g < 32768 || g0 < 32768) {
        if (xs.g < 32768) xf_st.limited++;
        gs = (xs.g - g0) / (int32_t)XF_N;
        for (i = 0; i < XF_N; i++) {
            g0 += gs;
            if (i == XF_N - 1u) g0 = xs.g;
            xf_wet[2u * i] = (xf_wet[2u * i] >> 2) * g0 >> 13;      /* (|wet| < 2^17: fits) */
            xf_wet[2u * i + 1u] = (xf_wet[2u * i + 1u] >> 2) * g0 >> 13;
        }
    }
    if (xs.fade) {                       /* panic: 32767 -> 0 across this half, then all lines empty */
        for (i = 0; i < XF_N; i++) {
            int32_t f = 32767 - (int32_t)(i * 128u);
            if (f < 0) f = 0;
            xf_wet[2u * i] = xf_mq(xf_wet[2u * i] >> 2, f) * 4;
            xf_wet[2u * i + 1u] = xf_mq(xf_wet[2u * i + 1u] >> 2, f) * 4;
        }
        xf_dly_clear(); xf_rev_clear(); xf_drums_clear();
        xs.dsend = xs.rsend = 0;
        xs.fade = 0;
        xf_st.clear_ack = req;
    }
    /* the tails: an effect switched off and quiet for 8 halves is emptied and skipped */
    if (xs.dalive && !dsend_t && xs.dsend == 0) {
        if (dopk < 4) { if (++xs.dquiet >= 8u) xf_dly_clear(); } else xs.dquiet = 0;
    } else xs.dquiet = 0;
    if (xs.ralive && !rsend_t && xs.rsend == 0) {
        if (ropk < 4) { if (++xs.rquiet >= 8u) xf_rev_clear(); } else xs.rquiet = 0;
    } else xs.rquiet = 0;
    xf_st.dry_peak = dpk;
    xf_st.wet_peak = wpk;
    xf_keep = xs.mix ? 0 : -1;           /* (for the wet just made: read by the interrupt after this job ends) */
    xf_st.jobs++;
    xf_st.busy_last = fm1_c1_ticks() - t0;
    if (xf_st.busy_last > xf_st.busy_max) xf_st.busy_max = xf_st.busy_last;
}

/* the start-up check: core 1 runs it from RAM, as it will run the job */
static volatile uint32_t xf_test_out XT_KEEP;
XF_C1 uint32_t xf_c1_lcg(uint32_t x)
{
    uint32_t i;
    for (i = 0; i < 64u; i++) x = x * 1664525u + 1013904223u;
    return x;
}
XF_C1 void xf_c1_selftest(uint32_t arg) { xf_test_out = xf_c1_lcg(arg); }
uint32_t xf_lcg_ref(uint32_t x)       /* the same, on core 0 from the flash (a direct call cannot reach RAM) */
{
    uint32_t i;
    for (i = 0; i < 64u; i++) x = x * 1664525u + 1013904223u;
    return x;
}

/* ---- core 0, the audio interrupt (from app_audio, after polivoks_render): no waiting. Core 1 finished the
 * last job: add its wet to this half, hand it this half's dry. Not finished: this half stays dry. */
static inline void xf_isr_mix(int32_t *o)
{
    uint32_t j;
    int32_t m;
    if (!xf_run)
        return;
    if (!fm1_cpu1_idle()) {
        xf_late++;
        xf_late_run++;
        return;
    }
    xf_late_run = 0;
    xf_mixed++;
    m = xf_keep;                         /* Drift-Full: -1 adds the wet (as before); 0 = the wet is the whole mix */
    for (j = 0; j < 2u * XF_N; j += 2u) {
        int32_t d = o[j], k = d & m;
        xf_dry[j] = d;
        o[j] = k + xf_wet[j];
        o[j + 1u] = k + xf_wet[j + 1u];
    }
    fm1_cpu1_run(xf_c1_job, 0);
}

/* ---- core 0, the main program: the second core's state for the screen and the tests */
enum { XF_C2_NONE = 0, XF_C2_NOSTART, XF_C2_FAILED, XF_C2_OK, XF_C2_HELD };
static uint8_t xf_c2 XT_KEEP;

void xf_init(void)
{
    xs.rng = 0x2545F491u;
    xs.g = 32768;
    xf_keep = -1;
    xf_drums_clear();
    xf_st.magic = 0x32434658u;           /* "XFC2" */
    xf_st.idle = 1;
}

#ifndef DINGHY_HOST
/* at power-on, before the audio and the timers (FORM 1.3 cpu1_boot; X0X: started later, core 1 never reached
 * its entry). The PC limits are open for the start (it boots through the chip's ROM) and armed again after. */
void xf_core2_boot(void)
{
    xf_init();
    fm1_guard_pc_open();
    if (fm1_cpu1_start() != 0) {
        fm1_guard_enable(FM1_GUARD_PC);
        xf_c2 = XF_C2_NOSTART;
        return;
    }
    fm1_guard_enable(FM1_GUARD_PC);
    xf_test_out = 0;
    if (!fm1_cpu1_run(xf_c1_selftest, 20261115u) || fm1_cpu1_wait() != 0 || xf_test_out != xf_lcg_ref(20261115u)) {
        fm1_cpu1_hold();
        xf_c2 = XF_C2_FAILED;
        return;
    }
    xf_c2 = XF_C2_OK;
}
#else
void xf_core2_boot(void) { xf_init(); xf_c2 = XF_C2_OK; }
#endif

/* main program, every pass: a core 1 that stopped answering is held; the effects go dry (one core) */
void xf_watch(void)
{
    if (xf_c2 == XF_C2_OK && xf_late_run >= XF_LATE_HOLD) {
        xf_run = 0;
        fm1_cpu1_hold();
        xf_c2 = XF_C2_HELD;
    }
}

/* Core2-FxSeq: the second core's duties in the main program: hold it if it stopped answering, run the
 * hand-off while an added sound feature is on or its tail / a panic fade is still going */
void xf_service(void)
{
    xf_watch();
    xtra_set_c2(xf_c2, xf_late);
    if (xf_c2 != XF_C2_OK)
        return;
    if (xtra_fx_wanted() || xf_par.clear_req != xf_st.clear_ack)
        xf_run = 1;
    else if (xf_run && xf_st.idle)
        xf_run = 0;
}
