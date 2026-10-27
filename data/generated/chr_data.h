/*
 * CHR Data
 * Source: bank_02/03/07/08.dat, bin/bank_09_data.bin + bank_09.asm
 * Generated: 2026-10-27
 * Raw NES 2bpp tile data: tile n is bytes [n*16, n*16+16)
 * (8 bytes bit plane 0, then 8 bytes bit plane 1).
 */

#pragma once

#include <stdint.h>

/* BANK_MAPCHR: overworld tiles, mapman ($9000), map objects ($A200) */
extern const uint8_t chr_bank_02[16384];

/* BANK_TILESETCHR: 8 standard-map tilesets x 128 tiles */
extern const uint8_t chr_bank_03[16384];

/* BANK_BATTLECHR: battle backdrops + enemy pages 0-7 (128 tiles each) */
extern const uint8_t chr_bank_07[16384];

/* battle backdrops + enemy pages 8-15 */
extern const uint8_t chr_bank_08[16384];

/* BANK_MENUCHR/BANK_BTLCHR $8000-$AFFF: font $8800, battle sprites $9000 */
extern const uint8_t chr_bank_09[12288];

/* lut_OrbCHR (BANK_ORBCHR $B600, 2 rows): menu tiles $60-$7F */
extern const uint8_t chr_orbs[512];

/* data_BridgeCHR (bank_0B $B400, 8 rows): bridge scene tiles $00-$7F */
extern const uint8_t chr_bridge[2048];

