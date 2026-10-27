/*
 * Battle Formations
 * Source: bin/0B_8400_battleformations.bin (bank $0B, ROM $8400)
 * Generated: 2026-10-27
 * 16 bytes per formation. See some formats.txt.
 */

#pragma once

#include <stdint.h>

#define BATTLE_FORMATION_COUNT 128

typedef struct {
    uint8_t type_pattern;   // +0: High nibble=battle type, low nibble=pattern table
    uint8_t gfx_assign;     // +1: Graphics assignment (2 bits per enemy, bit-reversed)
    uint8_t enemy_ids[4];   // +2-+5: Enemy IDs
    uint8_t qty_a[4];       // +6-+9: Min/max counts, Formation A
    uint8_t palette_0;      // +10: Palette ID 0
    uint8_t palette_1;      // +11: Palette ID 1
    uint8_t surprise_rate;  // +12: Surprise rate
    uint8_t flags;          // +13: High nibble=palette assign, bit0=no-run
    uint8_t qty_b[2];       // +14-+15: Min/max counts, Formation B
} BattleFormation;

extern BattleFormation battle_formation_table[BATTLE_FORMATION_COUNT];
