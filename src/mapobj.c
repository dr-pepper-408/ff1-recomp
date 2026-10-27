/**
 * @file mapobj.c
 * @brief Standard-map objects: loading, wandering, collisions and drawing
 *        (ports of the map object routines in bank_0F, see mapobj.h).
 */

#include "mapobj.h"
#include "sm_map.h"
#include "ppu.h"
#include <string.h>
#include "../data/generated/mapobj_data.h"

#define GMFLG_OBJVISIBLE   0x01
#define MOFLAG_INROOM      0x80
#define MOFLAG_STATIONARY  0x40

/* Objects that flap in place and always face left when moving vertically. */
#define OBJID_SKYWAR_FIRST 0x3A
#define OBJID_SKYWAR_LAST  0x3E
#define OBJID_BAT          0x57

/* Map object CHR: bank_02 $A200 = bank tile $220, 16 tiles per graphic. */
#define MAPOBJ_CHR_BASE    0x220

/* Tile properties that stop an object (CanMapObjMove): teleports, walls. */
#define TP_TELE_MASK 0xC0
#define TP_NOMOVE    0x01

static int is_flapper(uint8_t id) {
    return (id >= OBJID_SKYWAR_FIRST && id <= OBJID_SKYWAR_LAST) || id == OBJID_BAT;
}

/* ---- Loading (LoadMapObjects / LoadSingleMapObject) ---- */

void MapObj_Load(MapObjects* mo, int map, const SaveData* save) {
    memset(mo, 0, sizeof(*mo));
    if (map < 0 || map >= MAPOBJ_MAP_COUNT) return;
    const uint8_t* src = lut_MapObjects + map * 0x30;
    for (int i = 0; i < MAPOBJ_SLOTS; i++, src += 3) {
        MapObj* o = &mo->obj[i];
        o->rawid = src[0];
        o->id = (save->game_flags[src[0]] & GMFLG_OBJVISIBLE) ? src[0] : 0;
        o->flags = src[1] & 0xC0;
        o->phys_x = o->gfx_x = src[1] & 0x3F;
        o->phys_y = o->gfx_y = src[2] & 0x3F;
        o->tsa = MOFACE_DOWN;
    }
}

/* ---- Movement ---- */

/* Port of CanMapObjMove: may object `self` step onto (x, y)? */
static int can_obj_move(const MapObjects* mo, int self, int x, int y, int px, int py) {
    const OWMetatile* m = SM_GetMetatile(SM_GetTileID(x, y));
    if (!m || (m->prop0 & (TP_TELE_MASK | TP_NOMOVE))) return 0;
    if (x == px && y == py) return 0;                    /* onto the party */
    for (int i = 0; i < MAPOBJ_SLOTS; i++) {
        const MapObj* o = &mo->obj[i];
        if (i != self && o->id && o->phys_x == x && o->phys_y == y) return 0;
    }
    return 1;
}

/* Port of MapObjectMove: one object per frame counts down its wait, then tries
 * a step in a pseudo-random direction. */
static void map_object_move(MapObjects* mo, uint8_t framecounter, int px, int py, int in_room) {
    mo->cur = (uint8_t)((mo->cur + 1) % MAPOBJ_SLOTS);
    int i = mo->cur;
    MapObj* o = &mo->obj[i];
    if (!o->id || (o->flags & MOFLAG_STATIONARY) || o->face) return;
    /* While the party is in a (normal) room, objects outside rooms wait. */
    if (in_room && !(o->flags & MOFLAG_INROOM)) return;

    if (o->movectr) { o->movectr--; return; }

    /* ADC framecounter to npcdir_seed.  The carry is left over from the
     * CMP #$F0 that wraps cur_mapobj: set only when it just wrapped to 0. */
    mo->npcdir_seed = (uint8_t)(mo->npcdir_seed + framecounter + (i == 0));
    int x = o->phys_x, y = o->phys_y;
    switch (mo->npcdir_seed & 3) {
    case 3:                                              /* down */
        y = (y + 1) & 0x3F;
        if (!can_obj_move(mo, i, x, y, px, py)) return;
        o->phys_y = (uint8_t)y;
        o->spd_y = 1;
        o->face = 8;
        o->tsa = is_flapper(o->id) ? MOFACE_LEFT : MOFACE_DOWN;
        break;
    case 2:                                              /* up */
        y = (y - 1) & 0x3F;
        if (!can_obj_move(mo, i, x, y, px, py)) return;
        o->phys_y = o->gfx_y = (uint8_t)y;               /* negative moves count down */
        o->spd_y = -1;
        o->ctr_y = 0x0F;
        o->face = 4;
        o->tsa = is_flapper(o->id) ? MOFACE_LEFT : MOFACE_UP;
        break;
    case 1:                                              /* left */
        x = (x - 1) & 0x3F;
        if (!can_obj_move(mo, i, x, y, px, py)) return;
        o->phys_x = o->gfx_x = (uint8_t)x;
        o->spd_x = -1;
        o->ctr_x = 0x0F;
        o->face = 2;
        o->tsa = MOFACE_LEFT;
        break;
    default:                                             /* right */
        x = (x + 1) & 0x3F;
        if (!can_obj_move(mo, i, x, y, px, py)) return;
        o->phys_x = (uint8_t)x;
        o->spd_x = 1;
        o->face = 1;
        o->tsa = MOFACE_RIGHT;
        break;
    }
    o->movectr = framecounter & 0x07;
}

/* The movement half of AnimateAndDrawMapObject: a pixel every other frame
 * (every frame while shoved). */
static void animate(MapObj* o, uint8_t framecounter) {
    if (!o->pl && (framecounter & 1)) return;
    if (o->spd_x) {
        o->ctr_x = (uint8_t)((o->ctr_x + o->spd_x) & 0x0F);
        if (o->ctr_x == 0) {
            o->spd_x = 0; o->face = 0; o->pl = 0;
            o->gfx_x = o->phys_x;
        }
    } else if (o->spd_y) {
        o->ctr_y = (uint8_t)((o->ctr_y + o->spd_y) & 0x0F);
        if (o->ctr_y == 0) {
            o->spd_y = 0; o->face = 0; o->pl = 0;
            o->gfx_y = o->phys_y;
        }
    }
}

void MapObj_Update(MapObjects* mo, uint8_t framecounter, int player_x, int player_y,
                   int in_room) {
    for (int i = 0; i < MAPOBJ_SLOTS; i++)
        if (mo->obj[i].id) animate(&mo->obj[i], framecounter);
    map_object_move(mo, framecounter, player_x, player_y, in_room);
}

/* ---- Interaction with the party ---- */

int MapObj_IsInPath(MapObjects* mo, int x, int y) {
    for (int i = 0; i < MAPOBJ_SLOTS; i++) {
        MapObj* o = &mo->obj[i];
        if (o->id && o->phys_x == x && o->phys_y == y) {
            o->movectr &= 3;                 /* "cut" its wait so it moves on sooner */
            return 1;
        }
    }
    /* An object stepping away from (x, y) is shoved: it moves every frame. */
    for (int i = 0; i < MAPOBJ_SLOTS; i++) {
        MapObj* o = &mo->obj[i];
        if (!o->id) continue;
        int fx = o->gfx_x, fy = o->gfx_y;    /* the tile it is moving from */
        if (o->spd_x < 0)      fx = (fx + 1) & 0x3F;
        else if (o->spd_y < 0) fy = (fy + 1) & 0x3F;
        if (fx == x && fy == y) { o->pl = 1; break; }
    }
    return 0;
}

int MapObj_FindTalkTarget(MapObjects* mo, int x, int y, uint8_t player_facing) {
    for (int i = 0; i < MAPOBJ_SLOTS; i++) {
        MapObj* o = &mo->obj[i];
        if (!o->id) continue;
        /* An object more than halfway into a step counts as on the next tile. */
        int ox = (o->gfx_x + (o->ctr_x >= 8)) & 0x3F;
        int oy = (o->gfx_y + (o->ctr_y >= 8)) & 0x3F;
        if (ox != x || oy != y) continue;
        /* DrawMapObject @FacePlayer: turn toward the party. */
        switch (player_facing) {
        case 0x01: o->tsa = MOFACE_LEFT;  break;
        case 0x02: o->tsa = MOFACE_RIGHT; break;
        case 0x04: o->tsa = MOFACE_UP;    break;
        default:   o->tsa = MOFACE_DOWN;  break;
        }
        return i;
    }
    return -1;
}

void MapObj_HideThis(MapObjects* mo, SaveData* save, uint8_t id, int slot) {
    save->game_flags[id] &= (uint8_t)~GMFLG_OBJVISIBLE;
    if (slot >= 0 && slot < MAPOBJ_SLOTS) mo->obj[slot].id = 0;
}

void MapObj_Hide(MapObjects* mo, SaveData* save, uint8_t id) {
    save->game_flags[id] &= (uint8_t)~GMFLG_OBJVISIBLE;
    for (int i = 0; i < MAPOBJ_SLOTS; i++)
        if (mo->obj[i].id == id) { mo->obj[i].id = 0; return; }
}

void MapObj_Show(MapObjects* mo, SaveData* save, uint8_t id) {
    save->game_flags[id] |= GMFLG_OBJVISIBLE;
    for (int i = 0; i < MAPOBJ_SLOTS; i++)
        if (mo->obj[i].rawid == id) { mo->obj[i].id = id; return; }
}

/* ---- Drawing (DrawMapObject / Draw2x2Sprite) ---- */

/* (lut_2x2MapObj, the sprites' layouts per facing and frame: mapobj_data.h) */

static void draw_object(const MapObj* o, uint8_t framecounter, int view_px, int view_py, int log_only) {
    /* Map-pixel position, relative to the viewport, wrapping at 64 tiles. */
    int rel_x = ((o->gfx_x * 16 + o->ctr_x - view_px) % 1024 + 1024) % 1024;
    int rel_y = ((o->gfx_y * 16 + o->ctr_y - view_py) % 1024 + 1024) % 1024;
    if (rel_x >= 0xF8 || rel_y >= 0xE8) return;

    /* Walking frame: stationary objects and bats walk in place (framecounter
     * bit 4); others use bit 2 of their step progress. */
    int frame;
    if ((o->flags & MOFLAG_STATIONARY) || is_flapper(o->id))
        frame = (framecounter >> 4) & 1;
    else
        frame = ((o->ctr_x | o->ctr_y) >> 2) & 1;

    /* The sprite is drawn 3 lines above the tile (SBC #2 with carry clear), and
     * the NES shows sprites one line below their OAM Y. */
    int sx = rel_x, sy = rel_y - 3 + 1;
    int chr = MAPOBJ_CHR_BASE + lut_MapObjGfx[o->rawid] * 16;
    const uint8_t* t = lut_2x2MapObj[o->tsa][frame];
    Ppu_SpriteGroup(0);
    for (int k = 0; k < 4; k++) {
        uint8_t attr = t[k * 2 + 1];
        int ox = (k >= 2) ? 8 : 0;     /* UL, DL, UR, DR */
        int oy = (k & 1) ? 8 : 0;
        (log_only ? Ppu_LogSprite : Ppu_DrawSprite)(CHR_BANK_02, chr + t[k * 2], sx + ox, sy + oy,
                                                    attr & 3, attr & PPU_FLIP_X);
    }
}

void MapObj_Draw(const MapObjects* mo, uint8_t framecounter, int view_px, int view_py,
                 int in_room) {
    /* On the NES the first sprite in OAM wins, and the game alternates the
     * order every frame; here later draws win, so draw in reverse OAM order. */
    for (int n = 0; n < MAPOBJ_SLOTS; n++) {
        int i = (framecounter & 1) ? n : MAPOBJ_SLOTS - 1 - n;
        const MapObj* o = &mo->obj[i];
        if (!o->id) continue;
        /* The objects on the other side of a room's door aren't drawn;
         * they're logged for the 3D view, which shows rooms' insides. */
        int other_side = ((o->flags & MOFLAG_INROOM) != 0) != (in_room != 0);
        draw_object(o, framecounter, view_px, view_py, other_side);
    }
}
