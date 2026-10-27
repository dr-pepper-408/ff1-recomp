#pragma once
#include <stdint.h>
#include "input.h"

/*
 * minigame.h -- the hidden sliding puzzle (bank_0D EnterMiniGame): in the
 * ship, hold A and press B the 55th time.  Solving it earns 100 GP.
 */

typedef enum { MINIGAME_PLAYING, MINIGAME_QUIT, MINIGAME_SOLVED } MinigameResult;

/** EnterMiniGame: the puzzle shuffled with the frame counter. */
void Minigame_Open(uint8_t framecounter);

/** One frame (MiniGameLoop, a slide's frame, or the victory loop). */
MinigameResult Minigame_Update(const InputState* in);

/** Draw the puzzle over the bridge picture. */
void Minigame_Draw(void);

/** The minigame's 16 BG colors (lut_BridgeBGPal), for the palette cycle out. */
const uint8_t* Minigame_Palette(void);

/** Tests: set the puzzle (and put the cursor on slot `cursor`). */
void Minigame_SetPuzzle(const uint8_t pieces[16], int cursor);
