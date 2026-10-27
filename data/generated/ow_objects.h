/*
 * Overworld Objects
 * Source: bank_0F.asm
 * Generated: 2026-10-27
 * Overworld object sprites (DrawOWObj_*).
 */

#pragma once

#include <stdint.h>

/* bank_0F.asm lut_OWObjectSprTbl: tile, attribute for UL, DL, UR, DR;
 * 0 airship shadow, 1 bridge, 2 canal.  Tiles are drawn + $10 (sprite
 * pattern table $1100, LoadOWObjectCHR: bank_02 $9C00). */
extern const uint8_t lut_OWObjectSprTbl[3][8];

/* bank_0F.asm lut_PlayerMapmanSprTbl: the party on foot: 8 tables (R0 R1 L0 L1 U0 U1 D0 D1) of tile, attribute for UL, DL, UR, DR; tiles from the lead class's mapman CHR */
extern const uint8_t lut_PlayerMapmanSprTbl[64];

/* bank_0F.asm lut_VehicleSprTbl: the same for the vehicles; tiles + $50 canoe, $20 ship, $38 airship */
extern const uint8_t lut_VehicleSprTbl[64];

/* bank_0F.asm lut_VehicleSprY: sprite y by vehicle (1 foot, 2 canoe, 4 ship, 8 airship) */
extern const uint8_t lut_VehicleSprY[9];

/* bank_0F.asm lut_VehicleFacingSprTblOffset: sprite table offset by facing bits */
extern const uint8_t lut_VehicleFacingSprTblOffset[16];

/* bank_00 lut_MapmanPalettes ($83A0): the mapman's two body colours by class
 * (LoadMapPalettes: sprite palettes 0 and 1, colour 2). */
extern const uint8_t lut_MapmanPalettes[24];

