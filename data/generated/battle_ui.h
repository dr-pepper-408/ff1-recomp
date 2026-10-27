/*
 * Battle UI Tables
 * Source: bank_0B / bank_0C / bank_0F .BYTE tables
 * Generated: 2026-10-27
 * Battle screen layout data, copied from the disassembly.
 */

#pragma once

#include <stdint.h>

/* bank_0C.asm lut_MainCombatBoxCursorPos: x,y per 2x4 command cell (col-major: FIGHT..ITEM, RUN x4) */
extern uint8_t lut_MainCombatBoxCursorPos[16];

/* bank_0C.asm lut_PlayerTargetCursorPos: x,y per character (mirrored) */
extern uint8_t lut_PlayerTargetCursorPos[16];

/* bank_0C.asm lut_Target9SmallCursorPos: x,y per cursor index */
extern uint8_t lut_Target9SmallCursorPos[18];

/* bank_0C.asm lut_Target4LargeCursorPos: x,y per cursor index */
extern uint8_t lut_Target4LargeCursorPos[8];

/* bank_0C.asm lut_TargetMixCursorPos: x,y per cursor index */
extern uint8_t lut_TargetMixCursorPos[16];

/* bank_0C.asm lut_MagicCursorPos: x,y, column-major (3 columns x 4 rows) */
extern uint8_t lut_MagicCursorPos[24];

/* bank_0C.asm lut_ItemCursorPos: x,y, weapons then armor */
extern uint8_t lut_ItemCursorPos[16];

/* bank_0C.asm lut_DrinkCursorPos: x,y, HEAL/PURE (mirrored) */
extern uint8_t lut_DrinkCursorPos[16];

/* bank_0C.asm lut_EnemyIndex9Small: cursor index -> enemy slot */
extern uint8_t lut_EnemyIndex9Small[9];

/* bank_0C.asm lut_EnemyIndex4Large: cursor index -> enemy slot */
extern uint8_t lut_EnemyIndex4Large[4];

/* bank_0C.asm lut_EnemyIndexMix: cursor index -> enemy slot */
extern uint8_t lut_EnemyIndexMix[8];

/* bank_0F.asm lut_CombatBoxes: per box: hdr,x,y,w,h (box) then hdr,x,y (text); y is relative to row 18 */
extern uint8_t lut_CombatBoxes[40];

/* bank_0F.asm lut_EnemyRosterBox: hdr,x,y,w,h */
extern uint8_t lut_EnemyRosterBox[5];

/* bank_0F.asm lut_CombatItemMagicBox: hdr,x,y,w,h */
extern uint8_t lut_CombatItemMagicBox[5];

/* bank_0F.asm lut_CombatDrinkBox: hdr,x,y,w,h */
extern uint8_t lut_CombatDrinkBox[5];

/* bank_0F.asm lua_BattleCommandBoxInfo: the box: hdr,x,y,w,h */
extern uint8_t lua_BattleCommandBoxInfo[5];

/* bank_0B.asm lut_RespondDelay: frames per respond rate */
extern uint8_t lut_RespondDelay[8];

/* bank_0F.asm @lut_BackdropLayout: backdrop tile per column */
extern uint8_t lut_BackdropLayout[14];

/* bank_0C.asm lut_ExplosionCoords_9Small: explosion area top-left x,y per slot (+$18) */
extern uint8_t lut_ExplosionCoords_9Small[18];

/* bank_0C.asm @lut_ExplosionCoords_4Large: explosion area top-left x,y per slot (+$28) */
extern uint8_t lut_ExplosionCoords_4Large[8];

/* bank_0C.asm @lut_ExplosionCoords_6Small: mix formation small slots 2-7 (+$18) */
extern uint8_t lut_ExplosionCoords_6Small[12];

/* bank_0C.asm lut_CharacterPoseTSA: per pose ($04 apart): the 3 row tiles + pad */
extern uint8_t lut_CharacterPoseTSA[32];

/* bank_0C.asm lut_WeaponSwingTSA: per frame: 4 tile offsets, 4 attributes */
extern uint8_t lut_WeaponSwingTSA[16];

/* bank_0C.asm lut_InBattleCharPaletteAssign: sprite palette per class */
extern uint8_t lut_InBattleCharPaletteAssign[12];

/* bank_0F.asm @BattleSpritePalettes: LoadBattleSpritePalettes: the 4 sprite palettes */
extern uint8_t BattleSpritePalettes[16];

/* bank_0F lua_BattleCommandBoxInfo @txt0-4: FIGHT, MAGIC, DRINK, ITEM, RUN ($00-terminated). */
extern uint8_t lut_BattleCommandText[5][5];

/* bank_0F lua_BattleCommandBoxInfo text blocks: x,y of each command (y relative to row 18). */
extern uint8_t lut_BattleCommandTextPos[10];

/* bank_0C lut_CharStatusPPUAddr: nametable address of each character's status. */
extern uint16_t lut_CharStatusPPUAddr[4];
