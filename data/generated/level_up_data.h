/*
 * Level-Up Data
 * Source: bin/0B_9094_levelupdata.bin (bank $0B, ROM $9094)
 * Generated: 2026-10-27
 * 2 bytes per level, 49 levels per class, 6 classes. See bank_0B.asm for bit layout.
 */

#pragma once

#include <stdint.h>

#define LEVEL_UP_CLASSES 6
#define LEVEL_UP_LEVELS 49

// Per-class, per-level stat gains.
// Byte 0: bit5=strong HP, bit4=guaranteed Str, bit3=Agil, bit2=Int, bit1=Vit, bit0=Luck
// Byte 1: MP up - each bit corresponds to a level of spell charge gained

extern uint8_t level_up_data[LEVEL_UP_CLASSES][LEVEL_UP_LEVELS][2];

/* bank_0B lut_ExpToAdvance: total EXP to reach the next level, by 0-based level. */
extern uint32_t lut_ExpToAdvance[49];

/* bank_0B lut_LvlUpHitRateBonus, by class. */
extern uint8_t lut_LvlUpHitRateBonus[12];

/* bank_0B lut_LvlUpMagDefBonus, by class. */
extern uint8_t lut_LvlUpMagDefBonus[12];
