/**
 * @file view3d_world.c
 * @brief The 3D view of the overworld and the standard maps (ff1core.h):
 *        terrain chunks, volumes, props, sprites and shadows; and the
 *        landmarks' reference pictures.
 */

#include <string.h>
#include <math.h>
#include <stdint.h>
#include "view3d.h"
#include "rom.h"
#include "romdata.h"
#include "game.h"
#include "battle.h"
#include "text.h"
#include "sm_map.h"
#include "render_sm.h"
#include "input.h"
#include "overworld_map.h"
#include "ppu.h"
#include "apu.h"
#include "config.h"
#include "../data/generated/palette_data.h"
#include "../data/generated/menu_data.h"
#include "../data/generated/battle_ui.h"


#define SCREEN_TILE 7           /* the party's tile on screen (game.c PLAYER_SCREEN_TILE) */
#define TP_FOREST   0x10        /* OWTP_FOREST (constants.h clashes with input.h) */

/* ---- The 3D view ---- */

FF1_API int ff1_view3d(void) {
    /* (on the overworld or a standard map; talking keeps the map drawn, and
     * so do the screen effects, but for the story's and the minigame's) */
    return g_ctx.state == GS_WALK || g_ctx.state == GS_DIALOGUE ||
           (g_ctx.state == GS_FX && !g_ctx.fx_story && !g_ctx.fx_minigame);
}

/* The frame's screen effects, as ScreenFx_Apply left them in the PPU:
 * [0, 1] the visible rows (the screen wipe: top, count), [2..4] the colour
 * emphasis as a tint (the battle transition), [5] 1 when the whole screen
 * is monochrome (the lava flash), [6..9] a monochrome rectangle x0, y0, x1,
 * y1 (the altar's beam; x1 = 0 when none), [10, 11] the shake. */
FF1_API void ff1_view3d_fx(float* out) {
    out[0] = (float)g_ppu.bg_top;
    out[1] = (float)g_ppu.bg_rows;
    int e = g_ppu.emphasis & 7;
    for (int c = 0; c < 3; c++)                     /* (Ppu_ToRGBA's emphasize) */
        out[2 + c] = (e == 0 || (e != 7 && (e & (1 << c)))) ? 1.0f : 0.75f;
    int rows = 0, full = 1, x0 = PPU_WIDTH, y0 = PPU_HEIGHT, x1 = 0, y1 = 0;
    for (int y = 0; y < PPU_HEIGHT; y++) {
        int a = g_ppu.mono_x0[y], b = g_ppu.mono_x1[y];
        if (b <= a) { full = 0; continue; }
        rows++;
        if (a > 0 || b < PPU_WIDTH) full = 0;
        if (a < x0) x0 = a;
        if (b > x1) x1 = b;
        if (y < y0) y0 = y;
        if (y + 1 > y1) y1 = y + 1;
    }
    out[5] = (float)(rows && full);
    if (rows && !full) { out[6] = (float)x0; out[7] = (float)y0; out[8] = (float)x1; out[9] = (float)y1; }
    else out[6] = out[7] = out[8] = out[9] = 0;
    out[10] = (float)g_ppu.shake_x;
    out[11] = (float)g_ppu.shake_y;
}

/* The dialogue box (DrawDialogueBox: tiles 2..29 x 2..13). */
#define DLG_X0 16
#define DLG_Y0 16
#define DLG_X1 240
#define DLG_Y1 112

FF1_API int ff1_view3d_overlay(uint8_t* rgba) {
    if (g_ctx.state != GS_DIALOGUE) return 0;
    for (int y = 0; y < PPU_HEIGHT; y++)
        for (int x = 0; x < PPU_WIDTH; x++)
            if (x < DLG_X0 || x >= DLG_X1 || y < DLG_Y0 || y >= DLG_Y1) rgba[(y * PPU_WIDTH + x) * 4 + 3] = 0;
    return 1;
}


/* ---- Shapes ----
 *
 * The overworld art is drawn in a three-quarter view: ground and the tops
 * of things as seen from above, the fronts of mountains and forests as
 * seen from the south.  So the art isn't read as a height map; each tile
 * gets a SHAPE (the presentation table below):
 *
 *   flat       ocean, river, ground: one height.
 *   shore      coast and river-bend tiles: the drawn grass at ground
 *              height, the drawn water at the water's, so the coastline
 *              is the one in the art, with a lip.
 *   mountain   the drawn silhouette (every colour but the grass, 1)
 *              stands up as a mass: a cliff at its edge, rising in steps
 *              towards the ridge.
 *   canopy     forests and the forest-edge tiles: the silhouette as a
 *              rounded canopy, a little lumpy where the leaves are lit.
 *   building   castles, towns, caves, towers (a profile per tile, below):
 *              a BODY stands as tall as it is drawn -- each pixel column
 *              rises by its run of drawn rows, so the south face shows
 *              the whole drawing upright and the top wears its top row;
 *              a WALL raises only its outline colours (the courtyard
 *              stays floor); FLAT tiles (gates, paths) lie on the ground.
 *
 * Walls facing south FOLD the art upright: the wall shows the rows drawn
 * just north of its foot, one row per pixel of height, never stretched --
 * a mountain's cliff stands as the cliff it was drawn as.  Other walls
 * take the material's colour from a little inside the edge, not the
 * outline (which would make them black slabs).
 */

enum { SH_OCEAN, SH_RIVER, SH_GROUND, SH_CANOPY, SH_MOUNTAIN, SH_BUILDING, SH_SHORE, SH_RIVER_SHORE };

/* The presentation table: the tiles whose shape isn't their class's.
 * Mountains are the blocked tiles that make up ranges (each appears
 * hundreds of times; the buildings a few times each); the canopy tiles
 * are the forest and the walkable tiles drawn with its edge. */
static const uint8_t mountain_ids[] = { 0x10, 0x11, 0x12, 0x20, 0x21, 0x22, 0x30, 0x31, 0x33 };
static const uint8_t canopy_ids[] = { 0x03, 0x04, 0x05, 0x13, 0x14, 0x15, 0x23, 0x24, 0x25 };
static const uint8_t shore_ids[] = { 0x06, 0x07, 0x08, 0x16, 0x18, 0x26, 0x27, 0x28 };
static const uint8_t river_shore_ids[] = { 0x40, 0x41, 0x50, 0x51 };
#define GRASS_CI 1              /* the colour that is ground in mountain and canopy art */

/* `floor`: the colours (bits 1 << ci) that are ground in the tile's art --
 * the grass, and the grey a courtyard or a town square is drawn in. */
enum { BM_BODY, BM_WALL, BM_FLAT };
typedef struct { uint8_t id, mode, cap, floor; } BuildingProfile;
#define FL_GRASS  (1 << 1)
#define FL_COURT  ((1 << 1) | (1 << 2))
static const BuildingProfile building_profile[] = {
    /* Coneria: the tower, the keep's wings, the town's houses, the front */
    { 0x09, BM_BODY, 40, FL_GRASS }, { 0x0A, BM_BODY, 40, FL_GRASS },
    { 0x19, BM_BODY, 40, FL_GRASS }, { 0x1A, BM_BODY, 40, FL_GRASS },
    { 0x3C, BM_BODY, 20, FL_GRASS }, { 0x3E, BM_BODY, 20, FL_GRASS },
    { 0x49, BM_BODY, 12, FL_COURT },
    { 0x7D, BM_BODY, 12, FL_COURT }, { 0x7E, BM_BODY, 12, FL_COURT },
    /* ...its ring of walls */
    { 0x2C, BM_WALL, 10, FL_COURT }, { 0x2E, BM_WALL, 10, FL_COURT },
    { 0x3B, BM_WALL, 10, FL_COURT }, { 0x3F, BM_WALL, 10, FL_COURT },
    { 0x4B, BM_WALL, 10, FL_COURT }, { 0x4F, BM_WALL, 10, FL_COURT },
    { 0x5B, BM_WALL, 10, FL_COURT }, { 0x5F, BM_WALL, 10, FL_COURT },
    { 0x6B, BM_WALL, 10, FL_COURT }, { 0x6F, BM_WALL, 10, FL_COURT },
    { 0x7B, BM_WALL, 10, FL_COURT }, { 0x7F, BM_WALL, 10, FL_COURT },
    /* ...and its gate */
    { 0x01, BM_FLAT, 0, 0 }, { 0x02, BM_FLAT, 0, 0 },
};
#define BUILDING_CAP 24         /* other building tiles: bodies up to this tall */
#define RUN_SCAN     64         /* how far a body's run is followed */

#define GROUND_H   4
#define MTN_CLIFF  16           /* a mountain's edge: one tile drawn front-on */
#define MTN_STEP   4            /* then this much higher... */
#define MTN_RUN    8            /* ...every this many pixels inward */
#define MTN_MAX    (GROUND_H + MTN_CLIFF + 3 * MTN_STEP)
#define CAN_EDGE   2            /* canopy: rises this much per pixel inward... */
#define CAN_RISE   5            /* ...for this many pixels */
static const int8_t can_leaf[4] = { -2, 0, 0, 1 };   /* by colour: shade low, lit high */

/* The world being built: the overworld, or the standard map the party is
 * on (64 x 64 tiles, its tileset's metatiles and CHR, with the door being
 * opened drawn in place of the map's tile).  Standard maps have no tile
 * lists: their shapes come from the map itself (classify_sm). */
static int w_sm, w_tiles = 256;
#define WMASK (w_tiles * 16 - 1)
#define SM_CAP 40               /* a standard map's bodies: up to this tall */

static void world_select(void) {
    w_sm = g_ctx.mode == MODE_SM;
    w_tiles = w_sm ? SM_MAP_WIDTH : OW_MAP_WIDTH;
}

static uint8_t world_tile(int col, int row) {
    if (!w_sm) return OWMap_GetTileID(col & 0xFF, row & 0xFF);
    col &= SM_MAP_WIDTH - 1; row &= SM_MAP_HEIGHT - 1;
    int d = SMRender_DoorTileAt(col, row);
    return (uint8_t)(d >= 0 ? d : SM_GetTileID(col, row));
}

static const OWMetatile* world_metatile(uint8_t id) {
    return w_sm ? SM_GetMetatile(id) : OWMap_GetMetatile(id);
}

static const uint8_t* world_chr(int t) {
    return w_sm ? Ppu_ChrTile(CHR_BANK_03, SM_GetTileset() * 128 + t) : Ppu_ChrTile(CHR_BANK_02, t);
}

/* What the tiles' shapes were worked out for: the overworld, or a map
 * with its tileset (and its palettes, read from the map). */
static uint32_t world_key(void) {
    if (!w_sm) return 0;
    uint8_t out[32];
    SM_GetMapPalettes(SM_GetMapId(), out);
    uint32_t h = 2166136261u ^ (uint32_t)(SM_GetMapId() << 8 | SM_GetTileset());
    for (int i = 0; i < 16; i++) { h ^= out[i]; h *= 16777619u; }
    return h | 1;
}

static uint8_t tile_shape[128];
static uint8_t tile_vol[128];            /* a building's tile: a box (see Volumes) */
static uint8_t tile_room[128];           /* a standard map's room tile (see classify_sm) */
static uint8_t tile_prop[128];           /* ...an object: a 2D card (see ff1_props) */
static uint8_t tile_cut[128][16][16];    /* ...its card's air: floor colour reaching the edge */
static int ground_u = -1, ground_v;      /* a texel of the map's floor colour (under objects) */

/* The tiles the game draws over doors as they open and close (game.c). */
#define DOOR_CLOSED 0x36
#define DOOR_OPEN   0x37
#define DOOR_LOCKED 0x3B
#define ATLAS_CUTOUT_Y 128      /* the atlas's cut-out copy of the metatiles (the cards) */
static uint8_t sm_out[32], sm_room[16];  /* the map's outdoor and room palettes */
static uint8_t tile_vunit[128];          /* ...its height per tile of its column's run... */
static uint8_t tile_vtiles[128];         /* ...for up to this many tiles */

/* Box sizes: a standard map's walls. */
#define SM_UNIT     16
#define SM_TILES    2
static uint8_t tile_bmode[128], tile_bcap[128], tile_bfloor[128];
static uint8_t tile_ci[128][16][16];     /* each metatile's colour indices */
static uint8_t rock_px[128][2];          /* a pixel of its commonest colour (x, y) */
static uint32_t tiles_key = 0xFFFFFFFFu;
static void classify_sm(void);

static void prepare_tiles(void) {
    world_select();
    uint32_t key = world_key();
    if (key == tiles_key) return;
    tiles_key = key;
    for (int id = 0; id < 128; id++) {
        const OWMetatile* m = world_metatile((uint8_t)id);
        if (!m) continue;
        uint8_t t = m->prop0;
        tile_shape[id] = !(t & 0x01) ? ((t & TP_FOREST) ? SH_CANOPY : SH_GROUND)
                       : !(t & 0x04) ? SH_OCEAN
                       : !(t & 0x02) ? SH_RIVER : SH_BUILDING;
        for (unsigned k = 0; k < sizeof(mountain_ids); k++)
            if (mountain_ids[k] == id) tile_shape[id] = SH_MOUNTAIN;
        for (unsigned k = 0; k < sizeof(canopy_ids); k++)
            if (canopy_ids[k] == id) tile_shape[id] = SH_CANOPY;
        for (unsigned k = 0; k < sizeof(shore_ids); k++)
            if (shore_ids[k] == id) tile_shape[id] = SH_SHORE;
        for (unsigned k = 0; k < sizeof(river_shore_ids); k++)
            if (river_shore_ids[k] == id) tile_shape[id] = SH_RIVER_SHORE;
        tile_bmode[id] = BM_BODY;
        tile_bcap[id] = BUILDING_CAP;
        tile_bfloor[id] = FL_GRASS;
        for (unsigned k = 0; k < sizeof(building_profile) / sizeof(building_profile[0]); k++)
            if (building_profile[k].id == id) {
                tile_bmode[id] = building_profile[k].mode;
                tile_bcap[id] = building_profile[k].cap;
                tile_bfloor[id] = building_profile[k].floor;
                tile_shape[id] = building_profile[k].mode == BM_FLAT ? SH_GROUND : SH_BUILDING;
            }
        const uint8_t tiles[4] = { m->ul, m->ur, m->dl, m->dr };
        for (int q = 0; q < 4; q++) {
            const uint8_t* c = world_chr(tiles[q]);
            if (!c) continue;
            for (int r = 0; r < 8; r++)
                for (int k = 0; k < 8; k++)
                    tile_ci[id][(q >> 1) * 8 + r][(q & 1) * 8 + k] = (uint8_t)chr_pixel(c, r, k);
        }
        int n[4] = { 0 }, best = 2;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) n[tile_ci[id][y][x]]++;
        if (n[3] > n[best]) best = 3;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                if (tile_ci[id][y][x] == best) { rock_px[id][0] = (uint8_t)x; rock_px[id][1] = (uint8_t)y; }
    }
    if (!w_sm) {
        /* The overworld's buildings stand as tall as they are drawn
         * (body_h), no boxes. */
        memset(tile_vol, 0, sizeof(tile_vol));
        memset(tile_room, 0, sizeof(tile_room));
        memset(tile_prop, 0, sizeof(tile_prop));
    }
    if (w_sm) classify_sm();

    /* Each tile's cut-out (for the cards): a floor-colour pixel is air
     * unless the drawing surrounds it -- drawn pixels on its left, right,
     * above and below (a tree's own green, inside its outline, stays). */
    for (int id = 0; id < 128; id++)
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                int floor = (tile_bfloor[id] >> tile_ci[id][y][x]) & 1, l = 0, r = 0, u = 0, d = 0;
                #define DRAWN(xx, yy) !((tile_bfloor[id] >> tile_ci[id][yy][xx]) & 1)
                for (int k = 0; k < x; k++) l |= DRAWN(k, y);
                for (int k = x + 1; k < 16; k++) r |= DRAWN(k, y);
                for (int k = 0; k < y; k++) u |= DRAWN(x, k);
                for (int k = y + 1; k < 16; k++) d |= DRAWN(x, k);
                #undef DRAWN
                tile_cut[id][y][x] = (uint8_t)(floor && !(l && r && u && d));
            }
}

/* A metatile pixel's NES colour: on the overworld in the current palette;
 * on a standard map in the map's own colours, its rooms' tiles in the room
 * palette (the 3D view shows rooms' insides). */
static uint8_t tile_colour(uint8_t id, int ci) {
    const OWMetatile* m = world_metatile(id);
    int pal = m ? m->attr & 3 : 0;
    const uint8_t* p = !w_sm ? g_ppu.palette : tile_room[id] ? sm_room : sm_out;
    return p[ci ? pal * 4 + ci : 0] & 0x3F;
}

static int is_water_colour(uint8_t c) {
    int hue = c & 0x0F;
    return hue == 0x01 || hue == 0x02 || hue == 0x0C;
}

/* A standard map's shapes, from the map: walkable tiles are floor; the
 * colour most of the floor is drawn in (grass, flagstones) is the ground
 * a blocked tile's drawing stands on; blocked tiles drawn mostly in blue
 * are water; the other blocked tiles are bodies standing as tall as drawn. */
static void find_rooms(const uint32_t (*hist)[4]);

static void classify_sm(void) {
    static uint32_t hist[128][4];
    uint32_t used[128] = { 0 }, colours[64] = { 0 };
    memset(hist, 0, sizeof(hist));
    for (int id = 0; id < 128; id++)
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) hist[id][tile_ci[id][y][x]]++;
    find_rooms((const uint32_t (*)[4])hist);
    for (int r = 0; r < SM_MAP_HEIGHT; r++)
        for (int c = 0; c < SM_MAP_WIDTH; c++) used[SM_GetTileID(c, r) & 0x7F]++;
    for (int id = 0; id < 128; id++) {
        const OWMetatile* m = world_metatile((uint8_t)id);
        if (!m || (m->prop0 & 0x01) || !used[id]) continue;
        for (int ci = 0; ci < 4; ci++) colours[tile_colour((uint8_t)id, ci)] += used[id] * hist[id][ci];
    }
    int ground = 0;
    for (int c = 1; c < 64; c++) if (colours[c] > colours[ground]) ground = c;

    for (int id = 0; id < 128; id++) {
        const OWMetatile* m = world_metatile((uint8_t)id);
        tile_bmode[id] = BM_BODY;
        tile_bcap[id] = SM_CAP;
        tile_bfloor[id] = 0;
        tile_vol[id] = 0;
        for (int ci = 0; ci < 4; ci++)
            if (m && tile_colour((uint8_t)id, ci) == ground) tile_bfloor[id] |= (uint8_t)(1 << ci);
        if (!m || !(m->prop0 & 0x01)) { tile_shape[id] = SH_GROUND; continue; }
        uint32_t water = 0, floor = 0;
        for (int ci = 0; ci < 4; ci++) {
            uint8_t col = tile_colour((uint8_t)id, ci);
            if (col == ground) floor += hist[id][ci];
            if (is_water_colour(col)) water += hist[id][ci];
        }
        tile_shape[id] = water * 2 > 256 ? SH_OCEAN : SH_BUILDING;
        /* Solid drawings (walls, towers, roofs: under a fifth ground) are
         * boxes; the rest (trees, fences, lamps) stand as silhouettes. */
        tile_vol[id] = tile_shape[id] == SH_BUILDING && floor * 5 < 256;
        tile_vunit[id] = SM_UNIT;
        tile_vtiles[id] = SM_TILES;
    }

    /* A room's walls (the blocked tiles round its floor, corners too: the
     * dwarves' houses, say) are solid boxes with their drawing on the
     * faces, like a town's walls, whatever their art.  A wall is a tile
     * found next to a room's floor at least half the times it's used (the
     * cave's rock, everywhere, sometimes touches a room too). */
    /* Then, once more, the tiles found next to those walls at least half
     * the time (the houses' fronts, which border their walls rather than
     * their floors). */
    static uint8_t wall[128];
    memset(wall, 0, sizeof(wall));
    for (int pass = 0; pass < 2; pass++) {
        uint32_t count[128] = { 0 }, beside[128] = { 0 };
        for (int r = 0; r < SM_MAP_HEIGHT; r++)
            for (int c = 0; c < SM_MAP_WIDTH; c++) {
                uint8_t id = SM_GetTileID(c, r) & 0x7F;
                const OWMetatile* m = world_metatile(id);
                if (!m || !(m->prop0 & 0x01) || tile_room[id] || wall[id]) continue;   /* blocked, not yet placed */
                count[id]++;
                int near = 0;
                for (int dy = -1; dy <= 1 && !near; dy++)
                    for (int dx = -1; dx <= 1 && !near; dx++) {
                        uint8_t n = SM_GetTileID(c + dx, r + dy) & 0x7F;
                        const OWMetatile* nm = world_metatile(n);
                        near = pass == 0 ? nm && tile_room[n] && !(nm->prop0 & 0x01)   /* a room's floor */
                                         : wall[n];                                     /* a wall */
                    }
                beside[id] += (uint32_t)near;
            }
        for (int id = 0; id < 128; id++)
            if (count[id] && beside[id] * 2 >= count[id]) wall[id] = 1;
    }
    for (int id = 0; id < 128; id++)
        if (wall[id]) {
            tile_shape[id] = SH_BUILDING;
            tile_vol[id] = 1;
        }

    /* Objects stand as 2D cards, like the characters: blocked tiles
     * standing alone (wells, statues), the drawings that aren't solid
     * (trees, fences, lamps: the blocked tiles that aren't boxes) and
     * stand alone or in rows -- not rock filling a cave, whose
     * tiles are mostly boxed in by more rock --, treasure chests and
     * doors.  They lie flat on the floor. */
    uint32_t uses[128] = { 0 }, enclosed[128] = { 0 }, lone[128] = { 0 };
    for (int r = 0; r < SM_MAP_HEIGHT; r++)
        for (int c = 0; c < SM_MAP_WIDTH; c++) {
            uint8_t id = SM_GetTileID(c, r) & 0x7F;
            uses[id]++;
            int blocked_near = 0;                /* (a well, a statue: nothing blocked beside it) */
            for (int k = 0; k < 4; k++) {
                static const int ox[4] = { 1, -1, 0, 0 }, oy[4] = { 0, 0, 1, -1 };
                const OWMetatile* nm = world_metatile(SM_GetTileID(c + ox[k], r + oy[k]) & 0x7F);
                if (nm && (nm->prop0 & 0x01)) blocked_near = 1;
            }
            lone[id] += (uint32_t)!blocked_near;
            int all = 1;
            for (int dy = -1; dy <= 1 && all; dy++)
                for (int dx = -1; dx <= 1 && all; dx++) {
                    const OWMetatile* nm = world_metatile(SM_GetTileID(c + dx, r + dy) & 0x7F);
                    all = nm && (nm->prop0 & 0x01);
                }
            enclosed[id] += (uint32_t)all;
        }
    for (int id = 0; id < 128; id++) {
        const OWMetatile* m = world_metatile((uint8_t)id);
        int spec = m ? m->prop0 & SM_TP_SPEC_MASK : 0;
        int standing = tile_shape[id] == SH_BUILDING && !tile_vol[id] && enclosed[id] * 2 < uses[id];
        int well = m && (m->prop0 & 0x01) && uses[id] && lone[id] * 2 >= uses[id];   /* (standing alone) */
        tile_prop[id] = standing || well || spec == SM_TP_SPEC_TREASURE ||
                        spec == SM_TP_SPEC_DOOR || spec == SM_TP_SPEC_LOCKED ||
                        id == DOOR_CLOSED || id == DOOR_OPEN || id == DOOR_LOCKED;
        if (tile_prop[id]) { tile_shape[id] = SH_GROUND; tile_vol[id] = 0; }
    }
    /* (and the floor colour under them: a pixel of it in the most used
     * floor tile) */
    ground_u = -1;
    uint32_t best = 0;
    for (int id = 0; id < 128; id++) {
        const OWMetatile* m = world_metatile((uint8_t)id);
        if (!m || (m->prop0 & 0x01) || tile_prop[id] || used[id] <= best) continue;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                if (tile_colour((uint8_t)id, tile_ci[id][y][x]) == ground) {
                    best = used[id];
                    ground_u = (id % 16) * 16 + x;
                    ground_v = (id / 16) * 16 + y;
                    x = y = 16;
                }
    }
}

/* Rooms.  The original hides a room's inside until the party walks in:
 * its tiles are drawn in BG palettes that the map's outdoor colours blank
 * out to one flat colour, and the room palette shows them once inside.
 * The 3D view shows rooms open: their tiles always in the room palette,
 * the rest of the map always in the outdoor one.  (The 2D picture is the
 * original's.)  Rooms are behind doors: a map without any, a cave say, has
 * none, whatever its room palette is. */
static void find_rooms(const uint32_t (*hist)[4]) {
    SM_GetMapPalettes(SM_GetMapId(), sm_out);
    SM_GetRoomPalette(SM_GetMapId(), sm_room);
    memset(tile_room, 0, sizeof(tile_room));
    int doors = 0;
    for (int r = 0; r < SM_MAP_HEIGHT && !doors; r++)
        for (int c = 0; c < SM_MAP_WIDTH && !doors; c++) {
            const OWMetatile* m = world_metatile(SM_GetTileID(c, r) & 0x7F);
            int spec = m ? m->prop0 & SM_TP_SPEC_MASK : 0;
            doors = spec == SM_TP_SPEC_DOOR || spec == SM_TP_SPEC_LOCKED;
        }
    if (!doors) return;
    int hidden = 0;                                   /* palettes the room palette changes */
    for (int p = 0; p < 4; p++)
        if (memcmp(sm_out + p * 4 + 1, sm_room + p * 4 + 1, 3)) hidden |= 1 << p;
    for (int id = 0; id < 128; id++) {
        const OWMetatile* m = world_metatile((uint8_t)id);
        if (!m || !(hidden & (1 << (m->attr & 3)))) continue;
        int spec = m->prop0 & SM_TP_SPEC_MASK;
        if (spec == SM_TP_SPEC_DOOR || spec == SM_TP_SPEC_LOCKED) continue;   /* (the way in) */
        /* A hidden room's tile is blanked out: one flat colour in the
         * outdoor palette (the castles' white, the dwarves' orange), not
         * black (the void round a map); floors and rock keep their detail. */
        int pal = m->attr & 3, flat = -1, uniform = 1;
        for (int ci = 0; ci < 4 && uniform; ci++) {
            if (!hist[id][ci]) continue;
            int c = sm_out[ci ? pal * 4 + ci : 0] & 0x3F;
            if (flat < 0) flat = c;
            else if (c != flat) uniform = 0;
        }
        if (!uniform || flat < 0 || (flat & 0x0F) >= 0x0D) continue;
        tile_room[id] = 1;
    }
}

static uint8_t tile_at(int wx, int wz) {
    return world_tile(wx >> 4, wz >> 4) & 0x7F;
}

static int ci_at(int wx, int wz) {
    wx &= WMASK; wz &= WMASK;
    return tile_ci[tile_at(wx, wz)][wz & 15][wx & 15];
}

/* Is the pixel part of a mass of this shape (its silhouette, not grass)? */
static int in_mass(int wx, int wz, int shape) {
    wx &= WMASK; wz &= WMASK;
    uint8_t id = tile_at(wx, wz);
    return tile_shape[id] == shape && tile_ci[id][wz & 15][wx & 15] != GRASS_CI;
}

/* The height of a pixel that isn't in a mountain or canopy mass. */
static int flat_h(int wx, int wz) {
    wx &= WMASK; wz &= WMASK;
    uint8_t id = tile_at(wx, wz);
    switch (tile_shape[id]) {
    case SH_OCEAN: return 0;
    case SH_RIVER: return 1;
    case SH_SHORE: return tile_ci[id][wz & 15][wx & 15] == GRASS_CI ? GROUND_H : 0;
    case SH_RIVER_SHORE: return tile_ci[id][wz & 15][wx & 15] == GRASS_CI ? GROUND_H : 1;
    default: return GROUND_H;          /* (and a building's floor) */
    }
}

/* Is the pixel part of a building's body (or wall)? */
static int in_body(int wx, int wz) {
    wx &= WMASK; wz &= WMASK;
    uint8_t id = tile_at(wx, wz);
    if (tile_shape[id] != SH_BUILDING) return 0;
    return !(tile_bfloor[id] & (1 << tile_ci[id][wz & 15][wx & 15]));
}

/* A building pixel's height, and the row its top wears. */
static int body_h(int wx, int wz, int* top_row) {
    uint8_t id = tile_at(wx & WMASK, wz & WMASK);
    if (tile_bmode[id] == BM_WALL) { *top_row = wz; return GROUND_H + tile_bcap[id]; }
    int up = 0, down = 0;
    while (up < RUN_SCAN && in_body(wx, wz - up - 1)) up++;
    while (down < RUN_SCAN && in_body(wx, wz + down + 1)) down++;
    int run = up + down + 1;
    *top_row = wz - up;
    /* (past the drawing's black outline, so the top shows its material) */
    for (int k = 0; k < 4 && *top_row < wz + down && ci_at(wx, *top_row) == 0; k++) (*top_row)++;
    return GROUND_H + (run < tile_bcap[id] ? run : tile_bcap[id]);
}

/* Where sprites stand: the ground (inside a forest, under the canopy). */
static int ground_h(int wx, int wz) {
    uint8_t id = tile_at(wx & WMASK, wz & WMASK);
    const OWMetatile* m = world_metatile(id);
    if (tile_vol[id] && m && !(m->prop0 & 0x01)) return GROUND_H + tile_vunit[id];   /* (a plinth) */
    int s = tile_shape[id];
    return (s == SH_CANOPY || s == SH_MOUNTAIN || s == SH_BUILDING) ? GROUND_H : flat_h(wx, wz);
}

FF1_API void ff1_terrain_atlas(uint8_t* rgba) {
    prepare_tiles();
    for (int id = 0; id < 128; id++) {
        const OWMetatile* m = world_metatile((uint8_t)id);
        if (!m) continue;
        int pal = m->attr & 3;
        /* (the live palette -- fades reach it -- for the side the party is
         * on; the map's own for the other: rooms are shown open) */
        const uint8_t* P = g_ppu.palette;
        if (w_sm && tile_room[id] != (g_ctx.inroom != 0)) P = tile_room[id] ? sm_room : sm_out;
        const uint8_t tiles[4] = { m->ul, m->ur, m->dl, m->dr };
        for (int q = 0; q < 4; q++) {
            const uint8_t* t = world_chr(tiles[q]);
            if (!t) continue;
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 8; c++) {
                    int x = (id % 16) * 16 + (q & 1) * 8 + c, y = (id / 16) * 16 + (q >> 1) * 8 + r;
                    int ci = chr_pixel(t, r, c);
                    uint8_t* px = rgba + (y * FF1_ATLAS_W + x) * 4;
                    nes_rgba(P[ci ? pal * 4 + ci : 0], px);
                    /* the cut-out copy (the objects' cards): the floor colour is air */
                    uint8_t* cut = rgba + ((ATLAS_CUTOUT_Y + y) * FF1_ATLAS_W + x) * 4;
                    memcpy(cut, px, 4);
                    if (tile_cut[id][(q >> 1) * 8 + r][(q & 1) * 8 + c]) cut[3] = 0;
                    else if ((tile_bfloor[id] >> ci) & 1) {    /* (floor colour inside: a shade darker,
                                                                * so a tree doesn't melt into the lawn) */
                        cut[0] = (uint8_t)(cut[0] * 3 / 4); cut[1] = (uint8_t)(cut[1] * 3 / 4); cut[2] = (uint8_t)(cut[2] * 3 / 4);
                    }
                }
        }
    }
}

/* The atlas position of a world pixel's colour (texels, pixel corner). */
static void atlas_uv(int wx, int wz, float* u, float* v) {
    uint8_t id = tile_at(wx & WMASK, wz & WMASK);
    *u = (float)((id % 16) * 16 + (wx & 15));
    *v = (float)((id / 16) * 16 + (wz & 15));
}

/* ---- Meshing ---- */


void emit_quad(Emit* e, const float p[4][3], const float uv[4][2], uint8_t shade) {
    static const int order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int k = 0; k < 6; k++, e->count++) {
        if (e->count >= e->max) continue;          /* (still counted: the size needed) */
        int c = order[k];
        e->out[e->count] = (Ff1Vertex){ p[c][0], p[c][1], p[c][2],
            uv[c][0] / FF1_ATLAS_W, uv[c][1] / FF1_ATLAS_H, shade, shade, shade, 255 };
    }
}

#define CH 256                  /* a chunk: 16 x 16 tiles of 16 pixels */
#define UV_IN 0.02f             /* keeps nearest sampling inside the texel */
static int16_t hbuf[CH + 2][CH + 2];   /* the chunk's heights with a 1-pixel border */
static int32_t tbuf[CH + 2][CH + 2];   /* building pixels: the row their top wears (else NO_TOP) */
#define NO_TOP INT32_MIN

/* Each pixel's distance (Chebyshev, capped) to the nearest pixel outside
 * its mass, over the chunk and a margin wide enough that chunks agree
 * along their edges; 0 outside the mass. */
#define DIST_CAP    32
#define MARGIN      (DIST_CAP + 2)
#define MW          (CH + 2 * MARGIN)
static uint8_t mdist[MW][MW], cdist[MW][MW];

static void distances(uint8_t (*dist)[MW], int x0, int z0, int shape) {
    for (int j = 0; j < MW; j++)
        for (int i = 0; i < MW; i++)
            dist[j][i] = in_mass(x0 + i - MARGIN, z0 + j - MARGIN, shape) ? DIST_CAP : 0;
    /* two passes: from the top-left, then from the bottom-right */
    for (int j = 0; j < MW; j++)
        for (int i = 0; i < MW; i++) {
            if (!dist[j][i]) continue;
            int d = dist[j][i];
            if (i > 0 && dist[j][i - 1] + 1 < d) d = dist[j][i - 1] + 1;
            if (j > 0)
                for (int k = -1; k <= 1; k++)
                    if (i + k >= 0 && i + k < MW && dist[j - 1][i + k] + 1 < d) d = dist[j - 1][i + k] + 1;
            dist[j][i] = (uint8_t)d;
        }
    for (int j = MW - 1; j >= 0; j--)
        for (int i = MW - 1; i >= 0; i--) {
            if (!dist[j][i]) continue;
            int d = dist[j][i];
            if (i + 1 < MW && dist[j][i + 1] + 1 < d) d = dist[j][i + 1] + 1;
            if (j + 1 < MW)
                for (int k = -1; k <= 1; k++)
                    if (i + k >= 0 && i + k < MW && dist[j + 1][i + k] + 1 < d) d = dist[j + 1][i + k] + 1;
            dist[j][i] = (uint8_t)d;
        }
}

static int mountain_h(int d) {
    int h = GROUND_H + MTN_CLIFF + (d - 1) / MTN_RUN * MTN_STEP;
    return h < MTN_MAX ? h : MTN_MAX;
}

static int canopy_h(int d, int ci) {
    return GROUND_H + (d < CAN_RISE ? d : CAN_RISE) * CAN_EDGE + can_leaf[ci];
}

FF1_API uint32_t ff1_world(int* tiles) {
    prepare_tiles();
    *tiles = w_tiles;
    return tiles_key;
}

FF1_API uint32_t ff1_chunk_key(int cx, int cz) {
    prepare_tiles();
    uint32_t h = 2166136261u ^ tiles_key;         /* FNV-1a over its tile ids */
    for (int j = 0; j < 16; j++)
        for (int i = 0; i < 16; i++) {
            h ^= world_tile(cx * 16 + i, cz * 16 + j);
            h *= 16777619u;
        }
    return h;
}

/* A side wall's colour: the higher pixel's, taken from `in` pixels inside
 * the edge (dx, dz: the direction into the higher side), past the black
 * outline; mountains use their rock colour. */
static void side_uv(int wx, int wz, int dx, int dz, float* u, float* v) {
    uint8_t id = tile_at(wx & WMASK, wz & WMASK);
    if (tile_shape[id] == SH_MOUNTAIN) {
        *u = (float)((id % 16) * 16 + rock_px[id][0]) + 0.5f;
        *v = (float)((id / 16) * 16 + rock_px[id][1]) + 0.5f;
        return;
    }
    for (int k = 0; k < 3 && ci_at(wx, wz) == 0; k++) { wx += dx; wz += dz; }
    atlas_uv(wx, wz, u, v);
    *u += 0.5f;
    *v += 0.5f;
}

/* ---- Volumes ----
 *
 * A standard map's solid tiles (walls, towers, roofs) are boxes rather than
 * voxels: each tile a 16 x 16 box as tall as the run of solid tiles in its
 * column (up to its tile's count, at its height per tile), the drawing
 * fitted to its faces.  The run's
 * southernmost tiles are its facade: the front shows them upright, one art
 * row per pixel; the sides and back wear them too, in 16-pixel bands (the
 * bricks go round the corner); the facade tiles' tops are the material,
 * the deeper tiles' tops (roofs, seen from above) their own art. */

static int vol_tile(int tc, int tr) {
    return tile_vol[world_tile(tc, tr) & 0x7F];
}

/* A tile drawn mostly in white or a pale colour ($2x, $3x): its own art
 * would glare as a box's top. */
static int pale_tile(uint8_t id) {
    int n[4] = { 0 }, best = 1;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) n[tile_ci[id][y][x]]++;
    for (int ci = 2; ci < 4; ci++) if (n[ci] > n[best]) best = ci;
    uint8_t c = tile_colour(id, best);
    return (c & 0x30) >= 0x20 && (c & 0x0F) < 0x0D;
}

/* A box's top (the facade's material): a pixel of the tile's commonest
 * colour that isn't white or pale ($2x, $3x) or black, so the tops don't
 * glare; else of its darkest colour but black. */
static void top_px(uint8_t id, float* u, float* v) {
    int n[4] = { 0 }, at[4][2] = { { 0 } };
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int ci = tile_ci[id][y][x];
            n[ci]++;
            at[ci][0] = x; at[ci][1] = y;
        }
    int best = -1, dark = -1;
    for (int ci = 1; ci < 4; ci++) {
        uint8_t c = tile_colour(id, ci);
        if (!n[ci] || (c & 0x0F) >= 0x0D) continue;               /* (black) */
        if (dark < 0 || (c & 0x30) < (tile_colour(id, dark) & 0x30)) dark = ci;
        if ((c & 0x30) >= 0x20) continue;                        /* (white, pale) */
        if (best < 0 || n[ci] > n[best]) best = ci;
    }
    if (best < 0) best = dark;
    int px = best >= 0 ? at[best][0] : rock_px[id][0], py = best >= 0 ? at[best][1] : rock_px[id][1];
    *u = (float)((id % 16) * 16 + px) + 0.5f;
    *v = (float)((id / 16) * 16 + py) + 0.5f;
}

/* The floor under an object's card at tile (tc, tr): the nearest plain
 * floor tile (walkable, not an object or a box, nothing special: no
 * stairs or exit, door, chest, altar...), the one in front first; -1:
 * none near. */
static int prop_floor(int tc, int tr) {
    static const int8_t near[][2] = { { 0, 1 }, { -1, 0 }, { 1, 0 }, { 0, -1 },
                                      { -1, 1 }, { 1, 1 }, { -1, -1 }, { 1, -1 } };
    for (int r = 1; r <= 3; r++)
        for (int k = 0; k < 8; k++) {
            uint8_t id = world_tile(tc + near[k][0] * r, tr + near[k][1] * r) & 0x7F;
            const OWMetatile* m = world_metatile(id);
            if (!m || (m->prop0 & 0x01) || tile_prop[id] || tile_vol[id]) continue;
            int spec = m->prop0 & SM_TP_SPEC_MASK;
            if ((m->prop0 & SM_TP_TELE_MASK) ||
                (spec && spec != SM_TP_SPEC_BATTLE && spec != SM_TP_SPEC_DAMAGE && spec != SM_TP_SPEC_CLOSEROOM))
                continue;
            return id;
        }
    return -1;
}

/* The chunk's tiles (and a border of one): the box's height in tiles (0:
 * not a volume) and the tile row of its run's facade bottom. */
static int8_t vt_tiles[18][18];
static int16_t vt_bottom[18][18], vt_h[18][18];

static void volume_tiles(int tc0, int tr0) {
    for (int j = 0; j < 18; j++)
        for (int i = 0; i < 18; i++) {
            int tc = tc0 + i - 1, tr = tr0 + j - 1;
            vt_tiles[j][i] = 0;
            if (!vol_tile(tc, tr)) continue;
            int up = 0, dn = 0;
            while (up < 8 && vol_tile(tc, tr - up - 1)) up++;
            while (dn < 64 && vol_tile(tc, tr + dn + 1)) dn++;
            int run = up + dn + 1;
            uint8_t id = world_tile(tc, tr) & 0x7F;
            int n = run < tile_vtiles[id] ? run : tile_vtiles[id];
            vt_tiles[j][i] = (int8_t)n;
            vt_h[j][i] = (int16_t)(n * tile_vunit[id]);
            vt_bottom[j][i] = (int16_t)(tr + dn);
        }
}
/* (pixel i, j of the chunk, -1 .. CH, to the table) */
#define VT(i, j) vt_tiles[((j) + 16) >> 4][((i) + 16) >> 4]
#define VB(i, j) vt_bottom[((j) + 16) >> 4][((i) + 16) >> 4]
#define VH(i, j) vt_h[((j) + 16) >> 4][((i) + 16) >> 4]

/* A wall of a volume, in 16-pixel bands: band k (from the ground up) wears
 * facade tile k (from the bottom up) of column `tc`, whose run's facade
 * ends at row `bottom`.  `a` runs along the wall from p0 to p1 (art
 * columns ca .. ca + run). */
static void vol_wall(Emit* e, int tc, int bottom, float lo, float hi, const float p0[3], const float p1[3],
                     int ca, int run, uint8_t shade) {
    for (int k = 0; GROUND_H + 16 * k < hi; k++) {
        float b0 = (float)(GROUND_H + 16 * k), b1 = b0 + 16;
        float y0 = lo > b0 ? lo : b0, y1 = hi < b1 ? hi : b1;
        if (y1 <= y0) continue;
        uint8_t id = world_tile(tc, bottom - k) & 0x7F;
        float u0 = (float)((id % 16) * 16 + ca), v0 = (float)((id / 16) * 16);
        float vt = v0 + (b1 - y1), vb = v0 + (b1 - y0);       /* art rows: band top = row 0 */
        const float p[4][3] = { { p0[0], y1, p0[2] }, { p1[0], y1, p1[2] }, { p1[0], y0, p1[2] }, { p0[0], y0, p0[2] } };
        const float uv[4][2] = { { u0 + UV_IN, vt + UV_IN }, { u0 + run - UV_IN, vt + UV_IN },
                                 { u0 + run - UV_IN, vb - UV_IN }, { u0 + UV_IN, vb - UV_IN } };
        emit_quad(e, p, uv, shade);
    }
}

FF1_API int ff1_chunk(int cx, int cz, Ff1Vertex* out, int max) {
    prepare_tiles();
    Emit e = { out, max, 0 };
    int x0 = cx * CH, z0 = cz * CH;
    distances(mdist, x0, z0, SH_MOUNTAIN);
    distances(cdist, x0, z0, SH_CANOPY);
    volume_tiles(cx * 16, cz * 16);
    for (int j = -1; j <= CH; j++)
        for (int i = -1; i <= CH; i++) {
            int md = mdist[j + MARGIN][i + MARGIN], cd = cdist[j + MARGIN][i + MARGIN];
            int top = NO_TOP;
            hbuf[j + 1][i + 1] = (int16_t)(VT(i, j) ? GROUND_H + VH(i, j)
                                         : in_body(x0 + i, z0 + j) ? body_h(x0 + i, z0 + j, &top)
                                         : md ? mountain_h(md)
                                         : cd ? canopy_h(cd, ci_at(x0 + i, z0 + j))
                                         : flat_h(x0 + i, z0 + j));
            tbuf[j + 1][i + 1] = top;
        }
#define H(i, j) hbuf[(j) + 1][(i) + 1]

    /* Tops: per tile, rectangles of equal height (greedy), textured with
     * the tile's pixels. */
    static uint8_t done[16][16];
    for (int tj = 0; tj < 16; tj++)
        for (int ti = 0; ti < 16; ti++) {
            memset(done, 0, sizeof(done));
            for (int py = 0; py < 16; py++)
                for (int px = 0; px < 16; px++) {
                    if (done[py][px]) continue;
                    int bx = ti * 16, bz = tj * 16, h = H(bx + px, bz + py);
                    if (VT(bx, bz)) {
                        /* A box's top: the facade's material, or a deeper
                         * tile's own art (a roof seen from above; on the
                         * overworld always the art). */
                        memset(done, 1, sizeof(done));
                        int tc = cx * 16 + ti, tr = cz * 16 + tj;
                        uint8_t id = world_tile(tc, tr) & 0x7F;
                        float u = (float)((id % 16) * 16), v = (float)((id / 16) * 16);
                        float X0 = (float)(x0 + bx), Z0 = (float)(z0 + bz);
                        const float p[4][3] = { { X0, h, Z0 }, { X0 + 16, h, Z0 }, { X0 + 16, h, Z0 + 16 }, { X0, h, Z0 + 16 } };
                        if (w_sm && (VB(bx, bz) - tr < VT(bx, bz) || pale_tile(id))) {   /* (overworld art is a top view) */
                            float mu, mv;
                            top_px(id, &mu, &mv);
                            const float uv[4][2] = { { mu, mv }, { mu, mv }, { mu, mv }, { mu, mv } };
                            emit_quad(&e, p, uv, 225);         /* (stone, not glare) */
                        } else {
                            const float uv[4][2] = { { u + UV_IN, v + UV_IN }, { u + 16 - UV_IN, v + UV_IN },
                                                     { u + 16 - UV_IN, v + 16 - UV_IN }, { u + UV_IN, v + 16 - UV_IN } };
                            emit_quad(&e, p, uv, 255);
                        }
                        continue;
                    }
                    uint8_t tid = world_tile(cx * 16 + ti, cz * 16 + tj) & 0x7F;
                    int fl = w_sm && tile_prop[tid] ? prop_floor(cx * 16 + ti, cz * 16 + tj) : -1;
                    if (fl >= 0 || (w_sm && tile_prop[tid] && ground_u >= 0)) {
                        /* An object's tile: the floor next to it (its card
                         * stands on it); else plain floor colour. */
                        memset(done, 1, sizeof(done));
                        float X0 = (float)(x0 + bx), Z0 = (float)(z0 + bz);
                        const float p[4][3] = { { X0, h, Z0 }, { X0 + 16, h, Z0 }, { X0 + 16, h, Z0 + 16 }, { X0, h, Z0 + 16 } };
                        if (fl >= 0) {
                            float u = (float)((fl % 16) * 16), v = (float)((fl / 16) * 16);
                            const float uv[4][2] = { { u + UV_IN, v + UV_IN }, { u + 16 - UV_IN, v + UV_IN },
                                                     { u + 16 - UV_IN, v + 16 - UV_IN }, { u + UV_IN, v + 16 - UV_IN } };
                            emit_quad(&e, p, uv, 255);
                        } else {
                            float u = ground_u + 0.5f, v = ground_v + 0.5f;
                            const float uv[4][2] = { { u, v }, { u, v }, { u, v }, { u, v } };
                            emit_quad(&e, p, uv, 255);
                        }
                        continue;
                    }
                    int32_t top = tbuf[bz + py + 1][bx + px + 1];
                    if (top != NO_TOP) {
                        /* A building's top: a strip down the column, all
                         * wearing the drawing's top row. */
                        int d = 1;
                        while (py + d < 16 && H(bx + px, bz + py + d) == h &&
                               tbuf[bz + py + d + 1][bx + px + 1] == top) d++;
                        for (int a = 0; a < d; a++) done[py + a][px] = 1;
                        float u, v;
                        atlas_uv(x0 + bx + px, top, &u, &v);
                        if (w_sm) {                    /* (no white or pale tops on a map) */
                            uint8_t tid2 = tile_at((x0 + bx + px) & WMASK, top & WMASK);
                            uint8_t col = tile_colour(tid2, ci_at(x0 + bx + px, top));
                            if ((col & 0x30) >= 0x20 && (col & 0x0F) < 0x0D) {
                                top_px(tid2, &u, &v);
                                u -= 0.5f; v -= 0.5f;
                            }
                        }
                        float X0 = (float)(x0 + bx + px), X1 = X0 + 1, Z0 = (float)(z0 + bz + py), Z1 = Z0 + d;
                        const float p[4][3] = { { X0, h, Z0 }, { X1, h, Z0 }, { X1, h, Z1 }, { X0, h, Z1 } };
                        const float uv[4][2] = { { u + UV_IN, v + 0.5f }, { u + 1 - UV_IN, v + 0.5f },
                                                 { u + 1 - UV_IN, v + 0.5f }, { u + UV_IN, v + 0.5f } };
                        emit_quad(&e, p, uv, 255);
                        continue;
                    }
                    int w = 1, d = 1;
                    while (px + w < 16 && !done[py][px + w] && H(bx + px + w, bz + py) == h) w++;
                    for (int ok = 1; ok && py + d < 16; ) {
                        for (int k = 0; k < w; k++)
                            if (done[py + d][px + k] || H(bx + px + k, bz + py + d) != h) { ok = 0; break; }
                        if (ok) d++;
                    }
                    for (int a = 0; a < d; a++)
                        for (int k = 0; k < w; k++) done[py + a][px + k] = 1;
                    float u, v;
                    atlas_uv(x0 + bx + px, z0 + bz + py, &u, &v);
                    float X0 = (float)(x0 + bx + px), X1 = X0 + w, Z0 = (float)(z0 + bz + py), Z1 = Z0 + d;
                    const float p[4][3] = { { X0, h, Z0 }, { X1, h, Z0 }, { X1, h, Z1 }, { X0, h, Z1 } };
                    const OWMetatile* tm = world_metatile(tid);
                    if (w_sm && tm && (tm->prop0 & 0x01) && tile_shape[tid] != SH_OCEAN && pale_tile(tid)) {
                        /* A wall drawn flat in white or pale colours: its
                         * material instead (no glaring tops). */
                        float mu, mv;
                        top_px(tid, &mu, &mv);
                        const float uv[4][2] = { { mu, mv }, { mu, mv }, { mu, mv }, { mu, mv } };
                        emit_quad(&e, p, uv, 225);
                        continue;
                    }
                    const float uv[4][2] = { { u + UV_IN, v + UV_IN }, { u + w - UV_IN, v + UV_IN },
                                             { u + w - UV_IN, v + d - UV_IN }, { u + UV_IN, v + d - UV_IN } };
                    emit_quad(&e, p, uv, 255);
                }
        }

    /* Walls between neighbouring pixels of different heights.  The chunk
     * owns the boundaries on its west and north edges and inside it; runs
     * along a boundary merge while both heights stay the same, within one
     * tile. */
    for (int x = 0; x < CH; x++)                 /* x-facing: between columns x-1 and x */
        for (int z = 0; z < CH; ) {
            int a = H(x - 1, z), b = H(x, z);
            if (a == b) { z++; continue; }
            int run = 1;
            while (z + run < CH && ((z + run) & 15) && H(x - 1, z + run) == a && H(x, z + run) == b) run++;
            int hi = a > b ? a : b, lo = a > b ? b : a;
            int hx = a > b ? x - 1 : x, in = a > b ? -1 : 1;     /* the higher column, inward */
            if (VT(hx, z)) {
                float X = (float)(x0 + x);
                const float p0[3] = { X, 0, (float)(z0 + z) }, p1[3] = { X, 0, (float)(z0 + z + run) };
                vol_wall(&e, (x0 + hx) >> 4, VB(hx, z), (float)lo, (float)hi, p0, p1, (z0 + z) & 15, run, 175);
                z += run;
                continue;
            }
            float u, v;
            side_uv(x0 + hx, z0 + z, in, 0, &u, &v);
            int solid = tile_shape[tile_at((x0 + hx) & WMASK, (z0 + z) & WMASK)] == SH_MOUNTAIN;
            float dv = solid ? 0 : run - 2 * UV_IN;      /* along the run: the pixels' column */
            float X = (float)(x0 + x), Z0 = (float)(z0 + z), Z1 = Z0 + run;
            const float p[4][3] = { { X, hi, Z0 }, { X, hi, Z1 }, { X, lo, Z1 }, { X, lo, Z0 } };
            const float v0 = solid ? v : v - 0.5f + UV_IN;
            const float uv[4][2] = { { u, v0 }, { u, v0 + dv }, { u, v0 + dv }, { u, v0 } };
            emit_quad(&e, p, uv, 175);
            z += run;
        }
    for (int z = 0; z < CH; z++)                 /* z-facing: between rows z-1 and z */
        for (int x = 0; x < CH; ) {
            int a = H(x, z - 1), b = H(x, z);
            if (a == b) { x++; continue; }
            int run = 1;
            while (x + run < CH && ((x + run) & 15) && H(x + run, z - 1) == a && H(x + run, z) == b) run++;
            int hi = a > b ? a : b, lo = a > b ? b : a;
            float X0 = (float)(x0 + x), X1 = X0 + run, Z = (float)(z0 + z);
            if (a > b) {
                /* Facing south: fold.  Height pixel k above the foot shows
                 * art row z-1-k, split where the rows cross into the tile
                 * above (each piece samples its own tile). */
                for (int k0 = 0; k0 < hi - lo; ) {
                    int r = z0 + z - 1 - k0, k1 = k0 + 1;
                    while (k1 < hi - lo && ((r - (k1 - k0)) & 15) != 15) k1++;   /* same tile row */
                    float u, vb;
                    atlas_uv(x0 + x, r, &u, &vb);                 /* row at the bottom of the piece */
                    float yb = (float)(lo + k0), yt = (float)(lo + k1);
                    float vt = vb - (float)(k1 - k0 - 1);         /* the piece's top row */
                    const float p[4][3] = { { X0, yt, Z }, { X1, yt, Z }, { X1, yb, Z }, { X0, yb, Z } };
                    const float uv[4][2] = { { u + UV_IN, vt + UV_IN }, { u + run - UV_IN, vt + UV_IN },
                                             { u + run - UV_IN, vb + 1 - UV_IN }, { u + UV_IN, vb + 1 - UV_IN } };
                    emit_quad(&e, p, uv, 215);
                    k0 = k1;
                }
            } else if (VT(x, z)) {
                const float p0[3] = { X0, 0, Z }, p1[3] = { X1, 0, Z };
                vol_wall(&e, (x0 + x) >> 4, VB(x, z), (float)lo, (float)hi, p0, p1, (x0 + x) & 15, run, 140);
            } else {
                /* Facing north: the material's colour along the run. */
                float u, v;
                side_uv(x0 + x, z0 + z, 0, 1, &u, &v);
                int solid = tile_shape[tile_at((x0 + x) & WMASK, (z0 + z) & WMASK)] == SH_MOUNTAIN;
                float du = solid ? 0 : run - 2 * UV_IN;
                const float u0 = solid ? u : u - 0.5f + UV_IN;
                const float p[4][3] = { { X0, hi, Z }, { X1, hi, Z }, { X1, lo, Z }, { X0, lo, Z } };
                const float uv[4][2] = { { u0, v }, { u0 + du, v }, { u0 + du, v }, { u0, v } };
                emit_quad(&e, p, uv, 140);
            }
            x += run;
        }
#undef H
    return e.count;
}

/* The point the camera follows: the party, in world pixels (not wrapped). */
FF1_API void ff1_focus(float* xyz) {
    prepare_tiles();
    int wx = g_ctx.player.tile_x * 16 + g_ctx.player.sub_x + 8;
    int wz = g_ctx.player.tile_y * 16 + g_ctx.player.sub_y + 8;
    xyz[0] = (float)wx;
    xyz[1] = (float)ground_h(wx, wz);
    if (g_ctx.player.vehicle == 8) xyz[1] += 18;     /* follow the airship up a little */
    xyz[2] = (float)wz;
}

/* The party's sprites: the 2x2 metasprite at screen x $70 (with the
 * vehicle's), standing at this y on foot (lut_VehicleSprY[1] + 1). */
#define PARTY_X      0x70
#define PARTY_FOOT_Y 0x6D
#define SHADOW_Y     0x70       /* DrawAirshipShadow's row */

/* This frame's sprites: the PPU's log, which also holds what the original
 * leaves out of the picture -- the party's lower half in a forest, the
 * airship's shadow on the frames it flickers off.  In 3D the canopy hides
 * the one, and the other stays on the ground. */
#define spr   g_ppu.sprites
#define spr_n g_ppu.sprite_count

/* Where this frame's standing sprites touch the ground (x, y, z, radius):
 * the soft shadows under them. */
#define MAX_SHADOWS 16
static float shadows[MAX_SHADOWS][4];
static int shadow_count;

static void add_shadow(float x, float y, float z, float r) {
    for (int k = 0; k < shadow_count; k++)
        if (shadows[k][0] == x && shadows[k][2] == z) return;    /* one per metasprite */
    if (shadow_count < MAX_SHADOWS) {
        float* s = shadows[shadow_count++];
        s[0] = x; s[1] = y; s[2] = z; s[3] = r;
    }
}

FF1_API int ff1_props(Ff1Vertex* out, int max, float yaw) {
    prepare_tiles();
    if (!w_sm) return 0;
    Emit e = { out, max, 0 };
    float rx = cosf(yaw), rz = -sinf(yaw);
    int ptx = g_ctx.player.tile_x, pty = g_ctx.player.tile_y;
    for (int ty = pty - 24; ty <= pty + 24; ty++)
        for (int tx = ptx - 24; tx <= ptx + 24; tx++) {
            uint8_t id = world_tile(tx, ty) & 0x7F;
            if (!tile_prop[id]) continue;
            float cx = tx * 16.0f + 8, cz = ty * 16.0f + 8;
            float top = GROUND_H + 16.0f, bot = GROUND_H;
            const float p[4][3] = { { cx - 8 * rx, top, cz - 8 * rz }, { cx + 8 * rx, top, cz + 8 * rz },
                                    { cx + 8 * rx, bot, cz + 8 * rz }, { cx - 8 * rx, bot, cz - 8 * rz } };
            float u = (float)((id % 16) * 16), v = (float)(ATLAS_CUTOUT_Y + (id / 16) * 16);
            const float uv[4][2] = { { u + UV_IN, v + UV_IN }, { u + 16 - UV_IN, v + UV_IN },
                                     { u + 16 - UV_IN, v + 16 - UV_IN }, { u + UV_IN, v + 16 - UV_IN } };
            emit_quad(&e, p, uv, 255);
            const OWMetatile* m = world_metatile(id);
            int spec = m ? m->prop0 & SM_TP_SPEC_MASK : 0;
            int door = spec == SM_TP_SPEC_DOOR || spec == SM_TP_SPEC_LOCKED ||
                       id == DOOR_CLOSED || id == DOOR_OPEN || id == DOOR_LOCKED;
            if (!door) add_shadow(cx, GROUND_H + 0.2f, cz, 7.0f);     /* (doors stand in walls) */
        }
    return e.count < max ? e.count : max;
}

FF1_API int ff1_shadows(float* out, int max) {
    int n = shadow_count < max ? shadow_count : max;
    memcpy(out, shadows, (size_t)n * 4 * sizeof(float));
    return n;
}

/* Is a sprite of metasprite `group` at (x, y)? */
static int logged_at(int group, int x, int y) {
    for (int o = 0; o < spr_n; o++)
        if (spr[o].group == group && spr[o].x == x && spr[o].y == y) return 1;
    return 0;
}

FF1_API int ff1_sprites(Ff1Vertex* out, int max, uint8_t* atlas, float yaw) {
    prepare_tiles();
    memset(atlas, 0, FF1_SPRITE_ATLAS * FF1_SPRITE_ATLAS * 4);
    /* The screen's top-left in world pixels (the party's tile is the
     * SCREEN_TILE'th on screen, scrolled by its sub-tile offset). */
    int ox = (g_ctx.player.tile_x - SCREEN_TILE) * 16 + g_ctx.player.sub_x;
    int oz = (g_ctx.player.tile_y - SCREEN_TILE) * 16 + g_ctx.player.sub_y;
    float rx = cosf(yaw), rz = -sinf(yaw);      /* the camera's right: sprites face it */
    int airship = g_ctx.player.vehicle == 8;

    /* The party group's top row (the vehicle's height on screen). */
    int party_top = 0x100;
    for (int s = 0; s < spr_n; s++) {
        const PpuSprite* sp = &spr[s];
        int shadow = airship && sp->y >= SHADOW_Y;
        if (sp->party && !shadow && sp->y < party_top) party_top = sp->y;
    }

    Emit e = { out, max, 0 };
    shadow_count = 0;
    for (int s = 0; s < spr_n; s++) {
        const PpuSprite* sp = &spr[s];
        const uint8_t* t = Ppu_ChrTile((ChrBank)sp->bank, sp->tile);
        if (!t) continue;
        int ax = (s % 8) * 8, ay = (s / 8) * 8;
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                int sr = (sp->flags & PPU_FLIP_Y) ? 7 - r : r, sc = (sp->flags & PPU_FLIP_X) ? 7 - c : c;
                int ci = chr_pixel(t, sr, sc);
                uint8_t* p = atlas + ((ay + r) * FF1_SPRITE_ATLAS + ax + c) * 4;
                if (ci) nes_rgba(g_ppu.palette[16 + (sp->palette & 3) * 4 + ci], p);
            }
        /* (in sprite-atlas texels, scaled to the terrain atlas's units) */
        const float k = (float)FF1_ATLAS_W / FF1_SPRITE_ATLAS, kv = (float)FF1_ATLAS_H / FF1_SPRITE_ATLAS;
        const float uv[4][2] = { { ax * k, ay * kv }, { (ax + 8) * k, ay * kv },
                                 { (ax + 8) * k, (ay + 8) * kv }, { ax * k, (ay + 8) * kv } };

        int party = sp->party;
        if (party && airship && sp->y >= SHADOW_Y) {
            /* The airship's shadow lies on the ground under it. */
            float x0 = (float)(ox + sp->x), z0 = (float)(oz + sp->y);
            float h = (float)ground_h(ox + PARTY_X + 8, oz + SHADOW_Y + 8) + 0.25f;
            const float p[4][3] = { { x0, h, z0 }, { x0 + 8, h, z0 }, { x0 + 8, h, z0 + 8 }, { x0, h, z0 + 8 } };
            emit_quad(&e, p, uv, 255);
            continue;
        }

        /* Where it stands: the line below its column of sprites (the
         * bottom of its metasprite) -- for the party, its own tile, the
         * vehicle as high (or low) as it's drawn above the walking party. */
        int ground = sp->y + 8;
        for (int n = 0; n < 4 && logged_at(sp->group, sp->x, ground); n++) ground += 8;
        float lift = 0;
        if (party) lift = (float)(PARTY_FOOT_Y - party_top);
        /* It turns about its metasprite's centre. */
        int pivot = logged_at(sp->group, sp->x + 8, sp->y) ? sp->x + 8
                  : logged_at(sp->group, sp->x - 8, sp->y) ? sp->x : sp->x + 4;
        int foot = party ? PARTY_FOOT_Y + 16 : ground;  /* the metasprite's foot on screen */
        float cx = (float)(ox + pivot), cz = (float)(oz + foot) - 8.0f;   /* mid-tile */
        float base = (float)ground_h(ox + pivot, oz + foot - 8) + lift;
        /* A soft shadow under what stands on land (not the airship, which
         * has its own, nor boats on the water). */
        if (!(party && airship) && base - lift > 1)
            add_shadow(cx, base - lift + 0.2f, cz, pivot == sp->x + 4 ? 4.0f : 7.0f);
        int above = party ? (party_top + 16) - sp->y : ground - sp->y;   /* foot to sprite top */
        float top = base + (float)above, bot = top - 8;
        float d0 = (float)(sp->x - pivot), d1 = d0 + 8;
        const float p[4][3] = { { cx + d0 * rx, top, cz + d0 * rz }, { cx + d1 * rx, top, cz + d1 * rz },
                                { cx + d1 * rx, bot, cz + d1 * rz }, { cx + d0 * rx, bot, cz + d0 * rz } };
        emit_quad(&e, p, uv, 255);
    }
    return e.count < max ? e.count : max;
}

/* ---- Reference pictures of the overworld's landmarks ----
 *
 * For modelling them (in a voxel editor, say): each landmark's tiles as
 * the game draws them, 1 pixel = 1 overworld pixel.  A landmark is a
 * group of connected tiles that are buildings (blocked to everything but
 * the mountains, or in the building profile) or entrances. */

#define REF_MAX 40
static struct { int tx0, ty0, tx1, ty1, entrance; } ref[REF_MAX];
static int ref_count;
static uint8_t ref_member[256][256];

static int ref_is_landmark(int tx, int ty) {
    uint8_t id = OWMap_GetTileID(tx & 0xFF, ty & 0xFF) & 0x7F;
    const OWMetatile* m = OWMap_GetMetatile(id);
    if (!m) return 0;
    for (unsigned k = 0; k < sizeof(mountain_ids); k++)
        if (mountain_ids[k] == id) return 0;
    if ((m->prop0 & 0x0F) == 0x0F) return 1;
    if (!(m->prop0 & 0x01) && (m->prop1 & 0x80)) return 1;
    for (unsigned k = 0; k < sizeof(building_profile) / sizeof(building_profile[0]); k++)
        if (building_profile[k].id == id) return 1;
    return 0;
}

/* Find them.  @return how many. */
FF1_API int ff1_landmark_count(void) {
    static int qx[4096], qy[4096];
    memset(ref_member, 0, sizeof(ref_member));
    ref_count = 0;
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            if (ref_member[y][x] || !ref_is_landmark(x, y) || ref_count == REF_MAX) continue;
            int n = 0, h = 0, *r0 = &ref[ref_count].tx0;
            r0[0] = r0[2] = x; r0[1] = r0[3] = y;
            ref[ref_count].entrance = -1;
            qx[n] = x; qy[n++] = y; ref_member[y][x] = 1;
            while (h < n) {
                int cx = qx[h], cy = qy[h++];
                const OWMetatile* m = OWMap_GetMetatile(OWMap_GetTileID(cx, cy) & 0x7F);
                if (m && !(m->prop0 & 1) && (m->prop1 & 0x80)) {      /* an entrance: the lowest names it */
                    int ent = m->prop1 & 0x7F;
                    if (ref[ref_count].entrance < 0 || ent < ref[ref_count].entrance) ref[ref_count].entrance = ent;
                }
                if (cx < r0[0]) r0[0] = cx;
                if (cy < r0[1]) r0[1] = cy;
                if (cx > r0[2]) r0[2] = cx;
                if (cy > r0[3]) r0[3] = cy;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = (cx + dx) & 255, ny = (cy + dy) & 255;
                        if (!ref_member[ny][nx] && ref_is_landmark(nx, ny) && n < 4096) {
                            ref_member[ny][nx] = 1;
                            qx[n] = nx; qy[n++] = ny;
                        }
                    }
            }
            ref_count++;
        }
    return ref_count;
}

/* Landmark i: its first tile, size in tiles and lowest entrance id. */
FF1_API void ff1_landmark_info(int i, int* out) {
    out[0] = ref[i].tx0; out[1] = ref[i].ty0;
    out[2] = ref[i].tx1 - ref[i].tx0 + 1; out[3] = ref[i].ty1 - ref[i].ty0 + 1;
    out[4] = ref[i].entrance;
}

/* Landmark i's picture (w*16 x h*16 RGBA) in the current palette: its own
 * tiles as drawn, other tiles in its rectangle dimmed and half clear. */
FF1_API void ff1_landmark_pixels(int i, uint8_t* rgba) {
    int w = ref[i].tx1 - ref[i].tx0 + 1, h = ref[i].ty1 - ref[i].ty0 + 1;
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++) {
            int tx = (ref[i].tx0 + col) & 255, ty = (ref[i].ty0 + row) & 255;
            const OWMetatile* m = OWMap_GetMetatile(OWMap_GetTileID(tx, ty) & 0x7F);
            if (!m) continue;
            const uint8_t q[4] = { m->ul, m->ur, m->dl, m->dr };
            int pal = m->attr & 3, own = ref_member[ty][tx];
            for (int k = 0; k < 4; k++) {
                const uint8_t* t = Ppu_ChrTile(CHR_BANK_02, q[k]);
                if (!t) continue;
                for (int r = 0; r < 8; r++)
                    for (int c = 0; c < 8; c++) {
                        int ci = chr_pixel(t, r, c);
                        int x = col * 16 + (k & 1) * 8 + c, y = row * 16 + (k >> 1) * 8 + r;
                        uint8_t* p = rgba + (y * w * 16 + x) * 4;
                        nes_rgba(g_ppu.palette[ci ? pal * 4 + ci : 0], p);
                        if (!own) { p[0] /= 2; p[1] /= 2; p[2] /= 2; p[3] = 96; }
                    }
            }
        }
}
