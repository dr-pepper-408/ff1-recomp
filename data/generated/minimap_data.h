/*
 * Minimap
 * Source: bank_09.asm, bin/minimap_*
 * Generated: 2026-10-27
 * The overworld minimap: nametable, decoration/title/sprite CHR, tables.
 */

#pragma once

#include <stdint.h>

/* bank_09 lut_MinimapNT: the full nametable (960 tiles + attributes) */
extern const uint8_t lut_MinimapNT[1024];

/* bank_09 chr_MinimapDecor: $30 tiles of the dragon decorations */
extern const uint8_t chr_MinimapDecor[768];

/* bank_09 chr_MinimapTitle: $28 tiles of the FINAL FANTASY title */
extern const uint8_t chr_MinimapTitle[640];

/* bank_09 lut_MinimapSprCHR: sprite tiles $80 (you are here) and $81 (towns) */
extern const uint8_t lut_MinimapSprCHR[32];

/* bank_09.asm lut_MinimapBGPal: BG palettes */
extern const uint8_t lut_MinimapBGPal[16];

/* bank_09.asm lut_MinimapTileset: per overworld tile: color (bits 0-1), town marker (bit 2) */
extern const uint8_t lut_MinimapTileset[128];

/* bank_09.asm lut_MinimapBitplane: the byte of each tile written on each of 16 frames */
extern const uint8_t lut_MinimapBitplane[16];

/* bank_09.asm lut_MinimapDecorCHRDest_lo: decoration tiles' pattern table addresses */
extern const uint8_t lut_MinimapDecorCHRDest_lo[48];

extern const uint8_t lut_MinimapDecorCHRDest_hi[48];

/* bank_09.asm lut_MinimapTitleCHRDest_lo: title tiles' pattern table addresses */
extern const uint8_t lut_MinimapTitleCHRDest_lo[40];

extern const uint8_t lut_MinimapTitleCHRDest_hi[40];

