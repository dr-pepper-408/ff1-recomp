#pragma once
#include <stdint.h>
#include "input.h"

/*
 * title.h -- the title screen (bank_0E EnterTitleScreen): Continue, New
 * Game, and the respond rate (1-8, changed with left/right).
 */

typedef enum { TITLE_WAITING = -1, TITLE_CONTINUE = 0, TITLE_NEW_GAME = 1 } TitleChoice;

/** IntroTitlePrepare: cursor on Continue. */
void Title_Open(void);

/** One frame of EnterTitleScreen's loop.  `respond_rate` (0-7, the
 *  respondrate variable) is changed in place. */
TitleChoice Title_Update(const InputState* in, uint8_t* respond_rate);

/** Draw the title screen. */
void Title_Draw(uint8_t respond_rate);
