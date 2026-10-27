#pragma once
#include "input.h"
#include "party.h"

/*
 * shop.h -- town shops (bank_0E EnterShop and the per-type loops): weapon
 * and armor shops (buy / sell), white and black magic shops, clinics, inns
 * and item shops (and the caravan).
 */

/** EnterShop: open shop `shop_id` (a map tile's or the caravan's id). */
void Shop_Open(SaveData* save, int shop_id);

/** Run one frame.  @return 1 once the player has left the shop. */
int Shop_Update(SaveData* save, const InputState* in);

/** 1 once (then cleared) if the inn saved the game this visit (SaveGame). */
int Shop_TakeSaveRequest(void);

/** Draw the shop screen. */
void Shop_Draw(const SaveData* save);
