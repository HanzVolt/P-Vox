# P-Vox FM-1 Parity + PlayGen merge (25 Nov 2026)

Base: the FM-1 Parity tree (`/workspace/scratch/polivoks-parity-fm1-20261123/`, volt.bin 157,132 B, score 596, identity FM-1_7869), copied to `/workspace/scratch/polivoks-playgen-merge-20261125/`. The parent folders were not written. PlayGen behaviour comes from `/workspace/scratch/polivoks-playgen-smk37-20261124/CHANGES-PLAYGEN.diff`. Labels: [M] measured here, [E] estimated, [U] unknown (needs the device).

The earlier v1.1-7869-fm1 GitHub release shipped this Parity image without K7/K3 ONPLAY. This build is the real merge: everything from Parity, plus the DRUM STEP ONPLAY toggle with the same behaviour as on the Elite.

## Control on the FM-1
The toggle is window 11 (DRUM STEP), knob index i=2, range 0..1, default 0 (OFF at power-up), RAM only. On the FM-1 that footer slot is labelled **K3** (the FM-1 has four knobs). On the SMK-37 Elite the same slot is labelled **K7**. The label is **ONPLAY** / **АВТО**, the value **OFF**/**ON** (EN) or **ВЫКЛ**/**ВКЛ** (RU). It is shown only in the footer, so the 16×16 grid stays at 5 px row height.

When K3 is ON, GENERATE is not locked, and PAD REC is idle, each PLAY writes a fresh generated drum loop into the playing copy only (`xt.wp`), using the existing GENERATE STYLE and DENSITY rules and the drums' own RNG. `xt.user`, `xt.chance[]`, `xt.user_style`, the PATTERN knob, and the PAD REC flash blob are not written. STOP, turning K3 off, a PATTERN change, a DRUM STEP edit, or a PAD REC commit or load puts the programmed pattern back. LOCK on GENERATE blocks it. Board guards were kept only where the hardware differs (footer knob names K1–K4 vs K5–K8). The sound engine was not touched.

## Figures [M]
| | Parity parent | This merge |
|---|---|---|
| volt.bin | 157,132 B (44deae6f…816a40) | **157,472 B** (2648b1d5…399a5f), +340 B |
| spare in the 581,564 B app slot (measure_flash.py) | 424,432 B | **424,092 B**, all 0xFF (≥512: yes) |
| RAM .data+.bss | 42,032 B of 98,304 | **42,032 B** (unchanged; `wp_gen`/`wp_style` sit in the 3-byte tail after `st_voice`; `sizeof xt` stays 600) |
| 590 score (ref_score.py, checker sha 80f18ee7…e424) | 596 = 37+10+549 | **596 = 37+10+549** |
| package | 610,139 B, identity FM-1_7869 | 610,139 B, sha 22c3ed64…85b2, identity **FM-1_7869**, 63 presets |

## Proof nothing else changed [M]
- **Code:** all 24 audio-interrupt, synth, USB and core-2 functions disassemble IDENTICAL vs Parity (address loads by symbol). ramtext.bin, c1text.bin and the loader ota.bin are byte-identical.
- **Engine:** `polivoks_limited.c`, `poli_fcore.c`, `note_inc.c`, `poli_presets_factory.inc` byte-identical to the parent.
- **Presets:** render_presets, all 63 factory presets + report (64 files), byte-identical to Parity.
- **Drum / seq traces (PLAY GEN OFF):** 8,247-line drum trace sha 05ed8547…ae27bc and 2,489-line sequencer trace sha 09f08292…5505ba: byte-identical to Parity.
- **Tests:** `make clean && make test`: **597 PASS, 0 FAIL**, 0 warnings (Parity 584; +13 PLAY GEN). `make test-san`: the same 597 under UBSan, plus test_playgen under ASan **13 / 13 PASS**, 0 FAIL, 0 warnings.
- **Screens:** DRUM STEP EN/RU, K3 OFF and ON. Grid row height **5 px**. Footer K3 label **ONPLAY** 48 px / **АВТО** 32 px (both < 56, glyphs present). OFF vs ON differs only in the footer value (y 226–235), no grid change and no overlap with K2 CHANCE / ШАНС. K4 stays empty.

## SMK from this tree
A `--board smk37 --model elite` build of this merged tree did not produce an image. The Parity tree is an FM-1 flash map (preset and PAD REC sector asserts fail on the Elite layout). It would also have used identity SMK-37 Elite_960, not SMK-37 Elite_900. It is therefore not byte-identical to PlayGen SMK (be932dc2…cc06d2). The published SMK release `v1.1-smk37-900` is left untouched.

## Package and flash page
- `/workspace/artifacts/Polivoks-Parity-7869.fwsc` (and `pkg/Polivoks-Parity-PlayGen-FM1-20261125.fwsc`).
- `/workspace/artifacts/Polivoks-Flash-Parity-7869.html`, derived from the Parity FM-1 page. Headless Chrome with MIDI mocked: **23 / 23 simulations passed**, 0 MIDI calls, decrypted slot = volt.bin 157,472 B (2648b1d5…), identity FM-1_7869 (`ds-logs/headless-merge.json`). No device contacted.

## Screens
`/workspace/artifacts/pvox-playgen-fm1/`: `en-drum-step-k7-off.png`, `en-drum-step-k7-on.png`, `ru-drum-step-k7-off.png`, `ru-drum-step-k7-on.png`, and `@2x` for each.

## Unknown [U]
How it looks and sounds on a real FM-1, and CPU timing: unknown.
