#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Expand the linked pi32v2 disassembly with the unchanged target_budget.py weights.

Does not modify the checker. Imports its regexes and constants. Nest x4,
depth cap 4 (weight 64), divide x9. A call or a tail-goto to a function
entry inherits the call site's loop depth. Straight-line callee bodies
therefore count inside the caller's loop. Internal branches are not calls.
An instruction is counted once, at NEST ** (depth - 1), depth capped at 4.

Primary root: polivoks_render. That is the per-sample sound path reached
by the tail goto in inst_polivoks. The ISR's own loop cost is reported
separately and is not added: the handoff left ISR inclusion unresolved,
and the call to inst_polivoks is once per audio half, outside those loops.

This number is a static count of the linked instructions, the same rule
as target_budget.py (both sides of a forward branch count). It is not a
cycle measurement and not a pass against a budget file.
"""
import importlib.util
import re
import sys
from pathlib import Path

CHECKER = Path("/home/ubuntu/polivoks-scratch/inputs/polivoks-combined-inputs-20261110/checker/target_budget.py")
EXPECTED = "80f18ee705c1682d30bf4fd87bfb08f68019a4d5d033a999050427616cdde424"


def load_checker():
    import hashlib
    digest = hashlib.sha256(CHECKER.read_bytes()).hexdigest()
    if digest != EXPECTED:
        raise SystemExit(f"checker hash {digest} != {EXPECTED}")
    spec = importlib.util.spec_from_file_location("target_budget_ref", CHECKER)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod, digest


def parse_all(path, tb):
    funcs = {}
    cur = None
    with open(path) as f:
        for line in f:
            m = tb.LABEL.match(line.strip())
            if m:
                name = m.group(1)
                if not name.startswith("."):
                    cur = funcs.setdefault(name, [])
                continue
            m = tb.INSN.match(line)
            if cur is not None and m and not m.group(3).lstrip().startswith("<"):
                cur.append((int(m.group(1), 16), m.group(3).strip()))
    return funcs


def local_depth(insns, tb):
    """Same span rule as target_budget.cost: depth is how many loop spans cover the address."""
    if not insns:
        return {}
    lo = insns[0][0]
    heads = {}
    for addr, text in insns:
        m = tb.TARGET.search(text)
        if m and lo <= int(m.group(1), 16) < addr:
            h = int(m.group(1), 16)
            heads[h] = max(heads.get(h, addr), addr)
    depth = {}
    for addr, _text in insns:
        d = sum(1 for h, end in heads.items() if h <= addr <= end)
        depth[addr] = min(d, tb.MAXD)
    return depth


EDGE = re.compile(r"^(call|goto)\s+-?\d+\s+<([^>]*?)\s*:\s*([0-9a-f]+)\s*>")
DIV = re.compile(r"= r\d+ / r\d+")


def expand(name, inherited, funcs, entry, tb, stack, lines, seen_sites):
    if name in stack:
        lines.append(f"CYCLE {' -> '.join(stack + [name])}")
        return 0
    insns = funcs.get(name) or []
    if not insns:
        lines.append(f"MISSING {name} from {' -> '.join(stack)}")
        return 0
    depth = local_depth(insns, tb)
    start, end = insns[0][0], insns[-1][0]
    total = 0
    added = 0
    for addr, text in insns:
        d_local = depth[addr]
        d = d_local + inherited
        if d > tb.MAXD:
            d = tb.MAXD
        if d >= 1:
            w = tb.NEST ** (d - 1)
            if DIV.search(text):
                w *= 1 + tb.DIV_W
            total += w
            added += 1
        m = EDGE.match(text)
        if not m:
            continue
        kind, _label, tgt_s = m.group(1), m.group(2), m.group(3)
        tgt = int(tgt_s, 16)
        # A goto whose target sits in this function is a branch, already
        # reflected by the loop-span rule. It is not a tail call.
        if start <= tgt <= end:
            continue
        callee = entry.get(tgt)
        if callee is None:
            lines.append(f"UNRESOLVED {name} {kind} -> {tgt:#x} not a function entry")
            continue
        site = (name, addr, callee)
        if site in seen_sites:
            continue
        seen_sites.add(site)
        sub = expand(callee, d if d_local or inherited else inherited,
                     funcs, entry, tb, stack + [name], lines, seen_sites)
        # d at the call is the depth the callee inherits. If the call itself
        # is outside every loop, d is 0 and straight-line callees add 0,
        # matching the checker. Pass that inherited depth, not a forced 1.
        # The recursive call above used a confused expression. Recomputed
        # below by replacing this — see expand_at.
        total += 0  # placeholder, real add is in expand_at
        _ = sub
    return total


def expand_at(name, inherited, funcs, entry, tb, stack, lines, seen_sites):
    if name in stack:
        lines.append(f"CYCLE {' -> '.join(stack + [name])}")
        return 0, []
    insns = funcs.get(name) or []
    if not insns:
        lines.append(f"MISSING {name}")
        return 0, []
    depth = local_depth(insns, tb)
    start, end = insns[0][0], insns[-1][0]
    total = 0
    own = 0
    divs = 0
    details = []
    for addr, text in insns:
        d_here = min(depth[addr] + inherited, tb.MAXD)
        if d_here >= 1:
            w = tb.NEST ** (d_here - 1)
            if DIV.search(text):
                w *= 1 + tb.DIV_W
                divs += 1
            total += w
            own += w
        m = EDGE.match(text)
        if not m:
            continue
        kind = m.group(1)
        tgt = int(m.group(3), 16)
        if start <= tgt <= end:
            continue
        callee = entry.get(tgt)
        if callee is None:
            lines.append(f"UNRESOLVED {name}+{addr:#x} {kind} -> {tgt:#x}")
            continue
        # Tail goto: the rest of this function is not reached.
        inherit = min(depth[addr] + inherited, tb.MAXD)
        sub, sub_details = expand_at(callee, inherit, funcs, entry, tb, stack + [name], lines, seen_sites)
        total += sub
        details.append((f"{name}+{addr:#x}", kind, callee, inherit, sub))
        details.extend(sub_details)
        if kind == "goto":
            break
    details.insert(0, (name, "body", name, inherited, own))
    return total, details


def main():
    tb, digest = load_checker()
    dis = Path(sys.argv[1] if len(sys.argv) > 1 else "build/volt.dis")
    funcs = parse_all(dis, tb)
    entry = {}
    for name, insns in funcs.items():
        if insns:
            entry[insns[0][0]] = name
    # Self-check: with no expansion, a function's loop cost matches the checker.
    for name in ("polivoks_render", "fm1_alnk0_irq", "poli_eg", "poli_fcore_tick", "poli_fcore_run"):
        ins = funcs.get(name)
        if not ins:
            print(f"self-check skip {name} (not in this link)")
            continue
        official = tb.cost(ins)["cost"]
        # depth-only total, no calls followed
        depth = local_depth(ins, tb)
        mine = 0
        for addr, text in ins:
            d = depth[addr]
            if d >= 1:
                w = tb.NEST ** (d - 1)
                if DIV.search(text):
                    w *= 1 + tb.DIV_W
                mine += w
        if mine != official:
            raise SystemExit(f"self-check {name}: adapter {mine} != checker {official}")
    lines = []
    total, details = expand_at("polivoks_render", 0, funcs, entry, tb, [], lines, set())
    isr_total, isr_details = expand_at("fm1_alnk0_irq", 0, funcs, entry, tb, [], lines, set())
    print(f"checker {CHECKER}")
    print(f"checker_sha256 {digest}")
    print(f"dis {dis}")
    print(f"weights nest {tb.NEST} max_depth {tb.MAXD} max_weight {tb.NEST ** (tb.MAXD - 1)} divide {1 + tb.DIV_W}")
    print("policy primary_root polivoks_render inherited_depth 0")
    print("policy instructions_in_loop_spans count, including untaken forward-branch sides, same as target_budget.py")
    print("policy call_and_tail_goto inherit the call site depth; internal goto is not a call")
    print("policy each call site expands the callee once; the call instruction stays in the caller")
    print("policy depth 0 (once per render, outside the sample loop) weighs 0, same as the checker")
    print("policy ISR loops are not added to the primary total")
    print(f"MEASURED_DSP_FULLPATH {total}")
    print("additive_ledger rows sum to MEASURED_DSP_FULLPATH; call edges point at the callee row and are not added again")
    body_sum = 0
    for site, kind, callee, inherit, added in details:
        if kind == "body":
            body_sum += added
            print(f"  ADD {site} inherit {inherit} cost {added}")
        else:
            print(f"  EDGE {site} {kind} {callee} inherit {inherit} subtree {added}")
    if body_sum != total:
        raise SystemExit(f"ledger sum {body_sum} != total {total}")
    if lines:
        print("notes")
        for ln in lines:
            print(" ", ln)
    print(f"checker_polivoks_render_unexpanded {tb.cost(funcs['polivoks_render'])['cost']}")
    print(f"checker_fm1_alnk0_irq {tb.cost(funcs['fm1_alnk0_irq'])['cost']}")
    print("ISR expansion is diagnostic. It is not the primary score.")
    print(f"ISR_ROOTED_EXPANSION {isr_total}")
    for site, kind, callee, inherit, added in isr_details:
        if kind != "body" or site in ("fm1_alnk0_irq", "inst_polivoks", "polivoks_render", "uac_render_start", "uac_tap"):
            print(f"  isr {site} {kind} {callee} inherit {inherit} cost {added}")
    # Where the ISR calls sit relative to its loops.
    depth = local_depth(funcs["fm1_alnk0_irq"], tb)
    print("ISR call depths (0 means once per audio half, not inside a sample loop):")
    for addr, text in funcs["fm1_alnk0_irq"]:
        if text.startswith("call") or text.startswith("goto"):
            print(f"  {addr:#x} depth {depth[addr]} {text}")
    print("NOT_A_CHECKER_PASS original FUNCS list still excludes this path")
    print("NOT_A_BUDGET there is no target_budget.txt entry; 590 is not a pass line for this image")
    if total > 590:
        print(f"ABOVE_590 measured {total} is greater than 590 under this policy")
    return 0


if __name__ == "__main__":
    sys.exit(main())
