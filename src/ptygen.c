/**
 * @file ptygen.c
 * @brief Party generation: a port of bank_0E's NewGamePartyGeneration,
 *        DoPartyGen_OnCharacter, DoNameInput and their drawing routines.
 *
 * The ptygen buffer ($10 bytes per character, from lut_PtyGenBuf) holds
 * each character's class and name, and where its box, texts, sprite and
 * the cursor go.  Both screens are fully described by it and a few cursor
 * variables, so they're drawn from that state every frame.
 */

#include <string.h>
#include "ptygen.h"
#include "ppu.h"
#include "ui.h"
#include "text.h"
#include "sfx.h"
#include "../data/generated/menu_data.h"
#include "../data/generated/battle_ui.h"

/* ptygen buffer offsets (variables.inc ptygen_*) */
enum {
    PG_CLASS = 0x0, PG_NAME = 0x2, PG_NAME_X = 0x6, PG_NAME_Y = 0x7,
    PG_CLASS_X = 0x8, PG_CLASS_Y = 0x9, PG_SPR_X = 0xA, PG_SPR_Y = 0xB,
    PG_BOX_X = 0xC, PG_BOX_Y = 0xD, PG_CURS_X = 0xE, PG_CURS_Y = 0xF,
};

typedef enum { S_CLASS, S_NAME, S_CONFIRM } State;

static uint8_t ptygen[0x40];
static State state;
static int char_index;          /* $00, $10, $20, $30 */
static uint8_t joy_prevdir;
static int cursor;              /* name input: letter 0-4 */
static int namecurs_x, namecurs_y;

/* DoPartyGen_OnCharacter (PtyGen_DrawScreen clears the pad state). */
static void on_character(int ci) {
    char_index = ci;
    joy_prevdir = 0;
    state = S_CLASS;
}

/* DoNameInput: the character's name wiped, the cursor on 'A'. */
static void name_input(void) {
    joy_prevdir = 0;
    cursor = 0;
    namecurs_x = namecurs_y = 0;
    memset(&ptygen[char_index + PG_NAME], 0xFF, 4);
    state = S_NAME;
}

void PtyGen_Open(void) {
    memcpy(ptygen, lut_PtyGenBuf, sizeof(ptygen));
    on_character(0);
}

/* A new direction pressed (joy_prevdir), or 0. */
static uint8_t new_dir(const InputState* in) {
    uint8_t d = in->held & 0x0F;
    if (d == joy_prevdir) return 0;
    joy_prevdir = d;
    return d;
}

int PtyGen_Update(const InputState* in) {
    if (state != S_CONFIRM) Sfx_JoyFrame(in, 0x0F);   /* PtyGen_Joy */
    else if (in->pressed & (BTN_A | BTN_B)) Sfx_MenuSel();   /* MenuWaitForBtn_SFX */
    int a = (in->pressed & BTN_A) != 0, b = (in->pressed & BTN_B) != 0;
    switch (state) {
    case S_CLASS:
        if (a) { name_input(); break; }
        if (b) {                                   /* back a character (not past the first) */
            on_character(char_index ? char_index - 0x10 : 0);
            break;
        }
        if (new_dir(in)) {                         /* any direction: the next class */
            uint8_t* c = &ptygen[char_index + PG_CLASS];
            *c = (uint8_t)((*c + 1 < 6) ? *c + 1 : 0);
        }
        break;

    case S_NAME:
        if (a) {
            int i = (lut_NameInputRowStart[namecurs_y] + namecurs_x) * 2;
            if (cursor >= 4) {                     /* a 5th A: the name is done */
                if (char_index < 0x30) on_character(char_index + 0x10);
                else state = S_CONFIRM;            /* PtyGen_DrawScreen, then wait */
                break;
            }
            ptygen[char_index + PG_NAME + cursor] = lut_NameInput[i];
            cursor++;
        } else if (b) {                            /* erase the previous letter */
            if (cursor > 0) cursor--;
            ptygen[char_index + PG_NAME + cursor] = 0xFF;
        } else {
            uint8_t d = new_dir(in);
            if (!d) break;
            if (d < 0x04) {
                if (d == 0x02) namecurs_x = namecurs_x ? namecurs_x - 1 : 9;   /* left */
                else namecurs_x = (namecurs_x + 1 < 10) ? namecurs_x + 1 : 0;  /* right */
            } else if (d == 0x08) {
                namecurs_y = namecurs_y ? namecurs_y - 1 : 6;                  /* up */
            } else {
                namecurs_y = (namecurs_y + 1 < 7) ? namecurs_y + 1 : 0;        /* down */
            }
        }
        break;

    case S_CONFIRM:                                /* MenuWaitForBtn_SFX */
        if (!a && !b) break;
        if (in->held & BTN_B) { on_character(0x30); break; }
        return 1;
    }
    return 0;
}

void PtyGen_Result(uint8_t classes[4], uint8_t names[4][4]) {
    for (int c = 0; c < 4; c++) {                  /* @RecordClassAndName */
        classes[c] = ptygen[c * 0x10 + PG_CLASS];
        memcpy(names[c], &ptygen[c * 0x10 + PG_NAME], 4);
    }
}

/* ---- Drawing ---- */

/* Tiles and strings with an explicit palette (the name input screen gives
 * its top 8 rows palette 0, the orange name box). */
static void tile(uint8_t t, int x, int y, int pal) {
    if (t >= 0x80) Ppu_DrawBgTile(CHR_BANK_09, t, x * 8, y * 8, pal);
}

static int row_pal(int y) { return (state == S_NAME && y < 8) ? 0 : 3; }

static void box(int x, int y, int w, int h) {
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            uint8_t t;
            if (r == 0)          t = (c == 0) ? 0xF7 : (c == w - 1) ? 0xF9 : 0xF8;
            else if (r == h - 1) t = (c == 0) ? 0xFC : (c == w - 1) ? 0xFE : 0xFD;
            else                 t = (c == 0) ? 0xFA : (c == w - 1) ? 0xFB : 0xFF;
            tile(t, x + c, y + r, row_pal(y + r));
        }
}

static void string(const uint8_t* s, int x, int y) {
    static TextGlyph g[TEXT_MAX_GLYPHS];
    int n = Text_FormatComplex(s, NULL, g, TEXT_MAX_GLYPHS);
    int cx = x, cy = y;
    for (int i = 0; i < n; i++) {
        if (g[i] == TEXT_NEWLINE) { cx = x; cy++; continue; }
        tile((uint8_t)g[i], cx++, cy, row_pal(cy));
    }
}

/* DrawSimple2x3Sprite of a class (LoadBatSprCHRPalettes_NewGame loads all
 * six classes in order). */
static void char_sprite(int cls, int x, int y) {
    int pal = lutClassBatSprPalette[cls];
    for (int k = 0; k < 6; k++)
        Ppu_DrawSprite(CHR_BANK_09, 0x100 + cls * 32 + k, x + (k & 1) * 8, y + (k >> 1) * 8, pal, 0);
}

void PtyGen_Draw(void) {
    /* LoadNewGameCHRPal, then NewGamePartyGeneration's palette 0: the
     * border's grey and white with $16 (the name input's orange box). */
    static const uint8_t border_blue[4] = { 0x0F, 0x00, 0x01, 0x30 };
    static const uint8_t pal0[4] = { 0x0F, 0x00, 0x16, 0x30 };
    Ppu_SetPalette(0, pal0);
    Ppu_SetPalette(3, border_blue);
    for (int i = 0; i < 4; i++) Ppu_SetPalette(4 + i, BattleSpritePalettes + i * 4);
    Ppu_Clear(0);

    if (state == S_NAME) {
        /* DrawNameInputScreen + NameInput_DrawName + CharName_DrawCursor */
        box(0x04, 0x08, 0x17, 0x14);
        box(0x0D, 0x02, 0x06, 0x04);
        string(lut_NameInput, 0x06, 0x0A);
        uint8_t name[5];
        memcpy(name, &ptygen[char_index + PG_NAME], 4);
        name[4] = 0;
        string(name, 0x0E, 0x04);
        UI_DrawCursor(namecurs_x * 16 + 0x20, namecurs_y * 16 + 0x50);
        return;
    }

    /* PtyGen_DrawBoxes / PtyGen_DrawText */
    for (int c = 0; c < 0x40; c += 0x10) {
        const uint8_t* p = &ptygen[c];
        box(p[PG_BOX_X], p[PG_BOX_Y], 10, 10);
        uint8_t cls[3] = { 0x02, (uint8_t)(p[PG_CLASS] + 0xF0), 0x00 };   /* the class name */
        string(cls, p[PG_CLASS_X], p[PG_CLASS_Y]);
        uint8_t name[5];
        memcpy(name, &p[PG_NAME], 4);
        name[4] = 0;
        string(name, p[PG_NAME_X], p[PG_NAME_Y]);
    }
    /* PtyGen_Frame: the characters, then the cursor (behind them); the
     * final confirmation shows no cursor. */
    if (state == S_CLASS)
        UI_DrawCursor(ptygen[char_index + PG_CURS_X], ptygen[char_index + PG_CURS_Y]);
    for (int c = 0; c < 0x40; c += 0x10)
        char_sprite(ptygen[c + PG_CLASS], ptygen[c + PG_SPR_X], ptygen[c + PG_SPR_Y]);
}
