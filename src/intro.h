#pragma once
#include "input.h"

/*
 * intro.h -- the intro story (bank_0E EnterIntroStory): the prophecy text,
 * faded in two rows at a time, shown at power-on before the title screen.
 */

/** EnterIntroStory: the text drawn, every row faded out. */
void Intro_Open(void);

/** One frame (IntroStory_Frame).  @return 1 once the intro is over (A or B
 *  after the text is all shown, or Start at any time). */
int Intro_Update(const InputState* in);

/** Draw the intro story screen. */
void Intro_Draw(void);
