/**
 * @file view3d_battle.c
 * @brief The 3D view of a battle (ff1core.h): the diorama.
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


/* ---- The battle diorama ----
 *
 * The battle as a small stage: the battle's backdrop stands as a painted
 * wall at the far end, the floor continues its bottom row of pixels, and
 * the enemies and the party stand on it as 2D cutouts (characters stay 2D,
 * as in Octopath Traveler or Paper Mario).  The screen's layout becomes
 * the stage's: screen x is depth (the enemies far, the party near the
 * camera), screen y is across.  Each sprite belongs to an enemy (the
 * explosions over it) or a character's row (the weapon, the spell), and
 * stands with it.  The menus and the status column stay the 2D picture
 * (ff1_battle_overlay).
 */

#define BA_W 512                /* the battle atlas: the enemies from the top... */
#define BA_GRP_Y 256            /* ...the sprite groups from here... */
#define BA_BACK_Y 480           /* ...the backdrop's copy at (0, 480) */
#define BA_PAD 2                /* air round each cutout, for its outline */
static float ba_stagger;        /* each party row this much further back (ff1_battle_stagger) */
static int ba_flip;             /* the cutouts mirrored (ff1_battle_flip) */

FF1_API void ff1_battle_stagger(float s) { ba_stagger = s; }

/* Seen from the camera, the enemies stand to the right of the party: the
 * cutouts are mirrored, so the party (facing left on the NES screen) and
 * the enemies (facing right) still face each other. */
FF1_API void ff1_battle_flip(int on) { ba_flip = on; }
#define UI_X 200                /* the menus and status: x >= 200 or y >= 144 */
#define UI_Y 144

FF1_API int ff1_battle3d(void) {
    return g_ctx.state == GS_BATTLE;
}

static void ba_put(uint8_t* atlas, int ax, int ay, int sx, int sy, int transparent0) {
    uint8_t idx = g_ppu.pixels[sy][sx];
    uint8_t* p = atlas + (ay * BA_W + ax) * 4;
    if (transparent0 && (idx & 3) == 0) { p[0] = p[1] = p[2] = p[3] = 0; return; }
    nes_rgba(g_ppu.palette[idx & 0x1F], p);
}

/* A quad standing on the stage: centred on (lx, z) across, `w` wide along
 * the camera's right (rx, rz), from height y0 to y1; texels (u0,v0)-(u1,v1). */
static void ba_stand(Emit* e, float lx, float z, float w, float y0, float y1, float rx, float rz,
                     float u0, float v0, float u1, float v1) {
    float hx = w * 0.5f * rx, hz = w * 0.5f * rz;
    const float p[4][3] = { { lx - hx, y1, z - hz }, { lx + hx, y1, z + hz },
                            { lx + hx, y0, z + hz }, { lx - hx, y0, z - hz } };
    const float k = (float)FF1_ATLAS_W / BA_W, kv = (float)FF1_ATLAS_H / BA_W;   /* (emit_quad's units) */
    if (ba_flip) { float t = u0; u0 = u1; u1 = t; }
    const float uv[4][2] = { { u0 * k, v0 * kv }, { u1 * k, v0 * kv }, { u1 * k, v1 * kv }, { u0 * k, v1 * kv } };
    emit_quad(e, p, uv, 255);
}

/* An enemy's picture as a cutout: its black that reaches the picture's
 * edge is air (on the original's black background it never showed; the
 * fiends and Chaos are drawn with black in their colours), the black
 * inside the drawing stays. */
static int is_black(uint8_t c) { return (c & 0x0F) >= 0x0D; }

static void ba_cut_enemy(uint8_t* atlas, int ax, int ay, int ex, int ey, int w, int h) {
    static uint8_t seen[128][128];
    static int16_t stack[128 * 128][2];
    if (w > 128 || h > 128) return;
    memset(seen, 0, sizeof(seen));
    int n = 0;
    for (int x = 0; x < w; x++) { stack[n][0] = (int16_t)x; stack[n++][1] = 0; stack[n][0] = (int16_t)x; stack[n++][1] = (int16_t)(h - 1); }
    for (int y = 0; y < h; y++) { stack[n][0] = 0; stack[n++][1] = (int16_t)y; stack[n][0] = (int16_t)(w - 1); stack[n++][1] = (int16_t)y; }
    while (n > 0) {
        n--;
        int x = stack[n][0], y = stack[n][1];
        if (x < 0 || y < 0 || x >= w || y >= h || seen[y][x]) continue;
        seen[y][x] = 1;
        uint8_t* p = atlas + ((ay + y) * BA_W + ax + x) * 4;
        uint8_t idx = (ex + x < PPU_WIDTH && ey + y < PPU_HEIGHT) ? g_ppu.pixels[ey + y][ex + x] : 0;
        if (p[3] && !is_black(g_ppu.palette[idx & 0x1F])) continue;    /* the drawing: stop */
        p[0] = p[1] = p[2] = p[3] = 0;
        if (n + 4 > 128 * 128) continue;
        stack[n][0] = (int16_t)(x + 1); stack[n++][1] = (int16_t)y;
        stack[n][0] = (int16_t)(x - 1); stack[n++][1] = (int16_t)y;
        stack[n][0] = (int16_t)x; stack[n++][1] = (int16_t)(y + 1);
        stack[n][0] = (int16_t)x; stack[n++][1] = (int16_t)(y - 1);
    }
}

FF1_API int ff1_battle_scene(Ff1Vertex* out, int max, uint8_t* atlas, float rx, float rz, float* info) {
    BattleView v;
    Battle_GetView(&v);
    memset(atlas, 0, BA_W * BA_W * 4);
    Emit e = { out, max, 0 };
    const float k = (float)FF1_ATLAS_W / BA_W, kv = (float)FF1_ATLAS_H / BA_W;

    /* The backdrop: a wall across the far end, two pixels to its pixel,
     * and the floor in its bottom row -- both dimmed, so the cutouts in
     * front read clearly (in the original they stand on black). */
    for (int y = 0; y < v.backdrop_h && y < 32; y++)
        for (int x = 0; x < v.backdrop_w && x < 128; x++)
            ba_put(atlas, x, BA_BACK_Y + y, v.backdrop_x + x, v.backdrop_y + y, 0);
    {
        float bw = (float)v.backdrop_w, x0 = 120 - bw, x1 = 120 + bw, top = (float)v.backdrop_h * 2;
        const float wall[4][3] = { { x0, top, 0 }, { x1, top, 0 }, { x1, 0, 0 }, { x0, 0, 0 } };
        const float wuv[4][2] = { { 0, BA_BACK_Y * kv }, { bw * k, BA_BACK_Y * kv },
                                  { bw * k, (BA_BACK_Y + v.backdrop_h) * kv }, { 0, (BA_BACK_Y + v.backdrop_h) * kv } };
        emit_quad(&e, wall, wuv, 150);
        float fv = (BA_BACK_Y + v.backdrop_h - 0.5f) * kv;
        const float floor_[4][3] = { { x0, 0, 0 }, { x1, 0, 0 }, { x1, 0, 260 }, { x0, 0, 260 } };
        const float fuv[4][2] = { { 0, fv }, { bw * k, fv }, { bw * k, fv }, { 0, fv } };
        emit_quad(&e, floor_, fuv, 105);
    }

    /* The atlas: the enemies (shelves from the top), then the sprite groups
     * (shelves from BA_GRP_Y), each with BA_PAD texels of air round it for
     * the outline. */
    int sx = 0, sy = 0, shelf = 0;
    for (int i = 0; i < v.enemy_count; i++) {
        int ex = v.enemy[i].x, ey = v.enemy[i].y, w = v.enemy[i].w, h = v.enemy[i].h;
        int pw = w + 2 * BA_PAD, ph = h + 2 * BA_PAD;
        if (sx + pw > BA_W) { sx = 0; sy += shelf; shelf = 0; }
        if (sy + ph > BA_GRP_Y) break;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if (ex + x < PPU_WIDTH && ey + y < PPU_HEIGHT)
                    ba_put(atlas, sx + BA_PAD + x, sy + BA_PAD + y, ex + x, ey + y, 1);
        ba_cut_enemy(atlas, sx + BA_PAD, sy + BA_PAD, ex, ey, w, h);
        float p = (float)BA_PAD;
        ba_stand(&e, (float)(ey + h / 2), (float)(ex + w / 2), (float)pw, -p, (float)h + p, rx, rz,
                 (float)sx, (float)sy, (float)(sx + pw), (float)(sy + ph));
        sx += pw;
        if (ph > shelf) shelf = ph;
    }

    /* The sprite groups (a character, the weapon or spell, an explosion,
     * the cursor), each one cutout. */
    enum { MAXG = 32 };
    struct { int group, party, x0, y0, x1, y1; } g[MAXG];
    int ng = 0;
    for (int s = 0; s < g_ppu.sprite_count; s++) {
        const PpuSprite* sp = &g_ppu.sprites[s];
        if (sp->hidden || sp->x >= UI_X || sp->y >= UI_Y) continue;   /* (the menus' cursor) */
        int gi = 0;
        while (gi < ng && g[gi].group != sp->group) gi++;
        if (gi == ng) {
            if (ng == MAXG) continue;
            g[ng].group = sp->group; g[ng].party = sp->party;
            g[ng].x0 = g[ng].y0 = 999; g[ng].x1 = g[ng].y1 = -999;
            ng++;
        }
        if (sp->x < g[gi].x0) g[gi].x0 = sp->x;
        if (sp->y < g[gi].y0) g[gi].y0 = sp->y;
        if (sp->x + 8 > g[gi].x1) g[gi].x1 = sp->x + 8;
        if (sp->y + 8 > g[gi].y1) g[gi].y1 = sp->y + 8;
    }
    /* Each row's character: where the row stands on the stage. */
    float row_z[4];
    int row_has[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < ng; i++) {
        int b = ((g[i].y0 + g[i].y1) / 2 - v.party_y) / v.party_row;
        if (g[i].party && b >= 0 && b < 4) {
            /* (the rows on a slight diagonal: seen from the side the
             * party fans out rather than lining up behind each other) */
            row_z[b] = (g[i].x0 + g[i].x1) * 0.5f + (b - 1.5f) * ba_stagger;
            row_has[b] = 1;
        }
    }
    sx = 0; sy = BA_GRP_Y; shelf = 0;
    for (int i = 0; i < ng; i++) {
        int w = g[i].x1 - g[i].x0, h = g[i].y1 - g[i].y0;
        int pw = w + 2 * BA_PAD, ph = h + 2 * BA_PAD;
        if (sx + pw > BA_W) { sx = 0; sy += shelf; shelf = 0; }
        if (sy + ph > BA_BACK_Y) break;
        for (int s = 0; s < g_ppu.sprite_count; s++) {             /* paint its sprites */
            const PpuSprite* sp = &g_ppu.sprites[s];
            if (sp->group != g[i].group || sp->hidden) continue;
            const uint8_t* t = Ppu_ChrTile((ChrBank)sp->bank, sp->tile);
            if (!t) continue;
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 8; c++) {
                    int sr = (sp->flags & PPU_FLIP_Y) ? 7 - r : r, sc = (sp->flags & PPU_FLIP_X) ? 7 - c : c;
                    int ci = chr_pixel(t, sr, sc);
                    int ax = sx + BA_PAD + sp->x - g[i].x0 + c, ay = sy + BA_PAD + sp->y - g[i].y0 + r;
                    if (ci) nes_rgba(g_ppu.palette[16 + (sp->palette & 3) * 4 + ci], atlas + (ay * BA_W + ax) * 4);
                }
        }
        float cx = (g[i].x0 + g[i].x1) * 0.5f, cy = (g[i].y0 + g[i].y1) * 0.5f, p = (float)BA_PAD;
        int en = -1;
        for (int n = 0; n < v.enemy_count; n++) {    /* over an enemy (or just beside it)? */
            int ex = v.enemy[n].x - 16, ey = v.enemy[n].y, ew = v.enemy[n].w + 16, eh = v.enemy[n].h;
            if (cx >= ex && cx < ex + ew && cy >= ey && cy < ey + eh) { en = n; break; }
        }
        int b = ((int)cy - v.party_y) / v.party_row;
        float across, z, foot;
        if (en >= 0) {                                 /* just in front of that enemy */
            float ecx = (float)(v.enemy[en].x + v.enemy[en].w / 2);
            float off = (cx - ecx) * (ba_flip ? -1 : 1);
            z = ecx + 2 + off * rz;
            across = (float)(v.enemy[en].y + v.enemy[en].h / 2) + off * rx;
            foot = (float)(v.enemy[en].y + v.enemy[en].h);
        } else if (b >= 0 && b < 4 && cy >= v.party_y) {   /* with its row's character */
            float rzc = row_has[b] ? row_z[b] : cx;
            float off = g[i].party ? 0 : (cx - rzc) * (ba_flip ? -1 : 1);
            z = rzc + off * rz + (g[i].party ? 0 : 1);     /* (a weapon a hair in front) */
            across = (float)(v.party_y + b * v.party_row + v.party_row / 2) + off * rx;
            foot = (float)(v.party_y + b * v.party_row + 24);
        } else {
            sx += pw;
            if (ph > shelf) shelf = ph;
            continue;
        }
        ba_stand(&e, across, z, (float)pw, foot - g[i].y1 - p, foot - g[i].y0 + p, rx, rz,
                 (float)sx, (float)sy, (float)(sx + pw), (float)(sy + ph));
        sx += pw;
        if (ph > shelf) shelf = ph;
    }

    /* For the camera and the background: the shake, and the magic flash. */
    info[0] = (float)v.shake_x;
    info[1] = (float)v.shake_y;
    info[5] = (float)v.boss;
    /* [6..10] where the enemies stand: across min, max; depth min, max;
     * the tallest's height (for the camera's framing) */
    info[6] = info[8] = 1e9f; info[7] = info[9] = -1e9f; info[10] = 0;
    for (int i = 0; i < v.enemy_count; i++) {
        float a0 = (float)v.enemy[i].y, a1 = a0 + v.enemy[i].h, z0 = (float)v.enemy[i].x, z1 = z0 + v.enemy[i].w;
        if (a0 < info[6]) info[6] = a0;
        if (a1 > info[7]) info[7] = a1;
        if (z0 < info[8]) info[8] = z0;
        if (z1 > info[9]) info[9] = z1;
        if (v.enemy[i].h > info[10]) info[10] = (float)v.enemy[i].h;
    }
    if (!v.enemy_count) { info[6] = info[8] = 64; info[7] = info[9] = 96; }
    if (v.flash >= 0) {
        const uint8_t* c = nes_master_palette[v.flash & 0x3F];
        info[2] = c[0] / 255.0f; info[3] = c[1] / 255.0f; info[4] = c[2] / 255.0f;
    } else {
        info[2] = info[3] = info[4] = -1;
    }
    return e.count < max ? e.count : max;
}

/* The 2D picture with the stage area (the enemy and party boxes) cleared:
 * the menus and the status column, drawn over the diorama. */
FF1_API void ff1_battle_overlay(uint8_t* rgba) {
    BattleView v;
    Battle_GetView(&v);
    for (int y = 0; y < v.stage_bottom; y++)
        for (int x = 0; x < UI_X; x++) rgba[(y * PPU_WIDTH + x) * 4 + 3] = 0;
}
