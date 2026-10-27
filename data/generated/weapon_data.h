/*
 * Weapon Data
 * Source: bin/0C_8000_weapondata.bin (bank $0C, ROM $8000)
 * Generated: 2026-10-27
 * 8 bytes per weapon. See bank_0C.asm for equip logic.
 */

#pragma once

#include <stdint.h>

#define WEAPON_COUNT 40

typedef struct {
    uint8_t hit_rate;      // +0: Hit rate bonus
    uint8_t damage;        // +1: Base damage
    uint8_t crit_rate;     // +2: Critical hit rate (BUGGED - not used)
    uint8_t spell_id;      // +3: Spell cast on hit
    uint8_t element;       // +4: Elemental property
    uint8_t category;      // +5: Category effectiveness (vs Giant/Undead/etc)
    uint8_t battle_gfx;    // +6: Graphic index in battle
    uint8_t palette;       // +7: Palette index
} WeaponData;

extern WeaponData weapon_table[WEAPON_COUNT];
