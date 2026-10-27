#pragma once
#include <stdint.h>

/*
 * ppu.h — Indexed frame buffer modelled on the NES PPU (no SDL dependency).
 *
 * Game code draws 8x8 CHR tiles into a 256x240 buffer of palette-RAM indices
 * (0-31) and loads NES colors into a 32-entry palette RAM, exactly as FF1
 * writes cur_pal: entries 0-15 are the four background palettes, 16-31 the
 * four sprite palettes.  The platform layer turns the buffer into RGB once per
 * frame (Ppu_ToRGBA), so palette effects — fades, flashes, cycling — are just
 * palette writes, as on the NES.
 *
 * Background tiles are opaque, and their color 0 shows the universal
 * background color (palette entry 0), as on the NES.  Sprite color 0 is
 * transparent.
 */

#define PPU_WIDTH   256
#define PPU_HEIGHT  240

/** Palette RAM indices of the border palette (BG palette 3, see
 *  LoadBorderPalette_Blue/_Black): black, grey, box fill, white. */
#define PPU_BORDER_BLACK  12
#define PPU_BORDER_GREY   13
#define PPU_BORDER_FILL   14
#define PPU_BORDER_WHITE  15

/** Sprite flags (OAM attribute bits). */
#define PPU_FLIP_X  0x40
#define PPU_FLIP_Y  0x80

/** CHR banks compiled in from the ROM (chr_data.h). */
typedef enum {
    CHR_BANK_02,   ///< BANK_MAPCHR: overworld tiles, mapman ($9000), map objects.
    CHR_BANK_03,   ///< BANK_TILESETCHR: standard-map tilesets (128 tiles each).
    CHR_BANK_07,   ///< BANK_BATTLECHR: backdrops + enemy pages 0-7.
    CHR_BANK_08,   ///< Backdrops + enemy pages 8-15.
    CHR_BANK_09,   ///< BANK_MENUCHR: font ($8800 = tile $80), battle sprites ($9000).
    CHR_BANK_ORB,  ///< lut_OrbCHR: the menu's orb tiles ($60-$7F as tiles 0-31).
    CHR_BANK_BRIDGE, ///< data_BridgeCHR: the bridge scene's tiles $00-$7F.
    CHR_BANK_RAM,  ///< CHR-RAM drawn at run time (the minimap), see Ppu_SetRamChr.
    CHR_BANK_COUNT
} ChrBank;

/** Debug overlay tints, blended over the picture by Ppu_ToRGBA. */
enum { PPU_OVERLAY_NONE = 0, PPU_OVERLAY_GREEN, PPU_OVERLAY_RED };

/** The frame buffer and palette RAM. */
/** A sprite as drawn (Ppu_DrawSprite's arguments), for renderers that
 *  place sprites themselves (the 3D view). */
typedef struct {
    int16_t bank, tile, x, y;
    uint8_t palette, flags;
    uint8_t hidden;         ///< logged by Ppu_LogSprite, not drawn
    uint8_t group;          ///< the metasprite it is part of (Ppu_SpriteGroup)
    uint8_t party;          ///< part of the party's (or its vehicle's) picture
} PpuSprite;

#define PPU_SPRITE_LOG_MAX 64

typedef struct {
    uint8_t pixels[PPU_HEIGHT][PPU_WIDTH];   ///< Palette RAM index per pixel.
    uint8_t palette[32];                     ///< NES color per palette entry.
    uint8_t overlay[PPU_HEIGHT][PPU_WIDTH];  ///< Debug tint (PPU_OVERLAY_*).
    int clip_x0, clip_y0, clip_x1, clip_y1;  ///< Drawing clip rectangle.
    int bg_top, bg_rows;    ///< Rows outside [bg_top, bg_top+bg_rows) show the
                            ///< backdrop color, as with BG rendering switched
                            ///< off ($2001) during FF1's screen wipe.
    uint8_t emphasis;       ///< Color emphasis bits ($2001 bits 5-7 >> 5):
                            ///< 1 = red, 2 = green, 4 = blue.
    int16_t mono_x0[PPU_HEIGHT];   ///< Monochrome ($2001 bit 0) from this x...
    int16_t mono_x1[PPU_HEIGHT];   ///< ...up to (not including) this x, per line.
    int8_t  shake_x;        ///< Horizontal scroll offset of the whole picture.
    int8_t  shake_y;        ///< Vertical scroll offset of the whole picture.
    PpuSprite sprites[PPU_SPRITE_LOG_MAX];  ///< Sprites drawn since Ppu_Clear...
    int sprite_count;                       ///< ...and how many.
} Ppu;

extern Ppu g_ppu;

/** Fill the whole buffer with palette entry `index`, and reset the overlay,
 *  clip rectangle, visible BG rows, emphasis, monochrome and shake. */
void Ppu_Clear(uint8_t index);

/** Load the full 32-entry palette RAM. */
void Ppu_LoadPalettes(const uint8_t nes[32]);

/** Load one 4-color palette: 0-3 background, 4-7 sprite. */
void Ppu_SetPalette(int palette, const uint8_t nes[4]);

/** Restrict drawing to a rectangle (Ppu_ResetClip restores the full screen). */
void Ppu_SetClip(int x, int y, int w, int h);
void Ppu_ResetClip(void);

/** 16 bytes of 2bpp data for tile `tile` of a CHR bank (NULL if out of range). */
const uint8_t* Ppu_ChrTile(ChrBank bank, int tile);

/** Point CHR_BANK_RAM at `tiles` tiles of CHR the caller draws into. */
void Ppu_SetRamChr(const uint8_t* data, int tiles);

/** Draw an opaque background tile with BG palette 0-3. */
void Ppu_DrawBgTile(ChrBank bank, int tile, int x, int y, int palette);

/** Draw a sprite tile with sprite palette 0-3 (color 0 transparent). */
void Ppu_DrawSprite(ChrBank bank, int tile, int x, int y, int palette, int flags);

/** The sprites drawn from here on make up one metasprite (a character, an
 *  NPC, a vehicle); `party`: it is the party's.  For the 3D view, which
 *  stands each metasprite up as one cutout. */
void Ppu_SpriteGroup(int party);

/** Log a sprite the original leaves out (the party's lower half in a
 *  forest) without drawing it: the 3D view hides it its own way. */
void Ppu_LogSprite(ChrBank bank, int tile, int x, int y, int palette, int flags);

/** Fill a rectangle with palette entry `index`. */
void Ppu_FillRect(int x, int y, int w, int h, uint8_t index);

/** Set one pixel to palette entry `index` (clipped). */
void Ppu_SetPixel(int x, int y, uint8_t index);

/** Show scanline `y` in monochrome from x0 up to x1 (clipped). */
void Ppu_SetMonochrome(int y, int x0, int x1);

/** Tint a rectangle in the debug overlay. */
void Ppu_OverlayRect(int x, int y, int w, int h, uint8_t tint);

/** Convert the frame to 32-bit pixels: each output pixel is
 *  (r << 24) | (g << 16) | (b << 8) | 0xFF (RGBA8888 as an integer),
 *  using the NES master palette.  `pitch_px` is the output row stride. */
void Ppu_ToRGBA(uint32_t* out, int pitch_px);
