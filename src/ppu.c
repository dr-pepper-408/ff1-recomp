/**
 * @file ppu.c
 * @brief Indexed frame buffer + palette RAM (see ppu.h).
 */

#include "ppu.h"
#include <string.h>
#include "../data/generated/chr_data.h"
#include "../data/generated/palette_data.h"

Ppu g_ppu;

static struct { const uint8_t* data; int tiles; } g_chr[CHR_BANK_COUNT] = {
    { chr_bank_02, (int)sizeof(chr_bank_02) / 16 },
    { chr_bank_03, (int)sizeof(chr_bank_03) / 16 },
    { chr_bank_07, (int)sizeof(chr_bank_07) / 16 },
    { chr_bank_08, (int)sizeof(chr_bank_08) / 16 },
    { chr_bank_09, (int)sizeof(chr_bank_09) / 16 },
    { chr_orbs,    (int)sizeof(chr_orbs) / 16 },
    { chr_bridge,  (int)sizeof(chr_bridge) / 16 },
    { NULL, 0 },                                /* CHR_BANK_RAM */
};

void Ppu_ResetClip(void) {
    g_ppu.clip_x0 = 0;
    g_ppu.clip_y0 = 0;
    g_ppu.clip_x1 = PPU_WIDTH;
    g_ppu.clip_y1 = PPU_HEIGHT;
}

void Ppu_SetClip(int x, int y, int w, int h) {
    g_ppu.clip_x0 = x < 0 ? 0 : x;
    g_ppu.clip_y0 = y < 0 ? 0 : y;
    g_ppu.clip_x1 = (x + w > PPU_WIDTH) ? PPU_WIDTH : x + w;
    g_ppu.clip_y1 = (y + h > PPU_HEIGHT) ? PPU_HEIGHT : y + h;
}

static uint8_t s_group, s_party;

void Ppu_SpriteGroup(int party) {
    s_group++;
    s_party = (uint8_t)(party != 0);
}

void Ppu_Clear(uint8_t index) {
    s_group = 0;
    s_party = 0;
    memset(g_ppu.pixels, index & 0x1F, sizeof(g_ppu.pixels));
    memset(g_ppu.overlay, PPU_OVERLAY_NONE, sizeof(g_ppu.overlay));
    Ppu_ResetClip();
    g_ppu.bg_top = 0;
    g_ppu.bg_rows = PPU_HEIGHT;
    g_ppu.emphasis = 0;
    for (int y = 0; y < PPU_HEIGHT; y++) g_ppu.mono_x0[y] = g_ppu.mono_x1[y] = 0;
    g_ppu.shake_x = 0;
    g_ppu.shake_y = 0;
    g_ppu.sprite_count = 0;
}

void Ppu_SetMonochrome(int y, int x0, int x1) {
    if (y < 0 || y >= PPU_HEIGHT) return;
    g_ppu.mono_x0[y] = (int16_t)(x0 < 0 ? 0 : x0);
    g_ppu.mono_x1[y] = (int16_t)(x1 > PPU_WIDTH ? PPU_WIDTH : x1);
}

/* $3F10/$3F14/$3F18/$3F1C mirror $3F00/$3F04/$3F08/$3F0C: writing a sprite
 * palette's first color also sets the BG one, $3F00 being the universal
 * background color.  (DrawPalette writes all 32 bytes in order, so the
 * sprite palettes' first colors are the ones that stick.) */
void Ppu_LoadPalettes(const uint8_t nes[32]) {
    memcpy(g_ppu.palette, nes, 32);
    for (int i = 0; i < 4; i++) g_ppu.palette[i * 4] = nes[16 + i * 4];
}

void Ppu_SetPalette(int palette, const uint8_t nes[4]) {
    if (palette < 0 || palette > 7) return;
    memcpy(&g_ppu.palette[palette * 4], nes, 4);
    if (palette >= 4) g_ppu.palette[(palette - 4) * 4] = nes[0];
}

void Ppu_SetRamChr(const uint8_t* data, int tiles) {
    g_chr[CHR_BANK_RAM].data = data;
    g_chr[CHR_BANK_RAM].tiles = tiles;
}

const uint8_t* Ppu_ChrTile(ChrBank bank, int tile) {
    if (bank < 0 || bank >= CHR_BANK_COUNT || tile < 0 || tile >= g_chr[bank].tiles)
        return NULL;
    return g_chr[bank].data + tile * 16;
}

static inline int in_clip(int x, int y) {
    return x >= g_ppu.clip_x0 && x < g_ppu.clip_x1 &&
           y >= g_ppu.clip_y0 && y < g_ppu.clip_y1;
}

/* 2bpp pixel (0-3) at column `col` of a tile row. */
static inline int chr_pixel(const uint8_t* t, int row, int col) {
    int bit = 7 - col;
    return ((t[row] >> bit) & 1) | (((t[row + 8] >> bit) & 1) << 1);
}

void Ppu_DrawBgTile(ChrBank bank, int tile, int x, int y, int palette) {
    const uint8_t* t = Ppu_ChrTile(bank, tile);
    if (!t) return;
    int base = (palette & 3) * 4;
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            int px = x + col, py = y + row;
            if (!in_clip(px, py)) continue;
            int c = chr_pixel(t, row, col);
            g_ppu.pixels[py][px] = (uint8_t)(c ? base + c : 0);   /* color 0 = universal */
        }
    }
}

void Ppu_LogSprite(ChrBank bank, int tile, int x, int y, int palette, int flags) {
    if (g_ppu.sprite_count < PPU_SPRITE_LOG_MAX)
        g_ppu.sprites[g_ppu.sprite_count++] = (PpuSprite){ (int16_t)bank, (int16_t)tile,
            (int16_t)x, (int16_t)y, (uint8_t)palette, (uint8_t)flags, 1, s_group, s_party };
}

void Ppu_DrawSprite(ChrBank bank, int tile, int x, int y, int palette, int flags) {
    const uint8_t* t = Ppu_ChrTile(bank, tile);
    if (!t) return;
    if (g_ppu.sprite_count < PPU_SPRITE_LOG_MAX)
        g_ppu.sprites[g_ppu.sprite_count++] = (PpuSprite){ (int16_t)bank, (int16_t)tile,
            (int16_t)x, (int16_t)y, (uint8_t)palette, (uint8_t)flags, 0, s_group, s_party };
    int base = 16 + (palette & 3) * 4;
    for (int row = 0; row < 8; row++) {
        int src_row = (flags & PPU_FLIP_Y) ? 7 - row : row;
        for (int col = 0; col < 8; col++) {
            int src_col = (flags & PPU_FLIP_X) ? 7 - col : col;
            int c = chr_pixel(t, src_row, src_col);
            if (c == 0) continue;                                 /* transparent */
            int px = x + col, py = y + row;
            if (in_clip(px, py)) g_ppu.pixels[py][px] = (uint8_t)(base + c);
        }
    }
}

void Ppu_FillRect(int x, int y, int w, int h, uint8_t index) {
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            if (in_clip(px, py)) g_ppu.pixels[py][px] = index & 0x1F;
}

void Ppu_SetPixel(int x, int y, uint8_t index) {
    if (in_clip(x, y)) g_ppu.pixels[y][x] = index & 0x1F;
}

void Ppu_OverlayRect(int x, int y, int w, int h, uint8_t tint) {
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            if (px >= 0 && px < PPU_WIDTH && py >= 0 && py < PPU_HEIGHT)
                g_ppu.overlay[py][px] = tint;
}

/* Color emphasis: channels that aren't emphasized are attenuated (to about
 * 75%) whenever any emphasis bit is set; with all three set, all are. */
static uint32_t emphasize(uint32_t v, int channel_bit) {
    uint8_t e = g_ppu.emphasis & 7;
    if (e == 0 || (e != 7 && (e & channel_bit))) return v;
    return v * 3 / 4;
}

void Ppu_ToRGBA(uint32_t* out, int pitch_px) {
    for (int y = 0; y < PPU_HEIGHT; y++) {
        uint32_t* row = out + y * pitch_px;
        int bg_on = y >= g_ppu.bg_top && y < g_ppu.bg_top + g_ppu.bg_rows;
        for (int x = 0; x < PPU_WIDTH; x++) {
            int sx = x + g_ppu.shake_x, sy = y + g_ppu.shake_y;
            uint8_t index = (bg_on && sx >= 0 && sx < PPU_WIDTH && sy >= 0 && sy < PPU_HEIGHT)
                          ? g_ppu.pixels[sy][sx] : 0;
            uint8_t color = g_ppu.palette[index] & 0x3F;
            if (x >= g_ppu.mono_x0[y] && x < g_ppu.mono_x1[y]) color &= 0x30;   /* grey column */
            const uint8_t* c = nes_master_palette[color];
            uint32_t r = emphasize(c[0], 1), g = emphasize(c[1], 2), b = emphasize(c[2], 4);
            uint8_t tint = g_ppu.overlay[y][x];
            if (tint == PPU_OVERLAY_GREEN)    { r = r * 2 / 3;       g = (g * 2 + 255) / 3; b = b * 2 / 3; }
            else if (tint == PPU_OVERLAY_RED) { r = (r * 2 + 255) / 3; g = g * 2 / 3;       b = b * 2 / 3; }
            row[x] = (r << 24) | (g << 16) | (b << 8) | 0xFFu;
        }
    }
}
