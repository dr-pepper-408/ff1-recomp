/**
 * @file overworld_map.c
 * @brief Decompress the FF1 overworld map (256×256 terrain grid).
 *
 * Data layout (bank_01, $8000-$BFFF; generated into rom_banks.h as
 * rom_ow_bank from bin/bank_01_data.bin + the .BYTE rows in bank_01.asm):
 *
 *   - A 256-entry pointer table (256 x uint16_t little-endian) at $8000
 *     (lut_OWPtrTbl).  Each pointer is a ROM address in $8200..$BEA6 that
 *     locates the RLE stream for that map row.
 *
 *   - The RLE rows themselves, $8200 onward.
 *
 * RLE format (ported from the 6502 MinimapDecompress routine):
 *     code = next byte
 *     if code == 0xFF:        end of row
 *     else if code & 0x80:    run  — low 7 bits = value, next byte = count
 *     else:                   literal single terrain byte
 *
 * Each row pointer is resolved against the ROM base 0x8000.
 */

#include "overworld_map.h"
#include "../data/generated/rom_banks.h"
#include "../data/generated/constants.h"
#include "../data/generated/map_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OW_ROM_BASE      0x8000u   // bank_01 base
#define OW_ROM_SIZE      0x4000u   // 16 KB bank window
#define OW_PTR_TABLE_SZ  512u      // 256 × uint16_t

/** Decompressed 256×256 grid of tile IDs (0..127). */
static uint8_t g_ow_terrain[OW_MAP_SIZE];

/** Parsed overworld tileset: 128 metatiles (from ow_tileset[] / lut_OWTileset). */
static OWMetatile g_metatiles[OW_TILE_COUNT];


/**
 * Parses the 1024-byte ow_tileset[] (lut_OWTileset) into the g_metatiles[]
 * struct array and extracts the 4 background palettes from load_map_pal.
 *
 * Layout (see variables.inc):
 *   tileset_prop  = [0..255]   (128 × 2 bytes: prop0, prop1)
 *   tsa_ul        = [256..383]
 *   tsa_ur        = [384..511]
 *   tsa_dl        = [512..639]
 *   tsa_dr        = [640..767]
 *   tsa_attr      = [768..895]
 *   load_map_pal  = [896..943] (48 bytes; first 16 = 4 BG palettes)
 */
static void parse_tileset(void) {
    for (int t = 0; t < OW_TILE_COUNT; t++) {
        g_metatiles[t].prop0 = ow_tileset[2 * t];
        g_metatiles[t].prop1 = ow_tileset[2 * t + 1];
        g_metatiles[t].ul    = ow_tileset[256 + t];
        g_metatiles[t].ur    = ow_tileset[384 + t];
        g_metatiles[t].dl    = ow_tileset[512 + t];
        g_metatiles[t].dr    = ow_tileset[640 + t];
        g_metatiles[t].attr  = ow_tileset[768 + t];
    }
}

/**
 * Decompresses one row of terrain from a pointer into the combined bank.
 */
static void decompress_row(const uint8_t* bank, size_t bank_size, size_t pos,
                           uint8_t* out_row) {
    int col = 0;
    while (col < OW_MAP_WIDTH && pos < bank_size) {
        uint8_t code = bank[pos++];
        if (code == 0xFF) break;
        if (code & 0x80) {
            uint8_t value = code & 0x7F;
            if (pos >= bank_size) break;
            uint8_t count = bank[pos++];
            int n = (count == 0) ? OW_MAP_WIDTH : count;  /* 0 means a full 256-tile run */
            for (int k = 0; k < n && col < OW_MAP_WIDTH; k++)
                out_row[col++] = value;
        } else {
            out_row[col++] = code;
        }
    }
    while (col < OW_MAP_WIDTH) out_row[col++] = 0;
}

/**
 * Initializes the overworld map by decompressing bin/bank_01_data.bin
 * + data/overworld_rle.bin using the row pointer table.
 * @return 0 on success, -1 on error.
 */
int OWMap_Init(void) {
    memset(g_ow_terrain, 0, sizeof(g_ow_terrain));

    // ---- Bank_01 $8000-$BFFF (row pointer table + RLE rows) ----
    const uint8_t* bank = rom_ow_bank;
    size_t bank_size = sizeof(rom_ow_bank);

    // ---- Read 256-entry pointer table from the first 512 bytes ----
    uint16_t ptrs[OW_MAP_HEIGHT];
    for (int i = 0; i < OW_MAP_HEIGHT; i++)
        ptrs[i] = (uint16_t)(bank[i * 2] | (bank[i * 2 + 1] << 8));

    // ---- Decompress each row ----
    int ok_rows = 0;
    for (int row = 0; row < OW_MAP_HEIGHT; row++) {
        uint32_t addr = ptrs[row];
        if (addr >= OW_ROM_BASE && addr < OW_ROM_BASE + bank_size) {
            size_t pos = addr - OW_ROM_BASE;
            decompress_row(bank, bank_size, pos, g_ow_terrain + row * OW_MAP_WIDTH);
            ok_rows++;
        }
    }

    printf("OWMap_Init: decompressed %d/256 rows\n", ok_rows);

    // Parse the overworld tileset (metatiles + palettes) used for rendering.
    parse_tileset();

    return ok_rows > 0 ? 0 : -1;
}

void OWMap_Shutdown(void) {
    // static grid, nothing to free
}

uint8_t OWMap_GetTileID(int col, int row) {
    if (col < 0 || col >= OW_MAP_WIDTH || row < 0 || row >= OW_MAP_HEIGHT)
        return 0;
    return g_ow_terrain[row * OW_MAP_WIDTH + col];
}

uint8_t OWMap_GetTerrain(int col, int row) {
    /* The decompressed map byte is a tile ID (0..127).  The actual terrain
     * flags live in the tileset (metatile.prop0), so expose those here. */
    const OWMetatile* m = OWMap_GetMetatile(OWMap_GetTileID(col, row));
    return m ? m->prop0 : 0;
}

const OWTile* OWMap_GetTile(int col, int row) {
    static OWTile t;
    const OWMetatile* m = OWMap_GetMetatile(OWMap_GetTileID(col, row));
    if (!m)
        return NULL;
    t.terrain   = m->prop0;
    t.encounter = m->prop1;
    return &t;
}

uint8_t OWMap_GetEncounter(int col, int row) {
    const OWMetatile* m = OWMap_GetMetatile(OWMap_GetTileID(col, row));
    return m ? m->prop1 : 0;
}

const OWMetatile* OWMap_GetMetatile(uint8_t tile_id) {
    if (tile_id >= OW_TILE_COUNT)
        return NULL;
    return &g_metatiles[tile_id];
}

void OWMap_GetMapPalettes(uint8_t out[32]) {
    memcpy(out, &ow_tileset[896], 32);
}

bool OWMap_CanMove(int col, int row, uint8_t vehicle) {
    uint8_t t = OWMap_GetTerrain(col, row);
    switch (vehicle) {
    case 1: return !(t & 0x01);   // walking: bit 0
    case 2: return !(t & 0x02);   // canoe:   bit 1
    case 4: return !(t & 0x04);   // ship:    bit 2
    case 8: return !(t & 0x08);   // airship: bit 3
    default: return false;
    }
}

bool OWMap_IsForest(int col, int row) {
    return (OWMap_GetTerrain(col, row) & OWTP_FOREST) != 0;
}

bool OWMap_IsDock(int col, int row) {
    return (OWMap_GetTerrain(col, row) & OWTP_DOCKSHIP) != 0;
}

int OWMap_GetSpecial(int col, int row) {
    uint8_t t = OWMap_GetTerrain(col, row) & OWTP_SPEC_MASK;
    if (t == OWTP_SPEC_CHIME)   return 1;
    if (t == OWTP_SPEC_CARAVAN) return 2;
    if (t == OWTP_SPEC_FLOATER) return 3;
    return 0;
}

bool OWMap_IsTeleport(int col, int row, uint8_t* id) {
    uint8_t e = OWMap_GetEncounter(col, row);
    if (e & 0x80) {
        if (id) *id = e & 0x7F;
        return true;
    }
    return false;
}

int OWMap_GetEncounterType(int col, int row) {
    uint8_t e = OWMap_GetEncounter(col, row);
    if (e & 0x80)      return 0;   // teleport, not a battle
    if (e == 0x42)     return 2;   // ocean encounter
    if (e == 0x41 || e == 0x43) return 3;  // river encounter
    if (e & 0x40)      return 1;   // normal battle
    return 0;
}