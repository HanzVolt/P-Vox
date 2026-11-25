/* SPDX-License-Identifier: GPL-3.0-only */
/* Pad spectrum meter (lights only). Bank-A cutoff fader (fader 1) or
 * bank-A resonance fader (fader 2) movement shows an 8-column level
 * meter on the WS2812 pads; ~1.5 s after the last move of whichever
 * fader owns the pads, the normal preset colours come back. The fader
 * that moved last owns the display. Pad hits still load presets.
 * Encoder and MIDI CC (74 for cutoff, 71 for resonance) do not trigger
 * this. Bank B does not trigger it.
 *
 * Fill is left→right across the 8 columns. Column k is the top-row pad
 * (hardware index k) plus the bottom-row pad (index k+8); both light
 * together in the same colour.
 *
 * Cutoff palette: a horizontal wash interpolated in hue from blue
 * (column 1) through violet, purple, magenta and crimson to a deep
 * dark red (column 8), with brightness easing down only toward that
 * last column. The fill follows poli_cut (the smoothed audible cutoff),
 * not the leading fader target, so the lights track the filter in real
 * time.
 *
 * Resonance palette: the same brightness easing, but yellow arrives
 * one column sooner: column 1 green, column 2 a quick yellow-green,
 * column 3 yellow, columns 4-7 orange shades, column 8 the same
 * deep dark red (125,0,0). The fill follows poli_res (0..POLI_RES_TOP,
 * knob 127).
 *
 * The next column is dimmed by the fractional remainder, both pads,
 * in that column's own shade. */
#pragma once
#include "poli_nav.h"

enum {
    SMK_SPEC_PADS = 16,
    SMK_SPEC_COLS = 8,
    SMK_SPEC_HOLD_MS = 1500,    /* restore preset colours after this idle */
    SMK_SPEC_PERIOD_MS = 25,    /* 40 Hz LED updates, main loop only */
    SMK_SPEC_SRC_CUT = 0,
    SMK_SPEC_SRC_RES = 1
};

/* Knob 127 writes scale_u7(pos, 0, 30000) in poli_nav.c; the encoder
 * path clamps to the same top. Used only to scale the resonance meter. */
#ifndef POLI_RES_TOP
#define POLI_RES_TOP 30000
#endif

/* 8 column shades: hue 240°..360° in equal steps, V held near 0.88 and
 * eased with t^2.5 so column 8 lands on a deep dark red (~125,0,0).
 * Index 0 is the far-left column. */
static const uint8_t SMK_SPEC_COL_RGB[SMK_SPEC_COLS][3] = {
    {  0, 0, 224 }, /* 1 blue */
    { 64, 0, 224 }, /* 2 violet */
    {126, 0, 220 }, /* 3 purple */
    {182, 0, 212 }, /* 4 purple-magenta */
    {200, 0, 171 }, /* 5 magenta */
    {182, 0, 104 }, /* 6 crimson */
    {157, 0,  45 }, /* 7 deep crimson */
    {125, 0,   0 }  /* 8 deep dark red */
};

/* Hardware pad colours: 0..7 top row, 8..15 bottom row, same as the column. */
static const uint8_t SMK_SPEC_RGB[SMK_SPEC_PADS][3] = {
    {  0, 0, 224 }, { 64, 0, 224 }, {126, 0, 220 }, {182, 0, 212 },
    {200, 0, 171 }, {182, 0, 104 }, {157, 0,  45 }, {125, 0,   0 },
    {  0, 0, 224 }, { 64, 0, 224 }, {126, 0, 220 }, {182, 0, 212 },
    {200, 0, 171 }, {182, 0, 104 }, {157, 0,  45 }, {125, 0,   0 }
};

/* Resonance wash: same V easing as the cutoff table (max-channel
 * 224,224,220,212,200,182,157,125). Hue is not a G/B swap of the
 * cutoff table: yellow (60°) arrives at column 3, one column sooner.
 * Column 1 green (120°), column 2 a quick yellow-green (100°),
 * column 3 yellow (60°), columns 4-7 orange shades (48°,36°,22°,10°),
 * column 8 the same deep dark red (125,0,0). */
static const uint8_t SMK_SPEC_RES_COL_RGB[SMK_SPEC_COLS][3] = {
    {  0, 224, 0 }, /* 1 green */
    { 75, 224, 0 }, /* 2 quick yellow-green */
    {220, 220, 0 }, /* 3 yellow */
    {212, 170, 0 }, /* 4 orange */
    {200, 120, 0 }, /* 5 orange */
    {182,  67, 0 }, /* 6 deep orange */
    {157,  26, 0 }, /* 7 deeper orange */
    {125,   0, 0 }  /* 8 deep dark red */
};

static const uint8_t SMK_SPEC_RES_RGB[SMK_SPEC_PADS][3] = {
    {  0, 224, 0 }, { 75, 224, 0 }, {220, 220, 0 }, {212, 170, 0 },
    {200, 120, 0 }, {182,  67, 0 }, {157,  26, 0 }, {125,   0, 0 },
    {  0, 224, 0 }, { 75, 224, 0 }, {220, 220, 0 }, {212, 170, 0 },
    {200, 120, 0 }, {182,  67, 0 }, {157,  26, 0 }, {125,   0, 0 }
};

/* Column of a hardware pad: top k and bottom k+8 share column k. */
static int smk_spec_col(int pad)
{
    if (pad < 0 || pad >= SMK_SPEC_PADS)
        return -1;
    return pad < SMK_SPEC_COLS ? pad : pad - SMK_SPEC_COLS;
}

/* 8 columns × 256 fractions. full in 0..8, frac in 0..255 for column `full`. */
static void smk_spec_level_of(int32_t val, int32_t top, int *full, int *frac)
{
    uint32_t x;
    if (val <= 0 || top <= 0) {
        *full = 0;
        *frac = 0;
        return;
    }
    if (val >= top) {
        *full = SMK_SPEC_COLS;
        *frac = 0;
        return;
    }
    x = (uint32_t)val * (SMK_SPEC_COLS * 256u) / (uint32_t)top;
    *full = (int)(x / 256u);
    *frac = (int)(x % 256u);
}

static void smk_spec_level(int32_t cut, int *full, int *frac)
{
    smk_spec_level_of(cut, POLI_CUT_TOP, full, frac);
}

static void smk_spec_res_level(int32_t res, int *full, int *frac)
{
    smk_spec_level_of(res, POLI_RES_TOP, full, frac);
}

/* Colour of one pad from a value, a scale top, and a 16-pad palette. */
static void smk_spec_colour_from(int pad, int32_t val, int32_t top,
                                 const uint8_t rgb[SMK_SPEC_PADS][3],
                                 uint8_t *r, uint8_t *g, uint8_t *b)
{
    int col, full, frac;
    uint8_t cr, cg, cb;
    col = smk_spec_col(pad);
    if (col < 0) {
        *r = *g = *b = 0;
        return;
    }
    cr = rgb[pad][0];
    cg = rgb[pad][1];
    cb = rgb[pad][2];
    smk_spec_level_of(val, top, &full, &frac);
    if (col < full) {
        *r = cr;
        *g = cg;
        *b = cb;
    } else if (col == full && frac) {
        *r = (uint8_t)(((uint16_t)cr * (uint16_t)frac) / 256u);
        *g = (uint8_t)(((uint16_t)cg * (uint16_t)frac) / 256u);
        *b = (uint8_t)(((uint16_t)cb * (uint16_t)frac) / 256u);
    } else {
        *r = *g = *b = 0;
    }
}

/* Colour of one pad at the current cutoff. Dark if its column is past the fill. */
static void smk_spec_colour(int pad, int32_t cut, uint8_t *r, uint8_t *g, uint8_t *b)
{
    smk_spec_colour_from(pad, cut, POLI_CUT_TOP, SMK_SPEC_RGB, r, g, b);
}

/* Colour of one pad at the current resonance, green→red wash. */
static void smk_spec_res_colour(int pad, int32_t res, uint8_t *r, uint8_t *g, uint8_t *b)
{
    smk_spec_colour_from(pad, res, POLI_RES_TOP, SMK_SPEC_RES_RGB, r, g, b);
}

static uint32_t smk_spec_last_ms;
static uint8_t smk_spec_lit;
static uint8_t smk_spec_src;        /* SMK_SPEC_SRC_CUT or SMK_SPEC_SRC_RES */

static void smk_spec_reset(void)
{
    smk_spec_last_ms = 0;
    smk_spec_lit = 0;
    smk_spec_src = SMK_SPEC_SRC_CUT;
}

/* Physical bank-A fader move only. now==0 is treated as 1 so a boot-time
 * move is still a live timestamp. src is the fader that just moved and
 * now owns the pads. */
static void smk_spec_note_move(uint32_t now, int src)
{
    smk_spec_last_ms = now ? now : 1u;
    smk_spec_lit = 1;
    smk_spec_src = (src == SMK_SPEC_SRC_RES) ? SMK_SPEC_SRC_RES : SMK_SPEC_SRC_CUT;
}

static int smk_spec_owner(void)
{
    return (int)smk_spec_src;
}

/* 1: spectrum still showing. Clears itself when the hold expires. */
static int smk_spec_active(uint32_t now)
{
    if (!smk_spec_lit)
        return 0;
    if (now - smk_spec_last_ms >= (uint32_t)SMK_SPEC_HOLD_MS) {
        smk_spec_lit = 0;
        return 0;
    }
    return 1;
}
