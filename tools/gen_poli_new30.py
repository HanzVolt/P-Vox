#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""30 proposed new Polivoks factory presets, authored in law-5 panel positions.

These are NOT emitted into poli_presets_factory.inc: 80 slots do not fit in
the current 4 KiB preset-bank blobs (see REPORT). This tool only writes a
host-side table for safety renders and a JSON listing.

  tools/gen_poli_new30.py           -> host/pv_new30.inc, tools/new30.json
  tools/gen_poli_new30.py --check   exit 1 if stale
  tools/gen_poli_new30.py --json    print JSON
"""
import json, re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
GEN = ROOT / "tools/gen_poli_terminus.py"
OUT = ROOT / "host/pv_new30.inc"
JSON_OUT = ROOT / "tools/new30.json"

# law-5 panel ranges (same as gen_poli_presets.KNOBS)
KNOBS = [("cut", 0, 127, 59), ("res", 0, 127, 25), ("cont", 0, 127, 32), ("drive", 0, 127, 54),
         ("atk_f", 0, 127, 0), ("dec_f", 0, 127, 0), ("sus_f", 0, 127, 64), ("rel_f", 0, 127, 4),
         ("atk_a", 0, 127, 0), ("dec_a", 0, 127, 0), ("sus_a", 0, 127, 64), ("rel_a", 0, 127, 4),
         ("mod_cut", 0, 127, 4), ("mod_amp", 0, 127, 0), ("vib1", 0, 127, 0), ("vib2", 0, 127, 0),
         ("foot1", -2, 2, 0), ("foot2", -2, 2, 0), ("shape1", 0, 4, 0), ("shape2", 0, 4, 0),
         ("fm", 0, 127, 0), ("detune", -64, 64, 0), ("lv1", 0, 127, 127), ("lv2", 0, 127, 127),
         ("noise", 0, 127, 0), ("lfo_rate", 0, 127, 58), ("lfo_shape", 0, 3, 0),
         ("tune", -64, 64, 0), ("level", 0, 127, 127), ("glide", 0, 127, 5), ("duo", 0, 1, 0)]
SW = {"bp": 1, "rep_f": 2, "rep_a": 4, "link": 8, "glide_on": 16, "bass": 32, "modosc": 128}
INIT_SW = {"link": 1, "bass": 1, "modosc": 1}
SAW, TRI, SQ, P20, P9 = 1, 0, 2, 3, 4

def env(which, a, d, s, r):
    return {f"atk_{which}": a, f"dec_{which}": d, f"sus_{which}": s, f"rel_{which}": r}

def both(a, d, s, r):
    return dict(env("f", a, d, s, r), **env("a", a, d, s, r))

def P(name, desc, sw=None, **k):
    sw = dict(INIT_SW, **(sw or {}))
    for key in list(k):
        if key in SW:
            sw[key] = k.pop(key)
    return (name, desc, sw, k)

# Authored under current law-5 knob meanings (audible-range envelopes, octave cutoff).
# slot numbers 51-80 are the intended factory indices if/when the store is extended.
NEW = [
    P("Howl Lead",
      "A screaming low-pass saw lead: the resonance is near the top and the filter envelope shoves the cutoff open.",
      cut=82, res=118, cont=95, drive=100, **env("f", 8, 40, 80, 14), **env("a", 4, 25, 110, 16),
      shape1=SAW, detune=3, vib1=1, vib2=1, lfo_rate=62, duo=0),
    P("BP Howl",
      "The same kind of scream through the band-pass filter, thinner and more nasal, with a little vibrato.",
      {"bp": 1}, cut=74, res=122, cont=90, drive=95, **env("f", 6, 35, 75, 14), **env("a", 4, 22, 110, 16),
      shape1=SAW, detune=2, vib1=2, vib2=2, lfo_rate=64, duo=0),
    P("BP Рык",
      "A low band-pass growl bass with high resonance, a lot of contour, and a little FM grit. Bass support is on.",
      {"bp": 1}, cut=36, res=110, cont=105, drive=100, **env("f", 0, 28, 35, 10), **env("a", 0, 18, 105, 12),
      shape1=SAW, foot1=-2, foot2=-1, fm=28, duo=0),
    P("FM Snarl",
      "Generator II, a square an octave up, bites hard into a saw: a mid-range FM snarl. The generators are unlinked.",
      {"link": 0}, cut=52, res=85, cont=72, drive=100, fm=108, **env("f", 2, 30, 55, 12), **env("a", 2, 20, 110, 14),
      shape1=SAW, shape2=SQ, foot2=1, lv2=0, duo=0),
    P("Шум Взрыв",
      "A short band-pass noise blast: both generators muted, full noise, a fast filter snap. Percussive, not a drone.",
      {"bp": 1, "link": 1}, cut=102, res=72, cont=127, drive=70, **both(0, 8, 0, 6),
      lv1=0, lv2=0, noise=127, duo=0, bass=0),
    P("Drive Bass",
      "A heavy overdriven low-pass bass with bass support on, both generators down an octave, and a punchy decay.",
      cut=26, res=52, cont=82, drive=120, **env("f", 0, 22, 40, 10), **env("a", 0, 16, 110, 12),
      shape1=SAW, foot1=-2, foot2=-2, detune=2, duo=0),
    P("Raw Drive",
      "A similar overdriven bass but with bass support off, so the bottom is rawer and less supported.",
      {"bass": 0}, cut=30, res=68, cont=70, drive=118, **env("f", 0, 20, 45, 10), **env("a", 0, 16, 110, 12),
      shape1=SAW, foot1=-2, foot2=-1, detune=3, duo=0),
    P("LFO Stab",
      "A held stab whose cutoff is swept hard by a fast triangle LFO, so it chews in rhythm while the key is down.",
      cut=44, res=88, cont=18, drive=80, mod_cut=96, lfo_rate=88, **env("f", 0, 14, 70, 8), **env("a", 0, 10, 100, 8),
      shape1=SAW, detune=2, duo=0),
    P("F Repeat",
      "The filter envelope repeats while you hold a key, pumping a resonant low-pass bass in a steady rhythm.",
      {"rep_f": 1}, cut=38, res=98, cont=115, drive=75, **env("f", 18, 28, 0, 8), **env("a", 0, 16, 110, 10),
      shape1=SAW, foot1=-1, foot2=-1, duo=0),
    P("AD Repeat",
      "Filter and amp envelopes both repeat on a band-pass pulse, so the whole note comes back in a short loop.",
      {"bp": 1, "rep_f": 1, "rep_a": 1}, cut=58, res=78, cont=110, drive=70,
      **env("f", 16, 24, 0, 6), **env("a", 10, 20, 0, 6),
      shape1=SQ, foot2=1, lv2=80, duo=0, bass=0),
    P("Glide Drone",
      "A slow portamento drone: long attack, high sustain, two slightly detuned saws, glide on. Key-hold is not stored.",
      {"glide_on": 1}, cut=48, res=42, cont=28, drive=60, **env("f", 40, 50, 100, 36), **env("a", 48, 40, 120, 36),
      shape1=SAW, detune=8, duo=1, vib1=1, lfo_rate=36, glide=42),
    P("Duo Howl",
      "A detuned unison scream: DUO on, wide detune, high resonance, both generators full, low-pass.",
      cut=70, res=108, cont=80, drive=92, **env("f", 6, 32, 85, 16), **env("a", 4, 22, 115, 18),
      shape1=SAW, detune=14, duo=1, vib1=1, vib2=1, lfo_rate=58),
    P("Duo Grit",
      "A detuned unison bass with bass support on, overdrive, and a little FM so the two generators grind.",
      cut=28, res=64, cont=70, drive=108, fm=22, **env("f", 0, 24, 50, 12), **env("a", 0, 18, 115, 14),
      shape1=SAW, foot1=-1, foot2=-1, detune=18, duo=1),
    P("Harsh FM",
      "A harsher sync-like FM: square at +2 octaves modulating a saw, generator II muted in the mix, unlinked.",
      {"link": 0}, cut=66, res=58, cont=55, drive=88, fm=92, **env("f", 4, 28, 60, 14), **env("a", 4, 20, 115, 16),
      shape1=SAW, shape2=SQ, foot2=2, lv2=0, duo=0),
    P("Тревога",
      "An air-raid siren: slow wide vibrato on both generators, a high open cutoff, long sustain. Not the factory Сирена.",
      cut=88, res=48, cont=12, drive=54, **both(20, 30, 120, 22),
      shape1=SAW, vib1=48, vib2=48, lfo_rate=7, duo=0),
    P("Foundry",
      "An industrial metal hit: band-pass, a fast high-resonance snap, some FM and a dash of noise. Percussive.",
      {"bp": 1, "bass": 0}, cut=92, res=120, cont=127, drive=64, fm=48, noise=36,
      **env("f", 0, 6, 0, 6), **env("a", 0, 8, 0, 8),
      shape1=TRI, shape2=SQ, foot2=1, lv2=40, duo=0, link=0),
    P("Сталь",
      "A grinding band-pass industrial tone, mid resonance, mild FM, a slow cutoff LFO like a machine.",
      {"bp": 1, "bass": 0}, cut=50, res=102, cont=38, drive=90, fm=58, mod_cut=36, lfo_rate=18,
      **env("f", 8, 35, 80, 16), **env("a", 6, 24, 110, 16),
      shape1=SAW, shape2=SQ, foot2=0, duo=0, link=0),
    P("Acid Scream",
      "A harsher 303-style acid: cutoff almost closed, resonance near the stop, filter decay snaps it shut. Bass support on.",
      {"glide_on": 1}, cut=22, res=125, cont=120, drive=85, **env("f", 0, 32, 0, 10), **env("a", 0, 14, 110, 10),
      shape1=SAW, foot1=-1, foot2=-1, lv2=0, duo=0, glide=6),
    P("Formant",
      "A band-pass vocal-like growl: high resonance around a mid cutoff, narrow pulse, a little contour.",
      {"bp": 1}, cut=62, res=116, cont=68, drive=62, **env("f", 10, 36, 70, 16), **env("a", 6, 24, 110, 16),
      shape1=P20, detune=2, duo=0),
    P("Chaos FM",
      "FM and noise piled together on a band-pass: square into triangle, unlinked, bass support off, messy and mid-heavy.",
      {"bp": 1, "link": 0, "bass": 0}, cut=56, res=82, cont=60, drive=95, fm=112, noise=78,
      **env("f", 4, 26, 50, 12), **env("a", 2, 18, 110, 14),
      shape1=SQ, shape2=TRI, lv1=110, lv2=0, duo=0),
    P("Rage Wah",
      "An aggressive auto-wah: high resonance, a deep cutoff LFO, overdriven saw, bass support on.",
      cut=32, res=112, cont=14, drive=82, mod_cut=108, lfo_rate=46, **env("f", 0, 20, 80, 12), **env("a", 0, 16, 110, 12),
      shape1=SAW, detune=2, duo=0),
    P("Dirt Sub",
      "A dirty sub: both generators two octaves down, triangle, overdrive, a little noise, bass support on.",
      cut=14, res=18, cont=48, drive=112, noise=42, **env("f", 0, 18, 60, 12), **env("a", 0, 14, 120, 14),
      shape1=TRI, foot1=-2, foot2=-2, lv2=70, duo=0),
    P("Iron Bell",
      "An aggressive FM bell cluster through the band-pass: high cutoff, long metallic decay, wide detune, no sustain.",
      {"bp": 1, "link": 0, "bass": 0}, cut=96, res=38, cont=22, drive=50, fm=100, detune=32,
      **env("f", 0, 48, 0, 50), **env("a", 0, 52, 0, 54),
      shape1=TRI, shape2=TRI, foot2=2, lv2=0, duo=0),
    P("Монстр",
      "A portamento monster: glide on, low saws, overdrive, some FM, DUO detune, bass support on.",
      {"glide_on": 1}, cut=38, res=86, cont=72, drive=102, fm=42, **env("f", 8, 30, 70, 18), **env("a", 6, 22, 115, 20),
      shape1=SAW, foot1=-2, foot2=-1, detune=6, duo=1, glide=26),
    P("Pulse Rage",
      "A thin screaming 9% pulse, high resonance and contour, a little detune, low-pass. Bass support off.",
      {"link": 0, "bass": 0}, cut=78, res=104, cont=82, drive=90, **env("f", 4, 28, 75, 14), **env("a", 2, 20, 110, 16),
      shape1=P9, shape2=SAW, detune=4, lv2=50, duo=0),
    P("Org Stab",
      "An overdriven organ stab: two squares a fifth apart in footage, almost no resonance, shortish edges, lots of drive.",
      {"link": 1, "bass": 0}, cut=104, res=8, cont=0, drive=120, **env("f", 0, 10, 100, 8), **env("a", 0, 8, 115, 8),
      shape1=SQ, foot1=0, foot2=1, duo=0),
    P("BP Square",
      "A raspy band-pass square bass, high resonance, contour, overdrive, bass support off so it stays mid and raw.",
      {"bp": 1, "bass": 0}, cut=42, res=96, cont=88, drive=92, **env("f", 0, 24, 40, 10), **env("a", 0, 16, 110, 12),
      shape1=SQ, foot1=-1, foot2=-2, detune=2, duo=0),
    P("Станок",
      "A mechanical tremolo industrial: band-pass, fast amp LFO, some FM, mid resonance, like a machine on a shop floor.",
      {"bp": 1, "bass": 0}, cut=54, res=76, cont=30, drive=85, fm=32, mod_amp=84, lfo_rate=96,
      **env("f", 6, 28, 85, 14), **env("a", 4, 20, 110, 14),
      shape1=SQ, duo=0),
    P("SH Chaos",
      "Extreme sample-and-hold on the cutoff: very fast stepped LFO, high resonance, a little noise, band-pass, bass off.",
      {"bp": 1, "bass": 0}, cut=40, res=108, cont=18, drive=70, mod_cut=100, lfo_rate=110, lfo_shape=3, noise=22,
      **env("f", 4, 22, 80, 12), **env("a", 2, 16, 110, 12),
      shape1=SAW, duo=0),
    P("Feedback",
      "A howling low-pass almost at self-oscillation: resonance at the stop, a slow cutoff LFO, long sustain, bass off.",
      {"bass": 0}, cut=90, res=127, cont=36, drive=62, mod_cut=28, lfo_rate=22, **env("f", 22, 40, 100, 24), **env("a", 18, 30, 115, 24),
      shape1=TRI, lv2=0, duo=0),
]

def charset():
    s = GEN.read_text()
    src = re.search(r'NAME_CHARS = \((.*?)\)\n', s, re.S).group(1)
    return "".join(re.findall(r'"((?:[^"\\\\]|\\\\.)*)"', src))

def existing_names():
    # do not collide with the current 50 (gen_poli_presets.PRESETS)
    sys.path.insert(0, str(ROOT / "tools"))
    import gen_poli_presets as g
    return [n for n, _, _ in g.PRESETS]

def build():
    cs = charset()
    old = set(existing_names())
    out = []
    names = set()
    assert len(NEW) == 30, len(NEW)
    for name, desc, sw, k in NEW:
        assert 1 <= len(name) <= 12, name
        assert name not in names and name not in old, name
        names.add(name)
        for ch in name:
            assert ch in cs, (name, ch)
        unknown = set(k) - {n for n, *_ in KNOBS} - {"bass"}
        # bass may be in sw, not knobs; k should only be knobs
        unknown = set(k) - {n for n, *_ in KNOBS}
        assert not unknown, (name, unknown)
        k = dict(k)
        # sw may include bass=0 via P(..., bass=0) wrongly in k if passed as kw.
        # Our P() puts non-knob extras? We only pass bass in sw dict.
        if sw.get("link"):
            if "shape1" in k and "shape2" not in k:
                k["shape2"] = k.get("shape1", 0)
        vals = []
        pos = {}
        for kn, lo, hi, init in KNOBS:
            v = k.get(kn, init)
            assert lo <= v <= hi, (name, kn, v)
            pos[kn] = v
            vals.append(v - lo)
        bits = 0
        for s, on in sw.items():
            if on:
                bits |= SW[s]
        out.append({"name": name, "desc": desc, "code": [cs.index(c) for c in name.ljust(12)],
                    "sw": bits, "k": vals, "pos": pos,
                    "switch": {s: int(bool(v)) for s, v in sw.items()},
                    "bp": int(bool(sw.get("bp"))),
                    "bass": int(bool(sw.get("bass", 0) if "bass" in sw else INIT_SW["bass"]))})
    return out

def emit_text(tab):
    o = ["/* generated by tools/gen_poli_new30.py - do not edit.",
         " * 30 proposed law-5 factory presets (slots 51-80). Host safety renders only.",
         " * Not wired into the firmware store: 80 slots do not fit in a 4 KiB bank blob. */",
         "#define PV_NNEW 30",
         "static const pv_fact_t PV_NEW[PV_NNEW] = {"]
    for i, p in enumerate(tab):
        o.append("    {{" + ",".join(map(str, p["code"])) + "}, " + f"0x{p['sw']:02x}, {{" + ",".join(map(str, p["k"])) + "}}, "
                 + f"/* {i+51:02d} {p['name']} */")
    o.append("};")
    return "\n".join(o) + "\n"

def emit():
    tab = build()
    text = emit_text(tab)
    js = json.dumps(tab, ensure_ascii=False, indent=2)
    if "--json" in sys.argv:
        print(json.dumps(tab, ensure_ascii=False)); return 0
    if "--check" in sys.argv:
        ok = OUT.exists() and OUT.read_text() == text and JSON_OUT.exists() and JSON_OUT.read_text() == js + "\n"
        print("new30 up to date" if ok else "new30 STALE"); return 0 if ok else 1
    OUT.write_text(text)
    JSON_OUT.write_text(js + "\n")
    print(f"{len(tab)} new presets -> {OUT.relative_to(ROOT)}, {JSON_OUT.relative_to(ROOT)}")
    return 0

if __name__ == "__main__":
    sys.exit(emit())
