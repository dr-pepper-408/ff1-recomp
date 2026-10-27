/**
 * @file minigame.c
 * @brief The sliding puzzle: a port of bank_0D's EnterMiniGame,
 *        MiniGame_ShufflePuzzle, MiniGameLoop, MiniGame_ProcessInput,
 *        MiniGame_VertSlide / _HorzSlide / _AnimateSlide,
 *        DrawAllPuzzlePieces and MiniGame_CheckVictory.
 *
 * The 16 slots hold pieces 1-15 and 0 (the empty slot); each piece is a
 * 2x2 sprite.  A on a piece in the empty slot's row or column slides the
 * pieces between them over 16 frames (their sprites move a pixel a frame).
 * The puzzle counts as solved with each piece one more than the slot
 * before it (empty slot first or last).  Sound effects and music are not
 * ported.
 */

#include <string.h>
#include "minigame.h"
#include "ppu.h"
#include "../data/generated/story_data.h"
#include "../data/generated/rng_table.h"

typedef enum { MG_PLAY, MG_SLIDE, MG_WON } Phase;

static uint8_t puzzle[16];
static int cursor;
static uint8_t framecounter;
static uint8_t joy_prevdir;
static Phase phase;
static uint8_t oam[16 * 4][4];      /* y, tile, attribute, x: 4 sprites a slot */
static int slide_dir, slide_frame;
static int slide_spr[3];            /* mg_slidespr: slots whose sprites move (-1: none) */
static uint8_t spr_chr[64 * 16];    /* the pieces expanded to 2bpp */
static uint8_t pal_back;            /* cur_pal+$13 */
static uint8_t nt[0x400];

/* MiniGame_ShufflePuzzle: $7F random slides into the empty slot (moves
 * that would leave the puzzle are skipped, but still count). */
static void shuffle(void) {
    static const int dir[4] = { 1, -1, 4, -4 };
    for (int n = 0; n < 0x7F; n++) {
        int off = dir[rng_table[++framecounter] & 3];
        int empty = 15;
        while (puzzle[empty]) empty--;
        int from = empty + off;
        if (from < 0 || from >= 16) continue;
        if (off == 1 && !(from & 3)) continue;           /* can't come from the next row */
        if (off == -1 && (from & 3) == 3) continue;
        puzzle[empty] = puzzle[from];
        puzzle[from] = 0;
    }
}

void Minigame_Open(uint8_t fc) {
    /* LoadBridgeSceneGFX, then a box for the puzzle (8,5 10x10). */
    memcpy(nt, data_BridgeNT, sizeof(nt));
    for (int r = 0; r < 10; r++)
        for (int c = 0; c < 10; c++) {
            uint8_t v;
            if (r == 0)      v = (c == 0) ? 0xF7 : (c == 9) ? 0xF9 : 0xF8;
            else if (r == 9) v = (c == 0) ? 0xFC : (c == 9) ? 0xFE : 0xFD;
            else             v = (c == 0) ? 0xFA : (c == 9) ? 0xFB : 0xFF;
            nt[(5 + r) * 32 + 8 + c] = v;
        }
    /* The pieces' CHR: each numbered tile as the low bitplane, the plain
     * piece's matching tile as the high one. */
    for (int t = 0; t < 64; t++) {
        memcpy(spr_chr + t * 16, lut_MinigameCHR + t * 8, 8);
        memcpy(spr_chr + t * 16 + 8, lut_MinigameCHR + (t & 3) * 8, 8);
    }
    for (int i = 0; i < 16; i++) puzzle[i] = (uint8_t)lut_PuzzleStart[i];
    framecounter = fc;
    shuffle();
    pal_back = 0x30;
    joy_prevdir = 0;
    cursor = puzzle[0] ? 0 : 1;                          /* never on the empty slot */
    phase = MG_PLAY;
    memset(oam, 0xF8, sizeof(oam));
}

/* DrawAllPuzzlePieces: 4 sprites per slot, in slot order; the cursor's
 * piece blinks (behind the box every other 2 frames). */
static void draw_all_pieces(void) {
    memset(oam, 0xF8, sizeof(oam));
    for (int s = 0; s < 16; s++) {
        if (!puzzle[s]) continue;
        int x = 0x48 + (s & 3) * 16, y = 0x2F + (s >> 2) * 16;
        uint8_t attr = (cursor == s) ? (uint8_t)((framecounter & 0x02) << 4) : 0;
        for (int k = 0; k < 4; k++) {
            uint8_t* o = oam[s * 4 + k];
            o[0] = (uint8_t)(y + ((k >> 1) * 8));
            o[1] = (uint8_t)(puzzle[s] * 4 + k);
            o[2] = attr;
            o[3] = (uint8_t)(x + ((k & 1) * 8));
        }
    }
}

/* MiniGame_ProcessInput: the cursor moves, skipping the empty slot. */
static void process_input(const InputState* in) {
    uint8_t d = in->held & 0x0F;
    if (d == joy_prevdir) return;
    joy_prevdir = d;
    if (!d) return;
    int off = (d >= 0x04) ? (d == 0x04 ? 4 : -4) : (d == 0x01 ? 1 : -1);
    do cursor = (cursor + off) & 0x0F; while (!puzzle[cursor]);
}

/* @A_Pressed: slide the pieces between the cursor and the empty slot. */
static void slide(void) {
    framecounter = 0;                                    /* the cursor's piece shown */
    slide_spr[0] = slide_spr[1] = slide_spr[2] = -1;
    int c = cursor, e = 0;
    while (puzzle[e]) e++;
    int n, step;
    if ((c & 3) == (e & 3)) {                            /* MiniGame_VertSlide */
        step = (c < e) ? 4 : -4;
        slide_dir = (c < e) ? 2 : 3;
    } else if ((c & 0x0C) == (e & 0x0C)) {               /* MiniGame_HorzSlide */
        step = (c < e) ? 1 : -1;
        slide_dir = (c < e) ? 0 : 1;
    } else {
        return;                                          /* illegal: the error sound */
    }
    draw_all_pieces();
    n = (e - c) / step;
    for (int s = e; s != c; s -= step) puzzle[s] = puzzle[s - step];
    puzzle[c] = 0;
    for (int i = 0; i < n; i++) slide_spr[i] = c + i * step;
    cursor = e;                                          /* MiniGame_AnimateSlide */
    slide_frame = 0;
    phase = MG_SLIDE;
}

/* MiniGame_CheckVictory: each piece one more than the slot before it. */
static int solved(void) {
    for (int i = 1; i < 15; i++)
        if (puzzle[i] != (uint8_t)(puzzle[0] + i)) return 0;
    return 1;
}

MinigameResult Minigame_Update(const InputState* in) {
    switch (phase) {
    case MG_SLIDE:                                       /* 16 frames, a pixel each */
        for (int i = 0; i < 3; i++) {
            if (slide_spr[i] < 0) continue;
            for (int k = 0; k < 4; k++) {
                uint8_t* o = oam[slide_spr[i] * 4 + k];
                o[0] = (uint8_t)(o[0] + lut_MGDirection_Y[slide_dir]);
                o[3] = (uint8_t)(o[3] + lut_MGDirection_X[slide_dir]);
            }
        }
        if (++slide_frame == 16) phase = MG_PLAY;
        return MINIGAME_PLAYING;
    case MG_WON:                                         /* the backs cycle; A/B leaves */
        framecounter++;
        pal_back = (uint8_t)((framecounter << 1) & 0x30);
        process_input(in);
        return (in->pressed & (BTN_A | BTN_B)) ? MINIGAME_SOLVED : MINIGAME_PLAYING;
    default:                                             /* MiniGameLoop */
        framecounter++;
        draw_all_pieces();
        process_input(in);
        if (in->pressed & BTN_B) return MINIGAME_QUIT;
        if (in->pressed & BTN_A) { slide(); return MINIGAME_PLAYING; }
        if (solved()) {
            cursor = 0x12;                               /* off the puzzle: no blinking */
            draw_all_pieces();
            phase = MG_WON;
        }
        return MINIGAME_PLAYING;
    }
}

void Minigame_Draw(void) {
    for (int i = 0; i < 4; i++) Ppu_SetPalette(i, lut_BridgeBGPal + i * 4);
    uint8_t spr[4] = { lut_BridgeBGPal[0], 0x0F, 0x15, pal_back };   /* red numbers, white backs */
    Ppu_SetPalette(4, spr);
    Ppu_Clear(0);
    for (int y = 0; y < 30; y++)
        for (int x = 0; x < 32; x++) {
            uint8_t a = nt[0x3C0 + (y / 4) * 8 + x / 4];
            int p = (a >> (((y % 4) / 2) * 4 + ((x % 4) / 2) * 2)) & 3;
            uint8_t t = nt[y * 32 + x];
            if (t < 0x80) Ppu_DrawBgTile(CHR_BANK_BRIDGE, t, x * 8, y * 8, p);
            else          Ppu_DrawBgTile(CHR_BANK_09, t, x * 8, y * 8, p);
        }
    Ppu_SetRamChr(spr_chr, 64);
    for (int i = 63; i >= 0; i--) {                      /* earlier sprites in front */
        const uint8_t* o = oam[i];
        if (o[0] >= 0xEF || (o[2] & 0x20)) continue;     /* behind the box: hidden */
        Ppu_DrawSprite(CHR_BANK_RAM, o[1], o[3], o[0] + 1, o[2] & 3, 0);
    }
}

const uint8_t* Minigame_Palette(void) { return lut_BridgeBGPal; }

void Minigame_SetPuzzle(const uint8_t pieces[16], int c) {
    memcpy(puzzle, pieces, 16);
    cursor = c;
}
