/*
 * Battle Backdrops
 * Source: bank_00.dat (ROM $B200 lut_BackdropPal / $B300 lut_BtlBackdrops)
 * Generated: 2026-10-27
 * 16 backdrops; each is a 4x4 tile pattern in battle_chr.png/battle_chr2.png (16 tiles at backdrop_id*128), drawn as a tiled background.  backdrop_table maps an overworld tile id (0..127) to its backdrop id (0xFF = no battle).
 */

#pragma once

#include <stdint.h>

#define BATTLE_BACKDROP_COUNT 16

/* Overworld tile id (0..127) -> backdrop id (0..15, 0xFF = none). */
extern const uint8_t backdrop_table[128];

/* 4-color palette per backdrop (NES color indices; index 0 = base). */
extern const uint8_t backdrop_palettes[BATTLE_BACKDROP_COUNT][4];
