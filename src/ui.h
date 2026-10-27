#pragma once
#include "text.h"

/*
 * ui.h — Shared menu/dialogue/battle drawing: boxes, text, numbers, cursor.
 *
 * Draws into the PPU frame buffer (ppu.h).  Coordinates are in 8x8 tiles
 * unless named px/py (pixels).  Text and boxes use background palette 3 — the
 * border palette FF1 loads with LoadBorderPalette_Blue/_Black — and the cursor
 * uses sprite palette 3.  The caller loads those palettes.
 */

/** Font/box tiles live in bank_09 at $8800 (CHR-RAM tiles $80-$FF). */
#define UI_FONT_TILE_BASE  0x80   /* bank_09 tile index of font tile $80 */

/** The cursor's sprite palette ($0F,$30,$10,$00: LoadBattleSpritePalettes). */
extern const uint8_t UI_CURSOR_PALETTE[4];

/** Port of DrawBox (bank_0F $E063): a w x h tile box from the border tiles
 *  $F7-$FE with a $FF body.  Text inside starts at (tx+1, ty+2) — DrawBox's
 *  dest_x/dest_y. */
void UI_DrawBox(int tx, int ty, int tw, int th);

/** Draw one font tile ($80-$FF; other values are skipped). */
void UI_DrawTile(uint8_t tile, int tx, int ty);

/** Draw text tokens from text.h.  TEXT_NEWLINE returns to column tx on the
 *  next row.  @return the number of rows used. */
int UI_DrawGlyphs(const TextGlyph* glyphs, int count, int tx, int ty);

/** Format and draw an FF1 complex string (see Text_FormatComplex).
 *  @return the width in tiles of the last line drawn. */
int UI_DrawComplexString(const uint8_t* src, const SaveData* save, int tx, int ty);

/** Draw an ASCII string (digits, letters, . , - ! ? / and space).
 *  @return the number of tiles advanced. */
int UI_DrawAscii(const char* s, int tx, int ty);

/** Draw a number left-aligned with no padding.  @return tiles drawn. */
int UI_DrawNumber(uint32_t value, int tx, int ty);

/** Draw a number as FF1 does (Text_PrintNumber: `digits` wide, right-aligned). */
void UI_DrawNumberPadded(uint32_t value, int digits, int tx, int ty);

/** Draw the 2x2 finger cursor sprite at pixel coords (sprite palette 3). */
void UI_DrawCursor(int px, int py);
