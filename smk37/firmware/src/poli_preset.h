/* SPDX-License-Identifier: GPL-3.0-only */
/* Polivoks preset store: 63 slots, all writable (the 4 KiB bank maximum).
 * Older 50-slot banks (PV_NSLOT_V1) still load and expand to 63; 50 is not a
 * cap on this firmware or on the SMK-37 port. Locked decisions 1-12 still apply.
 * artifacts/polivoks-presets-spec-20261111/LOCKED-DECISIONS.md. */
#ifndef POLI_PRESET_H
#define POLI_PRESET_H
#include <stdint.h>

#define PV_NSLOT 63
#define PV_NSLOT_V1 50   /* banks written by the 50-slot builds */
#define PV_NAME 12

enum { PV_MODE_NONE = 0, PV_MODE_BAR = 1, PV_MODE_SAVE = 2 };
enum { PV_ACT_SAVE = 0, PV_ACT_BACKUP, PV_ACT_RESTORE, PV_ACT_FACTORY, PV_NACT };
enum { PV_MSG_NONE = 0, PV_MSG_LOADED, PV_MSG_SAVED, PV_MSG_BACKED, PV_MSG_RESTORED, PV_MSG_FACTORY,
       PV_MSG_NOBACKUP, PV_MSG_RAMONLY };

typedef struct {
    int mode;                  /* PV_MODE_* */
    int cur;                   /* 0..62, the loaded slot */
    int target;                /* bar: the slot SEL loads; save screen: where SAVE writes */
    int dirty;                 /* the sound differs from the stored slot (the asterisk) */
    int cmp;                   /* save screen: 1 = playing the stored version */
    int cursor;                /* save screen: name letter 0..11 */
    int action;                /* save screen: PV_ACT_* */
    int msg;                   /* PV_MSG_*, shown until the next control */
    uint8_t cur_name[PV_NAME];
    uint8_t target_name[PV_NAME];
    uint8_t edit_name[PV_NAME];
} poli_preset_view_t;

void poli_preset_init(uint32_t now_ms);    /* after poli_nav_init: loads the bank, boots the last slot */
void poli_preset_turn(int steps, uint32_t now_ms);  /* PRESETS encoder */
void poli_preset_sel(void);               /* bar: load; save screen: compare */
void poli_preset_load_slot(int slot, uint32_t now_ms); /* 0..PV_NSLOT-1: same as SEL on the bar (pv_load). Ignored if out of range or the save screen is open. */
void poli_preset_save_btn(uint32_t now_ms);/* SAVE: open the save screen, or do its action */
void poli_preset_cancel(void);            /* HOME, section buttons, SELECT turn while a preset view is up */
void poli_preset_knob(int index, int steps); /* save screen only */
void poli_preset_tick(uint32_t now_ms);    /* main loop */
int poli_preset_mode(void);
void poli_preset_view(poli_preset_view_t *v);
/* name code -> UTF-8, trailing spaces dropped when trim; out >= 3 * PV_NAME + 1 */
void poli_preset_name_utf8(const uint8_t code[PV_NAME], char *out, int trim);
#endif
