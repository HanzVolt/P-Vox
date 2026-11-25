# Polivoks SMK-37 Elite — PlayGen (24 Nov 2026)

Scratch build only. Parent: `/workspace/scratch/polivoks-notext-smk37-20261123/` (read only, untouched).
[M] measured here, [E] estimated, [U] unknown (needs hardware).

## What changed
SMK-37 Elite only (`#if BOARD_SMK37`). The tree's FM-1 build is byte-identical to the parent's FM-1 `volt.bin`.
On **DRUM STEP**, knob K7 (**ONPLAY** / **АВТО**) is a RAM-only toggle, default OFF. When it is ON and
GENERATE is not locked and PAD REC is idle, each PLAY writes a fresh generated drum loop into the
playing copy only (`xt.wp`), using the existing GENERATE STYLE and DENSITY rules and the drums' own
RNG. `xt.user`, `xt.chance[]`, `xt.user_style`, the PATTERN knob, and the PAD REC 408 B flash blob
are not written. STOP (and turning K7 off, a PATTERN change, a DRUM STEP edit, or a PAD REC commit)
puts the programmed pattern back. K7 is shown only in the footer, so the 16×16 grid stays at 5 px
row height. The sound engine is untouched.

Files: `tree/firmware/src/poli_xtra.c`, `poli_ui.c`, `poli_padrec.inc`, `tree/host/test_drumstep.c`,
`tree/host/test_playgen.c` (new), `tree/host/shot_playgen.c` (new), `tree/host/Makefile`.
See `CHANGES-PLAYGEN.diff`. Fonts were not regenerated.

## Figures [M]
| | NoText parent | PlayGen |
|---|---|---|
| volt.bin | 162,408 B (15bdf15e…78ad90) | **162,780 B** (be932dc2…cc06d2), +372 B |
| spare in the 619,520 B app slot (measure_flash.py) | 457,112 B | **456,740 B**, all 0xFF (≥512: yes) |
| RAM .data+.bss | 44,556 B | **44,556 B** (same starts and sizes; `wp_gen`/`wp_style` sit in the 7-byte tail after `st_voice`; `sizeof xt` stays 608) |
| 590 score (ref_score.py, checker sha 80f18ee7…e424) | 597 = 37+11+549 | **597 = 37+11+549** |
| package | 651,267 B, identity SMK-37 Elite_900 | 651,267 B, sha a6822725…e66c07, identity **SMK-37 Elite_900**, 63 presets |

`target_budget.py` was run on the same disassembly (FORM names absent, as in the parent log).

## Proof nothing else changed [M]
- **FM-1 volt.bin:** PlayGen `BOARD_SMK37=0` and the parent's FM-1 image are byte-identical (sha 8b78811a…22f67c, 144,872 B).
- **Presets:** render_presets, all 63 factory presets + report (64 files), SMK and the tree's FM-1 build: byte-identical to the parent and to each other.
- **Drum / seq traces (K7 OFF):** 8,247-line drum trace sha 05ed8547…ae27bc, synth-sequencer traces 2,489 lines sha 09f08292…5505ba and 2,479 lines sha f0469d33…cdc8d8ba: byte-identical to the parent.
- **Code:** all 24 audio-interrupt, synth, USB and core-2 functions disassemble IDENTICAL vs the parent (address loads by symbol). ramtext.bin, c1text.bin and the loader ota.bin are byte-identical.
- **Engine:** `polivoks_limited.c`, `poli_fcore.c`, `note_inc.c`, `poli_presets_factory.inc` byte-identical to the parent.
- **Tests:** `make clean && make test`: **765 PASS, 0 FAIL**, 0 warnings (NoText 752; +13 PLAY GEN). `make test-san`: the same 765 under UBSan, plus test_playgen under ASan **13 / 13 PASS**, 0 FAIL, 0 warnings.
- **Screens:** DRUM STEP EN/RU, K7 OFF and ON. Grid row height **5 px**. Footer K7 label **ONPLAY** 48 px / **АВТО** 32 px (both < 56, glyphs in `UI_F16_CP` and `GUI_CP`). No overlap with K6 CHANCE / ШАНС. K7 OFF vs ON differs only in the footer value (OFF/ON, ВЫКЛ/ВКЛ).

## Package and flash page
- `/workspace/artifacts/Polivoks-SMK37-Elite-PlayGen-20261124.fwsc` (copy of `pkg/`).
- `/workspace/artifacts/Polivoks-Flash-PlayGen-SMK37-20261124.html`, derived from the NoText SMK page. Headless Chrome with MIDI mocked: **23 / 23 simulations passed**, 0 MIDI calls, decrypted slot = volt.bin 162,780 B (be932dc2…), identity SMK-37 Elite_900 (`ds-logs/headless-playgen.json`). No device contacted.

## Screens
`/workspace/artifacts/pvox-playgen-smk37/`: `playgen-{en,ru}-drum-step-k7-{off,on}.png` and `@2x`.

## Unknown [U]
How it looks and sounds on the Elite's real panel and speakers, and CPU timing: unknown.
