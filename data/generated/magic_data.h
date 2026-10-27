/*
 * Magic Data
 * Source: bin/0C_81E0_magicdata.bin (bank $0C, ROM $81E0)
 * Generated: 2026-10-27
 * 8 bytes per entry: 64 spells + 2 potions + 26 enemy attacks.
 */

#pragma once

#include <stdint.h>

#define MAGIC_SPELL_COUNT 64
#define MAGIC_POTION_COUNT 2
#define MAGIC_ATTACK_COUNT 26
#define MAGIC_TOTAL_COUNT 92

typedef struct {
    uint8_t hit_rate;     // +0: Hit rate
    uint8_t effectivity;  // +1: Effectivity
    uint8_t element;      // +2: Element
    uint8_t target;       // +3: Target (1=all enemies, 2=one enemy, 4=self, 8=all party, 0x10=one party)
    uint8_t effect;       // +4: Effect type
    uint8_t graphic;      // +5: Graphic index
    uint8_t palette;      // +6: Palette index
    uint8_t unused;       // +7: Unused
} MagicData;

extern MagicData magic_table[MAGIC_TOTAL_COUNT];
