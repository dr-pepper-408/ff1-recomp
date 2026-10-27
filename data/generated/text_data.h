/*
 * Raw Text Tables
 * Source: bank_0A.dat ($8000 dialogue, $B700 item names) / bin/0E_8500_menutext.bin
 * Generated: 2026-10-27
 * Exact ROM string bytes (DTE + control codes), $00-terminated.
 * String i starts at <table>_bytes + <table>_offsets[i].
 * Decoded at runtime by text.c (port of DrawComplexString).
 */

#pragma once

#include <stdint.h>

/* lut_DialoguePtrTbl (bank_0A $8000) */
#define DIALOGUE_STRING_COUNT 256

extern uint16_t dialogue_offsets[DIALOGUE_STRING_COUNT];

extern uint8_t dialogue_bytes[12934];

/* lut_ItemNamePtrTbl (bank_0A $B700) */
#define ITEM_NAME_COUNT 256

extern uint16_t item_name_offsets[ITEM_NAME_COUNT];

extern uint8_t item_name_bytes[1715];

/* Menu text (bank_0E $8500) */
#define MENU_STRING_COUNT 64

extern uint16_t menu_text_offsets[MENU_STRING_COUNT];

extern uint8_t menu_text_bytes[2160];

/* lut_EnemyAttack (bank_0A $B600), attack id - $42 */
#define ENEMY_ATTACK_NAME_COUNT 26

extern uint16_t enemy_attack_offsets[ENEMY_ATTACK_NAME_COUNT];

extern uint8_t enemy_attack_bytes[177];

/* data_BattleMessages (bank_0B $8C40 + $310), BTLMSG id - 1 */
#define BATTLE_MSG_COUNT 78

extern uint16_t battle_msg_offsets[BATTLE_MSG_COUNT];

extern uint8_t battle_msg_bytes[796];

/* lut_DTE1 (bank_0F), indexed by byte - $1A for bytes $1A-$79. */
extern uint8_t lut_DTE1[96];

/* lut_DTE2 (bank_0F), indexed by byte - $1A for bytes $1A-$79. */
extern uint8_t lut_DTE2[96];

/* lut_ItemPrices (bank_0D $BC00), indexed like item names.  For the
 * gold-chest ids ($6C-$AF) the "price" is the gold amount. */
extern uint16_t item_prices[ITEM_NAME_COUNT];
