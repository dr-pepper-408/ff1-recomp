/**
 * @file ff1core.c
 * @brief The game as a library (ff1core.h): what main.c, input.c and
 *        audio.c do for the SDL build, and the test options.
 *
 * The 3D view's scene data is in view3d_world.c (the overworld and the
 * standard maps) and view3d_battle.c (the battle diorama). It is a
 * presentation of the same frame; nothing in the game changes.
 */

#include <string.h>
#include <math.h>
#include <stdint.h>
#include "view3d.h"
#include "rom.h"
#include "romdata.h"
#include "game.h"
#include "battle.h"
#include "btl_core.h"
#include "party.h"
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
#include "../data/generated/level_up_data.h"

#define SAMPLE_RATE 44100

GameContext g_ctx;              /* (view3d.h) */
static uint8_t prev_held;
static int16_t audio_buf[2048];
static int audio_n;
static double frame_samples;

FF1_API int ff1_load_rom(const char* path) {
    return (int)RomData_Open(path);
}

FF1_API const char* ff1_rom_message(int status) {
    return Rom_StatusText((RomStatus)status);
}

FF1_API int ff1_init(const char* save_path, int new_game) {
    if (!Rom_Data()) return 2;                    /* (ff1_load_rom first) */
    if (OWMap_Init() != 0) return 1;
    Game_SetSavePath(save_path);
    Apu_Reset();
    prev_held = 0;
    if (new_game) Game_Init(&g_ctx);
    else Game_PowerOn(&g_ctx);
    return 0;
}

FF1_API void ff1_quit(void) {
    Game_Shutdown();
    OWMap_Shutdown();
}

FF1_API void ff1_frame(int buttons) {
    /* Input_Poll's edge detection */
    uint8_t held = (uint8_t)buttons, changed = (uint8_t)(held ^ prev_held);
    InputState in = { held, (uint8_t)(changed & held), (uint8_t)(changed & ~held) };
    prev_held = held;
    Game_Update(&g_ctx, &in);

    /* Audio_Frame: a frame's worth of samples, the fraction carried over */
    frame_samples += SAMPLE_RATE * (NES_FRAME_NS / 1e9);
    int n = (int)frame_samples;
    frame_samples -= n;
    if (n > (int)(sizeof(audio_buf) / sizeof(audio_buf[0]))) n = sizeof(audio_buf) / sizeof(audio_buf[0]);
    Apu_Render(audio_buf, n, SAMPLE_RATE);
    audio_n = n;
}

FF1_API int ff1_audio(int16_t* out, int max) {
    int n = audio_n < max ? audio_n : max;
    memcpy(out, audio_buf, (size_t)n * sizeof(int16_t));
    return n;
}

FF1_API void ff1_draw(uint8_t* rgba) {
    static uint32_t px[PPU_WIDTH * PPU_HEIGHT];
    Game_Draw(&g_ctx);
    Ppu_ToRGBA(px, PPU_WIDTH);
    for (int i = 0; i < PPU_WIDTH * PPU_HEIGHT; i++) {   /* 0xRRGGBBAA -> bytes R,G,B,A */
        rgba[i * 4 + 0] = (uint8_t)(px[i] >> 24);
        rgba[i * 4 + 1] = (uint8_t)(px[i] >> 16);
        rgba[i * 4 + 2] = (uint8_t)(px[i] >> 8);
        rgba[i * 4 + 3] = (uint8_t)px[i];
    }
}

void nes_rgba(uint8_t color, uint8_t* out) {
    const uint8_t* c = nes_master_palette[color & 0x3F];
    out[0] = c[0]; out[1] = c[1]; out[2] = c[2]; out[3] = 255;
}

int chr_pixel(const uint8_t* t, int row, int col) {
    return ((t[row] >> (7 - col)) & 1) | (((t[row + 8] >> (7 - col)) & 1) << 1);
}

/* For screenshots and testing: put the party on overworld tile (tx, ty),
 * in `vehicle` (1 foot, 2 canoe, 4 ship, 8 airship; 0 keeps it). */
FF1_API void ff1_debug_place(int tx, int ty, int vehicle) {
    if (g_ctx.mode != MODE_OW) return;
    if (vehicle) g_ctx.player.vehicle = g_ctx.player.vehicle_next = (uint8_t)vehicle;
    g_ctx.player.tile_x = tx & 0xFF;
    g_ctx.player.tile_y = ty & 0xFF;
    g_ctx.player.sub_x = g_ctx.player.sub_y = 0;
}

/* For screenshots and testing: start battle `formation` (as FXNEXT_BATTLE
 * does, without the transition), the backdrop of the party's tile (or, in a
 * standard map, of its entrance). */
FF1_API void ff1_debug_battle(int formation) {
    Battle_SetRespondRate(g_ctx.respond_rate);
    /* the backdrop as game.c picks it: in a standard map, the entrance's tile */
    Battle_Start(&g_ctx.save, formation, g_ctx.mode == MODE_OW
                 ? OWMap_GetTileID(g_ctx.player.tile_x & 0xFF, g_ctx.player.tile_y & 0xFF)
                 : OWMap_GetTileID(g_ctx.ow_return_x, g_ctx.ow_return_y));
    g_ctx.state = GS_BATTLE;
}

/* For screenshots and testing: go through overworld entrance `id` (a town,
 * castle or dungeon), as stepping on it does. */
FF1_API void ff1_debug_enter(int id) {
    Game_EnterTeleport(&g_ctx, (uint8_t)id);
}

/* For screenshots and testing: as if the party had come through a door
 * (the room palette shows the rooms' insides). */
FF1_API void ff1_debug_inroom(void) {
    if (g_ctx.mode == MODE_SM) g_ctx.inroom = 0x81;
}

/* For screenshots and testing: show dialogue `id` (as talking does). */
FF1_API void ff1_debug_dialogue(int id) {
    g_ctx.text_count = Text_FormatDialogue(Text_Dialogue((uint8_t)id), &g_ctx.save, 0,
                                           g_ctx.text_glyphs, TEXT_MAX_GLYPHS);
    g_ctx.state = GS_DIALOGUE;
}

/* For screenshots and testing: character `i`'s class (0-5, or 6-11 after
 * the class change, as DoClassChange's +6). */
FF1_API void ff1_debug_class(int i, int cls) {
    if (i >= 0 && i < MAX_PARTY_SIZE && cls >= 0 && cls < 12)
        g_ctx.save.characters[i].class = (uint8_t)cls;
}

/* For screenshots and testing: the party raised to level `level` (1-based),
 * a level at a time as battles' EXP would (LvlUp_LevelUp: its stat
 * growth and random rolls); then HP and spell charges full. */
FF1_API void ff1_debug_level(int level) {
    SaveData* s = &g_ctx.save;
    for (int i = 0; i < MAX_PARTY_SIZE; i++) {
        CharacterStats* c = &s->characters[i];
        BtlLevelUp up;
        while (c->level + 1 < level && c->level < 49) {
            uint32_t e = c->exp[0] | ((uint32_t)c->exp[1] << 8) | ((uint32_t)c->exp[2] << 16);
            Btl_AwardExp(s, i, lut_ExpToAdvance[c->level] - e, &up);
        }
        c->cur_hp = c->max_hp;
        for (int l = 0; l < 8; l++) s->character_magic[i].cur_mp[l] = s->character_magic[i].max_mp[l];
    }
}

/* For screenshots and testing: take normal teleport `id` (stairs, a
 * dungeon's floors) from wherever the party is walking; 0: not now. */
FF1_API int ff1_debug_teleport(int id) {
    return Game_DebugGoto(&g_ctx, 1, (uint8_t)id);
}

/* For pictures: stage 1-5's equipment and spells (Party_DebugKit). */
FF1_API void ff1_debug_kit(int stage) {
    Party_DebugKit(&g_ctx.save, stage);
}

/* For pictures: battle commands chosen for the party (Battle_SetAutopilot;
 * spell ids, -1 none). */
FF1_API void ff1_debug_autopilot(int on, const int* attack, const int* heal) {
    int8_t a[4], h[4];
    for (int i = 0; i < 4; i++) { a[i] = (int8_t)attack[i]; h[i] = (int8_t)heal[i]; }
    Battle_SetAutopilot(on, a, h);
}

/* For testing, the location tour: every standard map (61), each reached by
 * the first entrance or normal teleport leading to it, entrances first. */
#define TOUR_MAPS 61
static int tour_n = -1;
static uint8_t tour[TOUR_MAPS][3];      /* map, normal teleport?, teleport id */

static void tour_build(void) {
    uint8_t seen[TOUR_MAPS] = { 0 };
    tour_n = 0;
    for (int normal = 0; normal < 2; normal++)
        for (int id = 0; id < (normal ? 64 : 32); id++) {
            int map, x, y, ts;
            if (normal) SM_LookupNormal((uint8_t)id, &map, &x, &y, &ts);
            else SM_LookupEntrance((uint8_t)id, &map, &x, &y, &ts);
            if (map < 0 || map >= TOUR_MAPS || seen[map] || (x == 0 && y == 0)) continue;
            seen[map] = 1;
            tour[tour_n][0] = (uint8_t)map;
            tour[tour_n][1] = (uint8_t)normal;
            tour[tour_n][2] = (uint8_t)id;
            tour_n++;
        }
}

FF1_API int ff1_tour_count(void) {
    if (tour_n < 0) tour_build();
    return tour_n;
}

/* Go to stop `i` of the tour (info: its map, 1 for a normal teleport, the
 * teleport id); 0: not now (the party isn't walking). */
FF1_API int ff1_tour_go(int i, int* info) {
    if (tour_n < 0) tour_build();
    if (i < 0 || i >= tour_n) return 0;
    info[0] = tour[i][0]; info[1] = tour[i][1]; info[2] = tour[i][2];
    return Game_DebugGoto(&g_ctx, tour[i][1], tour[i][2]);
}

/* Random encounters on or off (an option, not in the original). */
FF1_API void ff1_set_encounters(int on) {
    Game_SetRandomEncounters(on);
}

FF1_API void ff1_class_sprite(int cls, uint8_t* rgba) {
    const uint8_t* pal = BattleSpritePalettes + (lutClassBatSprPalette[cls % 12] & 3) * 4;
    memset(rgba, 0, 16 * 24 * 4);
    for (int k = 0; k < 6; k++) {
        const uint8_t* t = Ppu_ChrTile(CHR_BANK_09, 0x100 + cls * 32 + k);
        if (!t) continue;
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                int ci = chr_pixel(t, r, c);
                if (ci) nes_rgba(pal[ci], rgba + (((k >> 1) * 8 + r) * 16 + (k & 1) * 8 + c) * 4);
            }
    }
}
