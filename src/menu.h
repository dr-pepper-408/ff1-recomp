#pragma once
#include "input.h"
#include "mapobj.h"
#include "../data/generated/types.h"

/*
 * menu.h -- the field menu: a port of bank_0E's EnterMainMenu and its sub
 * menus (EnterItemMenu with the UseItem_* routines, EnterMagicMenu with the
 * UseMagic_* routines, EnterEquipMenu, EnterStatusMenu).
 *
 * The menu draws into its own nametable, the way the original draws into
 * the PPU's: what is on screen is what the routines last drew, with the
 * values they had then.  Menu_Draw just shows it.
 */

/** Where the menu was opened from (what the item routines check). */
typedef struct {
    int on_sm;              ///< mapflags bit 0: on a standard map, not the overworld
    uint8_t tileprop_now;   ///< first property byte of the tile the party stands on
    MapObjects* objs;       ///< the map's objects (NULL on the overworld)
} MenuEnv;

typedef enum {
    MENU_CONTINUE,          ///< still in the menu
    MENU_CLOSED,            ///< B on the main menu
    MENU_WARP,              ///< WARP cast (UseMagic_WARP's stack trick: back one floor)
    MENU_EXIT,              ///< EXIT cast (or WARP with nowhere to warp back to): DoOverworld
} MenuResult;

/** EnterMainMenu. */
void Menu_Open(SaveData* save, const MenuEnv* env);

/** One frame (MenuFrame).  `framecounter` is the game's frame counter,
 *  which the menu reads (the CURE spells' "random" amounts) and, like
 *  EquipMenuFrame, bumps. */
MenuResult Menu_Update(SaveData* save, const InputState* in, uint8_t* framecounter);

/** 1 once (then cleared) when MenuSaveConfirm has called SaveGame. */
int Menu_TakeSaveRequest(void);

/** Show the menu's nametable and sprites. */
void Menu_Draw(const SaveData* save);
