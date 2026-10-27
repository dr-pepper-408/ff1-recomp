/*
 * Battle Palettes
 * Source: bin/0C_8F20_battlepalettes.bin (bank $0C, ROM $8F20)
 * Generated: 2026-10-27
 * $20 bytes per palette (16 NES colors × 2 bytes each).
 */

#pragma once

#include <stdint.h>

#define BATTLE_PALETTE_COUNT 8
#define BATTLE_PALETTE_COLORS 16

extern const uint8_t battle_palette_table[BATTLE_PALETTE_COUNT][BATTLE_PALETTE_COLORS * 2];
