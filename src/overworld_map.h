/**
 * @file overworld_map.h
 * @brief Overworld map data structures and decompression.
 *
 * The overworld is a 256×256 tile grid stored as RLE-compressed data
 * in bin/bank_01_data.bin.  Each tile is 2 bytes: terrain flags + encounter.
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>

/** Overworld dimensions (256×256 tiles). */
#define OW_MAP_WIDTH   256
#define OW_MAP_HEIGHT  256
#define OW_MAP_SIZE    (OW_MAP_WIDTH * OW_MAP_HEIGHT)

/**
 * Number of distinct overworld tile IDs (0..127).  Each decompressed map
 * byte is one of these IDs and indexes into the tileset (lut_OWTileset).
 */
#define OW_TILE_COUNT  128

/**
 * A single overworld map tile.
 * Byte 0: terrain/vehicle flags  (bits: [SSdf ascw])
 * Byte 1: encounter/teleport data ($80=teleport, $40=fight, etc.)
 *
 * NOTE: these two bytes come from the tileset (tileset_prop), NOT directly
 * from the decompressed map.  The map stores only a tile ID (0..127).
 */
typedef struct {
    uint8_t terrain;     ///< Byte 0: vehicle/terrain flags (OWTP_*)
    uint8_t encounter;   ///< Byte 1: teleport/fight data
} OWTile;

/**
 * A 16×16 overworld "metatile": the graphics definition for one tile ID.
 * Each metatile is composed of four 8×8 CHR tiles (upper-left, upper-right,
 * down-left, down-right) plus an attribute byte that selects the palette.
 *
 * Source: lut_OWTileset (bank_00 @ $8000), parsed from map_data.h's
 * ow_tileset[] array.  Layout (from variables.inc):
 *   prop0/prop1  = tileset_prop  (2 bytes per tile ID)
 *   ul           = tsa_ul   (+$100)
 *   ur           = tsa_ur   (+$180)
 *   dl           = tsa_dl   (+$200)
 *   dr           = tsa_dr   (+$280)
 *   attr         = tsa_attr (+$300)
 */
typedef struct {
    uint8_t prop0;   ///< Terrain flags [SSdf ascw] (OWTP_*)
    uint8_t prop1;   ///< Encounter/teleport data
    uint8_t ul;      ///< CHR tile index: upper-left  (8×8)
    uint8_t ur;      ///< CHR tile index: upper-right
    uint8_t dl;      ///< CHR tile index: down-left
    uint8_t dr;      ///< CHR tile index: down-right
    uint8_t attr;    ///< Attribute byte: 2-bit palette index replicated 4×
} OWMetatile;

/** Initialize the overworld map system.
 *  Reads and decompresses bin/bank_01_data.bin into memory.
 *  @return 0 on success, -1 on error. */
int OWMap_Init(void);

/** Shut down and free the decompressed map memory. */
void OWMap_Shutdown(void);

/** Get a pointer to the tile at (col, row).
 *  @return Pointer to tile, or NULL if out of bounds. */
const OWTile* OWMap_GetTile(int col, int row);

/** Get the terrain byte for a tile. */
uint8_t OWMap_GetTerrain(int col, int row);

/** Get the encounter byte for a tile. */
uint8_t OWMap_GetEncounter(int col, int row);

/** Check if a tile blocks movement for a given vehicle type.
 *  @param vehicle  1=walking, 2=canoe, 4=ship, 8=airship
 *  @return true if the tile can be traversed. */
bool OWMap_CanMove(int col, int row, uint8_t vehicle);

/** Check if a tile has a forest. */
bool OWMap_IsForest(int col, int row);

/** Check if a tile is a dockable shoreline. */
bool OWMap_IsDock(int col, int row);

/** Get the special type (upper 2 bits of terrain byte).
 *  @return 0=none, 1=chime, 2=caravan, 3=floater (airship). */
int OWMap_GetSpecial(int col, int row);

/** Check if a tile has a teleport.
 *  @param teleport_id  Output: teleport index (low 7 bits of encounter byte).
 *  @return true if a teleport exists at this tile. */
bool OWMap_IsTeleport(int col, int row, uint8_t* teleport_id);

/** Check if a tile has an encounter.
 *  @return 0=none, 1=normal, 2=ocean, 3=river. */
int OWMap_GetEncounterType(int col, int row);

/** Get the raw tile ID (0..127) stored in the decompressed map at (col, row). */
uint8_t OWMap_GetTileID(int col, int row);

/** Get the metatile definition for a tile ID (0..127).
 *  @return Pointer to the metatile, or NULL if tile_id is out of range. */
const OWMetatile* OWMap_GetMetatile(uint8_t tile_id);

/** Copy the overworld palettes (load_map_pal from the OW tileset: 4 BG +
 *  4 sprite palettes) into out[32], as LoadMapPalettes copies them to cur_pal. */
void OWMap_GetMapPalettes(uint8_t out[32]);