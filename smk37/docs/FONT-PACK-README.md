# Follow-up: platform generated font (omission from 01)

Date: 2026-11-09 (America/New_York). Packaging only. No generation, builds, source edits, commits, pushes, or flashes.

## Why
Peer target build failed: `firmware/src/gfx.c:15` → fatal missing `felucca_font.h`. Archive `01-platform-and-sdk-blobs.tar.gz` omitted `firmware/gen/` and `tools/gen_font.py`. RMIF `build.py` compiles with `-Ifirmware/gen` but does not generate the header.

## Restore paths (merge onto unpacked 01 / platform root)
```
firmware/gen/felucca_font.h
tools/gen_font.py
assets/fonts/ter-u16n.bdf
assets/fonts/ter-u16b.bdf
assets/fonts/Terminus-LICENSE.txt
```
Optional provenance copies in this archive:
- `tools/gen_font.NEWER-struct-from-audit-base.py` — current `/workspace/fm1/audit/base/tools/gen_font.py` (emits extra struct fields; **not** compatible with RMIF/Dinghy `gfx.c` `felucca_font_t` as shipped in 01).
- `assets/fonts/Terminus-LICENSE.dinghy-copy.txt` — license text from the dinghy tree.

## Unblock
Use the **prebuilt** `firmware/gen/felucca_font.h`. Do **not** need to run the generator for the link to find the header.

## Provenance
| Item | Absolute source on packaging box | Notes |
| --- | --- | --- |
| `felucca_font.h` | `/workspace/fm1/standalone/dinghy/firmware/gen/felucca_font.h` | SHA256 `ce47ea8e…5119`. Same bytes in `repos/dinghy` and `scratch/dinghy-assess/src-0.4`. Header comment: generated from `ter-u16n.bdf`, FONT_S ASCII 32..126. **Matches** RMIF handoff `gfx.c` 8-field `felucca_font_t`. |
| `tools/gen_font.py` | `/workspace/fm1/repos/majnikool_sloop-fm1/tools/gen_font.py` | 8-field emitter compatible with that struct. Uses `ter-u16n.bdf`. Regenerating may not byte-match the prebuilt (this script’s S range is 32..255 vs prebuilt cut to 126). |
| `gen_font.NEWER-…` | `/workspace/fm1/audit/base/tools/gen_font.py` | Newer layout (`bits` / trailing flag). Would **not** match RMIF `gfx.c` without source changes (not done here). |
| BDF + OFL | `/workspace/fm1/audit/base/assets/fonts/` | `ter-u16n.bdf`, `ter-u16b.bdf`, `Terminus-LICENSE.txt` (SIL OFL 1.1). |

## Other generated headers
Narrow include scan of the RMIF platform tree shipped in 01: the **only** build-generated header referenced is `felucca_font.h` (via `platform.c` → `gfx.c`). No `felucca_icons.h` / `felucca_samples.h` / `felucca_tables.h` / `felucca_fm6.h` includes in that tree. `uac_fir.h` is already a checked-in `firmware/src/` file in 01.

## Known limits
- No claim the prebuilt font was the exact file used inside the historical 60896 B RMIF `volt.bin`; it is the existing dinghy/skeleton-lineage generated header that matches the shipped `gfx.c` struct.
- No regeneration was run in this handoff.
- Private/cyborg/splash generators and assets were not included (not referenced by this platform tree).
