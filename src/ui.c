/**
 * @file ui.c
 * @brief Shared box / text / number / cursor drawing for menus, dialogue and
 *        battle (ports of DrawBox and the font tile output of bank_0F).
 */

#include "ui.h"
#include "ppu.h"
#include <stdio.h>

#define T(x) ((x) * 8)
#define UI_BG_PALETTE      3
#define UI_CURSOR_SPR_PAL  3

const uint8_t UI_CURSOR_PALETTE[4] = { 0x0F, 0x30, 0x10, 0x00 };

void UI_DrawTile(uint8_t tile, int tx, int ty) {
    if (tile < 0x80) return;   /* not a font tile (only reachable via ROM quirks) */
    Ppu_DrawBgTile(CHR_BANK_09, tile, T(tx), T(ty), UI_BG_PALETTE);
}

/* Port of DrawBox / DrawBoxRow_Top/Mid/Bot. */
void UI_DrawBox(int tx, int ty, int tw, int th) {
    if (tw < 3 || th < 3) return;
    for (int row = 0; row < th; row++) {
        uint8_t left, mid, right;
        if (row == 0)           { left = 0xF7; mid = 0xF8; right = 0xF9; }
        else if (row == th - 1) { left = 0xFC; mid = 0xFD; right = 0xFE; }
        else                    { left = 0xFA; mid = 0xFF; right = 0xFB; }
        UI_DrawTile(left, tx, ty + row);
        for (int col = 1; col < tw - 1; col++) UI_DrawTile(mid, tx + col, ty + row);
        UI_DrawTile(right, tx + tw - 1, ty + row);
    }
}

int UI_DrawGlyphs(const TextGlyph* glyphs, int count, int tx, int ty) {
    int x = tx, y = ty;
    for (int i = 0; i < count; i++) {
        if (glyphs[i] == TEXT_NEWLINE) { x = tx; y++; continue; }
        UI_DrawTile((uint8_t)glyphs[i], x, y);
        x++;
    }
    return y - ty + 1;
}

int UI_DrawComplexString(const uint8_t* src, const SaveData* save, int tx, int ty) {
    TextGlyph glyphs[TEXT_MAX_GLYPHS];
    int n = Text_FormatComplex(src, save, glyphs, TEXT_MAX_GLYPHS);
    UI_DrawGlyphs(glyphs, n, tx, ty);
    int width = 0;
    for (int i = 0; i < n; i++) width = (glyphs[i] == TEXT_NEWLINE) ? 0 : width + 1;
    return width;
}

/* ASCII -> font tile (table_standard.tbl), or 0 if there is none. */
static uint8_t ascii_tile(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(0x80 + (c - '0'));
    if (c >= 'A' && c <= 'Z') return (uint8_t)(0x8A + (c - 'A'));
    if (c >= 'a' && c <= 'z') return (uint8_t)(0xA4 + (c - 'a'));
    switch (c) {
    case ',': return 0xBF;
    case '.': return 0xC0;
    case '-': return 0xC2;
    case '!': return 0xC4;
    case '?': return 0xC5;
    case ' ': return 0xFF;
    }
    return 0;
}

/* The font has no '/', so draw one: a blank cell with a white diagonal. */
static void draw_slash(int tx, int ty) {
    static const int col[7] = { 6, 5, 4, 4, 3, 2, 1 };   /* rows 1-7 */
    UI_DrawTile(0xFF, tx, ty);
    for (int i = 0; i < 7; i++) Ppu_SetPixel(T(tx) + col[i], T(ty) + 1 + i, PPU_BORDER_WHITE);
}

int UI_DrawAscii(const char* s, int tx, int ty) {
    int n = 0;
    for (; s[n]; n++) {
        if (s[n] == '/') draw_slash(tx + n, ty);
        else UI_DrawTile(ascii_tile(s[n]), tx + n, ty);
    }
    return n;
}

int UI_DrawNumber(uint32_t value, int tx, int ty) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%u", (unsigned)value);
    return UI_DrawAscii(buf, tx, ty);
}

void UI_DrawNumberPadded(uint32_t value, int digits, int tx, int ty) {
    uint8_t buf[6];
    int n = Text_PrintNumber(value, digits, buf);
    for (int i = 0; i < n; i++) UI_DrawTile(buf[i], tx + i, ty);
}

/* Port of DrawCursor (bank_0F $EC95): sprite tiles $F0-$F3.  Sprite CHR
 * $80-$FF is lut_BatObjCHR (bank_09 $A800), so sprite tile $F0 is bank_09
 * $AF00 = bank tile $2F0. */
#define CURSOR_TILE 0x2F0

void UI_DrawCursor(int px, int py) {
    Ppu_SpriteGroup(0);
    Ppu_DrawSprite(CHR_BANK_09, CURSOR_TILE + 0, px,     py,     UI_CURSOR_SPR_PAL, 0);
    Ppu_DrawSprite(CHR_BANK_09, CURSOR_TILE + 1, px + 8, py,     UI_CURSOR_SPR_PAL, 0);
    Ppu_DrawSprite(CHR_BANK_09, CURSOR_TILE + 2, px,     py + 8, UI_CURSOR_SPR_PAL, 0);
    Ppu_DrawSprite(CHR_BANK_09, CURSOR_TILE + 3, px + 8, py + 8, UI_CURSOR_SPR_PAL, 0);
}
