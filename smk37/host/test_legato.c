/* SPDX-License-Identifier: GPL-3.0-only */
/* SMK-37 37-key bed uses the authentic Polivoks panel rule: lowest held
 * key is GEN I; a higher legato key is silent with VOICES=1 and goes to
 * GEN II with VOICES=2. End keys and the old E3/F3 join follow the same
 * mask, not the retired outer-key MIDI path. */
#define BOARD_SMK37 1
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "note_inc.c"
#include "poli_fcore.c"
#include "polivoks_limited.c"

static int fail;

static void expect(const char *name, int ok, const char *detail)
{
    printf("%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok)
        fail++;
}

/* Key k of 0..36 → MIDI at octave 0: C3=60 .. C6=96 (F3, key 5, is 65). */
static int key_note(int k) { return 60 + k; }

static void bed(uint64_t mask)
{
    polivoks_panel(mask, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

static uint64_t bit(int k) { return (uint64_t)1u << k; }

static int sounding(int n1, int n2)
{
    int a = (int)poli_note == n1;
    if (n2 < 0)
        return a && poli_note2 == 255u && poli_key_on == 1;
    return a && (int)poli_note2 == n2 && poli_key_on == 1;
}

int main(void)
{
    char line[200];
    int e3 = 4, f3 = 5;     /* former MIDI / panel join */
    int mid_lo = 12, mid_hi = 16; /* C4 then E4 */
    int end_lo = 0, end_hi = 2;   /* C3 then D3 */
    int top_lo = 34, top_hi = 36; /* A#5 then C6 */

    fail = 0;
    polivoks_init();
    polivoks_set_kb_oct(0);
    polivoks_set_duo(1);    /* VOICES=2 */

    /* Low, then a higher key: main pitch stays on the low note. */
    bed(bit(mid_lo));
    expect("mid_single", sounding(key_note(mid_lo), -1), "");
    bed(bit(mid_lo) | bit(mid_hi));
    snprintf(line, sizeof line, "note %u note2 %u (want %d / %d)",
             poli_note, poli_note2, key_note(mid_lo), key_note(mid_hi));
    expect("mid_upward_legato_keeps_low", sounding(key_note(mid_lo), key_note(mid_hi)), line);

    /* VOICES=1: still lowest-note; the second key does not take GEN I. */
    polivoks_set_duo(0);
    bed(bit(mid_lo) | bit(mid_hi));
    expect("voices1_still_lowest", (int)poli_note == key_note(mid_lo) && poli_key_on == 1, "");
    polivoks_set_duo(1);

    /* Release the high key: the low one stays; it is not cut. */
    bed(bit(mid_lo));
    expect("release_high_keeps_low", sounding(key_note(mid_lo), -1), "");

    /* High first, then a lower key: pitch drops to the new lowest. */
    bed(0);
    bed(bit(mid_hi));
    bed(bit(mid_lo) | bit(mid_hi));
    expect("mid_downward_takes_low", sounding(key_note(mid_lo), key_note(mid_hi)), "");

    /* Release the low key: remaining high key sounds alone. */
    bed(bit(mid_hi));
    expect("release_low_keeps_high", sounding(key_note(mid_hi), -1), "");
    bed(0);

    /* Former join E3 (key 4) / F3 (key 5): same rule, no path clash. */
    bed(bit(e3));
    expect("e3_alone", sounding(key_note(e3), -1), "");
    bed(bit(e3) | bit(f3));
    snprintf(line, sizeof line, "note %u note2 %u", poli_note, poli_note2);
    expect("e3_f3_legato_keeps_e3", sounding(key_note(e3), key_note(f3)), line);
    bed(bit(e3));
    expect("release_f3_keeps_e3", sounding(key_note(e3), -1), "");
    bed(0);
    bed(bit(e3) | bit(f3));
    bed(bit(f3));
    expect("release_e3_keeps_f3", sounding(key_note(f3), -1), "");
    bed(0);

    /* End keys behave like middle keys. */
    bed(bit(end_lo));
    bed(bit(end_lo) | bit(end_hi));
    expect("low_end_upward_keeps_low", sounding(key_note(end_lo), key_note(end_hi)), "");
    bed(bit(end_lo));
    expect("low_end_release_high_keeps_low", sounding(key_note(end_lo), -1), "");
    bed(0);
    bed(bit(end_hi));
    bed(bit(end_lo) | bit(end_hi));
    expect("low_end_downward_takes_low", sounding(key_note(end_lo), key_note(end_hi)), line);
    bed(bit(end_hi));
    expect("low_end_release_low_keeps_high", sounding(key_note(end_hi), -1), "");
    bed(0);

    bed(bit(top_lo));
    bed(bit(top_lo) | bit(top_hi));
    expect("high_end_upward_keeps_low", sounding(key_note(top_lo), key_note(top_hi)), "");
    bed(bit(top_lo));
    expect("high_end_release_high_keeps_low", sounding(key_note(top_lo), -1), "");
    bed(0);
    bed(bit(top_hi));
    bed(bit(top_lo) | bit(top_hi));
    expect("high_end_downward_takes_low", sounding(key_note(top_lo), key_note(top_hi)), "");
    bed(bit(top_hi));
    expect("high_end_release_low_keeps_high", sounding(key_note(top_hi), -1), "");
    bed(0);

    /* Releasing one key never cuts another held key, across the join and both ends. */
    {
        const int pairs[][2] = { {0, 1}, {4, 5}, {5, 6}, {20, 24}, {31, 32}, {35, 36} };
        int i, ok = 1;
        for (i = 0; i < 6; i++) {
            int a = pairs[i][0], b = pairs[i][1];
            bed(bit(a) | bit(b));
            bed(bit(a));
            if (!sounding(key_note(a), -1))
                ok = 0;
            bed(bit(a) | bit(b));
            bed(bit(b));
            if (!sounding(key_note(b), -1))
                ok = 0;
            bed(0);
        }
        expect("release_one_never_cuts_held", ok, "pairs include E3/F3 and both ends");
    }

    /* Base map: key 0 is C3 (60), key 5 is F3 (65), key 36 is C6 (96). */
    bed(bit(0));
    expect("key0_is_c3", (int)poli_note == 60, "");
    bed(bit(5));
    expect("key5_is_f3", (int)poli_note == 65, "");
    bed(bit(36));
    expect("key36_is_c6", (int)poli_note == 96, "");
    bed(0);
    expect("all_up_releases", poli_key_on == 0 && poli_note2 == 255u, "");

    /* External MIDI is still last-note (unchanged). Not used for the keybed. */
    polivoks_midi(0x90, 70, 100);
    expect("midi_last_note", poli_note == 70 && poli_midi_on == 1, "");
    polivoks_midi(0x90, 74, 100);
    expect("midi_newer_takes", poli_note == 74 && poli_midi_on == 1, "");
    polivoks_midi(0x80, 74, 0);
    /* existing MIDI off clears the MIDI gate, even if 70 was earlier — documented. */
    expect("midi_any_off_clears_gate", poli_midi_on == 0, "");

    return fail ? 1 : 0;
}
