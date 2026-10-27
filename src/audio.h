#pragma once

/*
 * audio.h -- the sound device (SDL): plays what the APU (apu.c) renders.
 */

/** Open the audio device (after SDL_InitSubSystem(SDL_INIT_AUDIO)).
 *  @return 1 on success; the game runs silently otherwise. */
int Audio_Init(void);

/** After each NES frame of game logic: render that frame's samples and
 *  queue them. */
void Audio_Frame(void);

void Audio_Shutdown(void);
