#pragma once
#include <stdint.h>
#include "../data/generated/types.h"

/*
 * text.h — FF1 text engine (no SDL dependency).
 *
 * FF1 strings are byte streams: $00 ends the string, $01-$19 are control
 * codes, $1A-$79 are DTE pairs (two tiles via lut_DTE1/lut_DTE2), and
 * $7A-$FF are single tiles.  Font tiles are $80-$FF (font_chr.png index =
 * tile - $80); digits start at $80, letters at $8A, space is $FF.
 *
 * The format functions expand a string into a list of TextGlyph tokens: a
 * tile number (0-$FF) to draw at the cursor, or TEXT_NEWLINE, which moves
 * the cursor to the start column of the next row.  Drawing the tokens is the
 * UI layer's job (ui.h).
 */

typedef uint16_t TextGlyph;

#define TEXT_NEWLINE     0x100   ///< Token: next row, back to the start column.
#define TEXT_MAX_GLYPHS  512

/** Tile ids used by the text engine. */
#define TILE_DIGIT_0  0x80
#define TILE_SPACE    0xFF

/** Port of DrawComplexString (bank_0F): menus, status screens, shops.
 *  Control codes: $01 double line break, $02 xx item name, $03 xx item price,
 *  $04 gold, $05-$0F/$14-$19 line break, $10-$13 xx character stat.
 *  @return number of tokens written to `out`. */
int Text_FormatComplex(const uint8_t* src, const SaveData* save,
                       TextGlyph* out, int max);

/** Port of DrawDialogueString (bank_0F): map dialogue.
 *  Control codes: $02 name of item `item_id` (dlg_itemid, used by chests),
 *  $03 lead character's name (then stops, as in the original), everything
 *  else a single line break.  @return number of tokens written. */
int Text_FormatDialogue(const uint8_t* src, const SaveData* save,
                        uint8_t item_id, TextGlyph* out, int max);

/** Port of PrintNumber_NDigit (bank_0E): `digits` (1-6) tiles, right-aligned,
 *  leading zeros blanked (except the ones digit).  Reproduces the original's
 *  behaviour for values that don't fit.  @return digits written. */
int Text_PrintNumber(uint32_t value, int digits, uint8_t* out);

/** Raw string bytes ($00-terminated).  Dialogue and item-name ids cover the
 *  full byte range; menu string ids past the table return NULL. */
const uint8_t* Text_Dialogue(uint8_t id);
const uint8_t* Text_ItemName(uint8_t id);
const uint8_t* Text_MenuString(int id);

/** DrawCharMenuString's copy: `len` bytes of menu string `id` as they are in
 *  the ROM (past its terminator), each $10 byte ORed with character `c`
 *  (0-3) so the stat codes refer to that character. */
void Text_CharMenuString(int id, int c, uint8_t* out, int len);

/** Raw battle strings: an enemy attack's name (attack id - $42, 0-25) and
 *  a battle message (BTLMSG id, 1-based).  NULL when out of range. */
const uint8_t* Text_EnemyAttackName(int index);
const uint8_t* Text_BattleMessage(int id);

/** A raw string as font tiles: DTE bytes ($1A-$79) expand to their two
 *  tiles, tiles ($80+) are copied, control codes are dropped.  At most `max`
 *  tiles.  @return the number written. */
int Text_ToTiles(const uint8_t* src, uint8_t* out, int max);

/** Plain font tiles of a raw string as ASCII (digits, letters, space and
 *  , . - ! ?); icons and control codes are dropped.  For the battle message
 *  boxes, which draw ASCII.  @return the length written. */
int Text_ToAscii(const uint8_t* src, char* out, int size);

/** lut_ItemPrices (bank_0D $BC00).  For gold-chest ids it is the gold amount. */
uint16_t Text_ItemPrice(uint8_t id);
