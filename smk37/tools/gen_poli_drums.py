#!/usr/bin/env python3
"""Wander-Drift (16 Nov 2026): the drum patterns of the ADDED drum track, as readable 16-step grids -> C table
(firmware/src/poli_drum_pat.inc). Written from scratch from genre conventions (no machine's or firmware's patterns
copied). 'x' = hit, '.' = none; step 1 is the first character (beat 1). ROLL: '2' = the closed hat plays 2 hits in
that 16th (32nds), '3' = 3 hits (a triplet roll). BELL: the cowbell's pitch per step in semitones (hex digit, '.' = 0).
KIT: 0 = 808 family, 1 = 909 family (the default; GROOVE K4 KIT can override). LONG: the 808 long-decay sub kick.
Usage: gen_poli_drums.py [--check]"""
import sys, os
LANES = ['KICK', 'SNARE', 'GHOST', 'CLAP', 'CH', 'OH', 'LT', 'MT', 'HT', 'RIM', 'COWB', 'CLAVE', 'LC', 'MC', 'HC', 'RIDE', 'CRASH']
def hx(v): return ''.join('x' if (v >> i) & 1 else '.' for i in range(16))
P = []
def pat(name, kit, long=0, **lanes): P.append((name, kit, long, lanes))
# 1-8: the Core2-FxSeq patterns, unchanged hits (kick / snare / closed hat), on the 909 family (8: the 808)
for i, (k, s, h) in enumerate([(0x1111, 0x1010, 0x5555), (0x1111, 0x1010, 0xFFFF), (0x0505, 0x1010, 0x5555),
                               (0x2121, 0x1010, 0x4444), (0x0929, 0x9090, 0x5555), (0x8421, 0x1010, 0xAAAA),
                               (0x1111, 0x0000, 0x4444), (0x0101, 0x1010, 0x0000)]):
    pat(f'{i + 1}', 0 if i == 7 else 1, KICK=hx(k), SNARE=hx(s), CH=hx(h))
# HOUSE (120-128 BPM): four on the floor, clap on 2 and 4, open hat on every off-beat 8th; 909
pat('HOUSE1', 1, KICK='x...x...x...x...', CLAP='....x.......x...', OH='..x...x...x...x.')
pat('HOUSE2', 1, KICK='x...x...x...x...', CLAP='....x.......x...', CH='xx.xxx.xxx.xxx.x', OH='..x...x...x...x.')
pat('HOUSE3', 1, KICK='x...x...x...x...', CLAP='....x.......x...', SNARE='............x...', OH='..x...x...x...x.',
    RIDE='x.x.x.x.x.x.x.x.', RIM='...x.....x....x.')
pat('HOUSE4', 1, KICK='x...x...x...x...', CLAP='....x.......x..x', CH='x...x...x...x...',
    OH='..x...x...x...x.', GHOST='...........x....', HC='.....x.....x.x..', LC='...x.......x....')
# DUBSTEP (about 140 BPM, half time): snare + clap on beat 3 only, sparse kick; 909 (2, 4) and 808 (1, 3)
pat('DUBST1', 0, long=1, KICK='x.........x.....', SNARE='........x.......', CLAP='........x.......', CH='..x...x...x...x.')
pat('DUBST2', 1, KICK='x.....x...x.....', SNARE='........x.......', CH='x.x.x.x.x.x.x.x.', ROLL='..............2.')
pat('DUBST3', 0, KICK='x..x......x..x..', SNARE='........x.......', CLAP='........x.......', GHOST='..............x.',
    CH='....x.......x...', OH='......x.........', LT='.............x..', CRASH='x...............')
pat('DUBST4', 1, KICK='x.........x.x...', SNARE='........x.......', CH='x.xx.x.xx.xx.x.x',
    OH='..............x.', RIM='...x.......x....')
# DRUM AND BASS (set TEMPO about 170-174): two-step kick 1 and the 'and' of 3, snare 2 and 4, ride / hats; 909
pat('DNB1', 1, KICK='x.........x.....', SNARE='....x.......x...', CH='x.x.x.x.x.x.x.x.')
pat('DNB2', 1, KICK='x.........x.....', SNARE='....x.......x...', GHOST='.......x.x....x.', RIDE='x.x.x.x.x.x.x.x.')
pat('DNB3', 1, KICK='x.x.......x.....', SNARE='....x......xx...', GHOST='.x.....x.....x.x', CH='x.x.x.x.x.x.x...',
    OH='..............x.')
pat('DNB4', 1, KICK='x......x..x.....', SNARE='....x.......x...', CH='xxxxxxxxxxxxxxxx', RIDE='x.......x.......',
    CRASH='x...............')
# ---- Drums-12 (21 Nov 2026): 12 per pack. TRAP and PHONK packs removed (their kit flavours stay on GROOVE KIT).
# Patterns 5-12 of each pack, and TECHNO1-12, hand-written here from genre conventions (no copied patterns). Each keeps
# its genre's kernel and adds material on top: HOUSE four on the floor + clap 2/4 + off-beat open hats; DUBST half
# time, snare on beat 3, never four on the floor; DNB two-step, snare 2/4, kick 1 and the 'and' of 3; TECHNO four on
# the floor (clap or snare 2/4 and off-beat open hats on all but TECHNO6).
HK, HC, HO = 'x...x...x...x...', '....x.......x...', '..x...x...x...x.'
pat('HOUSE5', 1, KICK='x...x...x..xx...', CLAP=HC, OH=HO, CH='x...x...x...x...')
pat('HOUSE6', 1, KICK=HK, CLAP=HC, OH=HO, CH='xx.xxx.xxx.xxx.x', ROLL='...............2')
pat('HOUSE7', 1, KICK=HK, CLAP=HC, OH=HO, RIM='...x.......x....', COWB='.......x.......x')
pat('HOUSE8', 1, KICK=HK, CLAP=HC, OH=HO, RIDE='x.x.x.x.x.x.x.x.', HT='...........x....', LT='.............x..')
pat('HOUSE9', 1, KICK=HK, CLAP=HC, OH=HO, CH='x...x...x...x...', LC='...x.....x......', MC='.....x.......x..',
    HC='.x.......x.x...x')
pat('HOUSE10', 1, KICK=HK, CLAP=HC, SNARE=HC, OH=HO, CH='xx.xxx.xxx.xxx.x', CRASH='x...............')
pat('HOUSE11', 1, KICK=HK, CLAP=HC, OH=HO, GHOST='...x.......x...x', RIM='.....x.......x..')
pat('HOUSE12', 1, KICK=HK, CLAP=HC, OH=HO, CLAVE='x..x..x...x..x..', LC='.x.....x.....x..', HC='...x.....x.x....',
    RIM='.......x.......x')
DS = '........x.......'
pat('DUBST5', 0, long=1, KICK='x.....x.......x.', SNARE=DS, CLAP=DS, CH='....x.......x...')
pat('DUBST6', 1, KICK='x...............', SNARE=DS, CRASH='x...............', CH='..............x.')
pat('DUBST7', 1, KICK='x......x..x.....', SNARE=DS, CLAP=DS, CH='x.x.x.x.x.x.xx.x')
pat('DUBST8', 1, KICK='x.........x..x..', SNARE=DS, CH='x.x...x.x.x...xx', ROLL='..............3.')
pat('DUBST9', 0, long=1, KICK='x.........x.....', SNARE=DS, GHOST='.....x.x........', CH='..x...x...x...x.')
pat('DUBST10', 0, KICK='x..x.......x.x..', SNARE='........xx......', CLAP=DS, CH='x...x...x...x...')
pat('DUBST11', 1, KICK='x.........x.....', SNARE=DS, CH='x.x.x.x.x.x.x...', HT='.............x..',
    MT='..............x.', LT='...............x')
pat('DUBST12', 0, KICK='x............x..', SNARE=DS, CLAP=DS, CLAVE='...x...x...x...x', RIM='......x.......x.')
NK, NS = 'x.........x.....', '....x.......x...'
pat('DNB5', 1, KICK=NK, SNARE=NS, CH='xxxxxxxxxxxxxxxx', GHOST='.......x......x.')
pat('DNB6', 1, KICK='x.....x...x..x..', SNARE=NS, RIDE='x.x.x.x.x.x.x.x.', CH='..x...x...x...x.')
pat('DNB7', 1, KICK=NK, SNARE=NS, GHOST='.x.....x.x....xx', CH='x.x.x...x.x.x...', OH='......x.......x.')
pat('DNB8', 1, KICK='x......x..x.....', SNARE='....x.......x.xx', CH='xxxxxxxxxxxxxxxx', ROLL='...........3....')
pat('DNB9', 1, KICK='x.........x..x..', SNARE=NS, RIDE='x.x.x.x.x.x.x.x.', GHOST='.......x........')
pat('DNB10', 1, KICK=NK, SNARE=NS, GHOST='..x....x.x.x..x.', CH='x.x.x.x.x.x.x.x.', CRASH='x...............')
pat('DNB11', 1, KICK='x.........x..x..', SNARE=NS, CH='x...x...xxxxxxxx', GHOST='.........x...x.x')
pat('DNB12', 1, KICK='x.x.......x..x..', SNARE=NS, CH='xxxxxxxxxxxxxxxx', RIDE='x...x...x...x...',
    CRASH='x...............', GHOST='.......x......x.')
# TECHNO (130 BPM): four on the floor on the 909 family; AUTO kit = the TECHNO flavour (the lower kick)
pat('TECHNO1', 1, KICK=HK, CLAP=HC, OH=HO, CH='.x...x...x...x..')
pat('TECHNO2', 1, KICK=HK, CLAP=HC, OH=HO, CH='xx.xxx.xxx.xxx.x', RIM='.......x.......x')
pat('TECHNO3', 1, KICK=HK, CLAP=HC, OH=HO, RIDE='x.x.x.x.x.x.x.x.')
pat('TECHNO4', 1, KICK='x...x.x.x...x...', CLAP=HC, OH=HO, CH='x...x...x...x...')
pat('TECHNO5', 1, KICK=HK, CLAP=HC, OH=HO, RIM='...x...x...x...x', CLAVE='.x.......x......')
pat('TECHNO6', 1, KICK=HK, CLAP=HC, CH='xxxxxxxxxxxxxxxx')
pat('TECHNO7', 1, KICK=HK, SNARE=HC, OH=HO, RIDE='x.x.x.x.x.x.x.x.', RIM='...x.....x....x.')
pat('TECHNO8', 1, KICK=HK, CLAP=HC, SNARE=HC, OH=HO, CH='x...x...x...x...', CRASH='x...............')
pat('TECHNO9', 1, KICK='x...x...x...x..x', CLAP=HC, OH=HO, CH='xx.xxx.xxx.xxx.x', MT='.......x........')
pat('TECHNO10', 1, KICK=HK, CLAP=HC, OH=HO, LT='...x......x.....', MT='.......x........', HT='...............x')
pat('TECHNO11', 1, KICK=HK, CLAP=HC, OH=HO, CH='.x.x.x.x.x.xxx.x', ROLL='............2..3')
pat('TECHNO12', 1, KICK=HK, CLAP=HC, OH=HO, LC='.x.....x.....x..', CLAVE='x..x..x...x..x..')

# the knob order: 1-8, then HOUSE1-12, DUBST1-12, DNB1-12, TECHNO1-12 (USER is the 9th knob slot, in RAM)
import re
_PK = ['HOUSE', 'DUBST', 'DNB', 'TECHNO']
def _key(p):
    m = re.match(r'([A-Z]*)(\d+)$', p[0])
    return (0, int(m.group(2))) if not m.group(1) else (1 + _PK.index(m.group(1)), int(m.group(2)))
P.sort(key=_key)
def bits(s):
    assert len(s) == 16, s
    return sum(1 << i for i, c in enumerate(s) if c == 'x')
def emit():
    o = ['/* SPDX-License-Identifier: GPL-3.0-only */',
         '/* generated by tools/gen_poli_drums.py (Wander-Drift, 16 Nov 2026; Drums-12, 21 Nov 2026): the drum patterns, bit i = step i + 1.',
         ' * Lanes: ' + ' '.join(LANES) + '. Ground-up, written from genre conventions; no copied patterns. */',
         f'#define XT_LANES {len(LANES)}', f'#define XT_PAT_ROM {len(P)}']
    o.append('static const xt_pat_t XT_PAT[XT_PAT_ROM] = {')
    for name, kit, lng, lanes in P:
        for k in lanes: assert k in LANES + ['ROLL', 'BELL'], k
        l = [bits(lanes.get(n, '.' * 16)) for n in LANES]
        ch = l[LANES.index('CH')]; oh = l[LANES.index('OH')]
        assert not (ch & oh), name + ': closed and open hat on one step'
        roll = 0
        for i, c in enumerate(lanes.get('ROLL', '.' * 16)):
            if c in '23':
                assert (ch >> i) & 1, f'{name}: a roll needs a closed hat at step {i + 1}'
                roll |= (int(c) - 1) << (2 * i)
        bell = lanes.get('BELL', '.' * 16); assert len(bell) == 16
        cb = [0 if c == '.' else int(c, 16) for c in bell]
        lo = sum(v << (4 * i) for i, v in enumerate(cb[:8])); hi = sum(v << (4 * i) for i, v in enumerate(cb[8:]))
        o.append('    { {' + ', '.join(f'0x{v:04X}' for v in l) + f'}}, {kit}, {lng}, 0x{roll:08X}u, {{0x{lo:08X}u, 0x{hi:08X}u}} }},   /* {name} */')
    o.append('};')
    o.append('static const char *const XT_PAT_NAME[XT_PAT_ROM] = { ' + ', '.join(f'"{n}"' for n, *_ in P) + ' };')
    return '\n'.join(o) + '\n'
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'firmware', 'src', 'poli_drum_pat.inc')
txt = emit()
if '--check' in sys.argv:
    ok = open(out).read() == txt
    print('poli_drum_pat.inc', 'up to date' if ok else 'STALE'); sys.exit(0 if ok else 1)
assert len(P) == 8 + 4 * 12, len(P)
open(out, 'w').write(txt); print(out, len(P), 'patterns')
