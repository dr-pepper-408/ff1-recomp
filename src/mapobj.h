#pragma once
#include <stdint.h>
#include "../data/generated/types.h"

/*
 * mapobj.h — Standard-map objects: townspeople, guards, Garland, ... (no SDL).
 *
 * Ports of LoadMapObjects / LoadSingleMapObject, MapObjectMove /
 * CanMapObjMove, the movement half of AnimateAndDrawMapObject, DrawMapObject,
 * IsObjectInPath and CanTalkToMapObject (bank_0F).  Each map has up to 15
 * objects.  An object is shown only while its game flag has
 * GMFLG_OBJVISIBLE set.
 */

#define MAPOBJ_SLOTS 15

/** Facing / 2x2 sprite table (lut_2x2MapObj_Right/Left/Up/Down). */
typedef enum { MOFACE_RIGHT, MOFACE_LEFT, MOFACE_UP, MOFACE_DOWN } MapObjFacing;

/** One object (the 'mapobj' RAM record). */
typedef struct {
    uint8_t id;          ///< Object id, or 0 when there is no (visible) object.
    uint8_t rawid;       ///< Object id from the map data, even while hidden.
    uint8_t flags;       ///< $80 = in a room, $40 = stationary.
    uint8_t phys_x, phys_y;   ///< Tile the object occupies (or is moving to).
    uint8_t gfx_x, gfx_y;     ///< Tile the graphic is drawn from.
    uint8_t ctr_x, ctr_y;     ///< Pixel progress of a step (0-15).
    int8_t  spd_x, spd_y;     ///< Step direction (-1, 0, +1).
    uint8_t movectr;     ///< Frames to wait before the next step.
    uint8_t face;        ///< Nonzero while a step is in progress.
    uint8_t pl;          ///< Nonzero while being shoved by the player.
    uint8_t tsa;         ///< MapObjFacing.
} MapObj;

typedef struct {
    MapObj obj[MAPOBJ_SLOTS];
    uint8_t cur;         ///< cur_mapobj: which object MapObjectMove updates next.
    uint8_t npcdir_seed; ///< npcdir_seed: direction pRNG state.
} MapObjects;

/** Port of LoadMapObjects: the objects of standard map `map`. */
void MapObj_Load(MapObjects* mo, int map, const SaveData* save);

/** One frame: animate every object's step (UpdateAndDrawMapObjects without
 *  the drawing), then let one object take a step (MapObjectMove).
 *  (player_x, player_y) is the party's tile; `in_room` is inroom bit 0. */
void MapObj_Update(MapObjects* mo, uint8_t framecounter, int player_x, int player_y,
                   int in_room);

/** Port of IsObjectInPath: 1 if an object occupies (x, y) — the party can't
 *  step there.  As in the original, walking into an object shortens its wait,
 *  and an object leaving (x, y) is shoved to move faster. */
int MapObj_IsInPath(MapObjects* mo, int x, int y);

/** Port of CanTalkToMapObject: the slot of the object at (x, y), or -1.  The
 *  object turns to face the party (whose facing is `player_facing`, BTN_*
 *  bits: 1 right, 2 left, 4 down, 8 up). */
int MapObj_FindTalkTarget(MapObjects* mo, int x, int y, uint8_t player_facing);

/** HideThisMapObject: clear object `id`'s visibility flag and remove slot `slot`. */
void MapObj_HideThis(MapObjects* mo, SaveData* save, uint8_t id, int slot);

/** HideMapObject: clear `id`'s visibility flag and remove one object using it. */
void MapObj_Hide(MapObjects* mo, SaveData* save, uint8_t id);

/** ShowMapObject: set `id`'s visibility flag and show one object using it. */
void MapObj_Show(MapObjects* mo, SaveData* save, uint8_t id);

/** Draw the objects as sprites (sprite palettes 2/3) for a viewport whose
 *  top-left map pixel is (view_px, view_py).  `in_room` selects which objects
 *  are drawn (room objects, or the others).  Alternates the drawing order
 *  every frame like the original's OAM cycling. */
void MapObj_Draw(const MapObjects* mo, uint8_t framecounter, int view_px, int view_py,
                 int in_room);
