/**
 * @file lineup.c
 * @brief The lineup menu: a port of bank_0E's EnterLineupMenu,
 *        LineupMenu_ProcessJoy / _ProcessMode / _AnimStep / _Finalize.
 *
 * The slot table (lut_LineupSlots, 8 bytes per slot: dead/stone flag, the
 * character in it, sprite x/y, target x/y) drives everything.  Modes:
 *   0  choosing the first character
 *   1  its sprite slides left to x $88
 *   2  choosing the second one
 *   3  that sprite slides right to x $A8
 *   4  both sprites move to their new slots (y first, then x), after which
 *      the two slots are swapped
 * A and B pressed during modes 1, 3 and 4 aren't cleared (the "hanging"
 * sound effect in the original), so they act when the animation ends.
 */

#include <string.h>
#include "lineup.h"
#include "ppu.h"
#include "ui.h"
#include "sfx.h"
#include "../data/generated/menu_data.h"
#include "../data/generated/battle_ui.h"

/* Slot table fields */
enum { SL_DEAD, SL_CHAR, SL_X, SL_Y, SL_TX, SL_TY };

static uint8_t slots[0x20];     /* str_buf */
static int lu_mode, lu_cursor, lu_cursor2;
static uint8_t lu_joyprev;
static int joy_a, joy_b;        /* the button catchers */

void Lineup_Open(const SaveData* save) {
    memcpy(slots, lut_LineupSlots, sizeof(slots));
    for (int s = 0; s < 4; s++)                 /* (ailment + 1) & 2: dead or stone */
        slots[s * 8 + SL_DEAD] = (uint8_t)((save->characters[s].ailments + 1) & 2);
    lu_joyprev = 0;
    joy_a = joy_b = 0;
    lu_mode = lu_cursor = lu_cursor2 = 0;
}

/* LineupMenu_AnimStep: one pixel towards the target, y before x.
 * Returns 1 while there's more to move. */
static int anim_step(int y) {
    uint8_t* s = &slots[y];
    if (s[SL_Y] != s[SL_TY]) { s[SL_Y] += (s[SL_Y] > s[SL_TY]) ? -1 : 1; return 1; }
    if (s[SL_X] != s[SL_TX]) { s[SL_X] += (s[SL_X] > s[SL_TX]) ? -1 : 1; return 1; }
    return 0;
}

/* LineupMenu_ProcessMode */
static void process_mode(void) {
    switch (lu_mode) {
    case 1:
        if (slots[lu_cursor2 + SL_X] < 0x89) lu_mode++;
        else slots[lu_cursor2 + SL_X]--;
        break;
    case 3:
        if (slots[lu_cursor + SL_X] >= 0xA8) lu_mode++;
        else slots[lu_cursor + SL_X]++;
        break;
    case 4:
        if (anim_step(lu_cursor)) { anim_step(lu_cursor2); break; }
        if (anim_step(lu_cursor2)) break;
        for (int i = 0; i < 8; i++) {           /* both there: swap the slots */
            uint8_t t = slots[lu_cursor + i];
            slots[lu_cursor + i] = slots[lu_cursor2 + i];
            slots[lu_cursor2 + i] = t;
        }
        lu_mode = 0;
        lu_cursor = lu_cursor2;
        break;
    }
}

/* LineupMenu_Finalize: the characters (stats and magic) in their new order. */
static void finalize(SaveData* save) {
    CharacterStats stats[4];
    CharacterMagic magic[4];
    memcpy(stats, save->characters, sizeof(stats));
    memcpy(magic, save->character_magic, sizeof(magic));
    for (int s = 0; s < 4; s++) {
        int c = slots[s * 8 + SL_CHAR] >> 6;
        save->characters[s] = stats[c];
        save->character_magic[s] = magic[c];
    }
}

/* LineupMenu_ProcessJoy.  Returns 1 when leaving the menu. */
static int process_joy(SaveData* save, const InputState* in) {
    if (lu_mode != 0 && lu_mode != 2) return 0;   /* mid-animation: ignored */
    if (joy_b) {
        if (lu_mode == 0) { finalize(save); return 1; }
        lu_mode = 4;                              /* put the first one back */
        lu_cursor = lu_cursor2;
        joy_b = 0;
        return 0;
    }
    if (joy_a) {
        joy_a = 0;
        if (lu_mode == 0) {
            lu_mode = 1;
            lu_cursor2 = lu_cursor;
            return 0;
        }
        /* The leader can't become a dead or stoned character; picking the
         * same slot twice cancels too (@KillSwap: mode 3 with the cursors
         * equal, so the sprite just slides back). */
        int kill = lu_cursor == lu_cursor2 ||
                   (lu_cursor == 0 && slots[lu_cursor2 + SL_DEAD]) ||
                   (lu_cursor2 == 0 && slots[lu_cursor + SL_DEAD]);
        if (kill) {
            lu_cursor = lu_cursor2;
        } else {                                  /* @SwapSafe: trade target y */
            uint8_t t = slots[lu_cursor + SL_TY];
            slots[lu_cursor + SL_TY] = slots[lu_cursor2 + SL_TY];
            slots[lu_cursor2 + SL_TY] = t;
        }
        lu_mode++;
        return 0;
    }
    uint8_t d = in->held & 0x0C;
    if (d == lu_joyprev) return 0;
    lu_joyprev = d;
    if (!d) return 0;
    lu_cursor = (lu_cursor + (d == 0x08 ? -8 : 8)) & 0x1F;
    return 0;
}

int Lineup_Update(SaveData* save, const InputState* in) {
    Sfx_JoyFrame(in, 0x0C);                     /* LineupMenu_UpdateJoy */
    /* LineupMenu_UpdateJoy: the catchers count presses until handled. */
    if (in->pressed & BTN_A) joy_a = 1;
    if (in->pressed & BTN_B) joy_b = 1;
    process_mode();
    return process_joy(save, in);
}

void Lineup_Draw(const SaveData* save) {
    /* LoadMenuCHRPal: the blue border palette and the battle sprite palettes. */
    static const uint8_t border_blue[4] = { 0x0F, 0x00, 0x01, 0x30 };
    Ppu_SetPalette(3, border_blue);
    for (int i = 0; i < 4; i++) Ppu_SetPalette(4 + i, BattleSpritePalettes + i * 4);
    Ppu_Clear(0);

    UI_DrawBox(0x0A, 0x05, 0x0E, 0x13);
    for (int s = 0; s < 4; s++) {                 /* DrawLineupMenuNames */
        uint8_t name[5];
        memcpy(name, save->characters[slots[s * 8 + SL_CHAR] >> 6].name, 4);
        name[4] = 0;
        UI_DrawComplexString(name, save, 0x0D, 0x08 + s * 4);
    }

    /* The character sprites, then the cursor (behind them). */
    UI_DrawCursor(0x58, slots[lu_cursor + SL_Y] + 8);
    for (int s = 0; s < 4; s++) {                 /* DrawOBSprite */
        const uint8_t* sl = &slots[s * 8];
        const CharacterStats* c = &save->characters[sl[SL_CHAR] >> 6];
        if (c->ailments == 1) continue;           /* the dead aren't drawn */
        int pal = lutClassBatSprPalette[c->class < 12 ? c->class : 0];
        int t = 0x100 + c->class * 32;
        if (c->ailments == 3) t += 0x14;
        else if (c->ailments == 2) { t += 0x14; pal = 3; }
        for (int k = 0; k < 6; k++)
            Ppu_DrawSprite(CHR_BANK_09, t + k, sl[SL_X] + (k & 1) * 8, sl[SL_Y] + (k >> 1) * 8, pal, 0);
    }
}
