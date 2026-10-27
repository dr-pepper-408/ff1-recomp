/*
 * Armor Data
 * Source: bin/0C_8140_armordata.bin (bank $0C, ROM $8140)
 * Generated: 2026-10-27
 * 4 bytes per armor piece. See bank_0C.asm.
 */

#pragma once

#include <stdint.h>

#define ARMOR_COUNT 40

typedef struct {
    uint8_t evade_penalty;  // +0: Evade penalty
    uint8_t absorb_boost;   // +1: Absorb (defense) boost
    uint8_t elem_defense;   // +2: Elemental defense bits
    uint8_t spell_id;       // +3: Spell cast
} ArmorData;

extern ArmorData armor_table[ARMOR_COUNT];
