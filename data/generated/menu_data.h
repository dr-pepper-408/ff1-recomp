/*
 * Menu Data
 * Source: bank_0E.asm, bank_0F.asm
 * Generated: 2026-10-27
 * Field menu layout, cursors, palettes, equipment permissions.
 */

#pragma once

#include <stdint.h>

/* bank_0E.asm lut_MainItemBoxes: x, y, width, height: 0 orbs, 1-4 characters, 5 gold, 6 options, 7 title, 8 description, 9 list */
extern const uint8_t lut_MainItemBoxes[40];

/* bank_0E.asm lutMenuPalettes: BG palettes 0-2 (0: lit orbs) */
extern const uint8_t lutMenuPalettes[12];

/* bank_0E.asm lut_MainMenuSubCursor: x, y per character box */
extern const uint8_t lut_MainMenuSubCursor[8];

/* bank_0E.asm lut_MainMenuCursor_Y: option cursor y (x is $10) */
extern const uint8_t lut_MainMenuCursor_Y[5];

/* bank_0E.asm lut_ItemMenuCursor: x, y per item slot */
extern const uint8_t lut_ItemMenuCursor[48];

/* bank_0F.asm lutItemBoxStrPos: tile x, y per item slot */
extern const uint8_t lutItemBoxStrPos[48];

/* bank_0E.asm @lut: DrawItemTargetCursor: x per character (y is $68) */
extern const uint8_t lut_ItemTargetCursorX[4];

/* bank_0E.asm @lutStatusBoxes: x, y, width, height of the 6 status boxes */
extern const uint8_t lutStatusBoxes[24];

/* bank_0E.asm @lut_EquipBoxes: x, y, width, height: 0 title, 1 modes, then name/equipment box per character */
extern const uint8_t lut_EquipBoxes[40];

/* bank_0E.asm lut_EquipMenuCurs: x, y per equipment slot */
extern const uint8_t lut_EquipMenuCurs[32];

/* bank_0E.asm @lut_CursorX: DrawEquipMenuModeCurs: x per mode (y is $14) */
extern const uint8_t lut_EquipModeCursorX[3];

/* bank_0E.asm @lut_Attr: UpdateEquipMenuModeAttrib: attribute bytes $23C3-$23C6 per mode */
extern const uint8_t lut_EquipModeAttr[12];

/* bank_0F.asm lut_EquipStringPositions: tile x, y per equipment slot */
extern const uint8_t lut_EquipStringPositions[32];

/* bank_0E.asm lut_ArmorTypes: 0 body, 1 shield, 2 helmet, 3 gloves (by 0-based armor id) */
extern const uint8_t lut_ArmorTypes[40];

/* bank_0F.asm lutClassBatSprPalette: sprite palette per class */
extern const uint8_t lutClassBatSprPalette[12];

/* bank_0E.asm lut_TitleText_Continue: the title screen's strings */
extern const uint8_t lut_TitleText_Continue[9];

extern const uint8_t lut_TitleText_NewGame[9];

extern const uint8_t lut_TitleText_RespondRate[13];

/* bank_0E.asm lut_TitleCursor_Y: title cursor y: Continue, New Game (x is $48) */
extern const uint8_t lut_TitleCursor_Y[2];

/* bank_0E.asm lut_PtyGenBuf: the ptygen buffer per character: class, ?, name[4], name x/y, class x/y, sprite x/y, box x/y, cursor x/y */
extern const uint8_t lut_PtyGenBuf[64];

/* bank_0E.asm lut_NameInputRowStart: name input: first character of each row */
extern const uint8_t lut_NameInputRowStart[7];

/* bank_0E.asm lut_NameInput: name input: the characters (every other byte), drawn as one string */
extern const uint8_t lut_NameInput[157];

/* bank_0E.asm lut_LineupSlots: lineup menu slots: dead, char index, x, y, target x, target y, -, - */
extern const uint8_t lut_LineupSlots[32];

/* bank_0E.asm @str_charmaxhp: DrawItemTargetMenu's strings */
extern const uint8_t str_TargetMaxHP[22];

extern const uint8_t str_TargetCurHP[22];

extern const uint8_t str_TargetAilment[21];

extern const uint8_t str_TargetName[18];

/* bank_0E.asm lut_ClassEquipBit: a set bit in a permission word: the class can't equip it */
extern const uint16_t lut_ClassEquipBit[12];

/* bank_0E.asm lut_WeaponPermissions: by 0-based weapon id */
extern const uint16_t lut_WeaponPermissions[40];

/* bank_0E.asm lut_ArmorPermissions: by 0-based armor id */
extern const uint16_t lut_ArmorPermissions[40];

/* bank_0E.asm TitleScreen_Copyright @lut_Copyright: tile x, y, $00-terminated string */
extern const uint8_t lut_Copyright[2][18];

/* bank_0D lut_IntroStoryText ($BF20): the intro story, one complex string */
extern const uint8_t lut_IntroStoryText[224];

