/* SPDX-License-Identifier: GPL-3.0-only */
/* Parity (23 Nov 2026): the FM-1 build gets the Elite's PAD REC, its classic arpeggiator (with a screen page),
 * the FILTER / AMP EG plots, the 24-bar spectrum footer and the value-only repaint. Each one is a switch, so a
 * build can leave it out (DINGHY_DEFS=-DPOLI_PADREC=0 ./build.sh) and its flash cost can be measured alone.
 * Main-loop / UI side only: none of them is in the audio interrupt or the synth render. */
#ifndef POLI_FEAT_H
#define POLI_FEAT_H
#ifndef BOARD_SMK37
#define BOARD_SMK37 0
#endif
#ifndef POLI_PADREC
#define POLI_PADREC 1          /* PAD REC: the 16 white keys as pads, 8 grooves in flash blob 0 */
#endif
#ifndef POLI_ARP
#define POLI_ARP 1             /* the classic arpeggiator and its CLASSIC ARP page */
#endif
#ifndef POLI_EGPLOT
#define POLI_EGPLOT 1          /* FILTER page with the live filter-EG plot; AMP EG page with the plot and its dot */
#endif
#ifndef POLI_SPECTRUM
#define POLI_SPECTRUM 1        /* 24-bar spectrum footer on the synth pages (main-loop audio snapshot) */
#endif
#ifndef POLI_VALREPAINT
#define POLI_VALREPAINT 1      /* a knob that only moves a value repaints the panel, not the whole screen */
#endif
/* The features' RAM goes in its own sections. The compiler otherwise packs small statics together with the engine's
 * and USB's (one base address, offsets), so adding one would shift offsets inside the audio / USB interrupt code.
 * With these the interrupt code stays as in the parent build (checked with ds-tools/code_compare.py). The section
 * still sits before the COMMON block (xf_patch, xf_par), so core 2's code sees those two at new absolute addresses
 * (c1text.bin: 16 bytes, addresses only). PV_FEAT_DATA is defined for completeness; no feature uses .data. */
#ifndef DINGHY_HOST
#define PV_FEAT_BSS  __attribute__((section(".bss.pvfeat")))
#define PV_FEAT_DATA __attribute__((section(".data.pvfeat")))
#else
#define PV_FEAT_BSS
#define PV_FEAT_DATA
#endif
#endif
