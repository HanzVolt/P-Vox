/* SPDX-License-Identifier: GPL-3.0-only */
/* Polivoks preset store. Include after poli_nav.c.
 *
 * LOCKED decisions (Hans, 2026-11-11): PRESETS knob + SAVE button, windows
 * unchanged (1); all 63 slots writable, factory sounds kept in firmware only
 * for the reset (2, 6); free-form 12-character names (3); SAVE writes with no
 * warning box (4); boot into the last used slot (5); one backup of all 63
 * through the SAVE flow (7); no MIDI program change (8); sound parameters
 * only, language stays global (9); compare edit vs stored before saving (10);
 * PRESETS twists, a press loads (11); asterisk when edited (12).
 *
 * The FM-1 panel map in platform.c has no PRESETS push switch, so "press to
 * load" is the SEL button while the preset bar is up (PV_LOAD_IS_SEL).
 *
 * Storage (PV_STORE defined by the includer, volt.c): the bank is an A/B pair
 * of 4 KiB sectors (blob 6 = 0xFA000, blob 7 = 0xFB000). Each save goes to the
 * older copy, header last; the copy with the higher sequence number wins on
 * boot. The backup is one sector (blob 5 = 0xF9000). The last used slot is in
 * the settings sector (0xFC000). Every erase stops the sound for ~50 ms
 * (platform.c). UNTESTED ON HARDWARE: only a host flash model has run it.
 * Without PV_STORE (host harnesses) everything stays in RAM.
 *
 * A valid 50-slot bank (nrec == 50, payload 3212 B) from earlier builds is
 * accepted: slots 1-50 are kept (laws 2/3/4 converted as before), slots 51-63
 * are filled from factory, and the expanded 63-slot bank is written to the
 * other A/B copy (header last). A failed write leaves the old 50-slot copy
 * intact. Older 50-slot firmwares reject a 63-slot blob: the store header
 * length is 4044, they ask for 3212, the load fails, and they boot factory
 * 1-50 in RAM without writing. User data stays in flash until something
 * on that older build saves, which then writes a 50-slot bank and can
 * overwrite one A/B copy. */

#include "poli_preset.h"
#if POLI_ARP
#include "poli_arp.h"
#endif
#include "poli_name_font.inc"

#define PV_LAYOUT 1u
#define PV_LAW 5u                  /* audible-range envelope remap + octave cutoff (2026-11-12) */
#define PV_LAW_RC 4u               /* curved V3, linear cutoff (EnvRC / Fix1): converted on load */
#define PV_LAW_B 3u                /* step B records (EnvB-Oct3 build): converted on load */
#define PV_LAW_A 2u                /* step A records (presets / brief-mute builds): converted on load */
#define PV_REC 64u
#define PV_STATE_OK 0xA5u
#define PV_BANK_MAGIC 0x534B5650u  /* "PVKS" */
#define PV_BACK_MAGIC 0x424B5650u  /* "PVKB" */
#define PV_BAR_MS 4000u            /* the bar closes this long after the last turn */
#define PV_LAST_MS 3000u           /* the last used slot is written this long after a load, in silence */
enum { PV_ST_A = 0, PV_ST_B = 1, PV_ST_BACKUP = 2, PV_ST_SETTINGS = 3 };

typedef struct { uint8_t name[PV_NAME]; uint8_t sw; uint8_t k[31]; } pv_fact_t;
#include "poli_presets_factory.inc"
_Static_assert(PV_NFACT == PV_NSLOT, "63 factory presets");

/* record, 64 B: state, layout, law, nknob, name[12], sw, sw2, k[31], x[3], reserved[8], crc32 (LE) of 0..59.
   x[3] (bytes 49..51, LFOTail-Sync-Vel): LFO restart 0/1, LFO sync 0..9, velocity 0..127. Older builds wrote
   0 there (reserved, zero-filled, inside the CRC), so their records load as OFF / FREE / 0 with no format
   change: same layout byte, same nknob 31, same law. An unknown x value loads as its default. Older
   builds read these records normally and ignore x; a save on an older build writes x back to 0. */
typedef struct { uint8_t b[PV_REC]; } pv_rec_t;
typedef struct {
    uint32_t magic;
    uint16_t layout, nrec;
    uint32_t seq;
    pv_rec_t rec[PV_NSLOT];
} pv_bank_t;
_Static_assert(sizeof(pv_bank_t) == 12u + PV_NSLOT * PV_REC, "bank layout");
_Static_assert(sizeof(pv_bank_t) == 4044u, "63-slot payload is 12 + 63*64");
_Static_assert(sizeof(pv_bank_t) <= 4084u, "bank fits in a 4 KiB blob (SECTOR_MAX)");
_Static_assert(12u + (uint32_t)PV_NSLOT_V1 * PV_REC == 3212u, "50-slot payload");
/* 8-byte record, same size as before: ver, last-used slot, receive
 * channel 1-16 (0 or out of range -> 1), key-track mode in rsv[0]
 * (0 = old record -> default 62%; 1 = OFF; 2 = 62%; 3 = 100%), then
 * reserved. Old records have rsv all zero.
 * Drums12 (Hans, 21 Nov 2026): rsv[1] = drum PATTERN + 1 (0 = none: pattern 1), rsv[2] = its knob layout
 * (XT_PAT_LAYOUT 2 = 1-8, USER, 4 packs of 12; 1 = the Nov 17 0..28 knob, remapped). Older builds ignore both
 * and write them back as 0 (pattern 1 on the next boot here). rsv[3..4] still reserved. */
typedef struct { uint8_t ver, last, midi_ch, rsv[5]; } pv_settings_t;
_Static_assert(sizeof(pv_settings_t) == 8, "settings record stays 8 bytes");

/* Brief mute around a sound change (Hans, 2026-11-12). PV_MUTE (volt.c on the
 * FM-1) fades the output to silence in the output stage before the new
 * values reach the engine, and back up after (out_mute.h). The engine itself
 * is unchanged. Without PV_MUTE (host harnesses) a load is immediate, as before. */
#ifndef PV_MUTE
static void pv_mute_on(void) {}
static void pv_mute_off(void) {}
#endif

#ifndef PV_STORE
static int pv_store_load(int which, void *p, uint32_t n) { (void)which; (void)p; (void)n; return -1; }
static int pv_store_save(int which, const void *p, uint32_t n) { (void)which; (void)p; (void)n; return -1; }
#endif

static pv_bank_t pv_bank, pv_tmp;
static int pv_newest = -1;              /* PV_ST_A / PV_ST_B holding pv_bank.seq, -1 none */
static int pv_mode, pv_cur, pv_target, pv_cmp, pv_cursor, pv_action, pv_msg;
static uint32_t pv_msg_gen, pv_turn_ms, pv_load_ms;
static uint8_t pv_last_pending, pv_last_stored = 0xFFu, pv_ch_stored = 1, pv_ch_seen = 1;
static uint8_t pv_kt_stored = 2, pv_kt_seen = 2;   /* settings mode 1/2/3; 2 = 62% */
static uint8_t pv_pat_stored, pv_pat_seen;         /* Drums12: the PATTERN the settings record holds / last seen */
static uint8_t pv_name[PV_NAME];
static uint8_t pv_edit_k[31], pv_edit_sw;   /* compare: the edit, while the stored one plays */
static uint8_t pv_edit_x[NAV_NX];

static uint32_t pv_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

static void pv_rec_make_x(pv_rec_t *r, const uint8_t name[PV_NAME], uint8_t sw, const uint8_t k[31], const uint8_t *x)
{
    uint32_t c;
    int i;
    uint8_t xs[NAV_NX];
    for (i = 0; i < NAV_NX; i++) xs[i] = x ? x[i] : 0u;   /* x may point into r itself */
    memset(r, 0, sizeof *r);
    r->b[0] = PV_STATE_OK;
    r->b[1] = PV_LAYOUT;
    r->b[2] = PV_LAW;
    r->b[3] = 31u;
    for (i = 0; i < PV_NAME; i++) r->b[4 + i] = name[i] < TN_N ? name[i] : 0u;
    r->b[16] = (uint8_t)(sw & 0xBFu);        /* bit 6 (key hold) is never stored */
    for (i = 0; i < 31; i++) {
        int m = poli_nav_patch_max(i);
        r->b[18 + i] = k[i] > m ? (uint8_t)m : k[i];
    }
    for (i = 0; i < NAV_NX; i++) r->b[49 + i] = xs[i] > poli_nav_patch_max_x(i) ? 0u : xs[i];
    c = pv_crc32(r->b, 60u);
    r->b[60] = (uint8_t)c; r->b[61] = (uint8_t)(c >> 8); r->b[62] = (uint8_t)(c >> 16); r->b[63] = (uint8_t)(c >> 24);
}
/* the old call: extras OFF / FREE / 0 */
static void pv_rec_make(pv_rec_t *r, const uint8_t name[PV_NAME], uint8_t sw, const uint8_t k[31])
{
    pv_rec_make_x(r, name, sw, k, 0);
}

static int pv_rec_ok(const pv_rec_t *r)
{
    uint32_t c = pv_crc32(r->b, 60u);
    int i;
    if (r->b[0] != PV_STATE_OK || r->b[1] != PV_LAYOUT || r->b[3] != 31u) return 0;
    if (r->b[60] != (uint8_t)c || r->b[61] != (uint8_t)(c >> 8) || r->b[62] != (uint8_t)(c >> 16) ||
        r->b[63] != (uint8_t)(c >> 24)) return 0;
    for (i = 0; i < PV_NAME; i++) if (r->b[4 + i] >= TN_N) return 0;
    for (i = 0; i < 31; i++) if (r->b[18 + i] > poli_nav_patch_max(i)) return 0;
    /* law byte: 5 = this build; 4 = curved V3 / linear cutoff (EnvRC, Fix1);
       3 = step B and 2 = step A (earlier builds). 2/3/4 are converted by
       pv_bank_take. Any other law is not read as a sound: the slot falls
       back to its factory preset. */
    if (r->b[2] != PV_LAW && r->b[2] != PV_LAW_RC && r->b[2] != PV_LAW_B && r->b[2] != PV_LAW_A) return 0;
    return 1;
}

/* Knob indices (SLOT_CUT..) of attack, decay and release, both envelopes. */
static const uint8_t PV_ENV_IDX[6] = { 4, 5, 7, 8, 9, 11 };
_Static_assert(SLOT_ATK_F - SLOT_CUT == 4 && SLOT_DEC_F - SLOT_CUT == 5 && SLOT_REL_F - SLOT_CUT == 7 &&
               SLOT_ATK_A - SLOT_CUT == 8 && SLOT_DEC_A - SLOT_CUT == 9 && SLOT_REL_A - SLOT_CUT == 11,
               "envelope knob indices");

/* Old straight-line rate (Q15 levels per sample) -> curved position with the
   closest end time (poli_env_rc.inc, tools/gen_poli_env_rc.py conv()):
   attack  0 -> 26000 (both laws; curved attack law NA(q), ENV_MQ8_A);
   decay   26000 -> this record's sustain (-60 dB of the distance for the
           curve); a sustain within 1 % of the peak is timed as a full sweep;
   release this record's sustain -> 0; a sustain below 1 % of the peak is
           timed from the peak.
   Nearest in log time: q = number of midpoints M[q] with M[q]*rate < D*F*128,
   i.e. N(q) closest to D/rate * F/2^16. Integer only, no division. */
static uint8_t pv_env_conv(uint32_t rate, int kind, uint8_t sus_pos)
{
    int32_t s = scale_u7(sus_pos, 0, 28000);
    uint32_t d = 26000u, f = 65536u;
    uint64_t b;
    uint8_t q = 0;
    if (kind == 1 && s < 26000 - ENV_DEC_GAP) { d = (uint32_t)(26000 - s); f = ENV_F_DEC; }
    if (kind == 2 && s >= ENV_REL_MIN) { d = (uint32_t)s; f = ENV_REL_F[sus_pos & 127u]; }
    b = (uint64_t)d * f * 128u;
    {
        const uint32_t *m = kind == 0 ? ENV_MQ8_A : ENV_MQ8;
        while (q < 127u && (uint64_t)m[q + 1u] * rate < b) q++;
    }
    return q;
}

/* a valid step A (law 2) or step B (law 3) record -> the same record under
   law 4: the six envelope time positions go through pv_env_conv with that
   envelope's own sustain, everything else is kept; header and CRC rebuilt. */
static void pv_rec_law4(pv_rec_t *r)
{
    uint8_t name[PV_NAME], k[31];
    uint8_t law = r->b[2];
    int i;
    if (law != PV_LAW_A && law != PV_LAW_B) return;
    memcpy(name, &r->b[4], PV_NAME);
    memcpy(k, &r->b[18], 31);
    for (i = 0; i < 6; i++) {
        uint8_t p = k[PV_ENV_IDX[i]] & 127u;
        uint32_t rate = law == PV_LAW_A ? (uint32_t)ENV_LAW2_RATE[p] << 15 : ENV_LAW3_RATE[p];
        k[PV_ENV_IDX[i]] = pv_env_conv(rate, i % 3 == 0 ? 0 : i % 3 == 1 ? 1 : 2, k[i < 3 ? 6 : 10]);
    }
    pv_rec_make_x(r, name, r->b[16], k, &r->b[49]);
    /* stamp as law 4 so the law-5 remapper, which runs next, will see it */
    r->b[2] = PV_LAW_RC;
    {
        uint32_t c = pv_crc32(r->b, 60u);
        r->b[60] = (uint8_t)c; r->b[61] = (uint8_t)(c >> 8);
        r->b[62] = (uint8_t)(c >> 16); r->b[63] = (uint8_t)(c >> 24);
    }
}

/* law 4 (curved V3, linear cutoff) -> law 5: envelope knobs by floor/ceil of
   the exact inverse, smallest log-time error (tie: snappier); cutoff by
   cents on the effective fcore cut (tie: lower). Attack < 44 and decay < 50
   clamp to 0. */
static void pv_rec_law5(pv_rec_t *r)
{
    uint8_t name[PV_NAME], k[31];
    int i;
    if (r->b[2] != PV_LAW_RC) return;
    memcpy(name, &r->b[4], PV_NAME);
    memcpy(k, &r->b[18], 31);
    k[0] = poli_law5_cut(k[0]);
    for (i = 0; i < 6; i++)
        k[PV_ENV_IDX[i]] = poli_law5_env(k[PV_ENV_IDX[i]], i % 3);
    pv_rec_make_x(r, name, r->b[16], k, &r->b[49]);
}

static void pv_rec_migrate(pv_rec_t *r)
{
    pv_rec_law4(r);
    pv_rec_law5(r);
}

static void pv_factory_rec(int i, pv_rec_t *r)
{
    pv_rec_make(r, PV_FACTORY[i].name, PV_FACTORY[i].sw, PV_FACTORY[i].k);   /* factory: OFF / FREE / 0 */
}

static void pv_bank_factory(pv_bank_t *b)
{
    int i;
    for (i = 0; i < PV_NSLOT; i++) pv_factory_rec(i, &b->rec[i]);
}

static int pv_bank_valid(const pv_bank_t *b, uint32_t magic)
{
    return b->magic == magic && b->layout == PV_LAYOUT &&
           (b->nrec == PV_NSLOT || b->nrec == PV_NSLOT_V1);
}

/* a copy from flash into pv_bank: bad records fall back to factory.
 * src->nrec may be 50 (older build): those records are taken (and converted);
 * slots 51-63 are filled from this firmware's factory table. */
static void pv_bank_take(const pv_bank_t *src)
{
    int i, n = src->nrec;
    if (n > PV_NSLOT) n = PV_NSLOT;
    for (i = 0; i < n; i++) {
        if (pv_rec_ok(&src->rec[i])) { pv_bank.rec[i] = src->rec[i]; pv_rec_migrate(&pv_bank.rec[i]); }
        else pv_factory_rec(i, &pv_bank.rec[i]);
    }
    for (; i < PV_NSLOT; i++) pv_factory_rec(i, &pv_bank.rec[i]);
}

static int pv_bank_store(void)
{
    int to = pv_newest == PV_ST_A ? PV_ST_B : PV_ST_A;
    pv_bank.magic = PV_BANK_MAGIC;
    pv_bank.layout = PV_LAYOUT;
    pv_bank.nrec = PV_NSLOT;
    pv_bank.seq++;
    if (pv_store_save(to, &pv_bank, sizeof pv_bank) != 0) {
        pv_bank.seq--;
        return -1;
    }
    pv_newest = to;
    return 0;
}

static uint8_t pv_kt_mode(void)
{
    int p = poli_nav_key_track();
    if (p <= 0) return 1u;
    if (p >= 100) return 3u;
    return 2u;
}

static int pv_kt_pct(uint8_t mode)
{
    if (mode == 1u) return 0;
    if (mode == 3u) return 100;
    return 62;   /* 0, 2, or anything else: default 62% */
}

static void pv_last_store(void)
{
    pv_settings_t s;
    uint8_t ch = (uint8_t)poli_nav_midi_ch();
    uint8_t kt = pv_kt_mode();
    memset(&s, 0, sizeof s);
    s.ver = 1u;
    s.last = (uint8_t)pv_cur;
    if (ch < 1u || ch > 16u) ch = 1u;
    s.midi_ch = ch;
    s.rsv[0] = kt;
    s.rsv[1] = (uint8_t)(xtra_pattern() + 1);    /* Drums12 */
    s.rsv[2] = XT_PAT_LAYOUT;
    if (pv_last_stored == s.last && pv_ch_stored == s.midi_ch && pv_kt_stored == kt &&
        pv_pat_stored == (uint8_t)xtra_pattern()) {
        pv_last_pending = 0;
        return;
    }
    if (pv_store_save(PV_ST_SETTINGS, &s, sizeof s) == 0) {
        pv_last_stored = s.last;
        pv_ch_stored = s.midi_ch;
        pv_kt_stored = kt;
    }
    pv_pat_stored = (uint8_t)xtra_pattern();   /* Drums12: one try per change, also on a failed write (no erase loop) */
    pv_last_pending = 0;      /* one try: a failed write is not retried in a loop */
}

static void pv_say(int m)
{
    pv_msg = m;
    poli_nav_touch();
    pv_msg_gen = poli_nav_generation();
}

static void pv_set(const uint8_t k[31], uint8_t sw, const uint8_t *x)
{
    pv_mute_on();                  /* silent before the first new value lands */
    /* merge: the EXTRAS arpeggiator keeps running across a preset change, as on the FM-1 */
#if POLI_ARP
    poli_arp_clear();              /* Elite classic arp only; no-op on FM-1 */
#endif
    poli_nav_patch_set(k, sw);
    poli_nav_patch_set_x(x);
    pv_mute_off();                 /* the next half fades in with the new sound */
}

static void pv_apply(int slot)
{
    const pv_rec_t *r = &pv_bank.rec[slot];
    pv_set(&r->b[18], r->b[16], &r->b[49]);
}

static void pv_load(int slot, uint32_t now_ms)
{
    pv_cur = slot;
    pv_apply(slot);
    pv_last_pending = 1u;
    pv_load_ms = now_ms;
}

void poli_preset_init(uint32_t now_ms)
{
    pv_settings_t s;
    int okA, okB, expand = 0;
    uint32_t seqA = 0;
    pv_mode = PV_MODE_NONE;
    pv_cmp = 0;
    pv_msg = PV_MSG_NONE;
    pv_newest = -1;
    memset(&pv_bank, 0, sizeof pv_bank);
    pv_bank_factory(&pv_bank);
    okA = pv_store_load(PV_ST_A, &pv_tmp, sizeof pv_tmp) == 0 && pv_bank_valid(&pv_tmp, PV_BANK_MAGIC);
    if (okA) seqA = pv_tmp.seq;
    if (okA) { expand = pv_tmp.nrec < PV_NSLOT; pv_bank_take(&pv_tmp); pv_bank.seq = seqA; pv_newest = PV_ST_A; }
    okB = pv_store_load(PV_ST_B, &pv_tmp, sizeof pv_tmp) == 0 && pv_bank_valid(&pv_tmp, PV_BANK_MAGIC);
    if (okB && (!okA || (int32_t)(pv_tmp.seq - seqA) > 0)) {
        expand = pv_tmp.nrec < PV_NSLOT;
        pv_bank_take(&pv_tmp);
        pv_bank.seq = pv_tmp.seq;
        pv_newest = PV_ST_B;
    }
    if (pv_newest >= 0) {
        pv_bank.magic = PV_BANK_MAGIC;
        pv_bank.layout = PV_LAYOUT;
        pv_bank.nrec = PV_NSLOT;
    }
    /* a 50-slot bank: persist the expanded 63-slot copy (A/B, header last).
     * a failed write leaves the old copy; the next boot tries again. */
    if (expand && pv_newest >= 0)
        (void)pv_bank_store();
    pv_cur = 0;
    pv_last_stored = 0xFFu;
    pv_ch_stored = 1;
    pv_ch_seen = 1;
    pv_kt_stored = 2;
    pv_kt_seen = 2;
    pv_pat_stored = pv_pat_seen = 0;
    poli_nav_set_midi_ch(1);
    poli_nav_set_key_track(62);
    if (pv_store_load(PV_ST_SETTINGS, &s, sizeof s) == 0 && s.ver == 1u && s.last < PV_NSLOT) {
        uint8_t ch = 1;
        int kt;
        pv_cur = s.last;
        pv_last_stored = s.last;
        if (s.midi_ch >= 1u && s.midi_ch <= 16u) ch = s.midi_ch;
        pv_ch_stored = ch;
        pv_ch_seen = ch;
        poli_nav_set_midi_ch(ch);
        kt = pv_kt_pct(s.rsv[0]);
        pv_kt_stored = (s.rsv[0] == 1u || s.rsv[0] == 2u || s.rsv[0] == 3u) ? s.rsv[0] : 2u;
        pv_kt_seen = pv_kt_stored;
        poli_nav_set_key_track(kt);
        xtra_pattern_restore(s.rsv[1], s.rsv[2]);   /* Drums12: the drum PATTERN back (unknown: pattern 1) */
        pv_pat_stored = pv_pat_seen = (uint8_t)xtra_pattern();
    }
    poli_nav_patch_set(&pv_bank.rec[pv_cur].b[18], pv_bank.rec[pv_cur].b[16]);
    poli_nav_patch_set_x(&pv_bank.rec[pv_cur].b[49]);   /* the boot voice is the slot (factory
                                  first boot: 01 Init); nothing sounds yet, so no mute */
    pv_last_pending = 0;
    pv_load_ms = now_ms;
    poli_nav_touch();
}

int poli_preset_mode(void) { return pv_mode; }

static int pv_wrap(int v) { v %= PV_NSLOT; return v < 0 ? v + PV_NSLOT : v; }

void poli_preset_turn(int steps, uint32_t now_ms)
{
    if (!steps) return;
    if (pv_mode == PV_MODE_NONE) {
        pv_mode = PV_MODE_BAR;
        pv_target = pv_cur;
    }
    pv_target = pv_wrap(pv_target + steps);
    pv_turn_ms = now_ms;
    pv_msg = PV_MSG_NONE;
    poli_nav_touch();
}

static void pv_cmp_off(void)
{
    if (pv_cmp) {
        pv_set(pv_edit_k, pv_edit_sw, pv_edit_x);
        pv_cmp = 0;
    }
}

void poli_preset_sel(void)
{
    if (pv_mode == PV_MODE_BAR) {
        pv_mode = PV_MODE_NONE;
        pv_load(pv_target, pv_turn_ms);
        pv_say(PV_MSG_LOADED);
    } else if (pv_mode == PV_MODE_SAVE) {
        if (!pv_cmp) {
            poli_nav_patch_get(pv_edit_k, &pv_edit_sw);
            poli_nav_patch_get_x(pv_edit_x);
            pv_apply(pv_cur);
            pv_cmp = 1;
        } else
            pv_cmp_off();
        poli_nav_touch();
    }
}

void poli_preset_load_slot(int slot, uint32_t now_ms)
{
    /* Board-layer wrapper around pv_load: all 63 slots, no 50-slot cap. */
    if (slot < 0 || slot >= PV_NSLOT)
        return;
    if (pv_mode == PV_MODE_SAVE)
        return;                             /* do not clobber an open save screen */
    pv_cmp_off();
    pv_mode = PV_MODE_NONE;
    pv_target = slot;
    pv_load(slot, now_ms);
    pv_say(PV_MSG_LOADED);
}

void poli_preset_cancel(void)
{
    if (pv_mode == PV_MODE_NONE) return;
    pv_cmp_off();
    pv_mode = PV_MODE_NONE;
    poli_nav_touch();
}

static void pv_do(uint32_t now_ms)
{
    int rc = 0, m = PV_MSG_NONE;
    pv_cmp_off();
    switch (pv_action) {
    case PV_ACT_SAVE: {
        uint8_t k[31], sw, x[NAV_NX];
        poli_nav_patch_get(k, &sw);
        poli_nav_patch_get_x(x);
        pv_rec_make_x(&pv_bank.rec[pv_target], pv_name, sw, k, x);
        pv_cur = pv_target;
        rc = pv_bank_store();
        m = PV_MSG_SAVED;
        pv_last_pending = 1u;
        pv_load_ms = now_ms - PV_LAST_MS;   /* due at once, still only in silence */
        break;
    }
    case PV_ACT_BACKUP:
        pv_bank.magic = PV_BACK_MAGIC;
        pv_bank.layout = PV_LAYOUT;
        pv_bank.nrec = PV_NSLOT;
        rc = pv_store_save(PV_ST_BACKUP, &pv_bank, sizeof pv_bank);
        pv_bank.magic = PV_BANK_MAGIC;
        m = PV_MSG_BACKED;
        break;
    case PV_ACT_RESTORE:
        if (pv_store_load(PV_ST_BACKUP, &pv_tmp, sizeof pv_tmp) != 0 || !pv_bank_valid(&pv_tmp, PV_BACK_MAGIC)) {
            pv_mode = PV_MODE_NONE;
            pv_say(PV_MSG_NOBACKUP);
            return;
        }
        pv_bank_take(&pv_tmp);
        rc = pv_bank_store();
        pv_load(pv_cur, now_ms);
        m = PV_MSG_RESTORED;
        break;
    default:
        pv_bank_factory(&pv_bank);
        rc = pv_bank_store();
        pv_load(pv_cur, now_ms);
        m = PV_MSG_FACTORY;
        break;
    }
    pv_mode = PV_MODE_NONE;
    pv_say(rc ? PV_MSG_RAMONLY : m);
}

void poli_preset_save_btn(uint32_t now_ms)
{
    if (pv_mode == PV_MODE_SAVE) { pv_do(now_ms); return; }
    if (pv_mode != PV_MODE_BAR) pv_target = pv_cur;
    memcpy(pv_name, &pv_bank.rec[pv_cur].b[4], PV_NAME);
    pv_mode = PV_MODE_SAVE;
    pv_cmp = 0;
    pv_cursor = 0;
    pv_action = PV_ACT_SAVE;
    pv_msg = PV_MSG_NONE;
    poli_nav_touch();
}

static int pv_group_of(int code)
{
    int g;
    for (g = TN_NGROUP - 1; g > 0; g--) if (code >= TN_GROUP[g]) return g;
    return 0;
}

void poli_preset_knob(int index, int steps)
{
    int v;
    if (pv_mode != PV_MODE_SAVE || !steps) return;
    switch (index) {
    case 0:
        v = pv_cursor + steps;
        pv_cursor = v < 0 ? 0 : v > PV_NAME - 1 ? PV_NAME - 1 : v;
        break;
    case 1:
        v = ((int)pv_name[pv_cursor] + steps) % TN_N;
        pv_name[pv_cursor] = (uint8_t)(v < 0 ? v + TN_N : v);
        break;
    case 2:
        v = (pv_group_of(pv_name[pv_cursor]) + steps) % TN_NGROUP;
        pv_name[pv_cursor] = TN_GROUP[v < 0 ? v + TN_NGROUP : v];
        break;
    default:
        v = (pv_action + steps) % PV_NACT;
        pv_action = v < 0 ? v + PV_NACT : v;
        break;
    }
    poli_nav_touch();
}

void poli_preset_tick(uint32_t now_ms)
{
    if (pv_mode == PV_MODE_BAR && now_ms - pv_turn_ms > PV_BAR_MS) {
        pv_mode = PV_MODE_NONE;
        poli_nav_touch();
    }
    {   /* MIDI channel and key track are device settings, same 3 s + silence write as last-used */
        uint8_t ch = (uint8_t)poli_nav_midi_ch();
        uint8_t kt = pv_kt_mode();
        if (ch != pv_ch_stored || kt != pv_kt_stored) {
            if (ch != pv_ch_seen || kt != pv_kt_seen) pv_load_ms = now_ms;
            pv_ch_seen = ch;
            pv_kt_seen = kt;
            pv_last_pending = 1u;
        }
    }
    {   /* Drums12: the drum PATTERN, only once the transport is stopped (the Drift changes it while it runs; one
         * write per stop at most), 3 s after the last change, in the same silent write */
        uint8_t pat = (uint8_t)xtra_pattern();
        if (xtra_running()) pv_pat_seen = 0xFFu;             /* the 3 s count again from the STOP */
        else if (pat != pv_pat_stored) {
            if (pat != pv_pat_seen) pv_load_ms = now_ms;
            pv_pat_seen = pat;
            pv_last_pending = 1u;
        } else pv_pat_seen = pat;
    }
    if (pv_last_pending && now_ms - pv_load_ms >= PV_LAST_MS &&
        !(xtra_running() && (uint8_t)xtra_pattern() != pv_pat_stored)) {   /* (never over playing drums) */
        int32_t ega;
        polivoks_read(0, 0, 0, 0, 0, 0, 0, 0, &ega, 0);
        if (!poli_gate && ega == 0)      /* nothing sounding: the ~50 ms erase is not heard */
            pv_last_store();
    }
}

static int pv_dirty(void)
{
    uint8_t k[31], sw, x[NAV_NX], rx[NAV_NX];
    int i;
    const pv_rec_t *r = &pv_bank.rec[pv_cur];
    if (pv_cmp) { memcpy(k, pv_edit_k, sizeof k); sw = pv_edit_sw; memcpy(x, pv_edit_x, sizeof x); }
    else { poli_nav_patch_get(k, &sw); poli_nav_patch_get_x(x); }
    for (i = 0; i < NAV_NX; i++) rx[i] = r->b[49 + i] > poli_nav_patch_max_x(i) ? 0u : r->b[49 + i];
    return memcmp(k, &r->b[18], sizeof k) != 0 || (uint8_t)(sw & 0xBFu) != r->b[16] || memcmp(x, rx, sizeof x) != 0;
}

void poli_preset_view(poli_preset_view_t *v)
{
    if (!v) return;
    v->mode = pv_mode;
    v->cur = pv_cur;
    v->target = pv_target;
    v->dirty = pv_dirty();
    v->cmp = pv_cmp;
    v->cursor = pv_cursor;
    v->action = pv_action;
    v->msg = pv_msg_gen == poli_nav_generation() ? pv_msg : PV_MSG_NONE;
    memcpy(v->cur_name, &pv_bank.rec[pv_cur].b[4], PV_NAME);
    memcpy(v->target_name, &pv_bank.rec[pv_target].b[4], PV_NAME);
    memcpy(v->edit_name, pv_name, PV_NAME);
}

void poli_preset_name_utf8(const uint8_t code[PV_NAME], char *out, int trim)
{
    int i, n = PV_NAME;
    char *o = out;
    if (trim) while (n > 0 && code[n - 1] == 0u) n--;
    for (i = 0; i < n; i++) {
        uint32_t cp = TN_CP[code[i] < TN_N ? code[i] : 0];
        if (cp < 0x80u) *o++ = (char)cp;
        else { *o++ = (char)(0xC0u | (cp >> 6)); *o++ = (char)(0x80u | (cp & 0x3Fu)); }
    }
    *o = 0;
}
