/*
 * Map Data
 * Source: bank_00.dat, bank_01.asm, bank_04.dat
 * Generated: 2026-10-27
 * Overworld and standard map tile data. NEEDS FURTHER ANALYSIS for full format.
 */

#pragma once

#include <stdint.h>

// Overworld tileset properties (lut_OWTileset = $8000)
#define OW_TILESET_SIZE 1024
extern uint8_t ow_tileset[1024];

// Standard map tileset attributes (lut_SMTilesetAttr = $8400)
#define SM_TILESET_ATTR_SIZE 1024


// Standard map tileset properties (lut_SMTilesetProp = $8800)
#define SM_TILESET_PROP_SIZE 2048


// Standard map tileset TSA (lut_SMTilesetTSA = $9000)
#define SM_TILESET_TSA_SIZE 4096


// NOTE: Full map decoding (overworld grid, standard maps) needs further analysis
// of the assembly routines in bank_01.asm and bank_04.
