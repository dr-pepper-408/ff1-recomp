#pragma once
#include "input.h"
#include "party.h"

/** How a battle ended (btl_result). */
typedef enum {
    BATTLE_CONTINUE = 0,   ///< still fighting
    BATTLE_WON,            ///< all enemies gone; rewards handed out
    BATTLE_RAN,            ///< the party escaped
    BATTLE_LOST,           ///< the party is dead or stone: game over
    BATTLE_ENDGAME,        ///< Chaos is beaten: on to the ending (EnterBattle's C set)
} BattleResult;

/*
 * battle.h — FF1 random-encounter battle engine (milestone 1).
 *
 * A simplified native port of FF1's battle loop.  Battle_Start() builds an
 * enemy group from battle_formations.h using bank_0B's quantity encoding
 * (high nibble = min count, low nibble = max count); Battle_Update() drives
 * a FIGHT/RUN turn loop; Battle_Draw() renders a text battle screen.  Enemy
 * turns run the real FF1 AI (Enemy_DoAi/Enemy_DoMagicEffect).
 *
 * Deferred: level-up and surprise rounds.
 */

/** Port of GetBattleFormation (bank_0F): a weighted random formation from
 *  battle domain `domain` (0-63 overworld land grid, $40/$41 rivers, $42 sea,
 *  64 + map for standard maps).  Returns the raw formation byte (bit 7 =
 *  "B formation"). */
int Battle_PickFormation(int domain);

/** The land domain for an overworld tile (8x8 grid of 32x32-tile areas). */
int Battle_LandDomain(int tile_x, int tile_y);

/** Set up a battle from a raw formation byte, on the given overworld tile
 *  (0..127), which selects the battle backdrop (lut_BtlBackdrops). */
void Battle_Start(SaveData* save, int formation, int ow_tile);

/** Process one frame of battle input.  Returns how the battle ended once its
 *  end-of-battle screens have been stepped through, else BATTLE_CONTINUE. */
BattleResult Battle_Update(SaveData* save, const InputState* in);

/** 1 while the end-of-battle screens are showing (for tests). */
int Battle_IsOver(void);

/** 1 when a battle menu is waiting for the pad (no animation playing). */
int Battle_WaitingForInput(void);

/** The respond rate (0-7, lut_RespondDelay: 120 .. 1 frames per message)
 *  battles use from the next one on.  0 is the original's default. */
void Battle_SetRespondRate(int rate);

/** For pictures and testing (not in the original): when `on`, character
 *  i's commands are chosen for them -- heal[i] (a spell id, -1 none) on the
 *  most hurt ally under half HP, else attack[i] while it has charges, else
 *  FIGHT. */
void Battle_SetAutopilot(int on, const int8_t attack[4], const int8_t heal[4]);

/** Render the battle screen. */
void Battle_Draw(const SaveData* save);

/** Where the last Battle_Draw put things, for other presentations (the 3D
 *  view): rectangles in screen pixels. */
typedef struct {
    int enemy_count;
    struct { int x, y, w, h; } enemy[9];   ///< the enemies drawn (a fiend / Chaos: one)
    int backdrop_x, backdrop_y, backdrop_w, backdrop_h;   ///< one copy of the backdrop
    int party_y, party_row;                 ///< character i's rows start at party_y + i * party_row
    int shake_x, shake_y;                   ///< BattleScreenShake's offset
    int flash;                              ///< DoMagicFlash's background colour, or -1
    int boss;                               ///< a boss: no running (Garland, the fiends, Chaos...)
    int stage_bottom;                       ///< where the boxes below the stage start: row 18
                                            ///< (the menus) or 19 (the combat boxes, which
                                            ///< leave the frame's bottom border showing)
} BattleView;
void Battle_GetView(BattleView* v);
