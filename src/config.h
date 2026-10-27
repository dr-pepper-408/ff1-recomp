#pragma once
#include <stdint.h>

// ---- Compile-time defaults ----
#define DEFAULT_SCALE          3
#define DEFAULT_FULLSCREEN     0
#define WINDOW_TITLE           "FF1-recomp"

// ---- Debug party (set to 1 to seed a level-10 party with items, equipment,
//      magic and gold on new game, for exercising the menus; 0 for the normal
//      level-1 starting party). ----
#define FF1_DEBUG_STATE        1

// NTSC NES frame rate (60.0988 Hz), as nanoseconds per frame.  Game logic
// advances in whole frames of this length (fixed timestep in game.c).
#define NES_FRAME_NS          16639267ULL
#define MAX_FRAMES_PER_LOOP   4

// NES native resolution
#define NES_SCREEN_W          256
#define NES_SCREEN_H          240

// Tile dimensions in NES pixels
#define TILE_SIZE             8
#define TILE_SIZE_16          16   // metatile (2x2 tiles)

// CHR tileset: 256 tiles, 16 tiles per row in the PNG
#define CHR_TILES_PER_ROW     16
#define CHR_TILES_PER_COL     16
#define CHR_TILE_COUNT        256

// Texture atlas: all 256 tiles laid out in a single GPU texture
#define ATLAS_WIDTH_PX        (CHR_TILES_PER_ROW * TILE_SIZE)   // 128
#define ATLAS_HEIGHT_PX       (CHR_TILES_PER_COL * TILE_SIZE)   // 128

// ---- Runtime configuration struct ----
typedef struct {
    int scale;          // 1-4, pixel scale factor
    int fullscreen;     // 0 = windowed, 1 = fullscreen
    int window_w;       // computed: NES_SCREEN_W * scale
    int window_h;       // computed: NES_SCREEN_H * scale
} Config;

// Global config instance (defined in main.c)
extern Config g_config;

// Apply config after changes (recreate window if needed)
void Config_Apply(Config* cfg);