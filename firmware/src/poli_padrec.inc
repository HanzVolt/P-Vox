/* SPDX-License-Identifier: GPL-3.0-only */
/* SMK-37 Elite pad-record (22 Nov 2026). Included from poli_xtra.c under POLI_PADREC.
 * 16 RGB pads write the existing 17-lane drum pattern (ghost has no pad) into a working
 * copy, then into one of 8 flash-backed user slots. No engine code.
 * Parity (23 Nov 2026): the same file on the FM-1. Its pads are DRUM STEP's 16 white keys (poli_xtra.c
 * xt_padrec_keys); the FM-1 has no LEFT / RIGHT, so K4 moves the STEP cursor there. Same 408-byte blob, blob 0. */

#define PR_MAGIC 0x52445650u                     /* "PVDR" */
#define PR_VER   1u
#define PR_HOLD_MS 500u                          /* hold a pad this long to erase its lane */

static const uint8_t PR_LANE[16] = {
    L_RIM, L_COWB, L_CLAVE, L_LC, L_MC, L_HC, L_RIDE, L_CRASH,   /* top row, pads 0..7 */
    L_KICK, L_SNARE, L_CLAP, L_CH, L_OH, L_LT, L_MT, L_HT        /* bottom row, pads 8..15 */
};
static const int16_t PR_LO[4] = { 0, 0, 0, 0 };
#if BOARD_SMK37
static const int16_t PR_HI[4] = { 7, 1, 1, 0 };  /* SLOT 1-8, MODE LIVE/STEP, QUANT 16TH/OFF, unused */
#else
static const int16_t PR_HI[4] = { 7, 1, 1, 15 }; /* FM-1: + K4 STEP 1-16, the STEP cursor (the Elite's LEFT / RIGHT) */
#endif
static const int16_t PR_DEF[4] = { 0, 0, 1, 0 };

enum { PR_IDLE = 0, PR_COUNTIN, PR_WAITBAR, PR_REC };

static struct {
    uint8_t slot, mode, quant;
    uint8_t phase;                               /* PR_* */
    uint8_t cin;                                 /* sixteenths left in the count-in */
    uint8_t step, dirty, can_undo;
    uint8_t used[XT_PADREC_SLOTS], style[XT_PADREC_SLOTS];
    uint8_t down[16], erased[16];
    uint32_t down_ms[16];
    uint32_t step_ms, now;
    uint16_t sounded[17];
    xt_pat_t work, undo, bank[XT_PADREC_SLOTS];
} pr XT_KEEP PV_FEAT_BSS;

void xtra_transport(int run);                    /* defined later in poli_xtra.c */
static void xt_load_wp(void);

int xtra_padrec_armed(void) { return pr.phase != PR_IDLE; }
int xtra_padrec_countin(void) { return pr.phase == PR_COUNTIN ? pr.cin : 0; }
int xtra_padrec_waiting(void) { return pr.phase == PR_WAITBAR; }
int xtra_padrec_mode(void) { return pr.mode; }
int xtra_padrec_quant(void) { return pr.quant; }
int xtra_padrec_slot(void) { return pr.slot; }
int xtra_padrec_step(void) { return pr.step; }
int xtra_padrec_dirty(void) { return pr.dirty; }
int xtra_padrec_can_undo(void) { return pr.can_undo; }
int xtra_padrec_slot_used(int slot)
{
    return (slot < 0 || slot >= (int)XT_PADREC_SLOTS) ? 0 : pr.used[slot];
}
int xtra_padrec_lane_of_pad(int pad)
{
    return (pad < 0 || pad > 15) ? -1 : (int)PR_LANE[pad];
}
int xtra_padrec_pad_of_lane(int lane)
{
    int p;
    for (p = 0; p < 16; p++) if ((int)PR_LANE[p] == lane) return p;
    return -1;
}
int xtra_padrec_work_lane(int l) { return (l < 0 || l > 16) ? 0 : pr.work.l[l]; }
int xtra_padrec_lo(int i) { return (i < 0 || i > 3) ? 0 : PR_LO[i]; }
int xtra_padrec_hi(int i) { return (i < 0 || i > 3) ? 0 : PR_HI[i]; }
int xtra_padrec_get(int i)
{
    if (i == 0) return pr.slot;
    if (i == 1) return pr.mode;
    if (i == 2) return pr.quant;
#if !BOARD_SMK37
    if (i == 3) return pr.step;                  /* FM-1: K4 shows the cursor (LIVE: the playing step) */
#endif
    return 0;
}

static void pr_snap(void)
{
    pr.undo = pr.work;
    pr.can_undo = 1;
    xt.gen++;
}

/* Drum-Step (22 Nov 2026): a hit PAD REC adds or removes is 100 % (the chance is per position, xt.chance); a cleared
 * or loaded groove starts at 100 % everywhere. The chance is RAM only: SAVE stores the hits, not the chance. */
static void pr_cell_full(int l, int st) { if (l >= 0 && l <= 16 && st >= 0 && st <= 15) xt.chance[l] &= ~(3u << (2 * st)); }
/* DRUM STEP edited USER: the working groove follows, so PAD REC (its SAVE, its next take) starts from the edit */
void xtra_padrec_follow_user(void)
{
    if (pr.phase != PR_IDLE) return;             /* (a take keeps its own groove; it is written into USER as it goes) */
    if (!pr.can_undo) pr_snap();
    pr.work = xt.user;
    pr.dirty = 1;
}
static void pr_commit_user(void)
{
    xt.user = pr.work;
    xt.k[XW_DRUM][0] = XT_PAT_USER;
    xt.wp_gen = 0;                       /* PLAY GEN: a PAD REC commit / load replaces the temp beat */
    if (!xt.run) xt_load_wp();
    else if (xt.wp_src == XT_PAT_USER) xt.wp = pr.work;
    xt.gen++;
}

int xtra_padrec_skip_lane(int l, int st)
{
    if (pr.phase != PR_REC || l < 0 || l > 16 || st < 0 || st > 15) return 0;
    return (pr.sounded[l] >> st) & 1u;
}

static void pr_fire_lane(int l)
{
    int f = xt_flavour();
    const uint8_t *map = XT_KF_LANE[f];
    uint32_t in, pm = 4096u, vel;
    if (l < 0 || l > 16 || xt.c2 != 3) return;
    in = map[l];
    vel = XT_LANE_VEL[l];
    if (l == L_KICK) { if (pr.work.lng && f == KF_808) in = DI_BDL8; pm = XT_KF_KPM[f]; }
    xt_hit(in, vel, pm);
}

static void pr_add_hit(int l, int st)
{
    uint16_t b;
    if (l < 0 || l > 16 || st < 0 || st > 15) return;
    b = (uint16_t)(1u << st);
    pr.work.l[l] |= b;
    pr.sounded[l] |= b;
    pr.dirty = 1;
    pr_cell_full(l, st);
    pr_fire_lane(l);
    pr_commit_user();
}

void xtra_padrec_hit(int pad)
{
    int l = xtra_padrec_lane_of_pad(pad), st;
    uint32_t sixteenth, dt;
    if (l < 0) return;
    if (pr.mode) {                               /* STEP: toggle the cursor step */
        uint16_t b = (uint16_t)(1u << (pr.step & 15));
        if (!pr.can_undo) pr_snap();
        pr_cell_full(l, pr.step & 15);
        if (pr.work.l[l] & b) pr.work.l[l] = (uint16_t)(pr.work.l[l] & ~b);
        else {
            pr.work.l[l] |= b;
            pr_fire_lane(l);
        }
        pr.dirty = 1;
        pr_commit_user();
        xt.gen++;
        return;
    }
    if (pr.phase != PR_REC) {                    /* preview while idle / counting in */
        pr_fire_lane(l);
        return;
    }
    st = (int)pr.step;
    sixteenth = (xtra_tick_q8() * 6u) >> 8;
    dt = (pr.step_ms && pr.now) ? (uint32_t)(pr.now - pr.step_ms) : 0u;
    if (pr.quant && sixteenth && dt * 2u >= sixteenth) st = (st + 1) & 15;
    if (!pr.can_undo) pr_snap();
    pr_add_hit(l, st);
}

void xtra_padrec_pad(int pad, int vel, uint32_t now)
{
    if (pad < 0 || pad > 15) return;
    pr.now = now ? now : 1u;
    if (vel) {
        pr.down[pad] = 1;
        pr.erased[pad] = 0;
        pr.down_ms[pad] = now ? now : 1u;
        xtra_padrec_hit(pad);
    } else {
        pr.down[pad] = 0;
        pr.erased[pad] = 0;
    }
}

static void pr_hold_tick(uint32_t now)
{
    int p;
    for (p = 0; p < 16; p++) {
        int l;
        if (!pr.down[p] || pr.erased[p] || pr.phase == PR_COUNTIN || pr.phase == PR_WAITBAR) continue;
        if ((uint32_t)(now - pr.down_ms[p]) < PR_HOLD_MS) continue;
        l = xtra_padrec_lane_of_pad(p);
        if (l < 0) continue;
        if (!pr.can_undo) pr_snap();
        pr.work.l[l] = 0;
        xt.chance[l] = 0;
        pr.erased[p] = 1;
        pr.dirty = 1;
        pr_commit_user();
        xt.gen++;
    }
}

void xtra_padrec_sixteenth(uint32_t now, uint32_t st, int running)
{
    pr.now = now ? now : 1u;
    pr.step_ms = pr.now;
    pr_hold_tick(now);
    if (pr.phase == PR_COUNTIN) {
        if ((pr.cin % 4u) == 0u) pr_fire_lane(L_RIM);   /* beat clicks, not recorded */
        if (pr.cin) pr.cin--;
        if (!pr.cin) {
            memset(pr.sounded, 0, sizeof pr.sounded);
            pr.phase = PR_REC;
            pr.step = 0;
            if (!xt.run) xtra_transport(1);
            xt.gen++;
        }
        return;
    }
    if (pr.phase == PR_WAITBAR) {
        pr.step = (uint8_t)(st & 15u);
        if (running && (st & 15u) == 0u) {
            memset(pr.sounded, 0, sizeof pr.sounded);
            pr.phase = PR_REC;
            xt.gen++;
        }
        return;
    }
    if (pr.phase == PR_REC) {
        pr.step = (uint8_t)(st & 15u);
        if ((st & 15u) == 0u) memset(pr.sounded, 0, sizeof pr.sounded);
        return;
    }
    if (pr.mode) return;                         /* STEP: the cursor is moved by LEFT/RIGHT (FM-1: K4), not the clock */
    if (running) pr.step = (uint8_t)(st & 15u);
}

int xtra_padrec_rec(uint32_t now)
{
    (void)now;
    if (pr.phase != PR_IDLE) {
        pr.phase = PR_IDLE;
        pr.cin = 0;
        xt.gen++;
        return 1;
    }
    if (pr.mode) return 1;                       /* STEP: REC does not arm a take; editing is already live */
    if (xt.c2 != 3) return 0;
    pr_snap();
    memset(pr.sounded, 0, sizeof pr.sounded);
    xt.on[XW_DRUM] = 1;
    pr_commit_user();
    if (xt.run) pr.phase = PR_WAITBAR;
    else {
        pr.phase = PR_COUNTIN;
        pr.cin = 16;
    }
    xt.gen++;
    return 1;
}

void xtra_padrec_undo(void)
{
    if (!pr.can_undo) return;
    pr.work = pr.undo;
    pr.can_undo = 0;
    pr.dirty = 1;
    pr_commit_user();
    xt.gen++;
}

void xtra_padrec_clear(void)
{
    pr_snap();
    memset(&pr.work, 0, sizeof pr.work);
    memset(xt.chance, 0, sizeof xt.chance);
    pr.dirty = 1;
    pr_commit_user();
    xt.gen++;
}

int xtra_padrec_save(void)
{
    int s = pr.slot;
    if (s < 0 || s >= (int)XT_PADREC_SLOTS) return 0;
    pr.bank[s] = pr.work;
    pr.used[s] = 1;
    pr.style[s] = xt.user_style;
    pr.dirty = 0;
    pr_commit_user();
    xt.gen++;
    return 1;
}

int xtra_padrec_load(int slot)
{
    if (slot < 0 || slot >= (int)XT_PADREC_SLOTS) return 0;
    pr.slot = (uint8_t)slot;
    if (!pr.used[slot]) { xt.gen++; return 0; }
    if (pr.phase == PR_IDLE) {
        pr_snap();
        pr.work = pr.bank[slot];
        memset(xt.chance, 0, sizeof xt.chance);
        xt.user_style = pr.style[slot];
        pr.dirty = 0;
        pr_commit_user();
    }
    xt.gen++;
    return 1;
}

void xtra_padrec_knob(int i, int steps)
{
    int v, lo, hi;
    if (i < 0 || i > 3 || steps == 0 || PR_HI[i] <= PR_LO[i]) return;
    lo = PR_LO[i];
    hi = PR_HI[i];
    v = xtra_padrec_get(i) + steps;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    if (i == 0) {
        pr.slot = (uint8_t)v;
        if (pr.phase == PR_IDLE) (void)xtra_padrec_load(v);
    } else if (i == 1) pr.mode = (uint8_t)v;
    else if (i == 2) pr.quant = (uint8_t)v;
#if !BOARD_SMK37
    else if (i == 3) { if (pr.mode) pr.step = (uint8_t)v; }   /* FM-1: K4 = the STEP cursor (LIVE: no effect) */
#endif
    xt.gen++;
}

void xtra_padrec_step_move(int dir)
{
    int s;
    if (!pr.mode || !dir) return;
    s = ((int)pr.step + dir) & 15;
    pr.step = (uint8_t)s;
    xt.gen++;
}

static void pr_put_u16(uint8_t *p, uint32_t *o, uint16_t v)
{
    p[(*o)++] = (uint8_t)v;
    p[(*o)++] = (uint8_t)(v >> 8);
}
static void pr_put_u32(uint8_t *p, uint32_t *o, uint32_t v)
{
    p[(*o)++] = (uint8_t)v;
    p[(*o)++] = (uint8_t)(v >> 8);
    p[(*o)++] = (uint8_t)(v >> 16);
    p[(*o)++] = (uint8_t)(v >> 24);
}
static uint16_t pr_get_u16(const uint8_t *p, uint32_t *o)
{
    uint16_t v = (uint16_t)(p[*o] | (p[*o + 1u] << 8));
    *o += 2u;
    return v;
}
static uint32_t pr_get_u32(const uint8_t *p, uint32_t *o)
{
    uint32_t v = (uint32_t)p[*o] | (uint32_t)p[*o + 1u] << 8 | (uint32_t)p[*o + 2u] << 16 | (uint32_t)p[*o + 3u] << 24;
    *o += 4u;
    return v;
}

int xtra_padrec_export(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *)p;
    uint32_t o = 0, s, l;
    if (!b || n < XT_PADREC_BLOB) return -1;
    memset(b, 0, n);
    pr_put_u32(b, &o, PR_MAGIC);
    pr_put_u16(b, &o, (uint16_t)PR_VER);
    pr_put_u16(b, &o, (uint16_t)XT_PADREC_SLOTS);
    for (s = 0; s < XT_PADREC_SLOTS; s++) b[o++] = pr.used[s];
    for (s = 0; s < XT_PADREC_SLOTS; s++) b[o++] = pr.style[s];
    for (s = 0; s < XT_PADREC_SLOTS; s++) {
        for (l = 0; l < 17u; l++) pr_put_u16(b, &o, pr.bank[s].l[l]);
        b[o++] = pr.bank[s].kit;
        b[o++] = pr.bank[s].lng;
        pr_put_u32(b, &o, pr.bank[s].roll);
        pr_put_u32(b, &o, pr.bank[s].bell[0]);
        pr_put_u32(b, &o, pr.bank[s].bell[1]);
    }
    return (o == XT_PADREC_BLOB) ? (int)o : -1;
}

int xtra_padrec_import(const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t o = 0, s, l;
    if (!b || n < XT_PADREC_BLOB) return -1;
    if (pr_get_u32(b, &o) != PR_MAGIC) return -1;
    if (pr_get_u16(b, &o) != PR_VER) return -1;
    if (pr_get_u16(b, &o) != XT_PADREC_SLOTS) return -1;
    for (s = 0; s < XT_PADREC_SLOTS; s++) pr.used[s] = b[o++];
    for (s = 0; s < XT_PADREC_SLOTS; s++) pr.style[s] = b[o++];
    for (s = 0; s < XT_PADREC_SLOTS; s++) {
        memset(&pr.bank[s], 0, sizeof pr.bank[s]);
        for (l = 0; l < 17u; l++) pr.bank[s].l[l] = pr_get_u16(b, &o);
        pr.bank[s].kit = b[o++];
        pr.bank[s].lng = b[o++];
        pr.bank[s].roll = pr_get_u32(b, &o);
        pr.bank[s].bell[0] = pr_get_u32(b, &o);
        pr.bank[s].bell[1] = pr_get_u32(b, &o);
    }
    return (o == XT_PADREC_BLOB) ? 0 : -1;
}

void xtra_padrec_poll(uint32_t now)
{
    pr.now = now ? now : 1u;
    pr_hold_tick(pr.now);
}

void xtra_padrec_init(void)
{
    memset(&pr, 0, sizeof pr);
    pr.quant = 1;
    pr.work = XT_PAT[0];
}
