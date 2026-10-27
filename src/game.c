/**
 * @file game.c
 * @brief Game state machine and overworld/standard-map player movement.
 *
 * Ports the core of FF1's overworld logic (ProcessOWInput / OWCanMove /
 * OW_MovePlayer from bank_0F.asm) into a simplified native form:
 *
 *   - The party occupies a tile (tile_x, tile_y) on the 256×256 world map,
 *     plus a sub-tile pixel offset (sub_x, sub_y in 0..15) for smooth steps.
 *   - Movement is one 16-pixel step at a time, 1 pixel per frame (on foot).
 *   - A step is only started if OWMap_CanMove() reports the destination tile
 *     is traversable for the current vehicle (walking for now).
 *   - The viewport keeps the party fixed at screen tile (7,7) while the map
 *     scrolls underneath, matching the NES's fixed player position.
 */

#include "game.h"
#include "input.h"
#include "config.h"
#include "overworld_map.h"
#include "render_overworld.h"
#include "render_sm.h"
#include "sm_map.h"
#include "text.h"
#include "ui.h"
#include "ppu.h"
#include <string.h>
#include <stdio.h>
#include "party.h"
#include "menu.h"
#include "shop.h"
#include "save.h"
#include "title.h"
#include "ptygen.h"
#include "intro.h"
#include "lineup.h"
#include "story.h"
#include "minimap.h"
#include "minigame.h"
#include "music.h"
#include "sfx.h"
#include "battle.h"
#include "rng.h"
#include "../data/generated/treasure_data.h"
#include "../data/generated/palette_data.h"
#include "../data/generated/ow_objects.h"
#include "../data/generated/encounter_data.h"

/** Party starting position: on the road south of Coneria castle.
 *  From init_unsram_first_page[]: ow_scroll = (0x92, 0x9E), and the party is
 *  always 7 tiles right/down of the scroll, so tile = (0x99, 0xA5). */
#define PLAYER_START_X  0x99
#define PLAYER_START_Y  0xA5

/** Random-encounter chance per overworld step (out of 256), OWCanMove:
 *  10 normally, 3 on the ship. */
#define OW_ENCOUNTER_RATE       10
#define OW_ENCOUNTER_RATE_SHIP  3
#define VEHICLE_FOOT            1
#define VEHICLE_CANOE           2
#define VEHICLE_SHIP            4
#define VEHICLE_AIRSHIP         8
#define OWTP_DOCKSHIP           0x20

/** Player is fixed 7 tiles from the left/top of the screen. */
#define PLAYER_SCREEN_TILE 7

/* Constants.inc: door metatiles, and items[] index of the Mystic Key. */
#define MAPTILE_CLOSEDDOOR 0x36
#define MAPTILE_OPENDOOR   0x37
#define MAPTILE_LOCKEDDOOR 0x3B
#define ITEM_MYSTICKEY     0x05
#define ITEM_CROWN         0x02
#define ITEM_CHIME         0x0C
#define ITEM_CUBE          0x0E
#define ORB_FIRE           0x12
#define ORB_WATER          0x13
#define ORB_AIR            0x14
#define ORB_EARTH          0x15

/* Out-of-battle ailment ids (ch_ailments): 1 dead, 2 stone, 3 poison. */
#define AILMENT_POISON     0x03

/* Overworld special tile bits (Constants.inc OWTP_SPEC_*).  constants.h is
 * not included here: its BTN_START/RIGHT/... clash with input.h. */
#define OWTP_SPEC_MASK     0xC0
#define OWTP_SPEC_CHIME    0x40
#define OWTP_SPEC_CARAVAN  0x80
#define OBJID_FAIRY        0x13
#define SHOPID_CARAVAN     70

/* Battle domains past the 8x8 overworld land grid (OWCanMove, SMMove_Battle). */
#define DOMAIN_RIVER       0x40   /* +1 for the southern half of the map */
#define DOMAIN_SEA         0x42
#define DOMAIN_SM_BASE     0x40   /* 8*8 + map */

/** Resets the player to the starting position. */
static void player_init(GameContext* g) {
    g->player.tile_x  = PLAYER_START_X;
    g->player.tile_y  = PLAYER_START_Y;
    g->player.sub_x   = 0;
    g->player.sub_y   = 0;
    g->player.facing  = BTN_UP;   /* face north toward Coneria castle */
    g->player.moving  = 0;
    g->player.move_dir = 0;
    g->player.vehicle = VEHICLE_FOOT;
    g->player.vehicle_next = VEHICLE_FOOT;
}

/** Converts a single direction bit into a tile-space delta. */
static void dir_delta(int dir, int* dx, int* dy) {
    *dx = 0;
    *dy = 0;
    switch (dir) {
    case BTN_RIGHT: *dx =  1; break;
    case BTN_LEFT:  *dx = -1; break;
    case BTN_DOWN:  *dy =  1; break;
    case BTN_UP:    *dy = -1; break;
    default: break;
    }
}

static int all_orbs_lit(const SaveData* s) {
    return (s->items[ORB_FIRE] & s->items[ORB_WATER] & s->items[ORB_AIR] & s->items[ORB_EARTH]) != 0;
}

/** IsOnBridge: (x, y) is where the King's bridge stands, once built. */
static int is_on_bridge(const GameContext* g, int x, int y) {
    return g->save.bridge_vis && x == g->save.bridge_x && y == g->save.bridge_y;
}

/** IsOnCanal: (x, y) is where the canal is, while it's there. */
static int is_on_canal(const GameContext* g, int x, int y) {
    return g->save.canal_vis && x == g->save.canal_x && y == g->save.canal_y;
}

/** Collision check for the current map mode: OWCanMove / CanPlayerMoveSM
 *  (without their side effects, see step_start). */
static int can_move(const GameContext* g, int x, int y, uint8_t vehicle) {
    if (g->mode == MODE_OW) {
        x &= 0xFF;
        y &= 0xFF;
        /* On foot, a tile OWCanMove refuses can still be the bridge. */
        if (!OWMap_CanMove(x, y, vehicle)) return vehicle == 1 && is_on_bridge(g, x, y);
        /* On foot, a non-battle tile needing the Chime (the desert around
         * the Mirage Tower) blocks without it. */
        const OWMetatile* m = OWMap_GetMetatile(OWMap_GetTileID(x, y));
        if (m && vehicle == 1 && !(m->prop1 & 0x40) &&
            (m->prop0 & OWTP_SPEC_MASK) == OWTP_SPEC_CHIME)
            return g->save.items[ITEM_CHIME] != 0;
        return 1;
    }
    x &= SM_MAP_WIDTH - 1;                 /* standard maps wrap at 64 tiles */
    y &= SM_MAP_HEIGHT - 1;
    const OWMetatile* m = SM_GetMetatile(SM_GetTileID(x, y));
    if (m && (m->prop0 & 0x1F) != 0x01) {  /* not a plain wall */
        switch (m->prop0 & SM_TP_SPEC_MASK) {
        case SM_TP_SPEC_LOCKED: return g->save.items[ITEM_MYSTICKEY] != 0;   /* SMMove_Door */
        case SM_TP_SPEC_CROWN:  return g->save.items[ITEM_CROWN] != 0;
        case SM_TP_SPEC_CUBE:   return g->save.items[ITEM_CUBE] != 0;
        case SM_TP_SPEC_4ORBS:  return all_orbs_lit(&g->save);
        }
    }
    return SM_CanWalk(x, y);
}

/** The current map's 32 palette bytes as LoadMapPalettes builds them: the
 *  map palettes, with the lead character's two mapman colors in sprite
 *  palettes 0/1, color 2. */
static void map_palettes(const GameContext* g, uint8_t pal[32]) {
    if (g->mode == MODE_SM) {
        SM_GetMapPalettes(SM_GetMapId(), pal);
        if (g->inroom) SM_GetRoomPalette(SM_GetMapId(), pal);
    } else {
        OWMap_GetMapPalettes(pal);
    }
    uint8_t cls = g->save.characters[0].class;
    if (cls * 2 + 1 < (int)sizeof(lut_MapmanPalettes)) {
        pal[0x12] = lut_MapmanPalettes[cls * 2];
        pal[0x16] = lut_MapmanPalettes[cls * 2 + 1];
    }
}

/** Start a screen transition; `next` runs when it finishes. */
/* The current map's track: PrepOverworld (by vehicle) or PrepStandardMap
 * (by tileset). */
static uint8_t map_music(const GameContext* g) {
    return g->mode == MODE_OW ? Music_VehicleTrack(g->player.vehicle) : Music_TilesetTrack(SM_GetTileset());
}

static void begin_fx(GameContext* g, ScreenFxKind kind, FxNext next) {
    /* Back to a map (DoOverworld, EnterOW_PalCyc, EnterStandardMap,
     * ReenterStandardMap): its music starts as it's prepared. */
    if (next == FXNEXT_WALK && (kind == SCREENFX_WIPE_OPEN || kind == SCREENFX_PALCYC_IN)) {
        g->dlgmusic_backup = map_music(g);
        g_music_track = g->dlgmusic_backup;
        g_music_followup = 0;
    }
    uint8_t pal[32];
    map_palettes(g, pal);
    ScreenFx_Start(&g->fx, kind, pal);
    g->fx_next = next;
    g->state = GS_FX;
}

/** Port of SMMove_Door / SMMove_CloseRoom + RedrawDoor: a step onto (x, y)
 *  is starting.  Doors take the party into a room; the tile outside a room's
 *  door (TP_SPEC_CLOSEROOM) takes it back out. */
static void room_step(GameContext* g, int x, int y) {
    const OWMetatile* m = SM_GetMetatile(SM_GetTileID(x, y));
    if (!m) return;
    switch (m->prop0 & SM_TP_SPEC_MASK) {
    case SM_TP_SPEC_DOOR:
    case SM_TP_SPEC_LOCKED:
        if (!(g->inroom & 0x80)) Sfx_Door();      /* coming in from outside */
        g->inroom = ((m->prop0 & SM_TP_SPEC_MASK) == SM_TP_SPEC_DOOR) ? 0x81 : 0x82;
        g->door_x = (uint8_t)x;
        g->door_y = (uint8_t)y;
        SMRender_SetDoorTile(x, y, MAPTILE_OPENDOOR);
        /* @CheckShop: a door's prop1 is its shop id (locked doors erase it
         * first, so there are no locked shops). */
        if ((m->prop0 & SM_TP_SPEC_MASK) == SM_TP_SPEC_DOOR) g->step_shop = m->prop1;
        break;
    case SM_TP_SPEC_CLOSEROOM:
        if (!(g->inroom & 0x80)) break;             /* not coming from a room */
        Sfx_Door();
        /* $81 ^ $84 = $05 closes a normal door, $82 ^ $84 = $06 a locked one;
         * either way the party is then outside ($00). */
        SMRender_SetDoorTile(g->door_x, g->door_y,
                             ((g->inroom ^ 0x84) == 0x05) ? MAPTILE_CLOSEDDOOR : MAPTILE_LOCKEDDOOR);
        g->inroom = 0;
        break;
    }
}

/** Queue an overworld->standard-map entrance: wipe closed first
 *  (DoOWTransitions @Teleport). */
static void transition_begin_entrance(GameContext* g, uint8_t id) {
    g->pending_teleport_id = id;
    g->ow_return_x = g->player.tile_x;     /* a warp from the first floor returns here */
    g->ow_return_y = g->player.tile_y;
    g->warp_depth = 0;
    begin_fx(g, SCREENFX_WIPE_CLOSE, FXNEXT_ENTER_MAP);
}

/** Queue a standard-map->overworld exit: wipe closed first. */
static void transition_begin_exit(GameContext* g, uint8_t exit_id) {
    g->pending_exit_id = exit_id;
    begin_fx(g, SCREENFX_WIPE_CLOSE, FXNEXT_EXIT_MAP);
}

/** Place the party on standard map `map` at (x, y). */
static void place_on_map(GameContext* g, int map, int tileset, int x, int y) {
    SM_LoadMap(map);
    SM_LoadTileset(tileset);
    MapObj_Load(&g->objs, map, &g->save);
    g->inroom = 0;                         /* maps are entered outside rooms */
    SMRender_SetDoorTile(-1, -1, -1);
    g->mode = MODE_SM;
    g->player.tile_x = x;
    g->player.tile_y = y;
    g->player.sub_x = 0;
    g->player.sub_y = 0;
    g->player.moving = 0;
    g->player.facing = BTN_DOWN;
}

/** Normal teleport: remember this floor for a later warp, then go to the
 *  destination from lut_NormTele_X/Y/Map. */
static void normal_teleport(GameContext* g) {
    if (g->warp_depth < WARP_STACK_MAX) {
        MapReturn* r = &g->warp_stack[g->warp_depth++];
        r->map = (uint8_t)SM_GetMapId();
        r->tileset = (uint8_t)SM_GetTileset();
        r->x = (uint8_t)g->player.tile_x;
        r->y = (uint8_t)g->player.tile_y;
        r->inroom = g->inroom;
    }
    int map, x, y, ts;
    SM_LookupNormal(g->pending_normal_id, &map, &x, &y, &ts);
    place_on_map(g, map, ts, x, y);
}

/** Place the party on the overworld at (x, y). */
static void place_on_overworld(GameContext* g, int x, int y) {
    g->player.tile_x = x;
    g->player.tile_y = y;
    g->player.sub_x = 0;
    g->player.sub_y = 0;
    g->player.moving = 0;
    g->player.facing = BTN_DOWN;
    g->mode = MODE_OW;
    g->warp_depth = 0;
}

/** Warp: back to the floor the last normal teleport left, or out to the
 *  overworld tile the party entered from (DoStandardMap returning). */
static void warp_back(GameContext* g) {
    if (g->warp_depth > 0) {
        const MapReturn* r = &g->warp_stack[--g->warp_depth];
        place_on_map(g, r->map, r->tileset, r->x, r->y);
        g->inroom = r->inroom;
    } else {
        place_on_overworld(g, g->ow_return_x, g->ow_return_y);
    }
}

/** Handle overworld->map entrance and standard-map->overworld exit teleports. */
static void check_teleport(GameContext* g) {
    if (g->mode == MODE_OW) {
        /* The airship doesn't look at the tiles it flies over (@Movement
         * skips OWCanMove), so no teleport from the air. */
        if (g->player.vehicle == VEHICLE_AIRSHIP) return;
        /* Standing on an overworld teleport tile: queue an entrance transition. */
        const OWMetatile* m = OWMap_GetMetatile(OWMap_GetTileID(g->player.tile_x, g->player.tile_y));
        if (m && (m->prop1 & 0x80)) {
            transition_begin_entrance(g, (uint8_t)(m->prop1 & 0x3F));
        }
    } else {
        /* Standard map (StandardMapLoop @TeleOrWarp): the tile's teleport bits
         * (prop0) and teleport id (prop1). */
        const OWMetatile* m = SM_GetMetatile(SM_GetTileID(g->player.tile_x, g->player.tile_y));
        if (!m) return;
        switch (m->prop0 & SM_TP_TELE_MASK) {
        case SM_TP_TELE_EXIT:          /* to the overworld at lut_ExitTele_X/Y */
            transition_begin_exit(g, m->prop1);
            break;
        case SM_TP_TELE_NORM:          /* to another map (stairs, doors) */
            g->pending_normal_id = m->prop1;
            begin_fx(g, SCREENFX_WIPE_CLOSE, FXNEXT_NORMAL_TELE);
            break;
        case SM_TP_TELE_WARP:          /* back one floor, e.g. the grass around towns */
            begin_fx(g, SCREENFX_WIPE_CLOSE, FXNEXT_WARP);
            break;
        }
    }
}

/* Treasure-chest open flag + talk-to-tile dialogue IDs (see Constants.inc). */
#define GMFLG_TCOPEN   0x04
#define DLGID_NOTHING  0x00   /* "Nothing Here" */
#define DLGID_TCGET    0xF0   /* "In this chest you find..." */
#define DLGID_CANTCARRY 0xF1  /* "You can't carry anymore" */
#define DLGID_EMPTYTC  0xF2   /* "This treasure chest is empty" */

/* Treasure content type boundaries (Constants.inc TCITYPE_*). */
#define TCITYPE_WEPSTART  0x1C   /* 28: weapon ids begin */
#define ITEM_QTY_START    0x16   /* items[] from here are counted (TENT on) */
#define TCITYPE_ARMSTART  0x44   /* 68: armor ids begin */
#define TCITYPE_GPSTART   0x6C   /* 108: gold amounts begin */

/** A step onto (x, y) is starting: decide what it triggers when it ends.
 *  Ports the side effects of OWCanMove (@Fighting) and of lut_SMMoveJmpTbl
 *  (SMMove_Battle, SMMove_Dmg, SMMove_xOrb). */
/** OWCanMove: can the current vehicle move onto (x, y)?  If so, a fighting
 *  tile rolls for an encounter (step_battle), and on foot the caravan
 *  (step_shop) and the Chime's desert are checked. */
static int s_random_encounters = 1;

void Game_SetRandomEncounters(int on) { s_random_encounters = on != 0; }

static int ow_can_move(GameContext* g, int x, int y) {
    uint8_t v = g->player.vehicle;
    if (!OWMap_CanMove(x, y, v)) return 0;               /* tileprop AND vehicle */
    const OWMetatile* m = OWMap_GetMetatile(OWMap_GetTileID(x, y));
    if (!m) return 1;
    if (m->prop1 & 0x40) {                               /* @Fighting */
        if (m->prop1 & 0x80) return 1;
        int rate = (v == VEHICLE_SHIP) ? OW_ENCOUNTER_RATE_SHIP : OW_ENCOUNTER_RATE;
        if (Rng_BattleStep() >= rate) return 1;
        if (!s_random_encounters) return 1;              /* (an option: no random battles) */
        /* The land and river domains use the party's position before the
         * step (the original's comments call this bugged: crossing a domain
         * boundary picks from the domain being left). */
        int domain;
        switch (m->prop1 & 0x03) {
        case 0:  domain = Battle_LandDomain(g->player.tile_x, g->player.tile_y); break;
        case 2:  domain = DOMAIN_SEA; break;
        default: domain = DOMAIN_RIVER | ((g->player.tile_y >> 7) & 1); break;
        }
        g->step_battle = Battle_PickFormation(domain);
        return 1;
    }
    if (v != VEHICLE_FOOT) return 1;
    switch (m->prop0 & OWTP_SPEC_MASK) {                 /* @OnFoot */
    case OWTP_SPEC_CHIME:
        return g->save.items[ITEM_CHIME] != 0;
    case OWTP_SPEC_CARAVAN:                              /* until the fairy is out */
        if (!(g->save.game_flags[OBJID_FAIRY] & 1)) g->step_shop = SHOPID_CARAVAN;
        return 1;
    }
    return 1;
}

/** DockShip: the ship is left where the party stands. */
static void dock_ship(GameContext* g) {
    g->save.ship_x = (uint8_t)g->player.tile_x;
    g->save.ship_y = (uint8_t)g->player.tile_y;
    g_music_track = MUSIC_OVERWORLD;
    Sfx_NoiseSilence();
}

/** UnboardBoat_Abs: off the canoe or ship, on foot from now on (docking
 *  the ship if that's what it was). */
static void unboard_abs(GameContext* g) {
    uint8_t old = g->player.vehicle;
    g->player.vehicle = g->player.vehicle_next = VEHICLE_FOOT;
    g->step_battle = -1;
    if (old == VEHICLE_SHIP) dock_ship(g);
}

/** BoardCanoe: onto a river tile with the canoe.  The party walks onto it
 *  and is in the canoe once there. */
static int board_canoe(GameContext* g, int x, int y) {
    if ((OWMap_GetTerrain(x, y) & 0x02) || !g->save.has_canoe) return 0;
    g->player.vehchgpause = 4;
    uint8_t old = g->player.vehicle;
    g->player.vehicle = VEHICLE_FOOT;
    g->player.vehicle_next = VEHICLE_CANOE;
    g->step_battle = -1;
    if (old == VEHICLE_SHIP) dock_ship(g);
    return 1;
}

/** BoardShip: onto the tile where the ship is docked. */
static int board_ship(GameContext* g, int x, int y) {
    if (!g->save.ship_vis || x != g->save.ship_x || y != g->save.ship_y) return 0;
    g->player.vehicle = VEHICLE_FOOT;
    g->player.vehicle_next = VEHICLE_SHIP;
    g->player.vehchgpause = 4;
    g->step_battle = -1;
    g_music_track = MUSIC_SHIP;
    return 1;
}

/** ProcessOWInput @Movement: can the party move onto (x, y), by its own
 *  vehicle or by boarding / leaving one?  Sets up the step's battle, shop
 *  and vehicle changes. */
static int ow_start_move(GameContext* g, int x, int y) {
    g->step_battle = -1;
    g->step_damage = 0;
    g->step_altar = 0;
    switch (g->player.vehicle) {
    case VEHICLE_AIRSHIP:                                /* no collision at all */
        return 1;
    case VEHICLE_SHIP:                                   /* @MoveShip */
        if (ow_can_move(g, x, y)) {
            if (!is_on_canal(g, x, y)) return 1;         /* the canal blocks the ship */
            break;
        }
        if (board_canoe(g, x, y)) return 1;
        if ((OWMap_GetTerrain(x, y) & (OWTP_DOCKSHIP | 1)) == OWTP_DOCKSHIP) {
            unboard_abs(g);                              /* walk ashore where it can dock */
            return 1;
        }
        break;
    case VEHICLE_CANOE:                                  /* @MoveCanoe */
        if (ow_can_move(g, x, y)) return 1;
        if (!(OWMap_GetTerrain(x, y) & 0x01)) {          /* UnboardBoat */
            unboard_abs(g);
            return 1;
        }
        if (board_ship(g, x, y)) return 1;
        break;
    default:                                             /* @MoveOnFoot */
        if (ow_can_move(g, x, y)) return 1;
        g->step_battle = -1;
        if (is_on_bridge(g, x, y) || is_on_canal(g, x, y)) {   /* IsOnBridge (and IsOnCanal) */
            if (!g->save.bridgescene) g->save.bridgescene++;
            return 1;
        }
        if (board_canoe(g, x, y)) return 1;
        if (board_ship(g, x, y)) return 1;
        break;
    }
    g->step_battle = -1;                                 /* @CantMove */
    g->step_shop = 0;
    return 0;
}

static void step_start(GameContext* g, int x, int y) {
    g->step_battle = -1;
    g->step_damage = 0;
    g->step_altar = 0;

    const OWMetatile* m = SM_GetMetatile(SM_GetTileID(x, y));
    if (!m) return;
    uint8_t* orb = NULL;
    switch (m->prop0 & SM_TP_SPEC_MASK) {
    case SM_TP_SPEC_BATTLE:
        if (m->prop1 & 0x80) {             /* random encounter at this map's rate */
            if (Rng_BattleStep() < lut_BattleRates[SM_GetMapId() + 1] && s_random_encounters)
                g->step_battle = Battle_PickFormation(DOMAIN_SM_BASE + SM_GetMapId());
        } else {                           /* "spiked" tile: a fixed formation */
            g->step_battle = m->prop1;
        }
        break;
    case SM_TP_SPEC_DAMAGE:   g->step_damage = 1; break;
    case SM_TP_SPEC_EARTHORB: orb = &g->save.items[ORB_EARTH]; break;
    case SM_TP_SPEC_FIREORB:  orb = &g->save.items[ORB_FIRE];  break;
    case SM_TP_SPEC_WATERORB: orb = &g->save.items[ORB_WATER]; break;
    case SM_TP_SPEC_AIRORB:   orb = &g->save.items[ORB_AIR];   break;
    }
    if (orb && !*orb) {                    /* light the orb */
        *orb = 1;
        g->step_altar = 1;
    }
}

/** Ports MapPoisonDamage and AssignMapTileDamage: 1 HP per step to poisoned
 *  characters (or to everyone on a damage tile), never taking the last HP. */
static void step_damage(GameContext* g, int everyone) {
    for (int i = 0; i < MAX_PARTY_SIZE; i++) {
        CharacterStats* c = &g->save.characters[i];
        if (!everyone && c->ailments != AILMENT_POISON) continue;
        if (c->cur_hp >= 2) c->cur_hp--;
    }
}

/** A step has completed: damage, then the altar effect, a teleport or a
 *  battle (StandardMapLoop / DoOWTransitions). */
static void step_end(GameContext* g) {
    if (g->mode == MODE_SM || g->player.vehicle == 1) step_damage(g, 0);   /* poison */
    if (g->mode == MODE_SM && g->step_damage) step_damage(g, 1);

    if (g->step_altar) {
        g->step_altar = 0;
        begin_fx(g, SCREENFX_ALTAR, FXNEXT_WALK);
        return;
    }
    if (g->step_shop) {
        /* StandardMapLoop @Shop / DoOWTransitions: out of the room (shop
         * doors are also room doors), cycle the palettes out, EnterShop. */
        g->pending_shop_id = (uint8_t)g->step_shop;
        g->step_shop = 0;
        g->inroom = 0;
        begin_fx(g, SCREENFX_PALCYC_OUT, FXNEXT_SHOP);
        return;
    }
    check_teleport(g);
    if (g->state != GS_WALK) return;
    if (g->step_battle >= 0) {
        g->pending_formation = g->step_battle;
        g->pending_backdrop_tile = (g->mode == MODE_OW)
            ? OWMap_GetTileID(g->player.tile_x, g->player.tile_y)
            : OWMap_GetTileID(g->ow_return_x, g->ow_return_y);
        g->step_battle = -1;
        begin_fx(g, SCREENFX_BATTLE, FXNEXT_BATTLE);
    }
}

/** Port of TalkToSMTile (bank_0F): dialogue ID for the tile at (x, y).
 *  Treasure chests go through OpenTreasureChest. */
static int talk_to_tile(GameContext* g, int x, int y) {
    const OWMetatile* m = SM_GetMetatile(SM_GetTileID(x, y));
    if (!m) return DLGID_NOTHING;

    if ((m->prop0 & SM_TP_SPEC_MASK) == SM_TP_SPEC_TREASURE) {
        uint8_t chest = m->prop1;
        if (g->save.game_flags[chest] & GMFLG_TCOPEN) return DLGID_EMPTYTC;

        /* Port of OpenTreasureChest (bank_0F).  The contents byte is also the
         * item-name id printed by the $02 code in "In this chest you find". */
        uint8_t contents = treasure_data[chest];
        g->dlg_itemid = contents;
        if (contents < TCITYPE_WEPSTART) {
            /* Item: given only if the party holds fewer than 99, but the chest
             * opens either way (arguably a bug in the original). */
            Party_AddItem(&g->save, contents);
        } else if (contents < TCITYPE_ARMSTART) {
            /* Weapon (1-based id); a full inventory leaves the chest closed. */
            if (Party_AddWeapon(&g->save, (uint8_t)(contents - TCITYPE_WEPSTART + 1)) != 0)
                return DLGID_CANTCARRY;
        } else if (contents < TCITYPE_GPSTART) {
            /* Armor (1-based id), same rule. */
            if (Party_AddArmor(&g->save, (uint8_t)(contents - TCITYPE_ARMSTART + 1)) != 0)
                return DLGID_CANTCARRY;
        } else {
            /* Gold: the amount is the "price" of this item id. */
            Party_AddGold(&g->save, Text_ItemPrice(contents));
        }

        g->save.game_flags[chest] |= GMFLG_TCOPEN;
        /* dlgsfx: the key item fanfare, or the treasure jingle */
        g->talk.sfx = (contents < TCITYPE_WEPSTART && contents < ITEM_QTY_START) ? 1 : 2;
        return DLGID_TCGET;
    }

    if (m->prop0 & SM_TP_NOTEXT_MASK) return DLGID_NOTHING;
    return m->prop1;
}

/** Start showing a dialogue box for dialogue id `dlg_id`. */
static void start_dialogue(GameContext* g, int dlg_id) {
    g->text_count = Text_FormatDialogue(Text_Dialogue((uint8_t)dlg_id), &g->save,
                                        g->dlg_itemid, g->text_glyphs, TEXT_MAX_GLYPHS);
    g->state = GS_DIALOGUE;
    Sfx_DialogueOpen();
    g->dlgsfx = g->talk.sfx;
    if (g->dlgsfx) g_music_track = (g->dlgsfx == 1) ? MUSIC_FANFARE : MUSIC_TREASURE;
}

/** AnimateAirshipTakeoff ($6F up to $4F) / AnimateAirshipLanding ($4F
 *  down to $70): the airship moves a pixel every other frame. */
static void airship_animate(GameContext* g, int landing) {
    g->airship_landing = landing;
    g->airship_y = landing ? 0x4F : 0x6F;
    g->state = GS_AIRSHIP;
}

/** GS_AIRSHIP: a frame of the animation (AirshipTransitionFrame). */
static void update_airship(GameContext* g) {
    Sfx_AirshipTransition(g->framecounter);
    if (g->framecounter & 1) return;
    if (!g->airship_landing) {
        if (--g->airship_y >= 0x4F) return;
        g->player.facing = BTN_RIGHT;
        g->state = GS_WALK;
        return;
    }
    if (++g->airship_y < 0x70) return;
    g->player.facing = BTN_RIGHT;
    /* LandAirship: only where the airship may land; otherwise it takes off
     * again.  Landed, it's parked there and the party is on foot. */
    int x = g->player.tile_x & 0xFF, y = g->player.tile_y & 0xFF;
    if (!OWMap_CanMove(x, y, VEHICLE_AIRSHIP)) {
        airship_animate(g, 0);
        return;
    }
    Sfx_NoiseOff();                   /* AnimateAirshipLanding: the engine stops */
    g->save.airship_x = (uint8_t)x;
    g->save.airship_y = (uint8_t)y;
    g->player.vehicle = g->player.vehicle_next = VEHICLE_FOOT;
    g_music_track = MUSIC_OVERWORLD;
    g->state = GS_WALK;
}

/** GS_WALK: free movement + A-button interaction. */
static void update_walk(GameContext* g, const InputState* in) {
    /* DoOWTransitions: the bridge scene, once bridgescene is set (the first
     * step onto the bridge) and before it's been seen ($80). */
    if (g->mode == MODE_OW && !g->player.moving && g->save.bridgescene &&
        !(g->save.bridgescene & 0x80)) {
        begin_fx(g, SCREENFX_PALCYC_OUT, FXNEXT_BRIDGE);
        return;
    }

    /* Start opens the command menu, after cycling the palette out. */
    if (in->pressed & BTN_START) {
        begin_fx(g, SCREENFX_PALCYC_OUT, FXNEXT_MENU);
        return;
    }
    /* Select opens the lineup menu; on the overworld with B held, the
     * minimap. */
    if (in->pressed & BTN_SELECT) {
        int minimap = g->mode == MODE_OW && (in->held & BTN_B);
        begin_fx(g, SCREENFX_PALCYC_OUT, minimap ? FXNEXT_MINIMAP : FXNEXT_LINEUP);
        return;
    }

    if (g->player.moving) {
        /* Advance the sub-tile offset; crossing a tile boundary moves the
         * party to the next tile and completes the step.  Coordinates wrap:
         * 256 tiles on the overworld, 64 on standard maps. */
        int wrap = (g->mode == MODE_SM) ? 0x3F : 0xFF;
        switch (g->player.move_dir) {
        case BTN_RIGHT:
            g->player.sub_x += g->player.move_speed;
            if (g->player.sub_x >= 16) {
                g->player.sub_x = 0;
                g->player.tile_x = (g->player.tile_x + 1) & wrap;
                g->player.moving = 0;
            }
            break;
        case BTN_LEFT:
            g->player.sub_x -= g->player.move_speed;
            if (g->player.sub_x <= -16) {
                g->player.sub_x += 16;
                g->player.tile_x = (g->player.tile_x - 1) & wrap;
                g->player.moving = 0;
            }
            break;
        case BTN_DOWN:
            g->player.sub_y += g->player.move_speed;
            if (g->player.sub_y >= 16) {
                g->player.sub_y = 0;
                g->player.tile_y = (g->player.tile_y + 1) & wrap;
                g->player.moving = 0;
            }
            break;
        case BTN_UP:
            g->player.sub_y -= g->player.move_speed;
            if (g->player.sub_y <= -16) {
                g->player.sub_y += 16;
                g->player.tile_y = (g->player.tile_y - 1) & wrap;
                g->player.moving = 0;
            }
            break;
        default:
            g->player.moving = 0;
            break;
        }

        /* MapPoisonDamage's sound plays every frame of a step (on the
         * overworld only on foot), MapTileDamage's on lava and frost. */
        if (g->mode == MODE_SM || g->player.vehicle == VEHICLE_FOOT)
            for (int i = 0; i < MAX_PARTY_SIZE; i++)
                if (g->save.characters[i].ailments == AILMENT_POISON) { Sfx_PoisonStep(); break; }
        if (g->mode == MODE_SM && g->step_damage) Sfx_DamageTile();

        if (!g->player.moving) step_end(g);   /* a step just completed */
        return;
    }

    /* Not moving: the vehicle boarded during the last step is now the one
     * in use, and boarding pauses input briefly (ProcessOWInput). */
    if (g->mode == MODE_OW) {
        g->player.vehicle = g->player.vehicle_next;
        if (g->player.vehchgpause) {
            g->player.vehchgpause--;
            return;
        }
    }

    /* Read a held direction and try to start a step. */
    uint8_t dir = 0;
    if (in->held & BTN_UP)            dir = BTN_UP;
    else if (in->held & BTN_DOWN)     dir = BTN_DOWN;
    else if (in->held & BTN_LEFT)     dir = BTN_LEFT;
    else if (in->held & BTN_RIGHT)    dir = BTN_RIGHT;

    if (dir) {
        g->player.facing = dir;
        int dx, dy;
        dir_delta(dir, &dx, &dy);
        int tx = g->player.tile_x + dx;
        int ty = g->player.tile_y + dy;
        int go;
        if (g->mode == MODE_OW) {
            go = ow_start_move(g, tx & 0xFF, ty & 0xFF);
        } else {
            go = !MapObj_IsInPath(&g->objs, tx & 0x3F, ty & 0x3F) && can_move(g, tx, ty, g->player.vehicle);
            if (go) {
                room_step(g, tx & 0x3F, ty & 0x3F);
                step_start(g, tx & 0x3F, ty & 0x3F);
            }
        }
        if (go) {
            g->player.moving = 1;
            g->player.move_dir = dir;
            /* StartMapMove: 1 pixel a frame, or on the overworld vehicle / 2
             * (canoe 1, ship 2, airship 4). */
            uint8_t v = g->player.vehicle;
            g->player.move_speed = (g->mode == MODE_OW && v >= VEHICLE_CANOE) ? v / 2 : 1;
        }
    }

    /* joy_b counts B presses on the overworld (UpdateJoy) until the
     * overworld is re-entered. */
    if (g->mode == MODE_OW && (in->pressed & BTN_B)) g->joy_b++;

    /* A on the overworld (ProcessOWInput, when no direction is held): on
     * foot on the airship, FlyAirship; in the air, LandAirship. */
    if (g->mode == MODE_OW && !dir && (in->pressed & BTN_A) &&
        (g->player.vehicle == VEHICLE_AIRSHIP || g->player.vehicle == VEHICLE_FOOT)) {
        if (g->player.vehicle == VEHICLE_AIRSHIP) {
            airship_animate(g, 1);
        } else if (g->player.vehicle == VEHICLE_FOOT && g->save.airship_vis &&
                   g->player.tile_x == g->save.airship_x && g->player.tile_y == g->save.airship_y) {
            g->player.vehicle = g->player.vehicle_next = VEHICLE_AIRSHIP;
            g_music_track = MUSIC_AIRSHIP;
            airship_animate(g, 0);
        }
        return;
    }

    /* @ANotPressed: the 55th B press, in the ship with A and B held, starts
     * the minigame (once: joy_b goes on to 56). */
    if (g->mode == MODE_OW && !dir && g->joy_b == 55) {
        g->joy_b++;
        if (g->player.vehicle == VEHICLE_SHIP && (in->held & (BTN_A | BTN_B)) == (BTN_A | BTN_B)) {
            begin_fx(g, SCREENFX_PALCYC_OUT, FXNEXT_MINIGAME);
            return;
        }
    }

    /* A button: talk to the tile the party is facing (standard maps only). */
    if (in->pressed & BTN_A) {
        if (g->mode == MODE_SM) {
            int dx, dy;
            dir_delta(g->player.facing, &dx, &dy);
            int tx = (g->player.tile_x + dx) & 0x3F, ty = (g->player.tile_y + dy) & 0x3F;
            int slot = MapObj_FindTalkTarget(&g->objs, tx, ty, g->player.facing);
            g->talk.battle = g->talk.normal_tele = -1;
            g->talk.reentermap = 0;
            g->talk.sfx = 0;
            if (slot >= 0) {
                Talk_ToObject(&g->save, &g->objs, slot, &g->talk);
                start_dialogue(g, g->talk.dialogue);
            } else {
                start_dialogue(g, talk_to_tile(g, tx, ty));
            }
        }
    }
}

/** GS_DIALOGUE: dismiss the box on A/B. */
static void update_dialogue(GameContext* g, const InputState* in) {
    /* the jingle over: back to the map's music */
    if (g->dlgsfx && g_music_track == 0x81) {
        g_music_track = g->dlgmusic_backup;
        g->dlgsfx = 0;
    }
    if (in->pressed & (BTN_A | BTN_B)) g->dlg_closing = 1;
    if (!g->dlg_closing || g->dlgsfx) return;   /* @WaitForSfx: closes once it's over */
    g->dlg_closing = 0;
    Sfx_DialogueClose();
    g->state = GS_WALK;

    /* TalkBattle / TalkNormTeleport set the tile properties, so the battle or
     * teleport starts as soon as the box closes (StandardMapLoop @TeleOrBattle). */
    if (g->talk.battle >= 0) {
        g->pending_formation = g->talk.battle;
        g->pending_backdrop_tile = OWMap_GetTileID(g->ow_return_x, g->ow_return_y);
        begin_fx(g, SCREENFX_BATTLE, FXNEXT_BATTLE);
    } else if (g->talk.normal_tele >= 0) {
        g->pending_normal_id = (uint8_t)g->talk.normal_tele;
        begin_fx(g, SCREENFX_WIPE_CLOSE, FXNEXT_NORMAL_TELE);
    } else if (g->talk.reentermap) {
        /* ReenterStandardMap: the map redrawn (the promoted classes' mapman)
         * and the palette cycled in. */
        begin_fx(g, SCREENFX_PALCYC_IN, FXNEXT_WALK);
    }
    g->talk.battle = g->talk.normal_tele = -1;
    g->talk.reentermap = 0;
}

/** Load the standard map for entrance teleport `g->pending_teleport_id`. */
static void enter_map(GameContext* g) {
    int map, x, y, ts;
    SM_LookupEntrance(g->pending_teleport_id, &map, &x, &y, &ts);
    place_on_map(g, map, ts, x, y);
}

/** GS_FX: run the transition; when it finishes, do what it was leading to. */
static void update_fx(GameContext* g) {
    if (!ScreenFx_Step(&g->fx)) return;

    switch (g->fx_next) {
    case FXNEXT_ENTER_MAP:            /* EnterStandardMap ends with ScreenWipe_Open */
        enter_map(g);
        begin_fx(g, SCREENFX_WIPE_OPEN, FXNEXT_WALK);
        break;
    case FXNEXT_EXIT_MAP: {           /* DoOverworld: ScreenWipe_Open */
        int x, y;
        SM_LookupExit(g->pending_exit_id, &x, &y);
        place_on_overworld(g, x, y);
        begin_fx(g, SCREENFX_WIPE_OPEN, FXNEXT_WALK);
        break;
    }
    case FXNEXT_NORMAL_TELE:          /* DoStandardMap on the new floor */
        normal_teleport(g);
        begin_fx(g, SCREENFX_WIPE_OPEN, FXNEXT_WALK);
        break;
    case FXNEXT_WARP:
        warp_back(g);
        begin_fx(g, SCREENFX_WIPE_OPEN, FXNEXT_WALK);
        break;
    case FXNEXT_MENU: {
        /* GetSMTilePropNow / GetOWTile: the item routines look at the
         * tile the party is standing on. */
        MenuEnv env = { 0, 0, NULL };
        const OWMetatile* m;
        if (g->mode == MODE_SM) {
            m = SM_GetMetatile(SM_GetTileID(g->player.tile_x, g->player.tile_y));
            env.on_sm = 1;
            env.objs = &g->objs;
        } else {
            m = OWMap_GetMetatile(OWMap_GetTileID(g->player.tile_x, g->player.tile_y));
        }
        env.tileprop_now = m ? m->prop0 : 0;
        g_music_track = MUSIC_MENU;      /* EnterMainMenu */
        Menu_Open(&g->save, &env);
        g->state = GS_MENU;
        break;
    }
    case FXNEXT_BATTLE:
        Battle_SetRespondRate(g->respond_rate);
        Battle_Start(&g->save, g->pending_formation, g->pending_backdrop_tile);
        g->state = GS_BATTLE;
        break;
    case FXNEXT_BRIDGE:               /* LoadBridgeSceneGFX, EnterBridgeScene */
        g_music_track = MUSIC_BRIDGE;
        Story_OpenBridge();
        g->state = GS_STORY;
        break;
    case FXNEXT_OW_PALCYC_IN:         /* EnterOW_PalCyc */
        g->fx_story = 0;
        g->fx_minigame = 0;
        begin_fx(g, SCREENFX_PALCYC_IN, FXNEXT_WALK);
        break;
    case FXNEXT_MINIGAME:             /* LoadBridgeSceneGFX, EnterMiniGame */
        g_music_track = MUSIC_BRIDGE;
        Minigame_Open(g->framecounter);
        g->state = GS_MINIGAME;
        break;
    case FXNEXT_MINIMAP:              /* EnterMinimap */
        g_music_track = MUSIC_PRELUDE;
        Minimap_Open(g->player.tile_x, g->player.tile_y, g->framecounter);
        g->state = GS_MINIMAP;
        break;
    case FXNEXT_LINEUP:
        g_music_track = MUSIC_LINEUP;
        Lineup_Open(&g->save);
        g->state = GS_LINEUP;
        break;
    case FXNEXT_SHOP:
        g_music_track = MUSIC_SHOP;
        Shop_Open(&g->save, g->pending_shop_id);
        g->state = GS_SHOP;
        break;
    default:
        g->state = GS_WALK;
        if (g->mode == MODE_OW) g->joy_b = 0;   /* PrepOverworld */
        break;
    }
}

/** Port of LoadMapPalettes (bank_0F). */
static void load_map_palettes(const GameContext* g) {
    uint8_t pal[32];
    map_palettes(g, pal);
    Ppu_LoadPalettes(pal);
}

/** Tint the viewport: green = walkable, red = blocked.  Debug aid (F3). */
static void draw_walk_overlay(const GameContext* g, int view_x, int view_y) {
    for (int row = -1; row <= 15; row++) {
        for (int col = -1; col <= 16; col++) {
            int walk = can_move(g, view_x + col, view_y + row, g->player.vehicle);
            Ppu_OverlayRect(col * 16 - g->player.sub_x, row * 16 - g->player.sub_y, 16, 16,
                            walk ? PPU_OVERLAY_GREEN : PPU_OVERLAY_RED);
        }
    }
}

void Game_Update(GameContext* g, const InputState* input) {
    if (Sfx_ErrorFrame()) return;     /* PlaySFX_Error's own 16-frame loop */
    g->framecounter++;
    /* CallMusicPlay: a frame of music.  The screen wipes, the battle
     * transition and DoAltarEffect don't call it. */
    if (!(g->state == GS_FX && g->fx.kind != SCREENFX_PALCYC_OUT && g->fx.kind != SCREENFX_PALCYC_IN))
        Music_Play();
    /* DrawOWSprites @OnFoot: a rising airship (airship_vis < $1F) is drawn
     * every other 2 frames while its counter runs up. */
    if (g->mode == MODE_OW && g->player.vehicle == VEHICLE_FOOT &&
        (g->state == GS_WALK || g->state == GS_DIALOGUE)) {
        uint8_t a = g->save.airship_vis;
        g->airship_shown = a && (a >= 0x1F || !((a >> 1) & 1));
        if (a && a < 0x1F) g->save.airship_vis++;
    }
    switch (g->state) {
    case GS_WALK:
        update_walk(g, input);
        /* EnterOverworldLoop: the waves or the engine, each frame. */
        if (g->mode == MODE_OW && g->state == GS_WALK)
            Sfx_VehicleFrame(g->player.vehicle, g->framecounter);
        /* Townspeople move while the party walks (UpdateAndDrawMapObjects). */
        if (g->mode == MODE_SM && g->state == GS_WALK) {
            int px = g->player.tile_x, py = g->player.tile_y;
            if (g->player.moving) {                 /* the tile being moved to */
                int dx, dy;
                dir_delta(g->player.move_dir, &dx, &dy);
                px = (px + dx) & 0x3F;
                py = (py + dy) & 0x3F;
            }
            MapObj_Update(&g->objs, g->framecounter, px, py, g->inroom & 1);
        }
        break;
    case GS_DIALOGUE:
        update_dialogue(g, input);
        break;
    case GS_FX:
        update_fx(g);
        break;
    case GS_MENU: {
        MenuResult r = Menu_Update(&g->save, input, &g->framecounter);
        if (Menu_TakeSaveRequest()) Game_Save(g);    /* MenuSaveConfirm's SaveGame */
        if (r == MENU_CLOSED) {                      /* back to the map: CyclePalettes in */
            begin_fx(g, SCREENFX_PALCYC_IN, FXNEXT_WALK);
        } else if (r == MENU_WARP || r == MENU_EXIT) {
            /* UseMagic_WARP returns into the last normal teleport, which
             * reloads the floor it left; with none to return to (fewer than
             * 16 bytes on the stack: the first floor, or the overworld) it
             * does what EXIT does, JMP DoOverworld: back to the overworld
             * tile the party entered from.  Either way the map wipes open. */
            if (g->mode == MODE_SM) {
                if (r == MENU_EXIT) g->warp_depth = 0;
                warp_back(g);
            }
            begin_fx(g, SCREENFX_WIPE_OPEN, FXNEXT_WALK);
        }
        break;
    }
    case GS_BATTLE: {
        BattleResult r = Battle_Update(&g->save, input);
        if (r == BATTLE_LOST) {       /* BattleFadeOutAndRestartGame: back to GameStart */
            Game_Boot(g);
        } else if (r == BATTLE_ENDGAME) {   /* @VictoryLoop: LoadEpilogueSceneGFX, EnterEndingScene */
            g_music_followup = 0;
            g_music_track = MUSIC_ENDING;
            Story_OpenEnding();
            g->state = GS_STORY;
        } else if (r != BATTLE_CONTINUE)    /* EnterOW_PalCyc / ReenterStandardMap */
            begin_fx(g, SCREENFX_PALCYC_IN, FXNEXT_WALK);
        break;
    }
    case GS_INTRO:
        if (Intro_Update(input)) {
            g->respond_rate = 0;          /* IntroStory_Joy / EnterIntroStory reset it */
            Game_Boot(g);
        }
        break;
    case GS_AIRSHIP:
        update_airship(g);
        break;
    case GS_MINIGAME: {               /* MinigameReward if solved; CyclePalettes out */
        MinigameResult r = Minigame_Update(input);
        if (r != MINIGAME_PLAYING) {
            if (r == MINIGAME_SOLVED) Party_AddGold(&g->save, 100);
            ScreenFx_Start(&g->fx, SCREENFX_PALCYC_OUT, Minigame_Palette());
            g->fx_next = FXNEXT_OW_PALCYC_IN;
            g->fx_minigame = 1;
            g->state = GS_FX;
        }
        break;
    }
    case GS_MINIMAP:                  /* then EnterOW_PalCyc */
        if (Minimap_Update(input)) begin_fx(g, SCREENFX_PALCYC_IN, FXNEXT_WALK);
        break;
    case GS_STORY:                    /* then CyclePalettes (code 4: out) */
        if (Story_Update(&g->save, input)) {
            ScreenFx_Start(&g->fx, SCREENFX_PALCYC_OUT, Story_Palette());
            g->fx_next = FXNEXT_OW_PALCYC_IN;
            g->fx_story = 1;
            g->state = GS_FX;
        }
        break;
    case GS_LINEUP:                   /* EnterOW_PalCyc / ReenterStandardMap */
        if (Lineup_Update(&g->save, input)) begin_fx(g, SCREENFX_PALCYC_IN, FXNEXT_WALK);
        break;
    case GS_TITLE: {
        TitleChoice c = Title_Update(input, &g->respond_rate);
        if (c == TITLE_CONTINUE) Game_Start(g);      /* a bad save starts a new game */
        else if (c == TITLE_NEW_GAME) {                /* NewGamePartyGeneration */
            PtyGen_Open();
            g->state = GS_PARTYGEN;
        }
        break;
    }
    case GS_PARTYGEN:
        if (PtyGen_Update(input)) {
            uint8_t classes[4], names[4][4];
            PtyGen_Result(classes, names);
            Game_NewGame(g, classes, names);
        }
        break;
    case GS_SHOP: {
        int left = Shop_Update(&g->save, input);
        if (Shop_TakeSaveRequest()) Game_Save(g);    /* the inn's SaveGame */
        if (left) {
            /* ReenterStandardMap: DrawFullMap redraws the door closed.
             * (The caravan returns to the overworld: EnterOW_PalCyc.) */
            if (g->mode == MODE_SM) SMRender_SetDoorTile(g->door_x, g->door_y, -1);
            begin_fx(g, SCREENFX_PALCYC_IN, FXNEXT_WALK);
        }
        break;
    }
    }
}

/** DrawOWSprites: the party and the overworld objects, in the original's
 *  OAM order for each vehicle (earlier ones in front, so drawn last). */
static void draw_ow_sprites(const GameContext* g, int view_x, int view_y, int walk_frame) {
    const SaveData* s = &g->save;
    int sx = g->player.sub_x, sy = g->player.sub_y;
    uint8_t v = g->player.vehicle;
    /* inforest: on foot on a forest tile, the bottom half is hidden */
    int forest = v == VEHICLE_FOOT && OWMap_IsForest(g->player.tile_x & 0xFF, g->player.tile_y & 0xFF);
    #define BRIDGE_CANAL() do { \
        if (s->canal_vis) OWRender_DrawObject(OWOBJ_CANAL, s->canal_x, s->canal_y, view_x, view_y, sx, sy); \
        if (s->bridge_vis) OWRender_DrawObject(OWOBJ_BRIDGE, s->bridge_x, s->bridge_y, view_x, view_y, sx, sy); \
    } while (0)
    #define SHIP() do { if (s->ship_vis) \
        OWRender_DrawVehicle(VEHICLE_SHIP, s->ship_x, s->ship_y, view_x, view_y, sx, sy); } while (0)
    #define AIRSHIP() do { if (s->airship_vis) \
        OWRender_DrawVehicle(VEHICLE_AIRSHIP, s->airship_x, s->airship_y, view_x, view_y, sx, sy); } while (0)
    #define PLAYER() OWRender_DrawPlayer(v, g->player.facing, s->characters[0].class, \
        walk_frame ? 8 : 0, forest, g->framecounter)
    if (g->state == GS_AIRSHIP) {   /* AirshipTransitionFrame: no party */
        BRIDGE_CANAL(); SHIP();
        OWRender_DrawAirshipAt(g->airship_y, g->framecounter);
    } else switch (v) {
    case VEHICLE_SHIP:          /* bridge/canal over the ship: it sails under */
        AIRSHIP(); PLAYER(); BRIDGE_CANAL();
        break;
    case VEHICLE_CANOE:
        BRIDGE_CANAL(); AIRSHIP(); SHIP(); PLAYER();
        break;
    case VEHICLE_AIRSHIP:
        BRIDGE_CANAL(); SHIP(); PLAYER();
        break;
    default:                    /* on foot: the mapman, airship, ship, bridge/canal */
        BRIDGE_CANAL(); SHIP();
        if (g->airship_shown) AIRSHIP();
        PLAYER();
        break;
    }
    #undef BRIDGE_CANAL
    #undef SHIP
    #undef AIRSHIP
    #undef PLAYER
}

void Game_Draw(const GameContext* g) {
    if (g->state == GS_MENU) {
        Menu_Draw(&g->save);
    } else if (g->state == GS_BATTLE) {
        Battle_Draw(&g->save);
    } else if (g->state == GS_STORY || (g->state == GS_FX && g->fx_story)) {
        Story_Draw();
        if (g->state == GS_FX) ScreenFx_Apply(&g->fx);
    } else if (g->state == GS_MINIGAME || (g->state == GS_FX && g->fx_minigame)) {
        Minigame_Draw();
        if (g->state == GS_FX) ScreenFx_Apply(&g->fx);
    } else if (g->state == GS_MINIMAP) {
        Minimap_Draw();
    } else if (g->state == GS_LINEUP) {
        Lineup_Draw(&g->save);
    } else if (g->state == GS_INTRO) {
        Intro_Draw();
    } else if (g->state == GS_TITLE) {
        Title_Draw(g->respond_rate);
    } else if (g->state == GS_PARTYGEN) {
        PtyGen_Draw();
    } else if (g->state == GS_SHOP) {
        Shop_Draw(&g->save);
    } else {
        load_map_palettes(g);
        Ppu_Clear(0);
        int view_x = g->player.tile_x - PLAYER_SCREEN_TILE;
        int view_y = g->player.tile_y - PLAYER_SCREEN_TILE;

        if (g->mode == MODE_SM) {
            SMRender_Draw(view_x, view_y, g->player.sub_x, g->player.sub_y);
            if (g->state != GS_FX || ScreenFx_ShowsSprites(&g->fx))
                MapObj_Draw(&g->objs, g->framecounter, view_x * 16 + g->player.sub_x,
                            view_y * 16 + g->player.sub_y, g->inroom & 1);
        } else {
            OWRender_Draw(view_x, view_y, g->player.sub_x, g->player.sub_y);
        }

        /* Walk-frame animation: the NES toggles between the two mapman frames
         * at the 8-pixel midpoint of a step (move counter bit 3). */
        int walk_progress;
        switch (g->player.move_dir) {
        case BTN_LEFT:  walk_progress = -g->player.sub_x; break;
        case BTN_UP:    walk_progress = -g->player.sub_y; break;
        case BTN_DOWN:  walk_progress =  g->player.sub_y; break;
        default:        walk_progress =  g->player.sub_x; break; /* BTN_RIGHT */
        }
        int walk_frame = (g->player.moving && walk_progress >= 8) ? 1 : 0;

        /* Transitions hide sprites (OAM cleared / sprites disabled). */
        if (g->state == GS_FX && !ScreenFx_ShowsSprites(&g->fx)) {
            ScreenFx_Apply(&g->fx);
            return;
        }

        if (g->mode == MODE_OW) {
            draw_ow_sprites(g, view_x, view_y, walk_frame);
        } else {
            OWRender_DrawPlayer(VEHICLE_FOOT, g->player.facing, g->save.characters[0].class,
                                walk_frame ? 8 : 0, 0, g->framecounter);
        }

        if (g->state == GS_FX) ScreenFx_Apply(&g->fx);   /* altar effect */

        /* MapTileDamage: walking onto frost/lava flashes the screen
         * monochrome every other frame. */
        if (g->player.moving && g->step_damage && (g->framecounter & 1))
            for (int y = 0; y < PPU_HEIGHT; y++) Ppu_SetMonochrome(y, 0, PPU_WIDTH);

        if (g->show_walk_overlay) {
            draw_walk_overlay(g, view_x, view_y);
        }

        if (g->state == GS_DIALOGUE) {
            /* DrawDialogueBox: a 28x12-tile box below the top 16 pixels of
             * the map, text from tile (4, 4). */
            UI_DrawBox(2, 2, 28, 12);
            UI_DrawGlyphs(g->text_glyphs, g->text_count, 4, 4);
        }
    }
}

void Game_PowerOn(GameContext* g) {
    g_music_track = MUSIC_PRELUDE;    /* IntroTitlePrepare: the crystal theme */
    /* startintrocheck: only a cold boot shows the intro story. */
    g->state = GS_INTRO;
    Intro_Open();
}

void Game_Boot(GameContext* g) {
    g_music_track = MUSIC_PRELUDE;    /* IntroTitlePrepare */
    g_music_followup = 0;
    g->state = GS_TITLE;
    Title_Open();
}

/* GameStart @Begin: the party on the overworld, then DoOverworld's wipe.
 * The caller fills in g->save. */
static void begin_game(GameContext* g) {
    uint8_t rate = g->respond_rate;    /* zero page $FA: ClearZeroPage stops at $EF */
    memset(g, 0, sizeof(*g));
    g->respond_rate = rate;
    g->mode = MODE_OW;
    g->state = GS_WALK;
    player_init(g);
    SM_Init();
    g->step_battle = -1;
    begin_fx(g, SCREENFX_WIPE_OPEN, FXNEXT_WALK);
}

void Game_Init(GameContext* g) {
    begin_game(g);
    Party_Init(&g->save);
#if FF1_DEBUG_STATE
    Party_ApplyDebug(&g->save);
#endif
}

void Game_NewGame(GameContext* g, const uint8_t classes[4], const uint8_t names[4][4]) {
    begin_game(g);
    Party_NewGame(&g->save, classes, names);
}

static char g_save_path[512] = "ff1.sav";

void Game_SetSavePath(const char* path) {
    snprintf(g_save_path, sizeof(g_save_path), "%s", path);
}

int Game_Save(GameContext* g) {
    g_music_track = MUSIC_FANFARE_SAVED;   /* SaveGame */
    /* The overworld position is the party's, or (on a standard map) the
     * tile it entered the map from; ow_scroll puts the party 7 tiles in. */
    int x = (g->mode == MODE_OW) ? g->player.tile_x : g->ow_return_x;
    int y = (g->mode == MODE_OW) ? g->player.tile_y : g->ow_return_y;
    uint8_t vehicle = (g->mode == MODE_OW) ? g->player.vehicle : 1;
    SaveData sram;
    Save_Game(&g->save, (uint8_t)(x - PLAYER_SCREEN_TILE), (uint8_t)(y - PLAYER_SCREEN_TILE),
              vehicle, &sram);
    return Save_WriteFile(g_save_path, &g->save, &sram);
}

int Game_Start(GameContext* g) {
    SaveData saved;
    if (!Save_ReadFile(g_save_path, &saved)) {
        Game_Init(g);
        return 0;
    }
    /* Continue: sram copied to unsram, then @Begin takes the overworld
     * scroll and vehicle from it and flows into DoOverworld. */
    Game_Init(g);
    g->save = saved;
    g->player.tile_x = (uint8_t)(saved.ow_scroll_x + PLAYER_SCREEN_TILE);
    g->player.tile_y = (uint8_t)(saved.ow_scroll_y + PLAYER_SCREEN_TILE);
    g->player.vehicle = g->player.vehicle_next = saved.vehicle;
    g->dlgmusic_backup = g_music_track = map_music(g);
    return 1;
}

void Game_EnterTeleport(GameContext* g, uint8_t entrance_id) {
    /* Stand on a tile of that entrance first, as stepping on it would: the
     * battles inside take their backdrop from it (ow_return). */
    for (int y = 0; g->mode == MODE_OW && y < 256; y++)
        for (int x = 0; x < 256; x++) {
            const OWMetatile* m = OWMap_GetMetatile(OWMap_GetTileID((uint8_t)x, (uint8_t)y));
            if (m && (m->prop1 & 0x80) && (m->prop1 & 0x3F) == entrance_id) {
                g->player.tile_x = (uint8_t)x;
                g->player.tile_y = (uint8_t)y;
                y = 256;
                break;
            }
        }
    transition_begin_entrance(g, entrance_id);
}

void Game_Shutdown(void) {
    SM_Shutdown();
}
