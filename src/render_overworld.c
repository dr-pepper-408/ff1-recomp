/**
 * @file render_overworld.c
 * @brief Overworld viewport renderer and player mapman sprite.
 *
 * Draws a 16x15 metatile viewport over the 256x256 overworld map (with one
 * extra row/column for sub-tile scrolling) from bank_02 CHR.
 */

#include "render_overworld.h"
#include "overworld_map.h"
#include "ppu.h"
#include "../data/generated/ow_objects.h"

void OWRender_Draw(int scroll_x, int scroll_y, int sub_x, int sub_y) {
    /* One extra column and row so the sub-tile scroll offset never exposes
     * an undrawn edge. */
    for (int row = -1; row <= VIEWPORT_ROWS; row++) {
        for (int col = -1; col <= VIEWPORT_COLS; col++) {
            int map_col = (scroll_x + col) & (OW_MAP_WIDTH - 1);    /* wraps */
            int map_row = (scroll_y + row) & (OW_MAP_HEIGHT - 1);

            const OWMetatile* m = OWMap_GetMetatile(OWMap_GetTileID(map_col, map_row));
            if (!m) continue;

            int sx = col * 16 - sub_x;
            int sy = row * 16 - sub_y;
            int pal = m->attr & 0x03;   /* attribute byte repeats the 2-bit palette */
            Ppu_DrawBgTile(CHR_BANK_02, m->ul, sx,     sy,     pal);
            Ppu_DrawBgTile(CHR_BANK_02, m->ur, sx + 8, sy,     pal);
            Ppu_DrawBgTile(CHR_BANK_02, m->dl, sx,     sy + 8, pal);
            Ppu_DrawBgTile(CHR_BANK_02, m->dr, sx + 8, sy + 8, pal);
        }
    }
}

/* Draw2x2Sprite from a [tile, attribute] x (UL, DL, UR, DR) table, tiles
 * + `additive`.  Sprite tiles $00-$0F are the lead class's mapman
 * (LoadPlayerMapmanCHR: bank_02 $9000 + class * $100); $10 and up are the
 * overworld objects at $1100 (LoadOWObjectCHR: bank_02 $9C00, bank tile
 * $1C0 + n).  `hide` leaves sprites out of the picture: HIDE_BOTTOM drops
 * DL and DR (the party in a forest), HIDE_ALL all four (the airship's
 * shadow on the frames it flickers off).  They are still logged
 * (Ppu_LogSprite) for the 3D view. */
enum { HIDE_NONE, HIDE_BOTTOM, HIDE_ALL };
static int s_party;             /* drawing the party's sprites (for the PPU's log) */

static void draw_2x2(const uint8_t* tbl, int additive, int mapman_class, int px, int py,
                     int hide) {
    Ppu_SpriteGroup(s_party);
    for (int t = 0; t < 4; t++) {                      /* UL, DL, UR, DR */
        int tile = tbl[t * 2] + additive;
        uint8_t attr = tbl[t * 2 + 1];
        int bank_tile = (tile < 0x10) ? 256 + mapman_class * 16 + tile : 0x1C0 + (tile - 0x10);
        void (*draw)(ChrBank, int, int, int, int, int) =
            (hide == HIDE_ALL || (hide == HIDE_BOTTOM && (t % 2))) ? Ppu_LogSprite : Ppu_DrawSprite;
        draw(CHR_BANK_02, bank_tile, px + ((t >= 2) ? 8 : 0), py + ((t % 2) ? 8 : 0),
             attr & 0x03, attr & (PPU_FLIP_X | PPU_FLIP_Y));
    }
}

/**
 * Ports DrawPlayerMapmanSprite (bank_0F): the party at its fixed screen
 * spot, on foot (DrawMMV_OnFoot, lut_PlayerMapmanSprTbl) or in a vehicle
 * (Draw2x2Vehicle, lut_VehicleSprTbl + $50 canoe / $20 ship / $38
 * airship).  The picture comes from lut_VehicleFacingSprTblOffset[facing]
 * plus bit 3 of the move counter (bit 3 of framecounter * 2 for the
 * airship's propellers).  The airship's shadow flickers under it.
 */
void OWRender_DrawPlayer(int vehicle, uint8_t facing, uint8_t mapman_class, int move_ctr,
                         int inforest, uint8_t framecounter) {
    s_party = 1;
    int px = 0x70, py = lut_VehicleSprY[vehicle & 0x0F] + 1;   /* sprites show one line lower */
    int anim = (vehicle == 8) ? (framecounter << 1) : move_ctr;
    int off = (anim & 0x08) | lut_VehicleFacingSprTblOffset[facing & 0x0F];
    switch (vehicle) {
    case 1:  draw_2x2(lut_PlayerMapmanSprTbl + off, 0, mapman_class, px, py, inforest ? HIDE_BOTTOM : HIDE_NONE); break;
    case 2:  draw_2x2(lut_VehicleSprTbl + off, 0x50, mapman_class, px, py, HIDE_NONE); break;
    case 4:  draw_2x2(lut_VehicleSprTbl + off, 0x20, mapman_class, px, py, HIDE_NONE); break;
    default:                                           /* the shadow, behind it */
        draw_2x2(lut_OWObjectSprTbl[OWOBJ_AIRSHIP_SHADOW], 0x10, 0, 0x70, 0x6F + 1,
                 (framecounter & 1) ? HIDE_NONE : HIDE_ALL);    /* DrawAirshipShadow */
        draw_2x2(lut_VehicleSprTbl + off, 0x38, mapman_class, px, py, HIDE_NONE);
        break;
    }
    s_party = 0;
}

void OWRender_DrawAirshipAt(int y, uint8_t framecounter) {
    s_party = 1;
    draw_2x2(lut_OWObjectSprTbl[OWOBJ_AIRSHIP_SHADOW], 0x10, 0, 0x70, 0x6F + 1,
             (framecounter & 1) ? HIDE_NONE : HIDE_ALL);        /* DrawAirshipShadow */
    draw_2x2(lut_VehicleSprTbl + (framecounter & 0x08), 0x38, 0, 0x70, y + 1, HIDE_NONE);
    s_party = 0;
}

/* ConvertOWToSprite: the screen position of map tile (x, y), or 0 when
 * it's off screen (outside the 16x16-tile window, or off the left, right
 * or bottom edge). */
static int to_sprite(int x, int y, int scroll_x, int scroll_y, int sub_x, int sub_y,
                     int* px, int* py) {
    int dx = (x - scroll_x) & 0xFF, dy = (y - scroll_y) & 0xFF;
    if (dx >= 0x10 || dy >= 0x10) return 0;
    *px = dx * 16 - sub_x;
    *py = dy * 16 - sub_y;                             /* shown one line below spr_y */
    return !(*py - 1 < 0 || *py - 1 >= 0xEC || *px < 0 || *px >= 0xF8);
}

/** DrawOWObj_BridgeCanal: lut_OWObjectSprTbl, tiles + $10. */
void OWRender_DrawObject(int object, int obj_x, int obj_y, int scroll_x, int scroll_y,
                         int sub_x, int sub_y) {
    int px, py;
    if (to_sprite(obj_x, obj_y, scroll_x, scroll_y, sub_x, sub_y, &px, &py))
        draw_2x2(lut_OWObjectSprTbl[object], 0x10, 0, px, py, HIDE_NONE);
}

/** DrawOWObj_Ship / DrawOWObj_Airship: the vehicle facing right, frame 0. */
void OWRender_DrawVehicle(int vehicle, int obj_x, int obj_y, int scroll_x, int scroll_y,
                          int sub_x, int sub_y) {
    int px, py;
    if (to_sprite(obj_x, obj_y, scroll_x, scroll_y, sub_x, sub_y, &px, &py))
        draw_2x2(lut_VehicleSprTbl, vehicle == 8 ? 0x38 : 0x20, 0, px, py, HIDE_NONE);
}
