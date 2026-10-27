/**
 * @file render_sm.c
 * @brief Standard-map renderer (mirrors the overworld metatile renderer but
 *        uses the 64x64 map, the SM tileset, and bank_03 CHR).
 */

#include "render_sm.h"
#include "sm_map.h"
#include "ppu.h"

#define VIEWPORT_COLS 16
#define VIEWPORT_ROWS 15

static int g_door_x = -1, g_door_y = -1, g_door_tile = -1;

void SMRender_SetDoorTile(int x, int y, int tile) {
    g_door_x = x;
    g_door_y = y;
    g_door_tile = tile;
}

int SMRender_DoorTileAt(int x, int y) {
    return (g_door_tile >= 0 && x == g_door_x && y == g_door_y) ? g_door_tile : -1;
}

void SMRender_Draw(int scroll_x, int scroll_y, int sub_x, int sub_y) {
    /* Each standard-map tileset occupies 128 CHR tiles in bank_03, at offset
     * tileset*128 (LoadTilesetAndMenuCHR loads $8000 + tileset*$800). */
    int chr_off = SM_GetTileset() * 128;

    for (int row = -1; row <= VIEWPORT_ROWS; row++) {
        for (int col = -1; col <= VIEWPORT_COLS; col++) {
            int map_col = (scroll_x + col) & (SM_MAP_WIDTH - 1);    /* maps wrap */
            int map_row = (scroll_y + row) & (SM_MAP_HEIGHT - 1);

            int id = SM_GetTileID(map_col, map_row);
            if (g_door_tile >= 0 && map_col == g_door_x && map_row == g_door_y) id = g_door_tile;
            const OWMetatile* m = SM_GetMetatile((uint8_t)id);
            if (!m) continue;

            int sx = col * 16 - sub_x;
            int sy = row * 16 - sub_y;
            int pal = m->attr & 0x03;
            Ppu_DrawBgTile(CHR_BANK_03, chr_off + m->ul, sx,     sy,     pal);
            Ppu_DrawBgTile(CHR_BANK_03, chr_off + m->ur, sx + 8, sy,     pal);
            Ppu_DrawBgTile(CHR_BANK_03, chr_off + m->dl, sx,     sy + 8, pal);
            Ppu_DrawBgTile(CHR_BANK_03, chr_off + m->dr, sx + 8, sy + 8, pal);
        }
    }
}
