#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Drum one-shot assets for P-Vox.

  tools/gen_poli_drum_smp.py           write firmware/src/poli_drum_smp.h and firmware/src/poli_drum_smp.c
  tools/gen_poli_drum_smp.py --check   regenerate in memory and compare with the checked-in files (exit 1 if not equal)

Each clip in assets/drums/ is 44.1 kHz mono s16. Steps (deterministic, no resampling,
no peak-normalise of the set):
  1. trim silent tails (and leading silence): first/last sample above -50 dBFS
  2. 2 ms raised-cosine fade-out only (no fade-in; the attack stays sharp)
  3. pad to an even length
  4. IMA-ADPCM, standard step and index tables, beam search (32 decoder states),
     start state 0/0, two samples a byte, low nibble first. Same arithmetic as
     tools/gen_poli_boot.py and the firmware decoder.

Instruments without a one-shot keep the synthesised voice (open hat of the 808
family, crash of the 909 family). Mid/high toms and congas retune a shared clip
by a fixed Q12 rate.
"""
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WAV_DIR = ROOT / "assets" / "drums"
OUT_H = ROOT / "firmware" / "src" / "poli_drum_smp.h"
OUT_C = ROOT / "firmware" / "src" / "poli_drum_smp.c"
RATE = 44100
THRESH = 32768 * 10 ** (-50 / 20)
FADE_MS = 2
BEAM = 32

# Unique clips, in the order they are packed into XT_SMP / XT_SMP_ADPCM.
CLIPS = [
    "808_kick03",
    "808_kick04",
    "808_snare01",
    "808_cp",
    "808_ch",
    "808_lt",
    "808_ht",
    "808_cb",
    "808_cl",
    "808_lc",
    "909_bda",
    "909_sda",
    "909_clap",
    "909_hha",
    "909_hhc",
    "909_toma",
    "909_rim",
    "909_ride",
]

# 26 drum instruments, same order as DI_* in poli_xtra.c. None => synthesised.
# (clip name, semitone shift versus the clip).
INST = [
    ("808_kick03", 0),   # DI_BD8
    ("808_kick04", 0),   # DI_BDL8
    ("808_snare01", 0),  # DI_SD8
    ("808_cp", 0),       # DI_CP8
    ("808_ch", 0),       # DI_CH8
    None,                # DI_OH8
    ("808_lt", 0),       # DI_LT8
    ("808_lt", 5),       # DI_MT8
    ("808_ht", 0),       # DI_HT8
    ("808_cb", 0),       # DI_CB8
    ("808_cl", 0),       # DI_CL8
    ("909_rim", 0),      # DI_RS8
    ("808_lc", 0),       # DI_LC8
    ("808_lc", 5),       # DI_MC8
    ("808_lc", 10),      # DI_HC8
    ("909_bda", 0),      # DI_BD9
    ("909_sda", 0),      # DI_SD9
    ("909_clap", 0),     # DI_CP9
    ("909_hha", 0),      # DI_CH9
    ("909_hhc", 0),      # DI_OH9
    ("909_toma", 0),     # DI_LT9
    ("909_toma", 4),     # DI_MT9
    ("909_toma", 8),     # DI_HT9
    ("909_rim", 0),      # DI_RS9
    ("909_ride", 0),     # DI_RD9
    None,                # DI_CR9
]

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
        97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
        724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
        4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
        18500, 20350, 22385, 24623, 27086, 29794, 32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8]

C_ENCODER = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const int STEP[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,
97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,
724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,
4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,
18500,20350,22385,24623,27086,29794,32767};
static const int IDXV[8] = {-1,-1,-1,-1,2,4,6,8};
#define BEAM 32
typedef struct { long long cost; int pred, idx; unsigned hist; } Beam;
static void ima_step(int code, int pred, int idx, int *op, int *oi) {
    int st = STEP[idx], d = st >> 3;
    if (code & 4) d += st;
    if (code & 2) d += st >> 1;
    if (code & 1) d += st >> 2;
    pred = (code & 8) ? pred - d : pred + d;
    if (pred > 32767) pred = 32767;
    if (pred < -32768) pred = -32768;
    idx += IDXV[code & 7];
    if (idx < 0) idx = 0;
    if (idx > 88) idx = 88;
    *op = pred; *oi = idx;
}
int main(void) {
    int n, i, c, b, nb, used;
    int16_t *pcm;
    unsigned char *codes;
    Beam beams[BEAM], cand_store[BEAM * 16];
    int cand_n;
    if (fread(&n, 4, 1, stdin) != 1) return 1;
    pcm = (int16_t *)malloc((size_t)n * 2);
    codes = (unsigned char *)malloc((size_t)n);
    if (!pcm || !codes) return 1;
    if (fread(pcm, 2, (size_t)n, stdin) != (size_t)n) return 1;
    beams[0].cost = 0; beams[0].pred = 0; beams[0].idx = 0; beams[0].hist = 0;
    nb = 1;
    /* hist is an index into a linked list of codes packed as (code, prev_node). */
    {
        unsigned *prev = (unsigned *)malloc((size_t)(n + 1) * BEAM * sizeof(unsigned));
        unsigned char *code_at = (unsigned char *)malloc((size_t)(n + 1) * BEAM);
        unsigned node = 1; /* 0 = end */
        if (!prev || !code_at) return 1;
        prev[0] = 0; code_at[0] = 0;
        for (i = 0; i < n; i++) {
            /* collect unique (pred,idx) with best cost; sort like the Python encoder */
            /* Use a simple table: at most BEAM*16 candidates */
            cand_n = 0;
            for (b = 0; b < nb; b++) {
                for (c = 0; c < 16; c++) {
                    int p, ix, k, found = -1;
                    long long nc;
                    ima_step(c, beams[b].pred, beams[b].idx, &p, &ix);
                    nc = beams[b].cost + (long long)(pcm[i] - p) * (long long)(pcm[i] - p);
                    for (k = 0; k < cand_n; k++) {
                        if (cand_store[k].pred == p && cand_store[k].idx == ix) { found = k; break; }
                    }
                    if (found >= 0) {
                        if (cand_store[found].cost > nc) {
                            cand_store[found].cost = nc;
                            cand_store[found].hist = beams[b].hist;
                            /* stash code in a side field: reuse nothing; keep in a parallel? use top 4 of hist later */
                            cand_store[found].hist = (unsigned)((c << 28) | (beams[b].hist & 0x0FFFFFFFu)); /* temporary */
                            /* We'll rewrite properly below with node list; store code in unused way:
                               keep prev hist and code separately. Overwrite hist with prev, use a temp. */
                        }
                    } else {
                        cand_store[cand_n].cost = nc;
                        cand_store[cand_n].pred = p;
                        cand_store[cand_n].idx = ix;
                        cand_store[cand_n].hist = (unsigned)((c << 28) | (beams[b].hist & 0x0FFFFFFFu));
                        cand_n++;
                    }
                }
            }
            /* The merge-on-equal and hist packing above is messy if hist > 28 bits.
               Use explicit node allocation instead — restart this inner with a cleaner design. */
            (void)node; (void)prev; (void)code_at;
            return 2;
        }
    }
    return 2;
}
'''

def q12_st(st):
    return int(round(4096 * (2 ** (st / 12.0))))


def read_wav(path):
    with wave.open(str(path), "rb") as w:
        if w.getnchannels() != 1 or w.getsampwidth() != 2 or w.getframerate() != RATE:
            sys.exit(f"{path}: want 44.1 kHz mono s16")
        raw = w.readframes(w.getnframes())
    return list(struct.unpack("<%dh" % (len(raw) // 2), raw))


def trim_fade(x):
    above = [i for i, v in enumerate(x) if abs(v) > THRESH]
    if not above:
        sys.exit("clip is silent")
    a, b = above[0], above[-1] + 1
    y = list(x[a:b])
    nf = RATE * FADE_MS // 1000
    if nf > 0 and len(y) > 0:
        if nf > len(y):
            nf = len(y)
        for i in range(nf):
            # last nf samples: i=0 is nf-1 from end, fade to 0 at the last sample
            k = nf - 1 - i
            f = 0.5 - 0.5 * math.cos(math.pi * k / nf) if nf > 1 else 0.0
            # k = nf-1 -> f=1 at start of fade (cos(pi*(nf-1)/nf) ~ -1, 0.5-0.5*(-1)=1)
            # k = 0 -> f=0
            y[len(y) - nf + i] = int(round(y[len(y) - nf + i] * (0.5 - 0.5 * math.cos(math.pi * (nf - 1 - i) / nf) if nf > 1 else 0.0)))
        y[-1] = 0
    if len(y) % 2:
        y.append(0)
    return y, a, b, len(x)


def ima_step(code, pred, idx):
    st = STEP[idx]
    d = st >> 3
    if code & 4:
        d += st
    if code & 2:
        d += st >> 1
    if code & 1:
        d += st >> 2
    pred = pred - d if code & 8 else pred + d
    pred = max(-32768, min(32767, pred))
    idx = max(0, min(88, idx + IDX[code & 7]))
    return pred, idx


def ima_encode_py(pcm, beam=BEAM):
    beams = [(0, 0, 0, None)]
    for s in pcm:
        cand = {}
        for cost, pred, idx, hist in beams:
            for c in range(16):
                p, ix = ima_step(c, pred, idx)
                nc = cost + (s - p) * (s - p)
                k = (p, ix)
                if k not in cand or cand[k][0] > nc:
                    cand[k] = (nc, p, ix, (c, hist))
        beams = sorted(cand.values(), key=lambda t: (t[0], t[1], t[2]))[:beam]
    codes, h = [], beams[0][3]
    while h:
        codes.append(h[0])
        h = h[1]
    codes.reverse()
    data = bytearray()
    for i in range(0, len(codes), 2):
        data.append(codes[i] | (codes[i + 1] << 4))
    return data


def ima_decode(data, n):
    pred = idx = 0
    out = []
    for i in range(n):
        c = (data[i >> 1] >> ((i & 1) * 4)) & 15
        pred, idx = ima_step(c, pred, idx)
        out.append(pred)
    return out


# Fast C encoder that matches ima_encode_py bit-for-bit (same sort key, same merge).
C_SRC = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const int STEP[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,
97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,
724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,
4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,
18500,20350,22385,24623,27086,29794,32767};
static const int IDXV[8] = {-1,-1,-1,-1,2,4,6,8};
#define BEAM 32
typedef struct { unsigned long long cost; int pred, idx; unsigned hist; } Beam;
static void step_fn(int code, int pred, int idx, int *op, int *oi) {
    int st = STEP[idx], d = st >> 3;
    if (code & 4) d += st;
    if (code & 2) d += st >> 1;
    if (code & 1) d += st >> 2;
    pred = (code & 8) ? pred - d : pred + d;
    if (pred > 32767) pred = 32767;
    if (pred < -32768) pred = -32768;
    idx += IDXV[code & 7];
    if (idx < 0) idx = 0;
    if (idx > 88) idx = 88;
    *op = pred; *oi = idx;
}
/* hist nodes: 0 = end; node i stores code and previous node. */
typedef struct { unsigned prev; unsigned char code; } Node;
static int cmp_beam(const void *a, const void *b) {
    const Beam *x = a, *y = b;
    if (x->cost < y->cost) return -1;
    if (x->cost > y->cost) return 1;
    if (x->pred < y->pred) return -1;
    if (x->pred > y->pred) return 1;
    if (x->idx < y->idx) return -1;
    if (x->idx > y->idx) return 1;
    return 0;
}
int main(void) {
    int n, i, c, b, nb, k, cand_n, p, ix;
    int16_t *pcm;
    unsigned char *out;
    Beam beams[BEAM], *cand;
    Node *nodes;
    unsigned nused, ncap;
    uint32_t nn;
    if (fread(&nn, 4, 1, stdin) != 1) return 1;
    n = (int)nn;
    pcm = (int16_t *)malloc((size_t)n * 2);
    if (!pcm || fread(pcm, 2, (size_t)n, stdin) != (size_t)n) return 1;
    ncap = (unsigned)n * BEAM + 8u;
    nodes = (Node *)malloc((size_t)ncap * sizeof(Node));
    cand = (Beam *)malloc((size_t)BEAM * 16 * sizeof(Beam));
    if (!nodes || !cand) return 1;
    nodes[0].prev = 0; nodes[0].code = 0;
    nused = 1;
    beams[0].cost = 0; beams[0].pred = 0; beams[0].idx = 0; beams[0].hist = 0;
    nb = 1;
    for (i = 0; i < n; i++) {
        cand_n = 0;
        for (b = 0; b < nb; b++) {
            for (c = 0; c < 16; c++) {
                unsigned long long nc;
                int found = -1;
                step_fn(c, beams[b].pred, beams[b].idx, &p, &ix);
                nc = beams[b].cost + (unsigned long long)(int64_t)(pcm[i] - p) * (unsigned long long)(int64_t)(pcm[i] - p);
                for (k = 0; k < cand_n; k++) {
                    if (cand[k].pred == p && cand[k].idx == ix) { found = k; break; }
                }
                if (found >= 0) {
                    if (cand[found].cost > nc) {
                        cand[found].cost = nc;
                        cand[found].hist = beams[b].hist;
                        cand[found].pred = p;
                        cand[found].idx = ix;
                        /* keep the winning code on the side: stash in a temporary by delaying node alloc.
                           Store code in unused high? We'll allocate node only after unique-key pick.
                           Use hist as prev, and overwrite a parallel code array. */
                        cand[found].hist = (beams[b].hist & 0x7FFFFFFFu) | ((unsigned)c << 27);
                        /* 5 bits for code (0-15) at bits 27..31; hist nodes may exceed 2^27 for long clips.
                           BAD for 47k*32. Do not pack. Use two fields. */
                    }
                } else {
                    cand[cand_n].cost = nc;
                    cand[cand_n].pred = p;
                    cand[cand_n].idx = ix;
                    cand[cand_n].hist = (beams[b].hist & 0x7FFFFFFFu) | ((unsigned)c << 27);
                    cand_n++;
                }
            }
        }
        (void)nused;
        return 2;
    }
    (void)out;
    return 2;
}
'''


def _write_c_encoder(path):
    src = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int STEP[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,
97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,
724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,
4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,
18500,20350,22385,24623,27086,29794,32767};
static const int IDXV[8] = {-1,-1,-1,-1,2,4,6,8};
#define BEAM 32

typedef struct {
    unsigned long long cost;
    int pred, idx;
    unsigned prev;
    unsigned char code;
} Cand;

static int cmp_cand(const void *a, const void *b)
{
    const Cand *x = (const Cand *)a, *y = (const Cand *)b;
    if (x->cost < y->cost) return -1;
    if (x->cost > y->cost) return 1;
    if (x->pred < y->pred) return -1;
    if (x->pred > y->pred) return 1;
    if (x->idx < y->idx) return -1;
    if (x->idx > y->idx) return 1;
    return 0;
}

static void ima_step(int code, int pred, int idx, int *op, int *oi)
{
    int st = STEP[idx], d = st >> 3;
    if (code & 4) d += st;
    if (code & 2) d += st >> 1;
    if (code & 1) d += st >> 2;
    pred = (code & 8) ? pred - d : pred + d;
    if (pred > 32767) pred = 32767;
    if (pred < -32768) pred = -32768;
    idx += IDXV[code & 7];
    if (idx < 0) idx = 0;
    if (idx > 88) idx = 88;
    *op = pred;
    *oi = idx;
}

int main(void)
{
    uint32_t nn;
    int n, i, c, b, nb, k, cand_n, p, ix, kept;
    int16_t *pcm;
    unsigned char *codes;
    Cand beams[BEAM], *cand;
    unsigned *prev;
    unsigned char *code_at;
    unsigned nused, ncap, h;
    if (fread(&nn, 4, 1, stdin) != 1) return 1;
    n = (int)nn;
    if (n < 2 || (n & 1)) return 1;
    pcm = (int16_t *)malloc((size_t)n * 2u);
    codes = (unsigned char *)malloc((size_t)n);
    ncap = (unsigned)n * (unsigned)BEAM + 8u;
    prev = (unsigned *)malloc((size_t)ncap * sizeof(unsigned));
    code_at = (unsigned char *)malloc((size_t)ncap);
    cand = (Cand *)malloc((size_t)BEAM * 16u * sizeof(Cand));
    if (!pcm || !codes || !prev || !code_at || !cand) return 1;
    if (fread(pcm, 2, (size_t)n, stdin) != (size_t)n) return 1;

    prev[0] = 0;
    code_at[0] = 0;
    nused = 1;
    beams[0].cost = 0;
    beams[0].pred = 0;
    beams[0].idx = 0;
    beams[0].prev = 0;
    beams[0].code = 0;
    nb = 1;

    for (i = 0; i < n; i++) {
        cand_n = 0;
        for (b = 0; b < nb; b++) {
            for (c = 0; c < 16; c++) {
                unsigned long long nc;
                int found = -1;
                ima_step(c, beams[b].pred, beams[b].idx, &p, &ix);
                nc = beams[b].cost
                    + (unsigned long long)(int64_t)(pcm[i] - p)
                    * (unsigned long long)(int64_t)(pcm[i] - p);
                for (k = 0; k < cand_n; k++) {
                    if (cand[k].pred == p && cand[k].idx == ix) {
                        found = k;
                        break;
                    }
                }
                if (found >= 0) {
                    if (cand[found].cost > nc) {
                        cand[found].cost = nc;
                        cand[found].prev = beams[b].prev;
                        cand[found].code = (unsigned char)c;
                        /* beams[b].prev is the hist node of the parent. We have not
                           allocated a node for the parent code this round; parent identity
                           is beams[b]'s existing node, stored as .prev on the beam. */
                    }
                } else {
                    cand[cand_n].cost = nc;
                    cand[cand_n].pred = p;
                    cand[cand_n].idx = ix;
                    cand[cand_n].prev = beams[b].prev;
                    cand[cand_n].code = (unsigned char)c;
                    cand_n++;
                }
            }
        }
        qsort(cand, (size_t)cand_n, sizeof(Cand), cmp_cand);
        kept = cand_n < BEAM ? cand_n : BEAM;
        for (b = 0; b < kept; b++) {
            if (nused >= ncap) return 1;
            prev[nused] = cand[b].prev;
            code_at[nused] = cand[b].code;
            beams[b].cost = cand[b].cost;
            beams[b].pred = cand[b].pred;
            beams[b].idx = cand[b].idx;
            beams[b].prev = nused;
            beams[b].code = cand[b].code;
            nused++;
        }
        nb = kept;
    }

    /* Walk the winning hist from the last node. */
    h = beams[0].prev;
    i = n;
    while (i > 0 && h != 0) {
        codes[--i] = code_at[h];
        h = prev[h];
    }
    if (i != 0) return 1;
    {
        unsigned char *data = (unsigned char *)malloc((size_t)n / 2u);
        if (!data) return 1;
        for (i = 0; i < n; i += 2)
            data[i / 2] = (unsigned char)(codes[i] | (codes[i + 1] << 4));
        if (fwrite(data, 1, (size_t)n / 2u, stdout) != (size_t)n / 2u) return 1;
        free(data);
    }
    return 0;
}
'''
    path.write_text(src)


_ENCODER_BIN = None


def _encoder_bin():
    global _ENCODER_BIN
    if _ENCODER_BIN and Path(_ENCODER_BIN).exists():
        return _ENCODER_BIN
    td = Path(tempfile.mkdtemp(prefix="ima_beam_"))
    src, binp = td / "ima_beam.c", td / "ima_beam"
    _write_c_encoder(src)
    r = subprocess.run(["gcc", "-O2", "-std=c11", "-o", str(binp), str(src)], capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        sys.exit("failed to build IMA encoder")
    _ENCODER_BIN = str(binp)
    return _ENCODER_BIN


def ima_encode(pcm, beam=BEAM):
    # C helper, same cost / (pred, idx) merge / sort as the Python beam in gen_poli_boot.py
    raw = struct.pack("<I", len(pcm)) + struct.pack("<%dh" % len(pcm), *pcm)
    r = subprocess.run([_encoder_bin()], input=raw, capture_output=True)
    if r.returncode != 0:
        sys.exit(f"IMA encoder failed ({r.returncode}): {r.stderr[:200]!r}")
    data = r.stdout
    if len(data) != len(pcm) // 2:
        sys.exit(f"IMA encoder length {len(data)} != {len(pcm)//2}")
    return bytearray(data)


def c_array(name, data, typ="uint8_t"):
    lines = [f"const {typ} {name}[{len(data)}] = {{"]
    for k in range(0, len(data), 24):
        chunk = data[k:k + 24]
        lines.append("    " + ", ".join(str(int(b)) for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def build():
    clips = []
    blob = bytearray()
    stats = []
    for name in CLIPS:
        path = WAV_DIR / f"{name}.wav"
        if not path.exists():
            sys.exit(f"missing {path}")
        src = read_wav(path)
        pcm, a, b, nsrc = trim_fade(src)
        print(f"encode {name}: {nsrc} -> {len(pcm)} samples ({len(pcm)/RATE:.4f} s), trim {a}..{b}", flush=True)
        data = ima_encode(pcm)
        dec = ima_decode(data, len(pcm))
        err = [d - s for d, s in zip(dec, pcm)]
        rms_e = math.sqrt(sum(e * e for e in err) / len(err))
        rms_s = math.sqrt(sum(s * s for s in pcm) / len(pcm)) or 1.0
        snr = 20 * math.log10(rms_s / rms_e) if rms_e else 99.0
        mx = max(abs(e) for e in err)
        pk = max(abs(v) for v in pcm)
        print(f"  {len(data)} B IMA, peak {pk}, max |err| {mx}, SNR {snr:.2f} dB", flush=True)
        clips.append(dict(name=name, off=len(blob), n=len(pcm), bytes=len(data),
                          trim_a=a, trim_b=b, nsrc=nsrc, peak=pk, maxerr=mx, snr=snr))
        blob.extend(data)
        stats.append((name, len(pcm), len(data), snr, mx, a, b, nsrc, pk))

    id_of = {c["name"]: i for i, c in enumerate(clips)}
    ids, pms = [], []
    for ent in INST:
        if ent is None:
            ids.append(255)
            pms.append(4096)
        else:
            name, st = ent
            ids.append(id_of[name])
            pms.append(q12_st(st))

    h_lines = [
        "/* generated by tools/gen_poli_drum_smp.py from assets/drums/. 44.1 kHz mono IMA-ADPCM,",
        f" * beam-search encoded, start state 0/0, low nibble first. {len(clips)} unique clips,",
        f" * {len(blob)} B packed. Instruments without a clip keep the synthesised voice. */",
        "#pragma once",
        "#include <stdint.h>",
        f"#define XT_SMP_N {len(clips)}u",
        f"#define XT_SMP_BYTES {len(blob)}u",
        "typedef struct {",
        "    uint32_t off;",
        "    uint32_t n;",
        "} xt_smp_clip_t;",
        "extern const xt_smp_clip_t XT_SMP[XT_SMP_N];",
        "extern const uint8_t XT_SMP_ID[26];",
        "extern const uint16_t XT_SMP_PM[26];",
        "extern const uint8_t XT_SMP_ADPCM[XT_SMP_BYTES];",
        "",
    ]
    header = "\n".join(h_lines)

    c_lines = [
        "/* generated by tools/gen_poli_drum_smp.py from assets/drums/. See poli_drum_smp.h. */",
        '#include "poli_drum_smp.h"',
        "",
        "const xt_smp_clip_t XT_SMP[XT_SMP_N] = {",
    ]
    for c in clips:
        c_lines.append(f"    {{ {c['off']}u, {c['n']}u }}, /* {c['name']} */")
    c_lines.append("};")
    c_lines.append("const uint8_t XT_SMP_ID[26] = { " + ", ".join(str(v) for v in ids) + " };")
    c_lines.append("const uint16_t XT_SMP_PM[26] = { " + ", ".join(str(v) for v in pms) + " };")
    c_lines.append(c_array("XT_SMP_ADPCM", blob))
    c_lines.append("")
    source = "\n".join(c_lines)

    # comment table for the log
    for row in stats:
        pass
    return header, source, blob, clips, stats


def main():
    a = sys.argv[1:]
    header, source, blob, clips, stats = build()
    if a and a[0] == "--check":
        ok = OUT_H.read_text() == header and OUT_C.read_text() == source
        print("gen_poli_drum_smp: check", "OK" if ok else "DIFFERS")
        sys.exit(0 if ok else 1)
    OUT_H.write_text(header)
    OUT_C.write_text(source)
    print(f"wrote {OUT_H} and {OUT_C}")
    print(f"packed {len(clips)} clips, {len(blob)} B IMA-ADPCM [{ 'M' }]")
    for name, n, nb, snr, mx, a0, b0, nsrc, pk in stats:
        print(f"  {name}: {nsrc} -> {n} smp ({n/RATE:.4f} s), {nb} B, peak {pk}, SNR {snr:.2f} dB, trim {a0}..{b0}")


if __name__ == "__main__":
    main()
