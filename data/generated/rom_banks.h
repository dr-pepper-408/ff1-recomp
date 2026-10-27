/*
 * ROM Banks
 * Source: bank_00.dat, bank_04-06.dat, bin/bank_01_data.bin + bank_01.asm
 * Generated: 2026-10-27
 * Raw ROM bytes for the map code, indexed by ROM address - $8000.
 */

#pragma once

#include <stdint.h>

/* bank_00: standard-map tilesets, palettes, teleport tables. */
extern const uint8_t rom_bank_00[16384];

/* banks 04-06 back to back: standard-map data (lut_SMPtrTbl at $8000). */
extern const uint8_t rom_sm_maps[49152];

/* bank_01 $8000-$BFFF: overworld row pointers + RLE rows. */
extern const uint8_t rom_ow_bank[16384];

