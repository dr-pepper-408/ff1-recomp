#pragma once
#include <stdint.h>

/*
 * ff1core.h -- the game as a library, for front ends other than main.c
 * (the LOVE front end in love/ loads it through LuaJIT's FFI).
 *
 * One call to ff1_frame() is one NES frame of game logic and audio;
 * ff1_draw() renders the current state.  The 3D view reads the overworld
 * around the party as vertex arrays (Ff1Vertex, matching LOVE's mesh format
 * position float3, texcoord float2, color byte4).
 */

#ifdef _WIN32
#define FF1_API __declspec(dllexport)
#else
#define FF1_API __attribute__((visibility("default")))
#endif

typedef struct {
    float x, y, z;          ///< World pixels: x east, y up, z south
    float u, v;
    uint8_t r, g, b, a;
} Ff1Vertex;

#define FF1_ATLAS_W 256     ///< Metatile atlas: 16 x 8 tiles of 16x16, then
#define FF1_ATLAS_H 256     ///< their cut-out copy (the floor colour clear)
#define FF1_SPRITE_ATLAS 64 ///< Sprite atlas: 8 x 8 sprites of 8x8

/** Load the player's ROM of Final Fantasy (USA) and fill the game's tables
 *  from it; before ff1_init.  @return 0 if it's ours, else a status for
 *  ff1_rom_message. */
FF1_API int ff1_load_rom(const char* path);

/** A message for the player about a ff1_load_rom status. */
FF1_API const char* ff1_rom_message(int status);

/** Start the game (after ff1_load_rom).  `save_path` is the SRAM file;
 *  `new_game` skips the title screen.  @return 0 on success. */
FF1_API int ff1_init(const char* save_path, int new_game);
FF1_API void ff1_quit(void);

/** One NES frame with the buttons held (BTN_* bits, input.h). */
FF1_API void ff1_frame(int buttons);

/** The audio of the last frame (44100 Hz mono).  @return samples written. */
FF1_API int ff1_audio(int16_t* out, int max);

/** Draw the current state into `rgba` (256x240, 4 bytes per pixel). */
FF1_API void ff1_draw(uint8_t* rgba);

/* ---- The 3D view (valid after ff1_draw) ---- */

/** 1 when the 3D view can show the frame: walking (the overworld or a
 *  standard map), or talking there. */
FF1_API int ff1_view3d(void);

/** The frame's screen effects (valid after ff1_draw), for the 3D view to
 *  apply its own way: visible rows, emphasis tint, monochrome, shake (see
 *  ff1core.c for the 12 values). */
FF1_API void ff1_view3d_fx(float* out);

/** What of a picture from ff1_draw goes over the 3D view (the dialogue
 *  box): everything else is made transparent.  @return 0 when nothing does. */
FF1_API int ff1_view3d_overlay(uint8_t* rgba);

/** The world the chunks are of: its size in tiles (256 for the overworld,
 *  64 for a standard map), and a key that changes when the world does
 *  (another map, tileset or palette): drop the cached chunks then. */
FF1_API uint32_t ff1_world(int* tiles);

/** The overworld is meshed in 16 x 16 chunks of 16 x 16 tiles (world
 *  pixels: x east, z south, the map wrapping every 4096).  The key changes
 *  when the chunk's tiles do. */
FF1_API uint32_t ff1_chunk_key(int cx, int cz);

/** Chunk (cx, cz) as voxels: a height per pixel, faces merged, in world
 *  pixels.  @return the vertices it needs (only `max` are written). */
FF1_API int ff1_chunk(int cx, int cz, Ff1Vertex* out, int max);

/** The 128 overworld metatiles in the current palette (FF1_ATLAS_W x H). */
FF1_API void ff1_terrain_atlas(uint8_t* rgba);

/** The point the camera follows (the party), in world pixels. */
FF1_API void ff1_focus(float* xyz);

/** The frame's sprites as upright quads standing on the terrain, turned
 *  to face a camera at `yaw` (radians), and their pixels
 *  (FF1_SPRITE_ATLAS square).  Texture coordinates are scaled as the
 *  terrain atlas's (so one shader serves both).  @return vertices written. */
FF1_API int ff1_sprites(Ff1Vertex* out, int max, uint8_t* atlas, float yaw);

/** A standard map's objects (trees, fences, chests, doors...) as 2D cards
 *  like the characters: each object's tile, its floor colour cut away,
 *  standing on its tile and turned to face a camera at `yaw`; textured
 *  from the terrain atlas.  Call after ff1_sprites (their shadows join
 *  the sprites').  @return vertices written. */
FF1_API int ff1_props(Ff1Vertex* out, int max, float yaw);

/** Where the sprites of the last ff1_sprites call stand on land, for soft
 *  shadows: (x, y, z, radius) each.  @return how many were written. */
FF1_API int ff1_shadows(float* out, int max);

/* ---- The battle diorama (valid after ff1_draw) ---- */

#define FF1_BATTLE_ATLAS 512

/** 1 during a battle. */
FF1_API int ff1_battle3d(void);

/** The battle as a stage: the backdrop wall and floor, the enemies and the
 *  sprites as 2D cutouts facing a camera whose right is (rx, 0, rz), and
 *  their pixels (FF1_BATTLE_ATLAS square).  Stage axes: x across (screen
 *  y), z depth (screen x: the enemies far, the party near), y up.  `info`
 *  (11 floats) gets the screen shake (x, y), the magic flash's colour (r,
 *  g, b, or -1), 1 for a boss fight, and the enemies' extent on the stage
 *  (across min, max, depth min, max, tallest height).  @return vertices
 *  written. */
FF1_API int ff1_battle_scene(Ff1Vertex* out, int max, uint8_t* atlas, float rx, float rz, float* info);

/** The party's rows on a diagonal: each row `s` stage pixels further back
 *  than the one above (negative: forward).  Set it to fan the party out
 *  as seen from the camera's side. */
FF1_API void ff1_battle_stagger(float s);

/** Clear the stage area of a picture from ff1_draw, leaving the menus and
 *  the status column to draw over the diorama. */
FF1_API void ff1_battle_overlay(uint8_t* rgba);

/** For screenshots and testing: put the party on overworld tile (tx, ty),
 *  in `vehicle` (1 foot, 2 canoe, 4 ship, 8 airship; 0 keeps it). */
FF1_API void ff1_debug_place(int tx, int ty, int vehicle);

/** For screenshots and testing: start battle `formation` (no transition),
 *  with the backdrop of the party's tile. */
FF1_API void ff1_debug_battle(int formation);

/** For screenshots and testing: go through overworld entrance `id` (a
 *  town, castle or dungeon), as stepping on it does. */
FF1_API void ff1_debug_enter(int id);

/** For screenshots and testing: as if the party had come through a door. */
FF1_API void ff1_debug_inroom(void);

/** For screenshots and testing: show dialogue `id`, as talking does. */
FF1_API void ff1_debug_dialogue(int id);

/** Random encounters on (as the original) or off; fixed battles still happen. */
FF1_API void ff1_set_encounters(int on);

/** Class `cls`'s standing battle sprite (DrawSimple2x3Sprite), 16 x 24
 *  RGBA, colour 0 clear -- drawn from the ROM (call after ff1_init). */
FF1_API void ff1_class_sprite(int cls, uint8_t* rgba);

/* ---- Reference pictures of the overworld's landmarks (for modelling) ---- */

/** Find the overworld's landmarks (towns, castles, caves, the temple, the
 *  tower: connected building and entrance tiles).  @return how many. */
FF1_API int ff1_landmark_count(void);

/** Landmark i: first tile x, y; width, height in tiles; lowest entrance id. */
FF1_API void ff1_landmark_info(int i, int* out);

/** Landmark i's picture, (w * 16) x (h * 16) RGBA, 1 pixel = 1 map pixel,
 *  in the current palette (call while the overworld is drawn); tiles in
 *  its rectangle that aren't its own are dimmed and half clear. */
FF1_API void ff1_landmark_pixels(int i, uint8_t* rgba);
