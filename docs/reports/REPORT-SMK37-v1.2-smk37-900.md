# P-Vox SMK-37 Elite v1.2-smk37-900 (27 Nov 2026)

Base: current `main` of HanzVolt/P-Vox at `dffae32` (v1.1-smk37-900 source `b4f525f`, plus the already-pushed 26 Nov fix that turns off the dubstep/DnB drum-bus bitcrusher and fixes the Elite ARP button). Fresh scratch copy: `/workspace/scratch/pvox-smk37-hansvoice-20261127/`. Parent folders were not written. Labels: [M] measured here, [E] estimated, [U] unknown (needs the device).

The boot voice is Hans Volt's own recording of the line "Polivoks by Hans Volt" (iPhone Voice Memo, 24 November 2026, attachment sha256 `950683efd89764a28789af24355afa8ae30030476bfd44525a59f12c6fe42c4a`). It was resampled to 44.1 kHz mono s16 (sha256 `833820739654df23a8aff9603d4bd61fc0c4ea466578a075fd347324f01bd9df`) and encoded with `tools/gen_poli_boot.py` (16 kHz IMA-ADPCM, 30,664 samples / 1.916500 s, 15,332 B, SNR 30.65 dB). This replaces the earlier Microsoft Edge text-to-speech clip. The HV logo, the 0.6 s fade-in, and the "no hold, no fade-out" hand-over are unchanged. Package identity stays **SMK-37 Elite_900**. Built with `./build.sh --board smk37 --model elite` and no `--release` flag, so the identity did not become Elite_902.

The v1.1-smk37-900 release package was verified first: `/workspace/scratch/bootvoice-20261125/repo/smk37/build/volt-smk37-elite.fwsc` sha256 `a682272541a8cf3e15c8c729ad60ad6b5e6cc46bafbe40b3e60ea36123e66c07` matches the GitHub release asset; volt.bin 162,780 B sha256 `be932dc2cbd192e4a9693f85a49d138306eb939dc0d46975d3dedfea3dcc06d2`.

## Figures [M]

| | dffae32 baseline | This v1.2 build |
|---|---|---|
| volt.bin | 162,636 B | **161,500 B** (sha256 `8839c36c554618d8aa50a277551078b61600354dd83b52249ae1eb0a1fb3ee36`), −1,136 B |
| spare in the 619,520 B app slot (`measure_flash.py`) | 456,884 B | **458,020 B**, all 0xFF (≥512: yes) |
| RAM .data+.bss | 44,556 B of 98,304 | **44,556 B** (unchanged) |
| 590 score (original `target_budget.py` sha `80f18ee705c1682d30bf4fd87bfb08f68019a4d5d033a999050427616cdde424`) | 597 = 37+11+549 | **597 = 37+11+549** (unchanged) |
| package | 651,267 B, identity SMK-37 Elite_900 | 651,267 B, sha256 `05c34f35411249644b6dcac9384bfc65b919d59d5621a6d586ae8bf0107e281f`, identity **SMK-37 Elite_900** |

v1.1 published volt.bin was 162,780 B / spare 456,740 B. The 144 B drop from that image to the dffae32 baseline is the already-on-main bitcrusher/ARP fix, not this voice swap.

## Proof nothing else was meant to change [M]

- **Source:** the only source edits are the voice WAV and `poli_boot_voice.h`, credit/README/fmhub/install-page version and attribution text, and comment-only updates in `poli_boot.c` and `tools/gen_poli_boot.py`. Identities, fade timing (`BOOT_FADE_MS` 600), and the sound engine were not changed.
- **Identical to v1.1 and to the dffae32 baseline:** `ramtext.bin`, `c1text.bin`, `data.bin`, and the loader `ota.bin` (sha256 `55c5c1e8824caa06db55c867d5bf0ee05527723895ec5ad195c4631b041f0f30`).
- **Audio path:** `fm1_alnk0_irq`, `uac_render_start`, `polivoks_render`, `poli_eg`, `poli_wave`, `poli_fcore_run`, `poli_apply_bend`, and `xf_c1_job` disassemble IDENTICAL to the dffae32 baseline (addresses normalised to symbols).
- **Voice table:** IMA-ADPCM payload 15,332 B (was 16,471 B, −1,139 B). The image shrinks by 1,136 B after 4-byte alignment. `boot_adpcm` updates the clip-length immediate only. Other remaining instruction-text differences are relocated address constants (bare immediates differ by 1,136) or unity-build layout noise from the shorter rodata object.
- **Tests:** `make test`: **765 PASS, 0 FAIL**, 0 warnings. `make test-san`: the same 765 under UBSan, plus the PlayGen suite under ASan **13 / 13 PASS**, 0 FAIL, 0 warnings.

## Package and flash page

- `/workspace/artifacts/pvox-hansvoice-v1.2-20261127/Polivoks-SMK37-Elite-900-v1.2.fwsc`
- `/workspace/artifacts/pvox-hansvoice-v1.2-20261127/Polivoks-Flash-SMK37-Elite-900-v1.2.html` (derived from the published v1.1 page `Polivoks-Flash-PlayGen-SMK37-20261124.html`, sha256 `c9774c96…eff36`; embedded package bytes match the `.fwsc`)

## Unknown [U]

How the new boot voice sounds and how long the load takes on a real SMK-37 Elite, and device CPU timing: unknown. This v1.2 image has not been flashed. Headless Chrome with MIDI mocked (file://, no clicks): **23 / 23 simulations passed**, 0 MIDI calls (`headless-smk.json`). No device contacted.
