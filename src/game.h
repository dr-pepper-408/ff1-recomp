#pragma once
#include "input.h"
#include "party.h"
#include "text.h"
#include "screen_fx.h"
#include "mapobj.h"
#include "talk.h"

/** Whether the party is on the overworld or a standard map (town/dungeon). */
typedef enum { MODE_OW, MODE_SM } MapMode;

/** Top-level game state: what the player is doing right now.
 *  Orthogonal to MapMode, which only says whether the party is on the
 *  overworld or a standard map. */
typedef enum {
    GS_WALK,        ///< Free movement (overworld or standard map).
    GS_DIALOGUE,    ///< A dialogue box is showing.
    GS_FX,          ///< A screen transition is playing (wipe, palette cycle, battle flash).
    GS_MENU,        ///< Command menu open.
    GS_BATTLE,      ///< Battle in progress.
    GS_SHOP,        ///< In a shop, inn or clinic.
    GS_LINEUP,      ///< The lineup (party order) menu.
    GS_STORY,       ///< The bridge scene.
    GS_AIRSHIP,     ///< The airship taking off or landing.
    GS_MINIMAP,     ///< The overworld minimap.
    GS_MINIGAME,    ///< The sliding puzzle.
    GS_INTRO,       ///< The intro story (power-on).
    GS_TITLE,       ///< The title screen.
    GS_PARTYGEN,    ///< Party generation and name input (New Game).
} GameState;

typedef struct {
    int tile_x, tile_y;   ///< Party tile position.
    int sub_x, sub_y;     ///< Sub-tile pixel offset (0..15).
    uint8_t facing;       ///< BTN_RIGHT/LEFT/DOWN/UP.
    int moving;           ///< 1 while a step is in progress.
    int move_dir;         ///< Direction of the current step.
    uint8_t vehicle;      ///< 1 foot, 2 canoe, 4 ship, 8 airship.
    uint8_t vehicle_next; ///< The vehicle once the current step ends (boarding).
    int move_speed;       ///< Pixels per frame of the current step.
    int vehchgpause;      ///< Frames of input ignored after boarding.
} Player;

/** What happens when the running screen transition finishes. */
typedef enum {
    FXNEXT_WALK,        ///< Back to walking.
    FXNEXT_ENTER_MAP,   ///< Load the standard map for pending_teleport_id, then wipe open.
    FXNEXT_EXIT_MAP,    ///< Place the party on the overworld (pending_exit_id), then wipe open.
    FXNEXT_NORMAL_TELE, ///< Push the current map, load the normal teleport's map, wipe open.
    FXNEXT_WARP,        ///< Return to the previous map (or the overworld), wipe open.
    FXNEXT_MENU,        ///< Open the command menu.
    FXNEXT_BATTLE,      ///< Start the pending battle.
    FXNEXT_SHOP,        ///< Enter shop pending_shop_id.
    FXNEXT_LINEUP,      ///< Open the lineup menu.
    FXNEXT_MINIMAP,     ///< Open the minimap.
    FXNEXT_MINIGAME,    ///< Start the sliding puzzle.
    FXNEXT_BRIDGE,      ///< Start the bridge scene.
    FXNEXT_OW_PALCYC_IN, ///< EnterOW_PalCyc: cycle the overworld palette in.
} FxNext;

/** A standard-map position to return to with a warp (what FF1 pushes on the
 *  6502 stack before a normal teleport). */
typedef struct {
    uint8_t map, tileset;
    uint8_t x, y;
    uint8_t inroom;
} MapReturn;

#define WARP_STACK_MAX 16

/**
 * All game state, owned by the caller (main.c) and passed to every
 * update/draw function.  SaveData is the persistent part (what SRAM holds);
 * everything else is session state.
 */
typedef struct {
    SaveData  save;                 ///< Party, inventory, gold, game flags.
    Player    player;
    MapMode   mode;
    GameState state;

    ScreenFx fx;                    ///< Running transition (GS_FX).
    FxNext   fx_next;               ///< What to do when it finishes.
    uint8_t  pending_teleport_id;   ///< Entrance teleport (FXNEXT_ENTER_MAP).
    uint8_t  pending_exit_id;       ///< Exit teleport (FXNEXT_EXIT_MAP).
    uint8_t  pending_normal_id;     ///< Normal teleport (FXNEXT_NORMAL_TELE).
    MapReturn warp_stack[WARP_STACK_MAX];  ///< Floors to warp back to.
    int       warp_depth;
    int       ow_return_x, ow_return_y;    ///< Overworld tile the party entered from.
    int      pending_formation;     ///< Battle formation (FXNEXT_BATTLE).
    int      pending_backdrop_tile; ///< Overworld tile the battle started on.

    uint8_t   dlg_itemid;                     ///< Item named by dialogue code $02.
    TextGlyph text_glyphs[TEXT_MAX_GLYPHS];   ///< Formatted dialogue (GS_DIALOGUE).
    int       text_count;

    MapObjects objs;                ///< Objects on the current standard map.
    uint8_t    inroom;              ///< Room state: $81 in a room, $82 in a locked room
                                    ///< (outside sprites shown), 0 outside.
    uint8_t    door_x, door_y;      ///< The last door opened (RedrawDoor's doorppuaddr).
    TalkResult talk;                ///< Battle/teleport queued by the last conversation.
    uint8_t    framecounter;        ///< Incremented every frame (NMI's framecounter).

    /* Decided when a step starts (OWCanMove / CanPlayerMoveSM), acted on when
     * it completes. */
    int step_battle;                ///< Formation to fight, or -1.
    int step_damage;                ///< 1 if stepping onto a damage tile.
    int step_altar;                 ///< 1 if an orb was just lit (altar effect).
    int step_shop;                  ///< Shop id of the door being entered, or 0.
    uint8_t pending_shop_id;        ///< Shop to enter (FXNEXT_SHOP).

    int show_walk_overlay;          ///< Debug: walkability overlay (F3).
    int fx_story;                   ///< GS_FX is cycling the bridge scene out.
    int fx_minigame;                ///< GS_FX is cycling the minigame out.
    uint8_t dlgmusic_backup;        ///< The map's track, restored after a dialogue jingle.
    uint8_t dlgsfx;                 ///< The dialogue box's jingle still playing (1 fanfare,
                                    ///< 2 treasure jingle), or 0.
    int dlg_closing;                ///< A/B pressed while the jingle plays: close when it ends.
    uint8_t joy_b;                  ///< joy_b: B presses counted on the overworld (the
                                    ///< 55th, in the ship with A held, starts the minigame).
    int airship_y;                  ///< GS_AIRSHIP: the airship's sprite y (tmp+10).
    int airship_landing;            ///< GS_AIRSHIP: 1 landing, 0 taking off.
    int airship_shown;              ///< DrawOWSprites: the airship drawn this frame
                                    ///< (it flashes while rising, airship_vis < $1F).
    uint8_t respond_rate;           ///< respondrate (0-7): set on the title screen, kept
                                    ///< across new games and continues, not saved.
} GameContext;

/** GameStart at power-on: the intro story, then Game_Boot. */
void Game_PowerOn(GameContext* g);

/** GameStart (warm): the title screen, then Continue or New Game. */
void Game_Boot(GameContext* g);

/** A new game with the default party (and the debug loadout with
 *  FF1_DEBUG_STATE): what --new starts, skipping the front end. */
void Game_Init(GameContext* g);

/** A new game with the party chosen in party generation (level 1). */
void Game_NewGame(GameContext* g, const uint8_t classes[4], const uint8_t names[4][4]);

/** GameStart: continue the saved game if the save file holds a valid one
 *  (Save_ReadFile), otherwise start a new game.  @return 1 if continued. */
int Game_Start(GameContext* g);

/** Where the SRAM file is kept (default "ff1.sav"). */
void Game_SetSavePath(const char* path);

/** Random encounters on (the original) or off: the step's roll still
 *  happens (the RNG runs the same course), but no battle follows.  Fixed
 *  battles (spiked tiles, bosses) are not affected. */
void Game_SetRandomEncounters(int on);

/** SaveGame: save to the SRAM file now.  @return 1 on success. */
int Game_Save(GameContext* g);

/** Release game data (call once at exit). */
void Game_Shutdown(void);

/** Start the overworld entrance teleport `entrance_id`, as if the party had
 *  stepped on its tile: wipe closed, load the map, wipe open. */
void Game_EnterTeleport(GameContext* g, uint8_t entrance_id);

/** For testing (the location tour): go to entrance `id`, or normal teleport
 *  `id` if `normal`, from wherever the party is walking, through the usual
 *  wipe.  0: not now (not walking). */
int Game_DebugGoto(GameContext* g, int normal, uint8_t id);

/** Advance the game by one NES frame.  No rendering happens here. */
void Game_Update(GameContext* g, const InputState* input);

/** Render the current game state into the PPU frame buffer (ppu.h). */
void Game_Draw(const GameContext* g);
