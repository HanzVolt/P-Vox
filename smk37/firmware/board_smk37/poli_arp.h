/* SPDX-License-Identifier: GPL-3.0-only */
/* Arpeggiator for the Polivoks SMK-37 Elite port (Hans: "yes, code it", 14 Nov 2026, arp only).
 * Core 0 only: main loop (volt.c) plus the LCD idle hook, timed by fm1_ms (TIMER5, 10 kHz -> 1 ms).
 * Never the audio ISR or polivoks_render. Notes go to the engine through polivoks_arp_gate()
 * (the existing mono note path: poli_note + a gate source in poli_recompute_gate).
 * Settings are global and live in RAM only; the preset record layout is untouched.
 * Clock: the internal tempo (the last MIDI clock tempo, held, or 120 BPM if none was ever
 * measured) or, while MIDI clock (0xF8) arrives, the clock pulses themselves. Start / Continue /
 * Stop are honoured while the clock runs. Every panic (CC 120, CC 123) and every preset change
 * (pad, SEL load, compare, restore, factory, program change) calls poli_arp_clear(). */
#ifndef POLI_ARP_H
#define POLI_ARP_H
#include <stdint.h>

enum { ARP_UP = 0, ARP_DOWN, ARP_UPDOWN, ARP_RANDOM, ARP_PLAYED, ARP_NMODE };
enum { ARP_NRATE = 7 };        /* 1/4 1/4T 1/8 1/8T 1/16 1/16T 1/32 */
#define ARP_GATE_MIN 5
#define ARP_GATE_MAX 100       /* 100 % = tied (legato, no retrigger) */
#define ARP_GATE_STEP 5
#define ARP_MIN_OFF_MS 12u     /* two 5.8 ms render halves: the engine always sees the gap */
#define ARP_HOLD_MS 500u       /* ARP held this long opens the ARP page */
#define ARP_EXT_GAP_MS 500u    /* no 0xF8 for this long: back on the internal tempo */
#define ARP_MAXN 148           /* 37 keys x 4 octaves */

typedef struct {
    int on, latch, mode, rate, oct, gate;   /* settings */
    int page;                               /* ARP page is open */
    int ext, running;                       /* MIDI clock drives the steps; transport running */
    int nheld;                              /* notes in the arp's set */
    int bpm;                                /* internal tempo, whole BPM */
    int note;                               /* sounding arp note, -1 none */
} poli_arp_view_t;

void poli_arp_init(void);
/* keybed in, engine keys out: bit k = key k (base 60 + k + kb_oct*12 on the SMK, 65 on FM-1/host).
 * vel = per-key velocity array (NULL: 127). Returns the mask polivoks_panel should see
 * (the keys unchanged while the arp is off, 0 while it is on). Runs the step clock too. */
uint64_t poli_arp_keys(uint64_t keys, const volatile uint8_t *vel, uint32_t now_ms);
void poli_arp_poll(uint32_t now_ms);                 /* step clock only (LCD idle hook) */
void poli_arp_clear(void);                           /* panic / preset change: unconditional */
void poli_arp_set_on(int on);
int poli_arp_on(void);
void poli_arp_set(int mode, int rate, int oct, int gate, int latch);   /* tests; clamps */
void poli_arp_knob(int index, int steps);            /* ARP page K5..K8 = index 0..3 */
void poli_arp_latch_toggle(void);                    /* ARP page SEL */
/* ARP button: pressed / released edges and the held level, once per main-loop pass */
void poli_arp_button(int pressed, int released, int held, uint32_t now_ms);
int poli_arp_page_open(void);
int poli_arp_page_close(void);                       /* returns 1 if it was open */
void poli_arp_view(poli_arp_view_t *v);
/* MIDI realtime, from poli_nav_midi (main loop) */
void poli_arp_clock(uint32_t now_ms);
void poli_arp_start(void);
void poli_arp_continue(void);
void poli_arp_stop(void);
/* the step pattern for the current set, mode and octaves (tests); returns its length */
int poli_arp_pattern(uint8_t *out, int max);
uint32_t poli_arp_generation(void);                  /* bumps on every visible change */
#endif
