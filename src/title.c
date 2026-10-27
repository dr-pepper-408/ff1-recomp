/**
 * @file title.c
 * @brief The title screen: a port of bank_0E's EnterTitleScreen,
 *        TitleScreen_Copyright, IntroTitlePrepare and
 *        TitleScreen_DrawRespondRate.
 */

#include "title.h"
#include "ppu.h"
#include "ui.h"
#include "sfx.h"
#include "../data/generated/menu_data.h"
#include "../data/generated/battle_ui.h"

static int cursor;              /* 0 Continue, 1 New Game */
static uint8_t joy_prevdir;

void Title_Open(void) {
    cursor = 0;
    joy_prevdir = 0;
}

TitleChoice Title_Update(const InputState* in, uint8_t* respond_rate) {
    if (in->pressed & (BTN_A | BTN_START))      /* @OptionChosen */
        return cursor ? TITLE_NEW_GAME : TITLE_CONTINUE;
    uint8_t d = in->held & 0x0F;
    if (d == joy_prevdir) return TITLE_WAITING;
    joy_prevdir = d;
    if (!d) return TITLE_WAITING;
    if (d >= 0x04) {                            /* up/down: the other option */
        cursor ^= 1;
        Sfx_MenuSel();                          /* (the selection sound, not the move) */
    } else {
        *respond_rate = (uint8_t)((*respond_rate + (d == BTN_RIGHT ? 1 : -1)) & 7);
        Sfx_MenuMove();
    }
    return TITLE_WAITING;
}

void Title_Draw(uint8_t respond_rate) {
    /* LoadMenuCHRPal: the boxes and text use palette 3, the blue border;
     * the cursor a battle sprite palette. */
    static const uint8_t border_blue[4] = { 0x0F, 0x00, 0x01, 0x30 };
    Ppu_SetPalette(3, border_blue);
    for (int i = 0; i < 4; i++) Ppu_SetPalette(4 + i, BattleSpritePalettes + i * 4);
    Ppu_Clear(0);

    for (int i = 0; i < 2; i++)                 /* TitleScreen_Copyright */
        UI_DrawComplexString(&lut_Copyright[i][2], NULL, lut_Copyright[i][0], lut_Copyright[i][1]);

    /* Three boxes, each with its string at DrawBox's text position. */
    UI_DrawBox(11, 10, 10, 4);
    UI_DrawComplexString(lut_TitleText_Continue, NULL, 12, 12);
    UI_DrawBox(11, 15, 10, 4);
    UI_DrawComplexString(lut_TitleText_NewGame, NULL, 12, 17);
    UI_DrawBox(8, 20, 16, 4);
    UI_DrawComplexString(lut_TitleText_RespondRate, NULL, 9, 22);

    UI_DrawTile((uint8_t)(respond_rate + 0x80 + 1), 22, 22);   /* $22D6: 1-8 */
    UI_DrawCursor(0x48, lut_TitleCursor_Y[cursor]);
}
