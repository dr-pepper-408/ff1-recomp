#pragma once
#include <stdint.h>
#include "input.h"

/*
 * ptygen.h -- party generation (bank_0E NewGamePartyGeneration): choose the
 * four characters' classes, name each of them (DoNameInput), and confirm.
 */

/** NewGamePartyGeneration: the ptygen buffer reset, character 0 selected. */
void PtyGen_Open(void);

/** One frame.  @return 1 once the party has been confirmed. */
int PtyGen_Update(const InputState* in);

/** The confirmed party: classes (0-5) and names (4 font tiles each). */
void PtyGen_Result(uint8_t classes[4], uint8_t names[4][4]);

/** Draw the party generation or name input screen. */
void PtyGen_Draw(void);
