#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Factory preset table for the Polivoks preset store (scratch test copy).

  tools/gen_poli_presets.py           -> firmware/src/poli_presets_factory.inc
  tools/gen_poli_presets.py --check   exit 1 if the include is stale
  tools/gen_poli_presets.py --json    print the table as JSON (host tests, reports)

Every value is a panel knob position of this firmware (poli_nav.c ranges). The
PRESETS below are written under knob-law 2 (step A envelope table, OLD_ENV_RATE).
Envelope step B (Hans, 2026-11-12) changed ENV_RATE, so the emitted table is
knob-law 3: every attack/decay/release position goes through LAW2_TO_3, the step B
position whose rate is closest (in log ratio, i.e. time ratio) to the step A rate.
Curved envelope V3 is knob-law 4. Law 5 (Hans, 2026-11-12) remaps the 0-127
attack, decay and release knobs (audible-range floors) and gives cutoff an
octave-linear map. The emitted table is law 5: law-4 positions (after the
2->3->4 time conversion) go through poli_law5.pick_env / pick_cut (floor/ceil
of the exact inverse, smallest log-time or cents error; below-floor attack/decay
clamp to 0). Slot 1 "Init" follows the remapped boot positions.
Slots 51-63 are authored directly in law 5 (current panel meanings) and are not
run through the 2->3->4->5 converter. These are DRAFT starting points written on a
computer. Nobody has listened to them on an FM-1. Slot 1 "Init" is the boot voice
positions of the merged build.
"""
import json, math, re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import gen_poli_env_rc as rc
import poli_law5 as law5
OUT = ROOT / "firmware/src/poli_presets_factory.inc"
GEN = ROOT / "tools/gen_poli_terminus.py"

# knob order = SLOT_CUT..SLOT_DUO (poli_nav.h); (name, lo, hi, init position)
KNOBS = [("cut", 0, 127, 51), ("res", 0, 127, 25), ("cont", 0, 127, 32), ("drive", 0, 127, 54),
         ("atk_f", 0, 127, 42), ("dec_f", 0, 127, 72), ("sus_f", 0, 127, 64), ("rel_f", 0, 127, 77),
         ("atk_a", 0, 127, 42), ("dec_a", 0, 127, 72), ("sus_a", 0, 127, 64), ("rel_a", 0, 127, 77),
         ("mod_cut", 0, 127, 4), ("mod_amp", 0, 127, 0), ("vib1", 0, 127, 0), ("vib2", 0, 127, 0),
         ("foot1", -2, 2, 0), ("foot2", -2, 2, 0), ("shape1", 0, 4, 0), ("shape2", 0, 4, 0),
         ("fm", 0, 127, 0), ("detune", -64, 64, 0), ("lv1", 0, 127, 127), ("lv2", 0, 127, 127),
         ("noise", 0, 127, 0), ("lfo_rate", 0, 127, 58), ("lfo_shape", 0, 3, 0),
         ("tune", -64, 64, 0), ("level", 0, 127, 127), ("glide", 0, 127, 5), ("duo", 0, 1, 1)]
# step A (knob-law 2) envelope table, as in the presets/brief-mute builds' poli_nav.c
OLD_ENV_RATE = [
    8000, 7453, 6944, 6470, 6028, 5616, 5232, 4875, 4542, 4231, 3942, 3673, 3422, 3188, 2970, 2768,
    2578, 2402, 2238, 2085, 1943, 1810, 1686, 1571, 1464, 1364, 1271, 1184, 1103, 1028, 957, 892,
    831, 774, 721, 672, 626, 583, 544, 506, 472, 440, 420, 382, 355, 331, 309, 288,
    268, 250, 233, 217, 202, 188, 175, 163, 152, 142, 132, 123, 115, 107, 99, 93,
    86, 80, 75, 70, 65, 61, 56, 53, 50, 46, 43, 40, 37, 35, 32, 30,
    28, 26, 24, 23, 21, 20, 18, 17, 16, 15, 14, 13, 12, 11, 10, 10,
    9, 8, 8, 7, 7, 6, 6, 5, 5, 5, 4, 4, 4, 4, 3, 3,
    3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1]
EGK = 15
ENV_KNOBS = ("atk_f", "dec_f", "rel_f", "atk_a", "dec_a", "rel_a")

def new_env_rate():
    return list(rc.LAW3)       # the step B ENV_RATE of the EnvB-Oct3 build

def law2_to_3():
    new = new_env_rate()
    out = []
    for o in OLD_ENV_RATE:
        tgt = o << EGK
        out.append(min(range(128), key=lambda q: (abs(math.log(new[q] / tgt)), q)))
    return out

# switch bits (poli_nav.c PSW_*); key hold is not stored
SW = {"bp": 1, "rep_f": 2, "rep_a": 4, "link": 8, "glide_on": 16, "bass": 32, "modosc": 128}
INIT_SW = {"link": 1, "bass": 1, "modosc": 1}
SAW, TRI, SQ, P20, P9 = 1, 0, 2, 3, 4

def env(which, a, d, s, r):
    return {f"atk_{which}": a, f"dec_{which}": d, f"sus_{which}": s, f"rel_{which}": r}

def P(name, sw=None, **k):
    return (name, dict(INIT_SW, **(sw or {})), k, False)

def P5(name, sw=None, **k):
    """Law-5 native preset: knob values are this firmware's panel positions."""
    sw = dict(INIT_SW, **(sw or {}))
    for key in list(k):
        if key in SW:
            sw[key] = k.pop(key)
    return (name, sw, k, True)

def both(a, d, s, r):
    return dict(env("f", a, d, s, r), **env("a", a, d, s, r))

PRESETS = [
    P("Init"),
    # basses
    P("Bass Podval", {"link": 1}, cut=30, res=45, cont=70, **env("f", 0, 92, 20, 80), **env("a", 0, 100, 100, 80),
      shape1=SAW, foot1=-1, foot2=-1, detune=2, duo=0),
    P("Бас Завод", cut=24, res=60, cont=85, drive=80, **env("f", 0, 88, 10, 70), **env("a", 0, 96, 90, 70),
      shape1=SQ, foot1=-1, foot2=-2, duo=0),
    P("Sub Bass", cut=20, res=10, cont=30, **env("f", 0, 80, 40, 80), **env("a", 10, 90, 110, 85),
      shape1=TRI, foot1=-2, foot2=-1, lv2=90, duo=0),
    P("Acid Polik", cut=22, res=95, cont=100, drive=70, **env("f", 0, 90, 0, 80), **env("a", 0, 95, 100, 70),
      shape1=SAW, foot1=-1, foot2=-1, lv2=0, duo=0, glide=8, sw={"glide_on": 1}),
    P("Pulse Bass", sw={"link": 0}, cut=34, res=35, cont=60, **env("f", 0, 85, 25, 75), **env("a", 0, 92, 100, 72),
      shape1=P20, shape2=SAW, foot1=-1, foot2=-1, detune=3, duo=0),
    P("FM Growl", cut=40, res=50, cont=55, drive=90, **env("f", 0, 95, 30, 80), **env("a", 0, 98, 105, 80),
      shape1=SAW, foot1=-1, foot2=0, fm=40, duo=0),
    P("Резина", cut=28, res=70, cont=75, **env("f", 20, 100, 40, 90), **env("a", 0, 100, 110, 88),
      shape1=SAW, foot1=-1, foot2=-1, detune=5, duo=0),
    P("Bass Pluck", cut=26, res=40, cont=95, **env("f", 0, 80, 0, 70), **env("a", 0, 90, 0, 85),
      shape1=SQ, foot1=-1, foot2=-1, duo=0),
    P("Duo Bass", cut=32, res=40, cont=70, **env("f", 0, 90, 25, 80), **env("a", 0, 96, 100, 78),
      shape1=SAW, foot1=-1, foot2=-1),
    # leads
    P("Saw Lead", cut=60, res=40, cont=50, **env("f", 30, 100, 70, 95), **env("a", 20, 100, 115, 100),
      shape1=SAW, detune=3, vib1=1, vib2=1, lfo_rate=66, duo=0),
    P("Square Lead", sw={"link": 1}, cut=55, res=30, cont=45, **env("f", 20, 98, 60, 95), **env("a", 20, 100, 115, 100),
      shape1=SQ, foot2=1, lv2=70, vib1=1, vib2=1, duo=0),
    P("Глиссандо", cut=58, res=45, cont=40, **both(20, 100, 90, 100), shape1=SAW, detune=2,
      glide=2, duo=0, sw={"glide_on": 1}),
    P("Sync-ish FM", cut=70, res=35, cont=40, **env("f", 0, 100, 60, 95), **env("a", 10, 100, 115, 100),
      shape1=SAW, shape2=SQ, foot2=1, fm=60, lv2=0, duo=0, sw={"link": 0}),
    P("Thin Pulse", sw={"link": 1}, cut=75, res=25, cont=30, **both(20, 100, 100, 100), shape1=P9, detune=4,
      vib1=1, vib2=1, duo=0),
    P("Cosmos Lead", cut=62, res=70, cont=55, **env("f", 60, 105, 60, 105), **env("a", 40, 100, 115, 105),
      shape1=SAW, foot2=1, detune=1, vib1=2, vib2=2, lfo_rate=60, glide=4, duo=0, sw={"glide_on": 1}),
    P("Солист", cut=66, res=50, cont=35, **both(30, 100, 100, 102), shape1=TRI, foot2=1, lv2=80, vib1=1,
      vib2=1, lfo_rate=70, duo=0),
    P("Duo Lead", cut=60, res=40, cont=45, **both(20, 100, 100, 100), shape1=SAW, detune=0),
    P("Screamer", cut=50, res=105, cont=60, drive=110, **env("f", 0, 100, 60, 95), **env("a", 0, 100, 115, 100),
      shape1=SAW, fm=20, duo=0),
    P("Whistle", cut=40, res=115, cont=20, **both(40, 100, 110, 104), shape1=TRI, lv1=40, lv2=0, vib1=1,
      lfo_rate=64, duo=0),
    # brass / keys / plucks
    P("Brass 1", cut=35, res=20, cont=80, **env("f", 85, 100, 70, 95), **env("a", 60, 100, 110, 95),
      shape1=SAW, detune=3, duo=0),
    P("Брасс Мягкий", cut=30, res=15, cont=70, **env("f", 95, 104, 80, 98), **env("a", 80, 100, 115, 98),
      shape1=SAW, foot2=-1, detune=2, duo=0),
    P("Organ Pulse", sw={"link": 1}, cut=90, res=0, cont=0, **env("a", 0, 80, 127, 80), **env("f", 0, 72, 64, 77),
      shape1=SQ, foot2=1, lv2=90, duo=0),
    P("Clavi", sw={"link": 0}, cut=45, res=30, cont=90, **env("f", 0, 85, 10, 70), **env("a", 0, 92, 0, 80),
      shape1=P20, shape2=P9, foot2=1, lv2=80, duo=0),
    P("Pluck", cut=30, res=55, cont=100, **env("f", 0, 88, 0, 80), **env("a", 0, 95, 0, 92), shape1=SAW,
      detune=3, duo=0),
    P("Щипок", cut=36, res=75, cont=90, **env("f", 0, 84, 0, 75), **env("a", 0, 92, 0, 88), shape1=SQ,
      foot2=1, lv2=60, duo=0),
    P("Harpsi", sw={"link": 0}, cut=60, res=20, cont=60, **env("f", 0, 92, 0, 90), **env("a", 0, 100, 0, 98),
      shape1=P9, shape2=SAW, foot2=1, lv2=70, duo=0),
    P("Marimba-ish", cut=40, res=40, cont=70, **env("f", 0, 80, 0, 80), **env("a", 0, 90, 0, 86), shape1=TRI,
      foot2=2, lv2=40, duo=0),
    P("Bell FM", cut=80, res=20, cont=20, **env("f", 0, 104, 30, 108), **env("a", 0, 110, 0, 112),
      shape1=TRI, shape2=TRI, foot2=1, fm=70, lv2=0, detune=20, duo=0),
    P("Duo Keys", cut=50, res=25, cont=60, **env("f", 0, 95, 40, 92), **env("a", 0, 100, 90, 95), shape1=SQ),
    # pads, strings, slow
    P("Slow Pad", cut=40, res=20, cont=50, **env("f", 118, 110, 80, 115), **env("a", 120, 110, 120, 118),
      shape1=SAW, detune=4, vib1=1, lfo_rate=40, duo=0),
    P("Струнные", cut=55, res=10, cont=30, **env("f", 112, 110, 90, 112), **env("a", 116, 110, 120, 116),
      shape1=SAW, detune=6, vib1=1, vib2=1, lfo_rate=60, duo=0),
    P("Choir-ish", sw={"link": 1, "bp": 1}, cut=55, res=60, cont=20, **both(116, 110, 110, 116), shape1=P20,
      detune=5, vib1=1, lfo_rate=56, duo=0),
    P("Sweep Pad", cut=20, res=70, cont=60, mod_cut=30, lfo_rate=4, **env("f", 120, 115, 90, 118),
      **env("a", 118, 110, 120, 118), shape1=SAW, detune=4, duo=0),
    P("Glass Pad", cut=75, res=50, cont=10, **both(116, 110, 115, 118), shape1=TRI, foot2=1, detune=2,
      mod_amp=10, lfo_rate=30, duo=0),
    P("Swell", cut=35, res=30, cont=90, **env("f", 124, 110, 60, 110), **env("a", 120, 110, 115, 112),
      shape1=SAW, foot2=-1, detune=3, duo=0),
    # modulation, effects, percussion
    P("Wah LFO", cut=35, res=80, cont=10, mod_cut=45, lfo_rate=10, **both(20, 100, 115, 95), shape1=SAW,
      detune=2, duo=0),
    P("Tremolo", cut=60, res=20, cont=20, mod_amp=40, lfo_rate=60, **both(10, 100, 115, 95), shape1=SQ,
      foot2=1, lv2=80, duo=0),
    P("Сирена", cut=70, res=40, cont=10, vib1=25, vib2=25, lfo_rate=4, **both(30, 100, 120, 105), shape1=SAW,
      duo=0),
    P("Sample Hold", cut=30, res=90, cont=10, mod_cut=50, lfo_rate=70, lfo_shape=3, **both(10, 100, 115, 95),
      shape1=SAW, duo=0),
    P("Noise Wind", cut=40, res=100, cont=10, mod_cut=40, lfo_rate=3, lv1=0, lv2=0, noise=127,
      **both(120, 110, 120, 118), duo=0),
    P("Kick", cut=18, res=20, cont=127, **env("f", 0, 70, 0, 60), **env("a", 0, 88, 0, 80), shape1=TRI,
      foot1=-2, foot2=-2, lv2=0, duo=0),
    P("Snare", sw={"bp": 1, "link": 1}, cut=70, res=30, cont=40, lv1=50, lv2=0, noise=127,
      **env("f", 0, 75, 0, 70), **env("a", 0, 86, 0, 82), shape1=TRI, duo=0),
    P("Hat", sw={"bp": 1, "link": 1}, cut=120, res=60, cont=0, lv1=0, lv2=0, noise=127,
      **env("f", 0, 60, 0, 60), **env("a", 0, 74, 0, 70), duo=0),
    P("Zap", cut=90, res=90, cont=127, **env("f", 0, 82, 0, 80), **env("a", 0, 90, 0, 88), shape1=SAW,
      fm=30, duo=0),
    P("Repeat Pulse", sw={"rep_f": 1, "rep_a": 1, "link": 1}, cut=30, res=60, cont=90, **both(80, 85, 0, 80),
      shape1=SQ, foot2=1, lv2=60, duo=0),
    P("Повтор Бас", sw={"rep_f": 1, "link": 1}, cut=26, res=50, cont=80, **env("f", 60, 88, 0, 80),
      **env("a", 0, 96, 110, 80), shape1=SAW, foot1=-1, foot2=-1, duo=0),
    P("Терция", cut=55, res=30, cont=40, **both(20, 100, 110, 100), shape1=SAW, detune=64, duo=0),
    P("Robot FM", sw={"link": 0}, cut=60, res=60, cont=30, fm=90, **both(0, 100, 115, 95), shape1=SQ,
      shape2=TRI, foot2=-1, lv2=0, duo=0),
    P("Космос", cut=45, res=95, cont=50, mod_cut=20, vib1=3, vib2=4, lfo_rate=20, noise=30,
      **env("f", 110, 110, 60, 118), **env("a", 110, 110, 115, 120), shape1=SAW, detune=7, glide=3,
      duo=0, sw={"glide_on": 1}),
]

# Slots 51-63: law-5 native, selected from the 30 proposed in gen_poli_new30.py.
# Defaults for omitted knobs are this firmware's Init (law-5) positions.
NEW_PRESETS = [
    P5("Howl Lead", cut=82, res=118, cont=95, drive=100, **env("f", 8, 40, 80, 14), **env("a", 4, 25, 110, 16),
       shape1=SAW, detune=3, vib1=1, vib2=1, lfo_rate=62, duo=0),
    P5("BP Рык", {"bp": 1}, cut=36, res=110, cont=105, drive=100, **env("f", 0, 28, 35, 10), **env("a", 0, 18, 105, 12),
       shape1=SAW, foot1=-2, foot2=-1, fm=28, duo=0),
    P5("FM Snarl", {"link": 0}, cut=52, res=85, cont=72, drive=100, fm=108, **env("f", 2, 30, 55, 12), **env("a", 2, 20, 110, 14),
       shape1=SAW, shape2=SQ, foot2=1, lv2=0, duo=0),
    P5("Шум Взрыв", {"bp": 1, "link": 1}, cut=102, res=72, cont=127, drive=70, **both(0, 8, 0, 6),
       lv1=0, lv2=0, noise=127, duo=0, bass=0),
    P5("Drive Bass", cut=26, res=52, cont=82, drive=120, **env("f", 0, 22, 40, 10), **env("a", 0, 16, 110, 12),
       shape1=SAW, foot1=-2, foot2=-2, detune=2, duo=0),
    P5("F Repeat", {"rep_f": 1}, cut=38, res=98, cont=115, drive=75, **env("f", 18, 28, 0, 8), **env("a", 0, 16, 110, 10),
       shape1=SAW, foot1=-1, foot2=-1, duo=0),
    P5("Duo Howl", cut=70, res=108, cont=80, drive=92, **env("f", 6, 32, 85, 16), **env("a", 4, 22, 115, 18),
       shape1=SAW, detune=14, duo=1, vib1=1, vib2=1, lfo_rate=58),
    P5("Тревога", cut=88, res=48, cont=12, drive=54, **both(20, 30, 120, 22),
       shape1=SAW, vib1=48, vib2=48, lfo_rate=7, duo=0),
    P5("Foundry", {"bp": 1, "link": 0, "bass": 0}, cut=92, res=120, cont=127, drive=64, fm=48, noise=36,
       **env("f", 0, 6, 0, 6), **env("a", 0, 8, 0, 8),
       shape1=TRI, shape2=SQ, foot2=1, lv2=40, duo=0),
    P5("Acid Scream", {"glide_on": 1}, cut=22, res=125, cont=120, drive=85, **env("f", 0, 32, 0, 10), **env("a", 0, 14, 110, 10),
       shape1=SAW, foot1=-1, foot2=-1, lv2=0, duo=0, glide=6),
    P5("Chaos FM", {"bp": 1, "link": 0, "bass": 0}, cut=56, res=82, cont=60, drive=95, fm=112, noise=78,
       **env("f", 4, 26, 50, 12), **env("a", 2, 18, 110, 14),
       shape1=SQ, shape2=TRI, lv1=110, lv2=0, duo=0),
    P5("Монстр", {"glide_on": 1}, cut=38, res=86, cont=72, drive=102, fm=42, **env("f", 8, 30, 70, 18), **env("a", 6, 22, 115, 20),
       shape1=SAW, foot1=-2, foot2=-1, detune=6, duo=1, glide=26),
    P5("SH Chaos", {"bp": 1, "bass": 0}, cut=40, res=108, cont=18, drive=70, mod_cut=100, lfo_rate=110, lfo_shape=3, noise=22,
       **env("f", 4, 22, 80, 12), **env("a", 2, 16, 110, 12),
       shape1=SAW, duo=0),
]

# law-5 Init defaults for omitted knobs on NEW_PRESETS (KNOBS inits are law-2 era)
LAW5_INIT = {"cut": 59, "res": 25, "cont": 32, "drive": 54,
             "atk_f": 0, "dec_f": 0, "sus_f": 64, "rel_f": 4,
             "atk_a": 0, "dec_a": 0, "sus_a": 64, "rel_a": 4,
             "mod_cut": 4, "mod_amp": 0, "vib1": 0, "vib2": 0,
             "foot1": 0, "foot2": 0, "shape1": 0, "shape2": 0,
             "fm": 0, "detune": 0, "lv1": 127, "lv2": 127, "noise": 0,
             "lfo_rate": 58, "lfo_shape": 0, "tune": 0, "level": 127, "glide": 5, "duo": 0}



def charset():
    s = GEN.read_text()
    src = re.search(r'NAME_CHARS = \((.*?)\)\n', s, re.S).group(1)
    return "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', src))

def build():
    cs = charset()
    l23 = law2_to_3()
    out = []
    assert len(PRESETS) == 50, len(PRESETS)
    assert len(NEW_PRESETS) == 13, len(NEW_PRESETS)
    names = set()
    for name, sw, k, is5 in list(PRESETS) + list(NEW_PRESETS):
        assert 1 <= len(name) <= 12, name
        assert name not in names, name
        names.add(name)
        for ch in name: assert ch in cs, (name, ch)
        unknown = set(k) - {n for n, *_ in KNOBS}
        assert not unknown, (name, unknown)
        k = dict(k)
        if sw.get("link"):          # linked: generator II's shape follows generator I (what the panel shows)
            if "shape2" not in k:
                k["shape2"] = k.get("shape1", 0)
        vals = []
        if is5:
            pos5 = {}
            for kn, lo, hi, init in KNOBS:
                v = k.get(kn, LAW5_INIT[kn])
                assert lo <= v <= hi, (name, kn, v)
                pos5[kn] = v
                vals.append(v - lo)
            pos4 = pos3 = law2 = dict(pos5)  # no older-law origin
        else:
            law2 = {kn: k.get(kn, init) for kn, lo, hi, init in KNOBS}
            pos3 = {}
            for kn, lo, hi, init in KNOBS:
                v = law2[kn]
                assert lo <= v <= hi, (name, kn, v)
                if kn in ENV_KNOBS: v = l23[v]       # knob-law 2 -> 3 (step B)
                pos3[kn] = v
            pos4 = dict(pos3)
            for kn in ENV_KNOBS:                     # knob-law 3 -> 4 (curved V3)
                st, w = kn.split("_")
                kind = {"atk": 0, "dec": 1, "rel": 2}[st]
                pos4[kn] = rc.conv(rc.LAW3[pos3[kn]], kind, pos3["sus_" + w])
            pos5 = dict(pos4)
            for kn in ENV_KNOBS:                     # knob-law 4 -> 5 (audible-range remap)
                st, w = kn.split("_")
                kind = {"atk": 0, "dec": 1, "rel": 2}[st]
                pos5[kn] = law5.pick_env(pos4[kn], kind)["new"]
            pos5["cut"] = law5.pick_cut(pos4["cut"])["new"]
            for kn, lo, hi, init in KNOBS:
                vals.append(pos5[kn] - lo)
        bits = 0
        for s, on in sw.items():
            if on: bits |= SW[s]
        out.append({"name": name, "code": [cs.index(c) for c in name.ljust(12)], "sw": bits, "k": vals,
                    "pos": pos5, "pos_law4": pos4, "pos_law3": pos3, "pos_law2": law2,
                    "law5_native": bool(is5),
                    "switch": {s: int(bool(v)) for s, v in sw.items()}})
    return out

def emit():
    tab = build()
    o = ["/* generated by tools/gen_poli_presets.py - do not edit.",
         " * 63 factory presets: knob positions (SLOT_CUT..SLOT_DUO, signed ones offset to 0..),",
         " * switch byte, name codes (poli_name_font.inc order). Knob law 5 (audible-range",
         " * envelope remap + octave cutoff): written under law 2, mapped to law 3, to law 4",
         " * by closest end time (gen_poli_env_rc.conv), then to law 5 by poli_law5 (floor/ceil",
         " * inverse, log-time / cents). Slots 51-63 are authored in law 5 directly.",
         " * DRAFT sounds; not auditioned on an FM-1. */",
         "#define PV_NFACT 63",
         "static const pv_fact_t PV_FACTORY[PV_NFACT] = {"]
    for i, p in enumerate(tab):
        o.append("    {{" + ",".join(map(str, p["code"])) + "}, " + f"0x{p['sw']:02x}, {{" + ",".join(map(str, p["k"])) + "}}, "
                 + f"/* {i+1:02d} {p['name']} */")
    o.append("};")
    text = "\n".join(o) + "\n"
    # host-only: the original 50 presets as the EnvB-Oct3 build shipped them (law 3), for the migration test
    orig = tab[:50]
    h = ["/* generated by tools/gen_poli_presets.py - do not edit. Host tests only.",
         " * The original 50 factory knob records under knob-law 3 (step B), as in the EnvB-Oct3 build.",
         " * Slots 51-63 have no law-3 origin (authored in law 5). */",
         "static const uint8_t PV_FACTORY_LAW3[50][31] = {"]
    for i, p in enumerate(orig):
        v = [p["pos_law3"][kn] - lo for kn, lo, hi, init in KNOBS]
        h.append("    {" + ",".join(map(str, v)) + "}" + ("," if i < 49 else ""))
    h.append("};")
    htext = "\n".join(h) + "\n"
    HOUT = ROOT / "host/pv_factory_law3.inc"
    h4 = ["/* generated by tools/gen_poli_presets.py - do not edit. Host tests only.",
          " * The original 50 factory knob records under knob-law 4 (curved V3, linear cutoff),",
          " * as in the EnvRC / Fix1 builds, before the law-5 remap.",
          " * Slots 51-63 have no law-4 origin (authored in law 5). */",
          "static const uint8_t PV_FACTORY_LAW4[50][31] = {"]
    for i, p in enumerate(orig):
        v = [p["pos_law4"][kn] - lo for kn, lo, hi, init in KNOBS]
        h4.append("    {" + ",".join(map(str, v)) + "}" + ("," if i < 49 else ""))
    h4.append("};")
    h4text = "\n".join(h4) + "\n"
    H4OUT = ROOT / "host/pv_factory_law4.inc"
    if "--json" in sys.argv:
        print(json.dumps(tab, ensure_ascii=False)); return 0
    if "--check" in sys.argv:
        ok = (OUT.exists() and OUT.read_text() == text and HOUT.exists() and HOUT.read_text() == htext
              and H4OUT.exists() and H4OUT.read_text() == h4text)
        print("factory presets up to date" if ok else "factory presets STALE"); return 0 if ok else 1
    OUT.write_text(text)
    HOUT.write_text(htext)
    H4OUT.write_text(h4text)
    print(f"{len(tab)} presets -> {OUT.relative_to(ROOT)} ({len(tab) * 44} B), law3+law4 host tables")
    return 0

if __name__ == "__main__":
    sys.exit(emit())
