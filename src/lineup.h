#pragma once
#include "input.h"
#include "../data/generated/types.h"

/*
 * lineup.h -- the lineup menu (bank_0E EnterLineupMenu, Select on the map):
 * reorder the party by picking two characters to swap.
 */

/** EnterLineupMenu. */
void Lineup_Open(const SaveData* save);

/** One frame of the menu's main loop.  @return 1 once the player has left
 *  it (B), with the new order applied to `save` (LineupMenu_Finalize). */
int Lineup_Update(SaveData* save, const InputState* in);

/** Draw the lineup menu. */
void Lineup_Draw(const SaveData* save);
