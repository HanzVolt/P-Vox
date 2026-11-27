# P-Vox FM-1 v1.2-7869-fm1 (27 Nov 2026)

Base: current `main` of HanzVolt/P-Vox at `dffae32` (v1.1-7869-fm1 source `b4f525f`, plus the already-pushed 26 Nov fix that turns off the dubstep/DnB drum-bus bitcrusher and fixes the Elite ARP button). Fresh scratch copy: `/workspace/scratch/pvox-fm1-hansvoice-20261127/`. Parent folders were not written. Labels: [M] measured here, [E] estimated, [U] unknown (needs the device).

The boot voice is Hans Volt's own recording of the line "Polivoks by Hans Volt" (iPhone Voice Memo, 24 November 2026, attachment sha256 `950683efd89764a28789af24355afa8ae30030476bfd44525a59f12c6fe42c4a`). It was resampled to 44.1 kHz mono s16 (sha256 `833820739654df23a8aff9603d4bd61fc0c4ea466578a075fd347324f01bd9df`) and encoded with `tools/gen_poli_boot.py` (16 kHz IMA-ADPCM, 30,664 samples / 1.916500 s, 15,332 B, SNR 30.65 dB). This replaces the earlier Microsoft Edge text-to-speech clip. The HV logo, the 0.6 s fade-in, and the "no hold, no fade-out" hand-over are unchanged. Package identity stays **FM-1_7869**.

The v1.1-7869-fm1 release package was verified first: `/workspace/scratch/bootvoice-20261125/repo/build/volt.fwsc` sha256 `22c3ed64bedc24853200856d1858537ed1af12e1e609c3edd65a4668d54485b2` matches the GitHub release asset; volt.bin 157,472 B sha256 `2648b1d55e5c066d56aca35683ded729446880d6417c800fab70b7bb84399a5f`.

## Figures [M]

| | dffae32 baseline | This v1.2 build |
|---|---|---|
| volt.bin | 157,408 B | **156,272 B** (sha256 `7d49f1800c3d38e3a99b636147dd3c927cf11f33da211aab4f0402019f6db1ef`), −1,136 B |
| spare in the 581,564 B app slot (`measure_flash.py`) | 424,156 B | **425,292 B**, all 0xFF (≥512: yes) |
| RAM .data+.bss | 42,032 B of 98,304 | **42,032 B** (unchanged) |
| 590 score (original `target_budget.py` sha `80f18ee705c1682d30bf4fd87bfb08f68019a4d5d033a999050427616cdde424`) | 596 = 37+10+549 | **596 = 37+10+549** (unchanged) |
| package | 610,139 B, identity FM-1_7869 | 610,139 B, sha256 `88eaac0e97bbe1f7c91cc1f32d3a2ff74bc82a4a915bc2ee9d7797b2d8759e25`, identity **FM-1_7869** |

v1.1 published volt.bin was 157,472 B / spare 424,092 B. The 64 B drop from that image to the dffae32 baseline is the already-on-main bitcrusher/ARP fix, not this voice swap.

## Proof nothing else was meant to change [M]

- **Source:** the only source edits are the voice WAV and `poli_boot_voice.h`, credit/README/fmhub/install-page version and attribution text, and comment-only updates in `poli_boot.c` and `tools/gen_poli_boot.py`. Identities, fade timing (`BOOT_FADE_MS` 600), and the sound engine were not changed.
- **Identical to v1.1 and to the dffae32 baseline:** `ramtext.bin`, `c1text.bin`, `data.bin`, and the loader `ota.bin` (sha256 `04e912798038cf0305ef0a35e1f42e6f729821a7b0dfd8bfc9c0d6c9581572ed`).
- **Audio path:** `fm1_alnk0_irq`, `uac_render_start`, `polivoks_render`, `poli_eg`, `poli_wave`, `poli_fcore_run`, `poli_apply_bend`, and `xf_c1_job` disassemble IDENTICAL to the dffae32 baseline (addresses normalised to symbols).
- **Voice table:** IMA-ADPCM payload 15,332 B (was 16,471 B, −1,139 B). The image shrinks by 1,136 B after 4-byte alignment. `boot_adpcm` updates the clip-length immediate only. Other remaining instruction-text differences are relocated address constants (bare immediates differ by 1,136) or unity-build layout noise from the shorter rodata object.
- **Tests:** `make test`: **597 PASS, 0 FAIL**, 0 warnings. `make test-san`: the same 597 under UBSan, plus the PlayGen suite under ASan **13 / 13 PASS**, 0 FAIL, 0 warnings.

## Package and flash page

- `/workspace/artifacts/pvox-hansvoice-v1.2-20261127/Polivoks-FM1-7869-v1.2.fwsc`
- `/workspace/artifacts/pvox-hansvoice-v1.2-20261127/Polivoks-Flash-FM1-7869-v1.2.html` (derived from the published v1.1 page `Polivoks-Flash-Parity-7869.html`, sha256 `257c34ac…eeaac`; embedded package bytes match the `.fwsc`)

## Unknown [U]

How the new boot voice sounds and how long the load takes on a real FM-1, and device CPU timing: unknown. This v1.2 image has not been flashed. Headless Chrome with MIDI mocked (file://, no clicks): **23 / 23 simulations passed**, 0 MIDI calls (`headless-fm1.json`). No device contacted.
