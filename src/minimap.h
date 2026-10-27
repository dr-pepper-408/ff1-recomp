#pragma once
#include "input.h"

/*
 * minimap.h -- the overworld minimap (bank_09 EnterMinimap, Select with B
 * held on the overworld): the world drawn at a quarter size, with the
 * towns and dungeons marked and the party's position blinking.
 */

/** EnterMinimap; (party_x, party_y) is the party's overworld tile. */
void Minimap_Open(int party_x, int party_y, uint8_t framecounter);

/** One frame.  @return 1 once the player leaves (A or B, after the map is
 *  drawn). */
int Minimap_Update(const InputState* in);

/** Draw the minimap screen. */
void Minimap_Draw(void);
