/*
 * Fiend / Chaos TSA
 * Source: bin/0B_92E0_fiendtsa.bin, bin/0B_9420_chaostsa.bin (bank $0B)
 * Generated: 2026-10-27
 * Tile maps (tile ids in the enemy CHR page, 0 = blank) and the $10 attribute bytes
 * that PrepareEnemyFormation_Fiend/_Chaos copy to attribute rows 1-4, columns 0-3.
 */

#pragma once

#include <stdint.h>

/* 4 fiends (by formation graphic assignment): 8x8 tiles + 16 attribute bytes. */
extern const uint8_t fiend_tsa[4][0x50];

/* Chaos: 14x12 tiles, then 16 attribute bytes (and 8 bytes of padding). */
extern const uint8_t chaos_tsa[192];
