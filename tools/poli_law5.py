#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Law-5 envelope and cutoff maps (Hans, 2026-11-12).

Integer formulas must match firmware/src/poli_nav.c exactly.
round_half_up(n/d) for n>=0 is (n + d//2)//d; for odd d there is no exact half.
"""
import math

CUT_TOP = 25965   # last internal cut whose fcore 'a' is still below 24000 (a=23996)
                  # 25966 is the first cut that hits the cap (a computed 24001, clamped 24000)
CUT_BOT = 0       # 6.64 octaves below CUT_TOP is negative; clamped
FS = 44100.0
K_DR = math.log(26026.0 / 26.0)
K_DEC = math.log(1000.0)

def clamp127(n):
    if n < 0: return 0
    if n > 127: return 127
    return n

def old_atk(n):
    n = clamp127(n)
    return 44 + (n * 83 + 63) // 127

def old_dec(n):
    n = clamp127(n)
    return 50 + (n * 77 + 63) // 127

def old_rel(n):
    n = clamp127(n)
    if n <= 10:
        return 9 * n
    return 90 + ((n - 10) * 37 + 58) // 117

def cut_of(n):
    n = clamp127(n)
    return (n * CUT_TOP + 63) // 127

def scale_u7(p, lo, hi):
    p = clamp127(p)
    return lo + (hi - lo) * p // 127

def nfull(q):
    return 21.0 * (441000.0 / 21.0) ** (q / 127.0)

def nfull_a(q):
    return 5.5 * (441000.0 / 5.5) ** (q / 127.0)

def t_atk_ms(q):
    return nfull_a(q) * 1000.0 / FS

def t_dec_ms(q):
    return nfull(q) * K_DEC / K_DR * 1000.0 / FS

def t_rel_peak_ms(q):
    return nfull(q) * 1000.0 / FS

def t_rel_from_ms(q, sus_level):
    s = max(0, sus_level)
    return nfull(q) * math.log((s + 26.0) / 26.0) / K_DR * 1000.0 / FS

def fcore_a(cut):
    if cut < 0: cut = 0
    sh = cut >> 12
    if sh > 6: sh = 6
    frac = cut & 4095
    base = 280 << sh
    a = base + ((base * frac) >> 12)
    return 24000 if a > 24000 else a

def eff_cut(cut):
    """Octave-map position actually used after the sh>6 cap in poli_fcore.c."""
    if cut < 0: cut = 0
    if (cut >> 12) > 6:
        return (6 << 12) + (cut & 4095)
    return cut

def a_uncapped(cut):
    if cut < 0: cut = 0
    sh = cut >> 12
    if sh > 6: sh = 6
    frac = cut & 4095
    base = 280 << sh
    return base + ((base * frac) >> 12)

def floor_ceil_inv(exact, lo, hi):
    """exact is a real number; return unique candidates in lo..hi (floor and/or ceil)."""
    f = int(math.floor(exact))
    c = int(math.ceil(exact))
    out = []
    for n in (f, c):
        if n < lo: n = lo
        if n > hi: n = hi
        if n not in out:
            out.append(n)
    return out

def pick_env(old, stage):
    """stage 0=atk, 1=dec, 2=rel. Returns dict with new pos and reason.
    Log-ratio of time == distance in old-index space (geometric tables).
    Tie: keep the snappier (smaller old index / shorter time).
    Below the new floor: clamp to 0, cannot reproduce.
    """
    fwd = (old_atk, old_dec, old_rel)[stage]
    floor_old = (44, 50, None)[stage]
    name = ("attack", "decay", "release")[stage]
    if floor_old is not None and old < floor_old:
        n = 0
        q = fwd(n)
        return dict(new=n, old_eq=q, clamped=True, tie=False,
                    reason="below new floor (%s %d < %d): cannot be reproduced, clamped to new 0 (old %d)"
                    % (name, old, floor_old, q),
                    cand=[(0, q)])
    if stage == 0:
        exact = (old - 44) * 127 / 83.0
        cands = floor_ceil_inv(exact, 0, 127)
    elif stage == 1:
        exact = (old - 50) * 127 / 77.0
        cands = floor_ceil_inv(exact, 0, 127)
    else:
        if old <= 90:
            exact = old / 9.0
            cands = floor_ceil_inv(exact, 0, 10)
        else:
            exact = 10 + (old - 90) * 117 / 37.0
            cands = floor_ceil_inv(exact, 10, 127)
    scored = []
    for n in cands:
        q = fwd(n)
        scored.append((abs(q - old), q, n))  # log-time error, then old-eq, then new
    scored.sort(key=lambda t: (t[0], t[1], t[2]))  # smallest log-error, then snappier, then smaller new
    best = scored[0]
    tie = len(scored) > 1 and scored[0][0] == scored[1][0]
    n, q = best[2], best[1]
    if tie:
        reason = ("tie in log-time (|old-eq-old|=%d for new %s); kept snappier old-eq %d (new %d) rather than softer"
                  % (best[0], ",".join(str(s[2]) for s in scored), q, n))
    elif len(cands) == 1:
        reason = "exact inverse (new %d -> old-eq %d)" % (n, q)
    else:
        # describe both sides
        parts = ["new %d -> old-eq %d (err %d)" % (s[2], s[1], s[0]) for s in sorted(scored, key=lambda t: t[2])]
        reason = "floor/ceil of exact inverse: %s; picked smaller log-time error (new %d, old-eq %d)" % ("; ".join(parts), n, q)
    if stage == 2 and old <= 90 and old % 9 != 0:
        reason += "; release 0-10 only hits old multiples of 9 (old %d is not)" % old
    return dict(new=n, old_eq=q, clamped=False, tie=tie, reason=reason,
                cand=[(s[2], s[1]) for s in sorted(scored, key=lambda t: t[2])])

def pick_cut(old_pos):
    old_cut = scale_u7(old_pos, 0, 30000)
    eff = eff_cut(old_cut)
    exact = eff * 127 / float(CUT_TOP)
    cands = floor_ceil_inv(exact, 0, 127)
    scored = []
    for n in cands:
        c = cut_of(n)
        scored.append((abs(c - eff), c, n))
    scored.sort(key=lambda t: (t[0], t[1], t[2]))  # smaller cents, then lower cut, then smaller new
    best = scored[0]
    tie = len(scored) > 1 and scored[0][0] == scored[1][0]
    n, c = best[2], best[1]
    cents = best[0] * 1200.0 / 4096.0
    if tie:
        reason = "tie in cents; kept the lower cutoff (new %d, cut %d) so it does not open more than the original" % (n, c)
    elif len(cands) == 1:
        reason = "exact inverse (new %d -> cut %d)" % (n, c)
    else:
        parts = ["new %d -> cut %d (err %d steps, %.2f cents)"
                 % (s[2], s[1], s[0], s[0] * 1200.0 / 4096.0) for s in sorted(scored, key=lambda t: t[2])]
        reason = "floor/ceil of exact inverse: %s; picked smaller octave error (new %d)" % ("; ".join(parts), n)
    if old_cut != eff:
        reason += "; old raw cut %d folds to effective %d under the fcore sh>6 cap" % (old_cut, eff)
    return dict(new=n, new_cut=c, old_cut=old_cut, old_eff=eff, cents=cents,
                clamped=False, tie=tie, reason=reason,
                cand=[(s[2], s[1]) for s in sorted(scored, key=lambda t: t[2])])

def plateau_counts():
    out = {}
    for name, fn in (("attack", old_atk), ("decay", old_dec), ("release", old_rel)):
        vs = [fn(n) for n in range(128)]
        out[name] = dict(distinct=len(set(vs)), plateaus=128 - len(set(vs)),
                         values=vs)
    return out

def cutoff_verify():
    rows = []
    for c in range(25960, 25972):
        rows.append((c, a_uncapped(c), fcore_a(c)))
    span_steps = 6.64 * 4096
    bot_unclamped = CUT_TOP - span_steps
    return dict(rows=rows, first_cap=25966, last_below=25965, a_below=fcore_a(25965),
                a_cap=fcore_a(25966), a_uncap_cap=a_uncapped(25966),
                span_steps=span_steps, bot_unclamped=bot_unclamped,
                octaves_0_to_top=CUT_TOP / 4096.0)
