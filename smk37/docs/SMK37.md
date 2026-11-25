# Polivoks on the M-VAVE SMK-37 Elite (scratch board layer)

This is a scratch copy. Do not flash, commit, or push. The sound engine, presets,
filter, panel navigation, GUI and load screen start from the FM-1 Polivoks ones.
Keybed velocity now reaches the existing VELOCITY (SLOT_VEL) amount, which scales
both filter contour and amp level (Hans, 13 Nov 2026). Pitch bend is not in this
step. Package identity stays `SMK-37 Elite_900`.

## Packages

| Keyboard | Build | Package | Identity (dev, matches FM-1_900) |
| --- | --- | --- | --- |
| SMK-37 Elite | `BOARD=smk37 MODEL=elite ./build.sh` | `build/volt-smk37-elite.fwsc` | `SMK-37 Elite_900` |
| SMK-37 Pro | `BOARD=smk37 ./build.sh` | `build/volt-smk37.fwsc` | `SMK-37 Pro_900` |
| MKE-P37 | `BOARD=smk37 MODEL=mke ./build.sh` | `build/volt-smk37-mke.fwsc` | `MKE-P37_900` |
| FM-1 | `./build.sh` | `build/volt.fwsc` | `FM-1_900` |

The Elite, Pro and MKE builds are the same code and the same SMK flash map. Only
the package identity (and the loader / update-record name) changes, so M-VAVE's
updater can still take that model's factory firmware later.

**Package identity (Hans, 13 Nov 2026):** locked to this tree's own `FM-1_900`
style. The Elite package is exactly **`SMK-37 Elite_900`**. That is not an open
option: do not use the OberVave `SMK-37 Elite_95XY` pattern (or Skiff `_700`).
`tools/build.py` `BOARD_PREFIX` stays `_90`.

## Preset slots

The store is **63 slots** (`PV_NSLOT`), all writable, all used on this port.
There is no 50-preset cap. Older 50-slot banks (`PV_NSLOT_V1`) still load and
expand to 63; that is compatibility with old bank files, not a limit on the
Elite map. Hans (13 Nov 2026): use as many as possible; the 63 measured slots
are approved.

## What is wired

Approved plumbing (Hans, 13 Nov 2026), plus keybed velocity to filter and amp
(Hans, later that day). Existing hardware is mapped to existing engine
parameters and UI actions except for this approved velocity change. Pitch wheel
is unmapped. The mod wheel still drives FROM MOD (`SLOT_MOD_CUT` / CC 1).

- **37 velocity keys** are scanned (C3..C6). All 37 go through
  `polivoks_panel` on the 37-bit `fm1_in.keys` mask (key 0 = MIDI 60 at
  octave 0; physical F3, key 5, is still note 65, plus the signed OCTAVE
  fix `int oct`). The authentic Polivoks rule applies across the whole
  bed: the lowest held key is the main pitch (GEN I); a higher legato
  key makes no new sound when VOICES is 1, and goes to generator II when
  VOICES is 2. The earlier split — middle 27 keys (F3..G5) in the 27-bit
  panel mask, the ten keys at the ends queued through `polivoks_midi` as
  last-note — is removed. External USB/DIN MIDI still uses
  `polivoks_midi` and is unchanged. `poli_smk_poll` feeds the lowest
  held key’s measured velocity (`fm1_in.key_vel[k]`) through
  `polivoks_set_panel_vel` so `polivoks_panel` can latch it on a new
  gate-on (the engine has no HAL include).
  FILTER MOD K2 **VELOCITY** (`SLOT_VEL`, default OFF) is the depth for both:
  a softer new attack opens the filter less (existing contour law) and is
  quieter (same Q15 scale `1 - amt*(127-v)/127^2` on the amp VCA, skipped when
  the scale is 32768 so amount 0 or velocity 127 is bit-identical to before).
  Legato does not retake velocity. The on-screen note under the knob reads
  “FILTER AND LEVEL” / “ФИЛЬТР И УРОВЕНЬ”.
- The 14 printed-name buttons and K2..K8 keep their FM-1 jobs (PATCH=HOME,
  PARA=EDIT, …). K1 is a virtual MASTER pot (starts around 70% at power-on).
- **16 RGB pads** load presets (`poli_preset_load_slot`, a thin wrapper around
  `pv_load`). **KNOB BANK** pages all 63 slots: 1–16 / 17–32 / 33–48 / 49–63.
  The last page has 15 pads; pad 16 is unused. The KNOB BANK LED is on when
  the page is not 1–16. The current slot is lit cream; other slots on the page
  are dim. Pads do not play notes (`smk_pad_events=1`).
  Moving bank-A fader 1 (cutoff) is lights-only: the 16 pads switch to an
  8-column spectrum meter (`smk_pad_spectrum.h`). The fill is left to
  right across the 8 columns. Column k is the top-row pad (hardware
  index k) plus the bottom-row pad (index k+8); both light together in
  the same colour. The eight shades are a horizontal wash interpolated
  in hue: column 1 blue (0,0,224), then violet (64,0,224), purple
  (126,0,220), purple-magenta (182,0,212), magenta (200,0,171),
  crimson (182,0,104), deep crimson (157,0,45), and a deep dark red on
  column 8 (125,0,0), with brightness easing down only toward that last
  column. Columns 1..N follow the smoothed `poli_cut` (the audible
  cutoff, not the leading fader target) over 8 steps, with the partial
  next column dimmed in its own shade on both pads.
  Bank-A fader 2 (resonance) does the same, tracking `poli_res`
  (0..30000), with a green-to-deep-red wash that uses the same
  brightness easing: column 1 green (0,224,0), then yellow-green
  (64,224,0), (126,220,0), yellow (182,212,0), yellow-orange
  (200,171,0), orange (182,104,0), deep orange (157,45,0), and the
  same deep dark red on column 8 (125,0,0). Whichever of those two
  physical faders moved last owns the pads. About 1.5 s after that
  last move, the normal preset colours return. Encoder, MIDI CC 74,
  MIDI CC 71 and fader bank B do not trigger the meter. Pad taps still
  load presets. LED updates run in the main loop at 40 Hz, never in
  TIMER5 or the audio path.
- **4 faders**, two banks, via `poli_nav_cc`:
  - Bank A (default): cutoff / resonance / contour / drive (CC 74, 71, 70, 16).
    Cutoff (F1) is the zipper source: the HAL already reports a 7-bit value
    (`fm1_in.fader[]`, 0..127, from a 10-bit ADC with IIR + ±1 deadband), and
    `poli_nav_cc` used to write `poli_cut` in 128 steps of ~196 units
    (~0.57 semitone on the 4096-units-per-octave map). The filter's own
    smoother (`err>>5` per sample, τ ≈ 0.7 ms) is too short to hide that.
    Bank-A F1 now maps `fm1_in.fad_med` (the 10-bit median-of-3 already
    computed in the HAL, same law as `poli_cut_of_knob` at every 7-bit tick,
    8 extra steps between them). It does not follow `fad_acc`, whose x16 IIR
    (`smk_input.h:542`, τ ≈ 8.3 ms [E] at the 417 Hz fader scan) lagged the
    physical fader. `poli_cut` ramps with a 1/2-per-ms exponential (err>>1,
    τ ≈ 1.44 ms [E], snap when |err|<2). Host test of a 0→24874 jump [M]:
    first change in 1 ms, 95% in 5 ms, reaches the target in 16 ms. The
    first tick is applied on the poll that sees the move; both the ramp and
    the 10-bit target then tick from TIMER5 when `fm1_ms` increments, so a
    UI redraw cannot freeze either one. Existing first-movement soft
    takeover is unchanged (the same 7-bit arm as the other faders); no new
    takeover rule was added. Encoder and MIDI CC 74 still snap. Untouched
    faders do not write `poli_cut`.
  - Bank B = AMP EG: attack / decay / sustain / release (CC 73, 75, 79, 72).
  **FADER BANK** toggles the bank; its LED is on for bank B. Switching banks
  re-arms soft takeover so the physical positions do not slam the other bank
  (an in-flight cutoff ramp is stopped; `poli_cut` is left where it is).
- **Mod wheel** sets the existing filter-mod amount (`SLOT_MOD_CUT` / CC 1 /
  `polivoks_set_mod_cut`). It is not a new modulation source. Travel is short
  and inverted (up = lower raw); rest self-calibrates. Soft takeover on the
  faders and the mod wheel so rest / power-on does not jump the parameter.
- **Pitch wheel** is unmapped (not abused as `SLOT_TUNE`).
- **< >** call `poli_nav_select_turn`, or `poli_preset_turn` when the preset
  bar is open. They are left alone on the save screen.
- No key LEDs. Display is 240×240 on the SMK pins. TRS MIDI IN is off (PH8 is a
  key row). Flash uses the SMK map; USRTRIM (`trimdat` at 0xC1000) is read-only.
  First boot clears the VM store if the owner tag is not `VOLT`.

- **Fader display hysteresis** (display only, H=12 counts): live Bank A
  cutoff/resonance and Bank B AMP A/D/S/R numbers, pad-meter takeover,
  and AMP/filter plot geometry follow `smk_fader_steady.h`. Ends snap
  to 0 and 127. The engine still tracks the raw 10-bit median plus the
  existing 1 ms glide; there is no engine-side hysteresis.

- **Spectrum footer**: a 24-bar, 52 px analyser is the bottom band on
  every page except the save-name screen (which keeps the K1-K4
  footer). 128 samples from the playing I2S half, Hann + 128-point
  FFT, main loop at about 25 Hz. Stacked blocks, blue at the base
  fading to teal, with only the top block red. The FilterEQ footer has
  been removed (and its code compiled out) to free flash. The math is
  in `smk_spectrum.h`.

- **AMP ENV window** (window 8) draws a classic ADSR shape instead of
  four fader bars, using the height freed by moving the spectrum into
  the footer. The vertical axis is the full 0-127 fader throw: attack
  starts on the bottom baseline, the peak is at the top, and zero is
  the bottom pixel. Segment shapes are the firmware's one-pole RC
  contours (attack charges toward 34000 and stops at the peak, decay
  settles to sustain, release aims below 0 and stops on the baseline),
  sampled per column, not straight lines. Widths track the 7-bit
  faders in a fixed time window (knob 0 is a vertical edge) so moving
  one stage does not jump the others. The sustain hold is a fixed
  width whose height is linear in 0..127, from the baseline to the
  top; decay ends on it and release starts from it. The shape is taken from
  the steadied nav positions. While a note sounds, a cream dot rides the
  current stage and level, holds on sustain, and slides down on
  release. Snapshot and ~25 Hz partial redraw are main-loop only, never
  in the audio ISR or TIMER5. A plot-only blit clears only the interior
  of the window (one pixel inside the 1 px orange frame) and redraws
  that frame in the strip, so moving the envelope sliders cannot punch
  out the outline. The same restore applies to the FILTER EG plot.

- **FILTER window** (window 5) keeps the cutoff/resonance/contour/drive
  bars and adds an animated filter-EG plot in the same style as the
  AMP plot, simulated with the firmware's own envelope code, plus
  DEPTH (the existing CONTOUR amount) and KEY (0 / 62 / 100 %)
  readouts in a right-hand column beside the plot, each label on the
  same row as its own value, clear of the CUTOFF / RESONANCE /
  CONTOUR / DRIVE bars.
  FILTER EG (window 7) still uses the four parameter bars.


- **Velocity fold**: `poli_vel_scale` is folded into the output-level
  multiply once per 256-sample block (`amp_g = (vs * out_lvl) >> 15`).
  The sample loop has a single skip-if-unity multiply after tremolo.
  Bit-identical when either scale is 32768; otherwise measured max
  |err| is 2 LSB versus the old two-step law.

## Update path (do not run from this scratch tree until Hans says so)

Back up a full 1 MiB flash first and check `trimdat` at offset `0xC1224`.
Install with `python3 tools/fm1_install.py build/volt-smk37-elite.fwsc` on an
Elite only. USB rescue: hold OCTAVE- at power-on. Return to stock with the
Elite factory package via M-UPGRADE (or a loader that never writes calibration).

Hardware facts: [genkigenki/fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) `docs/01-hardware.md`.
Notes: `/workspace/smk37-research/board-layer-notes.md`.
