/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1: the Elite classic arpeggiator is compiled out (Drums-12 uses EXTRAS in poli_xtra.c). */
#ifndef POLI_ARP_H
#define POLI_ARP_H
#include <stdint.h>
enum { ARP_UP = 0, ARP_DOWN, ARP_UPDOWN, ARP_RANDOM, ARP_PLAYED, ARP_NMODE };
enum { ARP_NRATE = 7 };
#define ARP_GATE_MIN 5
#define ARP_GATE_MAX 100
#define ARP_GATE_STEP 5
#define ARP_MIN_OFF_MS 12u
#define ARP_HOLD_MS 500u
#define ARP_EXT_GAP_MS 500u
#define ARP_MAXN 148
typedef struct {
    int on, latch, mode, rate, oct, gate;
    int page;
    int ext, running;
    int nheld;
    int bpm;
    int note;
} poli_arp_view_t;
static void poli_arp_init(void) {}
static uint64_t poli_arp_keys(uint64_t keys, const volatile uint8_t *vel, uint32_t now_ms)
{ (void)vel; (void)now_ms; return keys; }
static void poli_arp_poll(uint32_t now_ms) { (void)now_ms; }
static void poli_arp_clear(void) {}
static void poli_arp_set_on(int on) { (void)on; }
static int poli_arp_on(void) { return 0; }
static void poli_arp_set(int mode, int rate, int oct, int gate, int latch)
{ (void)mode; (void)rate; (void)oct; (void)gate; (void)latch; }
static void poli_arp_knob(int index, int steps) { (void)index; (void)steps; }
static void poli_arp_latch_toggle(void) {}
static void poli_arp_button(int pressed, int released, int held, uint32_t now_ms)
{ (void)pressed; (void)released; (void)held; (void)now_ms; }
static int poli_arp_page_open(void) { return 0; }
static int poli_arp_page_close(void) { return 0; }
static void poli_arp_view(poli_arp_view_t *v) { if (v) { *v = (poli_arp_view_t){0}; v->note = -1; } }
static void poli_arp_clock(uint32_t now_ms) { (void)now_ms; }
static void poli_arp_start(void) {}
static void poli_arp_continue(void) {}
static void poli_arp_stop(void) {}
static int poli_arp_pattern(uint8_t *out, int max) { (void)out; (void)max; return 0; }
static uint32_t poli_arp_generation(void) { return 0; }
#endif
