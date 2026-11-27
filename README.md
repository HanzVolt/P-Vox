# P-Vox
P-Vox — Polivoks firmware for the M-VAVE FM-1 pocket synth by Hans Volt. Two generators, two-op-amp filter, Bass Support, sequencer, drum generator, effects on second core.
image IMG_6362.jpg
<img width="1920" height="821" alt="IMG_6362" src="https://github.com/user-attachments/assets/4c71f3ac-1596-4da5-acf4-ad3d408fb5ba" />

## Identity and testing

- Package identity: **FM-1_7869** (P-Vox's own; `tools/build.py`, `FM1_IDENTITY`). The update loader keeps the Felucca loader's name `ota-FM-1_900`.
- The Drums12-Transpose firmware was tested by Hans on a real FM-1 as FM-1_900.
- The FM-1_7869 package was hardware-tested by Hans on his own real FM-1 (Nov 22, 2026).
- The update loader accepts both FM-1 flash chips: Puya P25Q80 (JEDEC 0x856014) and UC25HQ80 (0xB36014). It has not yet run on a newer-chip unit.

## Source

The complete firmware source is in this repository:

- `firmware/`: the app (`src/`), the hardware layer (`hal/`), generated tables (`gen/`) and the update loader (`loader/`).
- `tools/`: the build script (`build.py`), the package maker (`fm1pkg_make.py`) and the table generators.
- `host/`, `tests/`: host tests and render tools (run on a PC, no FM-1 needed).
- `assets/`: the logo, the fonts and the boot voice the generated headers come from.

Build: `./build.sh` (needs the JieLi Linux toolchain in `JIELI_TOOLCHAIN` and the AC79 SDK files in `AC79_SDK`; see `tools/build.py`).

## Layout (v1.2)

The FM-1 and SMK-37 Elite builds do not cross-compile, so each has its own complete source tree:

- Repository root (`firmware/`, `tools/`, `host/`, `tests/`, `assets/`, `build.sh`): the FM-1 build, package identity **FM-1_7869** (release `v1.2-7869-fm1`).
- `smk37/`: the SMK-37 Elite build, package identity **SMK-37 Elite_900** (release `v1.2-smk37-900`). It is self-contained; build it with `cd smk37 && ./build.sh --board smk37 --model elite`.
- `docs/reports/`: the measured build reports published with each release.

Both trees use the same sound engine and the same 63 factory presets (byte-identical output in the host render tests). The licence (`LICENSE`), `NOTICE.txt` and `ATTRIBUTION.txt` at the root apply to both.

## Licence

- The code is **GPL-3.0-only** (`LICENSE`, and each file's header).
- **The HV logo is © Hans Volt, all rights reserved.** It is not covered by the GPL, and the GPL gives no right to it (`assets/logo/`, `firmware/src/poli_boot_logo.h`). To build without it, use `-DPOLI_BOOT_SCREEN=0` or replace the picture.
- **The boot voice** ("Polivoks by Hans Volt") is Hans Volt's own recording. It is not offered under the GPL.
- Fonts and other notices: `NOTICE.txt` and `ATTRIBUTION.txt`.
