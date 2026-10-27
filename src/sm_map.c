/**
 * @file sm_map.c
 * @brief Standard-map (town/dungeon) data: decompression, tileset, palettes.
 */

#include "sm_map.h"
#include "../data/generated/rom_banks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ROM bank addresses (each 16KB bank mapped at $8000-$BFFF). */
#define BANK_SIZE 0x4000u
#define ROM_BASE  0x8000u

/* bank_00 offsets for SM info (ROM addr - $8000). */
#define SM_TSA_OFF    (0x9000u - 0x8000u)   /* 8 x 512 bytes */
#define SM_PROP_OFF   (0x8800u - 0x8000u)   /* 8 x 256 bytes */
#define SM_ATTR_OFF   (0x8400u - 0x8000u)   /* 8 x 128 bytes */
#define SM_PAL_OFF    (0xA000u - 0x8000u)   /* N x 48 bytes */
#define TELE_X_OFF    (0xAC00u - 0x8000u)
#define TELE_Y_OFF    (0xAC20u - 0x8000u)
#define TELE_MAP_OFF  (0xAC40u - 0x8000u)
#define TELE_TS_OFF   (0xACC0u - 0x8000u)
#define EXIT_X_OFF    (0xAC60u - 0x8000u)   /* lut_ExitTele_X */
#define EXIT_Y_OFF    (0xAC70u - 0x8000u)   /* lut_ExitTele_Y */
#define NORM_X_OFF    (0xAD00u - 0x8000u)   /* lut_NormTele_X */
#define NORM_Y_OFF    (0xAD40u - 0x8000u)   /* lut_NormTele_Y */
#define NORM_MAP_OFF  (0xAD80u - 0x8000u)   /* lut_NormTele_Map */

static const uint8_t* g_bank00 = NULL;    /* SM info bank */
static const uint8_t* g_bank_maps = NULL; /* bank_04+05+06 concatenated */
static size_t   g_bank_maps_size = 0;

static uint8_t  g_grid[SM_MAP_SIZE];      /* decompressed 64x64 map */
static OWMetatile g_metatiles[OW_TILE_COUNT];
static int g_map_id = -1;
static int g_tileset = -1;

int SM_Init(void) {
    g_bank00 = rom_bank_00;
    g_bank_maps = rom_sm_maps;
    g_bank_maps_size = sizeof(rom_sm_maps);
    return 0;
}

void SM_Shutdown(void) {
    g_bank00 = NULL;
    g_bank_maps = NULL;
}

int SM_LoadMap(int map_id) {
    if (!g_bank_maps) return -1;
    if (map_id < 0 || (size_t)(map_id * 2 + 1) >= g_bank_maps_size) return -1;

    uint16_t ptr = (uint16_t)(g_bank_maps[map_id * 2] | (g_bank_maps[map_id * 2 + 1] << 8));

    /* The SM pointer is NOT an absolute ROM address.  From LoadStandardMap:
     * top 2 bits = bank offset (bank $04 + offset), low 14 bits = offset within
     * that bank (address = $8000 | low14).  So the raw 16-bit value is the
     * offset directly into the concatenated bank_04+05+06 buffer. */
    size_t pos = (size_t)ptr;
    if (pos >= g_bank_maps_size) return -1;
    int idx = 0;
    while (idx < SM_MAP_SIZE) {
        uint8_t c = g_bank_maps[pos++];
        if (c & 0x80) {
            if (c == 0xFF) break;
            uint8_t v = c & 0x7F;
            uint8_t run = g_bank_maps[pos++];
            int n = (run == 0) ? 256 : run;   /* run length 0 means 256 tiles */
            for (int k = 0; k < n && idx < SM_MAP_SIZE; k++)
                g_grid[idx++] = v;
        } else {
            g_grid[idx++] = c;
        }
    }
    while (idx < SM_MAP_SIZE) g_grid[idx++] = 0;

    g_map_id = map_id;
    return 0;
}

int SM_LoadTileset(int ts) {
    if (!g_bank00 || ts < 0 || ts > 7) return -1;

    const uint8_t* prop = g_bank00 + SM_PROP_OFF + ts * 256;
    const uint8_t* tsa  = g_bank00 + SM_TSA_OFF  + ts * 512;
    const uint8_t* attr = g_bank00 + SM_ATTR_OFF + ts * 128;

    for (int t = 0; t < OW_TILE_COUNT; t++) {
        g_metatiles[t].prop0 = prop[2 * t];
        g_metatiles[t].prop1 = prop[2 * t + 1];
        g_metatiles[t].ul    = tsa[t];
        g_metatiles[t].ur    = tsa[128 + t];
        g_metatiles[t].dl    = tsa[256 + t];
        g_metatiles[t].dr    = tsa[384 + t];
        g_metatiles[t].attr  = attr[t];
    }

    g_tileset = ts;
    return 0;
}

void SM_GetRoomPalette(int map_id, uint8_t out[16]) {
    for (int i = 0; i < 16; i++)
        out[i] = g_bank00 ? g_bank00[SM_PAL_OFF + map_id * 48 + 32 + i] : 0x0F;
}

void SM_GetMapPalettes(int map_id, uint8_t out[32]) {
    for (int i = 0; i < 32; i++)
        out[i] = g_bank00 ? g_bank00[SM_PAL_OFF + map_id * 48 + i] : 0x0F;
}

uint8_t SM_GetTileID(int x, int y) {
    x = ((x % SM_MAP_WIDTH) + SM_MAP_WIDTH) % SM_MAP_WIDTH;
    y = ((y % SM_MAP_HEIGHT) + SM_MAP_HEIGHT) % SM_MAP_HEIGHT;
    return g_grid[y * SM_MAP_WIDTH + x];
}

const OWMetatile* SM_GetMetatile(uint8_t tile_id) {
    if (tile_id >= OW_TILE_COUNT) return NULL;
    return &g_metatiles[tile_id];
}

int SM_CanWalk(int x, int y) {
    const OWMetatile* m = SM_GetMetatile(SM_GetTileID(x, y));
    if (!m) return 0;

    uint8_t p0 = m->prop0;

    /* CanPlayerMoveSM (bank_0F) + lut_SMMoveJmpTbl, without the party's
     * items: a pure no-move tile has (p0 & 0x1F) == TP_NOMOVE (0x01);
     * otherwise the special bits pick a handler.  Treasure chests block;
     * locked doors and the crown/cube/4-orb tiles depend on items and are
     * decided by game.c (here they count as blocked). */
    if ((p0 & 0x1F) == 0x01) return 0;   /* TP_NOMOVE wall (no special bits) */

    switch (p0 & SM_TP_SPEC_MASK) {
    case 0x00:   /* SMMove_Norm       */ return 1;
    case 0x02:   /* SMMove_Door       */ return 1;
    case 0x04:   /* locked door       */ return 0;   /* needs the Mystic Key: see game.c */
    case 0x06:   /* SMMove_CloseRoom  */ return 1;
    case 0x08:   /* SMMove_Treasure   */ return 0;
    case 0x0A:   /* SMMove_Battle     */ return 1;
    case 0x0C:   /* SMMove_Dmg        */ return 1;
    case 0x0E:   /* SMMove_Crown      */
    case 0x10:   /* SMMove_Cube       */
    case 0x12:   /* SMMove_4Orbs      */ return 0;
    default:     /* SMMove_UseRod/UseLute, the four orb altars */
        return 1;
    }
}

void SM_LookupEntrance(uint8_t teleport_id, int* map, int* x, int* y, int* tileset) {
    *map = g_bank00 ? g_bank00[TELE_MAP_OFF + (teleport_id & 0x3F)] : 0;
    *x   = g_bank00 ? g_bank00[TELE_X_OFF + (teleport_id & 0x3F)] : 0;
    *y   = g_bank00 ? g_bank00[TELE_Y_OFF + (teleport_id & 0x3F)] : 0;
    *tileset = g_bank00 ? g_bank00[TELE_TS_OFF + *map] : 0;
}

void SM_LookupNormal(uint8_t teleport_id, int* map, int* x, int* y, int* tileset) {
    uint8_t id = teleport_id & 0x3F;
    *map = g_bank00 ? g_bank00[NORM_MAP_OFF + id] : 0;
    *x   = g_bank00 ? g_bank00[NORM_X_OFF + id] : 0;
    *y   = g_bank00 ? g_bank00[NORM_Y_OFF + id] : 0;
    *tileset = g_bank00 ? g_bank00[TELE_TS_OFF + *map] : 0;   /* lut_Tilesets */
}

void SM_LookupExit(uint8_t teleport_id, int* x, int* y) {
    *x = g_bank00 ? g_bank00[EXIT_X_OFF + (teleport_id & 0x0F)] : 0;
    *y = g_bank00 ? g_bank00[EXIT_Y_OFF + (teleport_id & 0x0F)] : 0;
}

int SM_GetMapId(void) { return g_map_id; }
int SM_GetTileset(void) { return g_tileset; }
