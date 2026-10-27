#pragma once
#include <stdint.h>
#include "overworld_map.h"   /* reuses OWMetatile (same 128-tile metatile layout) */

/*
 * Standard-map metatile prop0 teleport-type bits (top 2 bits; see Constants.inc
 * TP_TELE_*).  Kept here (rather than constants.h) to avoid a BTN_START name
 * clash with input.h.
 */
#define SM_TP_TELE_MASK  0xC0
#define SM_TP_TELE_EXIT  0xC0   /* exit teleport:  standard map -> overworld */
#define SM_TP_TELE_NORM  0x80   /* normal teleport: standard map -> standard map */
#define SM_TP_TELE_WARP  0x40   /* warp teleport (return to previous floor) */
#define SM_TP_TELE_NONE  0x00

/* Standard-map metatile prop0 special-property bits (for A-button tile talk). */
#define SM_TP_SPEC_MASK     0x1E   /* special-property bits (bits 4-1) */
#define SM_TP_SPEC_DOOR      0x02  /* door into a room */
#define SM_TP_SPEC_LOCKED    0x04  /* locked door (Mystic Key) */
#define SM_TP_SPEC_CLOSEROOM 0x06  /* just outside a room's door */
#define SM_TP_SPEC_TREASURE 0x08   /* treasure chest */
#define SM_TP_SPEC_BATTLE    0x0A  /* battle tile (random or fixed formation) */
#define SM_TP_SPEC_DAMAGE    0x0C  /* frost / lava: 1 HP per step */
#define SM_TP_SPEC_CROWN     0x0E  /* passable with the Crown */
#define SM_TP_SPEC_CUBE      0x10  /* passable with the Cube */
#define SM_TP_SPEC_4ORBS     0x12  /* passable with all four orbs lit */
#define SM_TP_SPEC_EARTHORB  0x18  /* the four altars: light their orb */
#define SM_TP_SPEC_FIREORB   0x1A
#define SM_TP_SPEC_WATERORB  0x1C
#define SM_TP_SPEC_AIRORB    0x1E
#define SM_TP_NOTEXT_MASK   0xC2   /* any bit set forces "Nothing Here" */

/*
 * Standard-map (town/dungeon) data.
 *
 * Maps are 64x64 tiles, RLE-compressed, starting from a per-map pointer in
 * bank_04 (lut_SMPtrTbl @ $8000); the data may span banks 04/05/06.
 * Tilesets (8 of them) and per-map palettes live in bank_00.
 */
#define SM_MAP_WIDTH   64
#define SM_MAP_HEIGHT  64
#define SM_MAP_SIZE    (SM_MAP_WIDTH * SM_MAP_HEIGHT)

/** Initialize SM data (loads bank_00 + bank_04/05/06).  @return 0 or -1. */
int SM_Init(void);
void SM_Shutdown(void);

/** Decompress map `map_id` into the internal 64x64 grid.  @return 0 or -1. */
int SM_LoadMap(int map_id);

/** Parse tileset `ts` into the internal 128-metatile table.  @return 0 or -1. */
int SM_LoadTileset(int ts);

/** Copy map `map_id`'s palettes (4 BG + 4 sprite, from lut_SMPalettes'
 *  48-byte block) into out[32], as LoadMapPalettes copies them to cur_pal. */
void SM_GetMapPalettes(int map_id, uint8_t out[32]);

/** Copy map `map_id`'s in-room BG palette (bytes $20-$2F of the block,
 *  inroom_pal), used while the party is inside a room. */
void SM_GetRoomPalette(int map_id, uint8_t out[16]);

/** Raw tile ID (0..127) at (x, y).  Wraps. */
uint8_t SM_GetTileID(int x, int y);

/** Metatile definition for a tile ID. */
const OWMetatile* SM_GetMetatile(uint8_t tile_id);

/** True if the tile at (x, y) is walkable on foot. */
int SM_CanWalk(int x, int y);

/** Resolve an overworld entrance teleport ID to (map, x, y, tileset). */
void SM_LookupEntrance(uint8_t teleport_id, int* map, int* x, int* y, int* tileset);

/** Resolve a normal (standard map -> standard map) teleport ID to
 *  (map, x, y, tileset) via lut_NormTele_X/Y/Map and lut_Tilesets. */
void SM_LookupNormal(uint8_t teleport_id, int* map, int* x, int* y, int* tileset);

/** Resolve a standard-map exit teleport ID to overworld (x, y). */
void SM_LookupExit(uint8_t teleport_id, int* x, int* y);

/** Current loaded map id and tileset (after SM_LoadMap/SM_LoadTileset). */
int SM_GetMapId(void);
int SM_GetTileset(void);
