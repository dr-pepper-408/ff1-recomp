/**
 * @file intro.c
 * @brief The intro story: a port of bank_0E's EnterIntroStory,
 *        IntroStory_MainLoop / _AnimateBlock / _AnimateRow / _Frame.
 *
 * The whole text is drawn at once over a screen of blank tiles, every
 * attribute set to palette 1 (blue on blue: invisible).  Then, a block of
 * two text rows (8 attribute bytes) at a time, the top row is switched to
 * palette 2 and faded in by animating that palette's color 3, then the
 * bottom row, after which the block uses palette 3 (white) for good.
 *
 * IntroStory_Frame returns twice ("double RTS") when A or B is pressed, so
 * what A/B does depends on where it lands: during a row's fade it ends that
 * row at once; on the frame that ends a block it ends the whole intro (the
 * disassembly calls this a 1-frame BUG); once everything is shown it ends
 * the intro.  Start always goes back to GameStart: the title screen.
 */

#include <string.h>
#include "intro.h"
#include "ppu.h"
#include "text.h"
#include "../data/generated/menu_data.h"
#include "../data/generated/battle_ui.h"

#define NT_W 32
#define NT_H 30

typedef enum { P_ROW, P_BLOCK_END, P_IDLE } Phase;

static uint8_t nt[NT_H][NT_W];
static uint8_t attr[64];
static Phase phase;
static int ataddr;              /* intro_ataddr - $23C0 */
static uint8_t atbyte;          /* intro_atbyte */
static int row;                 /* 0 top, 1 bottom row of the block */
static uint8_t intro_color;     /* the fading row's "main" color */
static uint8_t pal_b;           /* cur_pal+$B: palette 2, color 3 */
static uint8_t framecounter;

/* IntroStory_AnimateRow's start (@MainLoop). */
static void start_row(void) {
    intro_color = 0x00;
    pal_b = intro_color;
    phase = P_ROW;
}

/* IntroStory_AnimateBlock's start: the top row animates, the bottom row
 * stays faded out. */
static void start_block(void) {
    atbyte = 0x5A;
    row = 0;
    start_row();
}

/* AnimateRow returned (finished, or cut short by A/B). */
static void row_done(void) {
    if (row == 0) {
        atbyte = 0xAF;          /* the top row stays in, the bottom row animates */
        row = 1;
        start_row();
    } else {
        atbyte = 0xFF;          /* the whole block in; then JMP IntroStory_Frame */
        phase = P_BLOCK_END;
    }
}

void Intro_Open(void) {
    /* The nametable all blank ($FF), every attribute palette 1, and the
     * text drawn once from (1, 3). */
    memset(nt, 0xFF, sizeof(nt));
    memset(attr, 0x55, sizeof(attr));
    static TextGlyph g[TEXT_MAX_GLYPHS];
    int n = Text_FormatComplex(lut_IntroStoryText, NULL, g, TEXT_MAX_GLYPHS);
    int x = 1, y = 3;
    for (int i = 0; i < n; i++) {
        if (g[i] == TEXT_NEWLINE) { x = 1; y++; continue; }
        if (x < NT_W && y < NT_H) nt[y][x] = (uint8_t)g[i];
        x++;
    }
    framecounter = 0;
    ataddr = 0;
    start_block();
}

int Intro_Update(const InputState* in) {
    /* IntroStory_Frame: the attribute row and palette as last set. */
    memset(&attr[ataddr], atbyte, 8);                   /* IntroStory_WriteAttr */
    if (in->pressed & BTN_START) return 1;              /* JMP GameStart */
    int ab = (in->pressed & (BTN_A | BTN_B)) != 0;

    switch (phase) {
    case P_ROW:                                         /* IntroStory_AnimateRow @SubLoop */
        if (ab) { row_done(); break; }                  /* double RTS: the row is done */
        framecounter++;
        if (!(framecounter & 0x0F)) {                   /* every 16th frame: a shade brighter */
            intro_color = (uint8_t)(intro_color + 0x10);
            if (intro_color >= 0x40) { row_done(); break; }
            pal_b = intro_color;
        } else if (!(framecounter & 1)) {
            pal_b = intro_color;                        /* even frame: the main color */
        } else {
            pal_b = (pal_b >= 0x10) ? (uint8_t)(pal_b - 0x10) : 0x01;   /* odd: one shade darker */
        }
        break;
    case P_BLOCK_END:                                   /* AnimateBlock's last frame */
        if (ab) return 1;                               /* BUGGED: double RTS out of the intro */
        ataddr += 8;
        if (ataddr < 0x38) start_block();               /* every block but the last half-block */
        else phase = P_IDLE;
        break;
    case P_IDLE:                                        /* @InfiniteLoop */
        if (ab) return 1;
        break;
    }
    return 0;
}

void Intro_Draw(void) {
    /* LoadMenuCHRPal (palette 3: the blue border palette) and
     * EnterIntroStory's palettes 1 and 2 (colors 2-3 blue).  Their colors
     * 0-1 are never set, so they keep power-on RAM ($00). */
    static const uint8_t pal1[4] = { 0x0F, 0x00, 0x01, 0x01 };
    static const uint8_t pal3[4] = { 0x0F, 0x00, 0x01, 0x30 };
    uint8_t pal2[4] = { 0x0F, 0x00, 0x01, pal_b };
    Ppu_SetPalette(1, pal1);
    Ppu_SetPalette(2, pal2);
    Ppu_SetPalette(3, pal3);
    for (int i = 0; i < 4; i++) Ppu_SetPalette(4 + i, BattleSpritePalettes + i * 4);
    Ppu_Clear(0);
    for (int y = 0; y < NT_H; y++)
        for (int x = 0; x < NT_W; x++) {
            uint8_t a = attr[(y / 4) * 8 + x / 4];
            int pal = (a >> (((y % 4) / 2) * 4 + ((x % 4) / 2) * 2)) & 3;
            if (nt[y][x] >= 0x80) Ppu_DrawBgTile(CHR_BANK_09, nt[y][x], x * 8, y * 8, pal);
        }
}
