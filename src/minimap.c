/**
 * @file minimap.c
 * @brief The overworld minimap: a port of bank_09's EnterMinimap,
 *        Minimap_PrepRow / _DrawRows, the decoration and title drawing,
 *        Minimap_YouAreHere, Minimap_FillNTPal and MinimapFrame.
 *
 * The BG pattern table is the canvas: 256 tiles of 8x8 pixels make a
 * 128x128 picture, one pixel per 2x2 overworld tiles (lut_MinimapTileset's
 * colors ORed together), and a fixed nametable (lut_MinimapNT) puts those
 * tiles on screen.  The picture is written a tile row at a time, one pixel
 * row per frame; then the dragon decorations and the title are written
 * over all-water tiles, one byte of each tile per frame.  Sprites (the
 * blinking "you are here" and the town markers) show once that's done.
 */

#include <string.h>
#include "minimap.h"
#include "ppu.h"
#include "overworld_map.h"
#include "sfx.h"
#include "../data/generated/minimap_data.h"

typedef enum { P_MAP, P_DECOR, P_TITLE, P_WAIT } Phase;

static uint8_t chr[0x1000 + 0x20];  /* the BG pattern table, then sprite tiles $80-$81 */
static uint8_t drawbuf[0x100];      /* mm_drawbuf: 8 pixel rows of 16 tiles */
static uint8_t oam[0x100];
static uint8_t sprindex;
static Phase phase;
static int batch, pixrow, frame;
static uint8_t framecounter;
static uint8_t pal_you, pal_towns;  /* cur_pal+$11, +$12 */

/* DrawDungeonSprite: a town marker at minimap pixel (x, y).  The sprite
 * index wraps, so a 64th marker would land on the "you are here" sprite. */
static void dungeon_sprite(int x, int y) {
    sprindex = (uint8_t)(sprindex + 4);
    oam[sprindex + 0] = (uint8_t)(y + 0x34);
    oam[sprindex + 1] = 0x81;
    oam[sprindex + 2] = 0x00;
    oam[sprindex + 3] = (uint8_t)(x + 0x3D);
}

/* Minimap_PrepRow, 8 times: pixel rows 8*batch .. +7 (map rows twice
 * that) into drawbuf, with their town markers. */
static void prep_batch(void) {
    for (int r = 0; r < 8; r++) {
        int y = batch * 8 + r, my = y * 2;
        for (int t = 0; t < 16; t++) {
            uint8_t lo = 0, hi = 0;
            for (int b = 0; b < 8; b++) {
                int mx = (t * 8 + b) * 2;
                uint8_t a = lut_MinimapTileset[OWMap_GetTileID(mx, my) & 0x7F] |
                            lut_MinimapTileset[OWMap_GetTileID(mx + 1, my) & 0x7F] |
                            lut_MinimapTileset[OWMap_GetTileID(mx, my + 1) & 0x7F] |
                            lut_MinimapTileset[OWMap_GetTileID(mx + 1, my + 1) & 0x7F];
                lo = (uint8_t)((lo << 1) | (a & 1));
                hi = (uint8_t)((hi << 1) | ((a >> 1) & 1));
                if (a & 4) dungeon_sprite(mx / 2, y);
            }
            drawbuf[t * 16 + r] = lo;
            drawbuf[t * 16 + 8 + r] = hi;
        }
    }
}

void Minimap_Open(int party_x, int party_y, uint8_t fc) {
    /* Minimap_YouAreHere: OAM cleared, sprite 0 at the party's position. */
    memset(oam, 0xF8, sizeof(oam));
    sprindex = 0;
    oam[1] = 0x80;
    oam[2] = 0x00;
    oam[3] = (uint8_t)(((party_x & 0xFF) >> 1) + 0x3D);
    oam[0] = (uint8_t)(((party_y & 0xFF) >> 1) + 0x34);
    /* Minimap_FillNTPal: an empty canvas, the sprite tiles, both marker
     * colors black. */
    memset(chr, 0, 0x1000);
    memcpy(chr + 0x1000, lut_MinimapSprCHR, 0x20);
    Ppu_SetRamChr(chr, 0x102);
    pal_you = pal_towns = 0x0F;
    framecounter = fc;
    phase = P_MAP;
    batch = pixrow = 0;
}

int Minimap_Update(const InputState* in) {
    if (phase != P_WAIT) Sfx_MinimapDraw();     /* the drawing buzz */
    switch (phase) {
    case P_MAP:                                     /* Minimap_DrawRows */
        if (pixrow == 0) prep_batch();
        for (int t = 0; t < 16; t++) {
            chr[batch * 0x100 + t * 16 + pixrow] = drawbuf[t * 16 + pixrow];
            chr[batch * 0x100 + t * 16 + 8 + pixrow] = drawbuf[t * 16 + 8 + pixrow];
        }
        if (++pixrow == 8) {
            pixrow = 0;
            if (++batch == 16) { phase = P_DECOR; frame = 0; }
        }
        break;
    case P_DECOR:                                   /* Minimap_DrawDecorCHR */
    case P_TITLE: {                                 /* Minimap_DrawTitleCHR */
        int decor = phase == P_DECOR, n = decor ? 0x30 : 0x28;
        const uint8_t* src = decor ? chr_MinimapDecor : chr_MinimapTitle;
        const uint8_t* lo = decor ? lut_MinimapDecorCHRDest_lo : lut_MinimapTitleCHRDest_lo;
        const uint8_t* hi = decor ? lut_MinimapDecorCHRDest_hi : lut_MinimapTitleCHRDest_hi;
        int b = lut_MinimapBitplane[frame];
        for (int i = 0; i < n; i++)
            chr[((hi[i] << 8) | lo[i]) + b] = src[i * 16 + b];
        if (++frame == 16) {
            frame = 0;
            phase = decor ? P_TITLE : P_WAIT;
        }
        break;
    }
    case P_WAIT:                                    /* MinimapFrame */
        framecounter++;
        pal_you = (framecounter & 0x08) ? 0x0F : 0x37;    /* "you are here" blinks */
        pal_towns = (uint8_t)(framecounter & 0x30);       /* towns: $00 $10 $20 $0F */
        if (pal_towns == 0x30) pal_towns = 0x0F;
        if (in->pressed & (BTN_A | BTN_B)) return 1;
        break;
    }
    return 0;
}

void Minimap_Draw(void) {
    for (int i = 0; i < 4; i++) Ppu_SetPalette(i, lut_MinimapBGPal + i * 4);
    uint8_t spr[4] = { lut_MinimapBGPal[0], pal_you, pal_towns, 0x0F };
    Ppu_SetPalette(4, spr);
    Ppu_Clear(0);
    /* The scroll is $00,$E8: the picture sits 8 lines lower, the
     * nametable's last row wrapping to the top. */
    for (int y = 0; y < 30; y++)
        for (int x = 0; x < 32; x++) {
            uint8_t a = lut_MinimapNT[0x3C0 + (y / 4) * 8 + x / 4];
            int pal = (a >> (((y % 4) / 2) * 4 + ((x % 4) / 2) * 2)) & 3;
            int sy = y * 8 + 8;
            if (sy >= 240) sy -= 240;
            Ppu_DrawBgTile(CHR_BANK_RAM, lut_MinimapNT[y * 32 + x], x * 8, sy, pal);
        }
    if (phase != P_WAIT) return;                    /* sprites only once MinimapFrame runs */
    for (int i = 63; i >= 0; i--) {                 /* earlier sprites in front */
        const uint8_t* s = &oam[i * 4];
        if (s[0] >= 0xEF) continue;
        Ppu_DrawSprite(CHR_BANK_RAM, 0x100 + (s[1] - 0x80), s[3], s[0] + 1, s[2] & 3, s[2] & 0xC0);
    }
}
