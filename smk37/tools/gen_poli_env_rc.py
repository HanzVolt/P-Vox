#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Curved (one-pole, RC-style) envelope tables - envelope option V3 (Hans, 2026-11-12).

  tools/gen_poli_env_rc.py          -> firmware/src/poli_env_rc.h + poli_env_rc.inc
  tools/gen_poli_env_rc.py --check  exit 1 if stale

Engine (polivoks_limited.c poli_eg): level env is Q15 (level << 15). Each
sample  env += ((tgt - env) * c) >> 24,  c = Q24 coefficient (0 < c < 2^24).
  attack : tgt = 34000 (overshoot), clipped at the 26000 peak -> decay.
  decay  : tgt = sustain.
  release: tgt = -26 (one thousandth of the peak below 0), clamped at exact 0.
"Time" of a knob position = full-sweep time N(q):
  attack  0 -> 26000 peak;
  decay   26000 -> within 1/1000 of the distance to sustain (-60 dB);
  release 26000 -> exact 0 (the -26 target makes it cross 0 and stop).
Decay/release: N(q) = 21 * (441000/21)^(q/127) samples at 44.1 kHz: 0.48 ms .. 10.0 s, 8.2 %/click.
Attack:        NA(q) = 5.5 * (441000/5.5)^(q/127) samples: 0.12 ms .. 10.0 s, 9.3 %/click.
The fast ends are where the largest per-sample step reaches the old 8000-level cap
(attack from 0 toward 34000; release from 28000 toward -26).
"""
import math, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
H = ROOT / "firmware/src/poli_env_rc.h"
INC = ROOT / "firmware/src/poli_env_rc.inc"

FS = 44100.0
PEAK, OVER, REL_T = 26000, 34000, 26
N0, NA0, NMAX = 21.0, 5.5, 441000.0
K_A = math.log(OVER / (OVER - PEAK))              # 1.4469
K_DR = math.log((PEAK + REL_T) / REL_T)           # 6.9088
K_DEC = math.log(1000.0)                          # decay settles to 1/1000 of its distance
CMAX_A = (1 << 24) * 8000 // OVER                 # largest attack step 8000 levels (= step A/B max)
CMAX_DR = (1 << 24) * 8000 // (28000 + REL_T)     # largest decay/release step 8000 levels

# step A (law 2) rates, levels/sample, and step B (law 3) ENV_RATE, Q15 - for migration
LAW2 = [
    8000, 7453, 6944, 6470, 6028, 5616, 5232, 4875, 4542, 4231, 3942, 3673, 3422, 3188, 2970, 2768,
    2578, 2402, 2238, 2085, 1943, 1810, 1686, 1571, 1464, 1364, 1271, 1184, 1103, 1028, 957, 892,
    831, 774, 721, 672, 626, 583, 544, 506, 472, 440, 420, 382, 355, 331, 309, 288,
    268, 250, 233, 217, 202, 188, 175, 163, 152, 142, 132, 123, 115, 107, 99, 93,
    86, 80, 75, 70, 65, 61, 56, 53, 50, 46, 43, 40, 37, 35, 32, 30,
    28, 26, 24, 23, 21, 20, 18, 17, 16, 15, 14, 13, 12, 11, 10, 10,
    9, 8, 8, 7, 7, 6, 6, 5, 5, 5, 4, 4, 4, 4, 3, 3,
    3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1]
LAW3 = [
    262144000, 238850568, 217626929, 198289168, 180669709, 164615870, 149988533, 136660943,
    124517608, 113453298, 103372134, 94186756, 85817566, 78192041, 71244100, 64913535,
    59145488, 53889974, 49101451, 44738424, 40763085, 37140983, 33840732, 30833733,
    28093928, 25597575, 23323041, 21250616, 19362342, 17641855, 16074246, 14645930,
    13762560, 12158771, 11078374, 10093979, 9197054, 8379828, 7635218, 6956773,
    6338612, 5775379, 5262194, 4794609, 4368573, 3980393, 3626706, 3304446,
    3010821, 2743288, 2499526, 2277425, 2075059, 1890675, 1722674, 1638400,
    1430131, 1303053, 1146880, 1081770, 985647, 898065, 818265, 745556,
    679308, 618946, 563948, 513837, 468179, 426578, 388673, 354137,
    322669, 293998, 267874, 244071, 222384, 202623, 184619, 168214,
    153267, 139648, 127239, 115933, 105632, 96245, 87693, 79901,
    72801, 66332, 60438, 55068, 50175, 45716, 41654, 37953,
    34580, 31508, 28708, 26157, 23833, 21715, 19786, 18027,
    16426, 14966, 13636, 12425, 11321, 10315, 9398, 8563,
    7802, 7109, 6477, 5902, 5377, 4899, 4464, 4067,
    3706, 3377, 3077, 2803, 2554, 2327, 2120, 1932]

def nfull(q): return N0 * (NMAX / N0) ** (q / 127.0)
def nfull_a(q): return NA0 * (NMAX / NA0) ** (q / 127.0)
def coef(k, n): return max(1, round((1 << 24) * (1.0 - math.exp(-k / n))))
C_A = [min(CMAX_A, coef(K_A, nfull_a(q))) for q in range(128)]
C_DR = [min(CMAX_DR, coef(K_DR, nfull(q))) for q in range(128)]
NQ8 = [round(nfull(q) * 256) for q in range(128)]
MQ8 = [0] + [round(math.sqrt(NQ8[q - 1] * NQ8[q])) for q in range(1, 128)]   # geometric midpoints
NQ8A = [round(nfull_a(q) * 256) for q in range(128)]
MQ8A = [0] + [round(math.sqrt(NQ8A[q - 1] * NQ8A[q])) for q in range(1, 128)]
def scale_u7(p, lo, hi): return lo + ((hi - lo) * p) // 127
SUS = [scale_u7(p, 0, 28000) for p in range(128)]
REL_MIN = PEAK // 100           # below 1 % of the peak a release is timed as a full sweep
DEC_GAP = PEAK // 100           # decay within 1 % of the peak: timed as a full sweep
F_DEC = round(65536 * K_DR / K_DEC)
F_REL = [65536 if s < REL_MIN else round(65536 * K_DR / math.log((s + REL_T) / REL_T)) for s in SUS]

# old step-A/step-B default end times (samples) -> pick nearest defaults
def lin_time(rate_q15, kind, s):
    d = PEAK if kind == 0 else (PEAK - s if kind == 1 else s)
    return d * 32768.0 / rate_q15
def rc_time(q, kind, s):
    if kind == 0: return nfull_a(q)
    n = nfull(q)
    if kind == 1: return n * K_DEC / K_DR
    return n * math.log((s + REL_T) / REL_T) / K_DR

def conv(rate_q15, kind, sus_pos):
    """Integer mirror of poli_preset.c pv_env_conv (no division)."""
    s = SUS[sus_pos & 127]
    if kind == 0: d, f = PEAK, 65536
    elif kind == 1: d, f = (PEAK - s, F_DEC) if s < PEAK - DEC_GAP else (PEAK, 65536)
    else: d, f = (s, F_REL[sus_pos & 127]) if s >= REL_MIN else (PEAK, 65536)
    b = d * f * 128
    m = MQ8A if kind == 0 else MQ8
    q = 0
    while q < 127 and m[q + 1] * rate_q15 < b: q += 1
    return q

def defaults():
    # step B defaults: atk 420, dec 50, rel 35 (levels/sample), sustain 14000
    s = 14000
    def best(kind, r):
        t = PEAK / r if kind == 0 else ((PEAK - s) / r if kind == 1 else s / r)
        tt = lambda q: nfull_a(q) if kind == 0 else (nfull(q) * K_DEC / K_DR if kind == 1 else nfull(q) * math.log((s + REL_T) / REL_T) / K_DR)
        return min(range(128), key=lambda q: abs(math.log(tt(q) / t)))
    return best(0, 420), best(1, 50), best(2, 35)

def text():
    pa, pd, pr = defaults()
    h = ["/* generated by tools/gen_poli_env_rc.py - do not edit. Curved envelope (V3). */",
         "#ifndef POLI_ENV_RC_H", "#define POLI_ENV_RC_H",
         f"#define POLI_EG_OVER {OVER}      /* attack aims here, clipped at the 26000 peak */",
         f"#define POLI_EG_REL_T {REL_T}       /* release aims at -26 and stops at exact 0 */",
         f"#define POLI_EG_CMAX_A {CMAX_A}   /* attack step <= 8000 levels */",
         f"#define POLI_EG_CMAX_DR {CMAX_DR}  /* decay/release step <= 8000 levels */",
         f"#define ENV_POS_ATK {pa}", f"#define ENV_POS_DEC {pd}", f"#define ENV_POS_REL {pr}",
         f"#define POLI_EG_CA0 {C_A[pa]}      /* == ENV_C_ATK[ENV_POS_ATK] */",
         f"#define POLI_EG_CD0 {C_DR[pd]}      /* == ENV_C_DR[ENV_POS_DEC] */",
         f"#define POLI_EG_CR0 {C_DR[pr]}      /* == ENV_C_DR[ENV_POS_REL] */",
         f"#define POLI_EG_LIN_A {round((1 << 24) * K_A / PEAK)}   /* old API: Q24 per level/sample, attack */",
         f"#define POLI_EG_LIN_DR {round((1 << 24) * K_DR / PEAK)}  /* old API: Q24 per level/sample, decay/release */",
         "#endif", ""]
    def arr(name, typ, v, per=8):
        o = [f"static const {typ} {name}[128] = {{"]
        for i in range(0, 128, per):
            o.append("    " + ", ".join(map(str, v[i:i + per])) + ("," if i + per < 128 else ""))
        o.append("};")
        return o
    i = ["/* generated by tools/gen_poli_env_rc.py - do not edit. Curved envelope (V3).",
         " * N(q) = 21*(441000/21)^(q/127) samples full sweep (0.48 ms .. 10.0 s at 44.1 kHz);",
         " * attack NA(q) = 5.5*(441000/5.5)^(q/127) (0.12 ms .. 10.0 s).",
         " * ENV_C_ATK: Q24 coefficient, attack toward 34000 reaches the 26000 peak in NA(q).",
         " * ENV_C_DR : Q24 coefficient, release 26000 -> exact 0 (target -26) in N(q);",
         " *            decay uses the same coefficient (26000 -> -60 dB of its distance in 0.99986 N(q)).",
         " * ENV_MQ8/ENV_MQ8_A: geometric midpoints of N(q)/NA(q) in 1/256 sample (preset migration).",
         " * ENV_LAW2_RATE (step A, levels/sample) and ENV_LAW3_RATE (step B, Q15): old knob laws,",
         " * kept only to convert saved law-2/law-3 presets. ENV_REL_F: Q16 time factor per sustain",
         " * position for the release-from-sustain equivalence. */"]
    i += arr("ENV_C_ATK", "uint32_t", C_A) + arr("ENV_C_DR", "uint32_t", C_DR)
    i += arr("ENV_MQ8", "uint32_t", MQ8) + arr("ENV_MQ8_A", "uint32_t", MQ8A)
    i += arr("ENV_LAW2_RATE", "uint16_t", LAW2, 16) + arr("ENV_LAW3_RATE", "uint32_t", LAW3)
    i += arr("ENV_REL_F", "uint32_t", F_REL)
    i += [f"#define ENV_F_DEC {F_DEC}u", f"#define ENV_REL_MIN {REL_MIN}", f"#define ENV_DEC_GAP {DEC_GAP}", ""]
    return "\n".join(h), "\n".join(i)

if __name__ == "__main__":
    a, b = text()
    if "--check" in sys.argv:
        ok = H.read_text() == a and INC.read_text() == b
        print("env rc tables up to date" if ok else "env rc tables STALE"); sys.exit(0 if ok else 1)
    H.write_text(a); INC.write_text(b)
    pa, pd, pr = defaults()
    print("defaults", pa, pd, pr, "CA", C_A[pa], "CD", C_DR[pd], "CR", C_DR[pr], "caps", CMAX_A, CMAX_DR)
    print("C_A[0..3]", C_A[:4], "C_A[127]", C_A[127], "C_DR[0..3]", C_DR[:4], "C_DR[127]", C_DR[127])
