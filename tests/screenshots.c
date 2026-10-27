/**
 * @file screenshots.c
 * @brief Headless renderer check: drives the game into a few scenes and
 *        writes each frame as a PPM image (no SDL; links the game modules).
 *
 * Usage: ff1-screenshots <output-dir>
 * Writes overworld.ppm, wipe.ppm, town.ppm, dialogue.ppm, palcyc.ppm,
 * menu.ppm, battle_flash.ppm, battle.ppm, castle.ppm, king.ppm.
 *
 * Also prints PASS/FAIL checks for walking out of town and talking to the
 * King of Coneria.
 */

#include "romdata.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "game.h"
#include "ppu.h"
#include "overworld_map.h"
#include "battle.h"
#include "battle_internal.h"
#include "menu.h"
#include "text.h"
#include "screen_fx.h"
#include "sm_map.h"
#include "shop.h"
#include "talk.h"
#include "save.h"
#include "story.h"
#include "music.h"
#include "minigame.h"
#include "party.h"
#include "../data/generated/shop_data.h"

/* game.c calls Input_Init; input.c needs SDL, so stub it here. */
void Input_Init(void) {}

static GameContext g_ctx;
static char save_path[600];
static uint32_t g_rgba[PPU_HEIGHT * PPU_WIDTH];

static void shot(const char* dir, const char* name) {
    Game_Draw(&g_ctx);
    Ppu_ToRGBA(g_rgba, PPU_WIDTH);

    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE* f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); return; }
    fprintf(f, "P6\n%d %d\n255\n", PPU_WIDTH, PPU_HEIGHT);
    for (int i = 0; i < PPU_WIDTH * PPU_HEIGHT; i++) {
        uint8_t rgb[3] = { (uint8_t)(g_rgba[i] >> 24), (uint8_t)(g_rgba[i] >> 16),
                           (uint8_t)(g_rgba[i] >> 8) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("wrote %s\n", path);
}

static void step(const InputState* in, int frames) {
    for (int i = 0; i < frames; i++) Game_Update(&g_ctx, in);
}

/* Run frames until the game is back to walking (transitions finished). */
/* Wait (up to 10 s) until a battle menu is reading the pad. */
static void wait_menu(void) {
    const InputState none = { 0, 0, 0 };
    for (int k = 0; k < 600 && g_ctx.state == GS_BATTLE && !Battle_WaitingForInput(); k++)
        step(&none, 1);
}

/* Press a button in a battle menu, once it is ready for it. */
static void press(const InputState* in) {
    const InputState none = { 0, 0, 0 };
    wait_menu();
    step(in, 1);
    step(&none, 1);
}

static void settle(void) {
    const InputState none = { 0, 0, 0 };
    for (int i = 0; i < 600 && g_ctx.state != GS_WALK; i++) Game_Update(&g_ctx, &none);
}

/* ---- Walking on standard maps ---- */

static int passable(int x, int y, int tx, int ty) {
    if (x == tx && y == ty) return 1;
    const OWMetatile* m = SM_GetMetatile(SM_GetTileID(x, y));
    if (!m || (m->prop0 & 0xC0)) return 0;              /* avoid teleports on the way */
    if ((m->prop0 & 0x1F) == SM_TP_SPEC_LOCKED) return g_ctx.save.items[5] != 0;
    return SM_CanWalk(x, y);
}

/* First direction (BTN_*) of a shortest path from the party to (tx, ty), or 0. */
static uint8_t path_dir(int tx, int ty) {
    static int16_t from[64 * 64];
    static uint8_t q[64 * 64 * 2];
    int sx = g_ctx.player.tile_x, sy = g_ctx.player.tile_y;
    for (int i = 0; i < 64 * 64; i++) from[i] = -1;
    int head = 0, tail = 0;
    from[sy * 64 + sx] = (int16_t)(sy * 64 + sx);
    q[tail++] = (uint8_t)sx; q[tail++] = (uint8_t)sy;
    static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
    while (head < tail) {
        int x = q[head++], y = q[head++];
        if (x == tx && y == ty) break;
        for (int d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d];
            if (nx < 0 || ny < 0 || nx >= 64 || ny >= 64 || from[ny * 64 + nx] >= 0) continue;
            if (!passable(nx, ny, tx, ty)) continue;
            from[ny * 64 + nx] = (int16_t)(y * 64 + x);
            q[tail++] = (uint8_t)nx; q[tail++] = (uint8_t)ny;
        }
    }
    if (from[ty * 64 + tx] < 0) return 0;
    int cur = ty * 64 + tx;
    while (from[cur] != sy * 64 + sx) cur = from[cur];
    int cx = cur % 64, cy = cur / 64;
    if (cx > sx) return BTN_RIGHT;
    if (cx < sx) return BTN_LEFT;
    return (cy > sy) ? BTN_DOWN : BTN_UP;
}

/* Walk to (tx, ty) on the current map, one step at a time (re-planning around
 * townspeople).  Returns 1 when there. */
static int walk_to(int tx, int ty) {
    const InputState none = { 0, 0, 0 };
    for (int steps = 0; steps < 400; steps++) {
        if (g_ctx.player.tile_x == tx && g_ctx.player.tile_y == ty) return 1;
        uint8_t d = path_dir(tx, ty);
        if (!d) return 0;
        InputState in = { d, d, 0 };
        step(&in, 1);
        while (g_ctx.player.moving) step(&none, 1);
        if (g_ctx.state != GS_WALK) return g_ctx.player.tile_x == tx && g_ctx.player.tile_y == ty;
    }
    return 0;
}

/* Glyph tokens -> ASCII, for printing dialogue. */
static const char* glyphs_ascii(const TextGlyph* g, int n) {
    static char buf[TEXT_MAX_GLYPHS + 1];
    int k = 0;
    for (int i = 0; i < n; i++) {
        TextGlyph c = g[i];
        char ch = '#';
        if (c == TEXT_NEWLINE) ch = ' ';
        else if (c >= 0x80 && c <= 0x89) ch = (char)('0' + c - 0x80);
        else if (c >= 0x8A && c <= 0xA3) ch = (char)('A' + c - 0x8A);
        else if (c >= 0xA4 && c <= 0xBD) ch = (char)('a' + c - 0xA4);
        else if (c == 0xFF) ch = ' ';
        else if (c == 0xC0) ch = '.';
        else if (c == 0xBF) ch = ',';
        else if (c == 0xC4) ch = '!';
        buf[k++] = ch;
    }
    buf[k] = '\0';
    return buf;
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : ".";
    const InputState none = { 0, 0, 0 };
    const InputState start = { BTN_START, BTN_START, 0 };

    static const char* const roms[] = { "../FinalFantasy.nes", "../../FinalFantasy.nes", NULL };
    RomStatus st = RomData_Find(NULL, roms);
    if (st != ROM_OK) { printf("No ROM of Final Fantasy (USA) (set FF1_ROM): %s\n", Rom_StatusText(st)); return 1; }
    if (OWMap_Init() != 0) { printf("OWMap_Init failed\n"); return 1; }
    snprintf(save_path, sizeof(save_path), "%s/ff1-screenshots.sav", dir);
    remove(save_path);                /* every run starts with no save */
    Game_SetSavePath(save_path);
    /* Power on: the intro story fades in two rows at a time (each row 64
     * frames); A or B once it's all shown, or Start at any time, goes to
     * the title screen with the respond rate reset. */
    {
        const InputState a = { BTN_A, BTN_A, 0 }, st = { BTN_START, BTN_START, 0 };
        Game_PowerOn(&g_ctx);
        step(&none, 40);
        shot(dir, "intro_fading");
        int frames = 0;
        for (; frames < 2000 && g_ctx.state == GS_INTRO; frames++) {
            step(&none, 1);
            if (frames == 1000) shot(dir, "intro");
        }
        int still = g_ctx.state == GS_INTRO;
        g_ctx.respond_rate = 4;
        step(&a, 1);
        int to_title = g_ctx.state == GS_TITLE && g_ctx.respond_rate == 0;
        Game_PowerOn(&g_ctx);
        step(&none, 10);
        step(&st, 1);
        int skipped = g_ctx.state == GS_TITLE;
        printf("%s: intro story: stays up %d, A -> title %d (respond rate reset), Start skips %d\n",
               still && to_title && skipped ? "PASS" : "FAIL", still, to_title, skipped);
    }

    /* Power on: the title screen is the first thing drawn.  Its background
     * is black because sprite palette 0's first color ($0F) mirrors the
     * universal background color. */
    Game_Boot(&g_ctx);
    shot(dir, "title_boot");
    printf("%s: title screen at power-on, background color $%02X\n",
           g_ppu.palette[0] == 0x0F ? "PASS" : "FAIL", g_ppu.palette[0]);

    /* New Game: party generation.  Classes Fighter, Thief, White Mage,
     * Black Mage (any direction steps the class), names typed on the name
     * input screen (a 5th A ends a name); B on the last character goes
     * back one; A on the finished party starts the game at level 1. */
    {
        const InputState A = { BTN_A, BTN_A, 0 }, B = { BTN_B, BTN_B, 0 };
        const InputState D = { BTN_DOWN, BTN_DOWN, 0 }, R = { BTN_RIGHT, BTN_RIGHT, 0 };
        #define TAP(in) do { step(&(in), 1); step(&none, 1); } while (0)
        TAP(D); TAP(A);                                   /* New Game */
        int in_pg = g_ctx.state == GS_PARTYGEN;
        shot(dir, "ptygen");
        /* character 0: Fighter, "AAAA" */
        TAP(A);
        shot(dir, "ptygen_name");
        for (int i = 0; i < 4; i++) TAP(A);
        TAP(A);
        /* character 1: Thief (its default), "KLLL": K is a row down; B erases */
        TAP(A);
        TAP(D); TAP(A); TAP(A); TAP(B);                   /* "K", "K", erase -> "K" */
        TAP(R); TAP(A);                                   /* "KL" */
        TAP(A); TAP(A); TAP(A);                           /* two more L, then done */
        /* character 2: Black Belt -> White Mage (two steps) */
        TAP(R); TAP(R); TAP(A);
        for (int i = 0; i < 5; i++) TAP(A);
        /* character 3: Red Mage -> Black Mage; B first goes back to character 2 */
        TAP(B); TAP(A);
        for (int i = 0; i < 5; i++) TAP(A);               /* character 2 renamed */
        TAP(R); TAP(R); TAP(A);
        for (int i = 0; i < 5; i++) TAP(A);
        shot(dir, "ptygen_confirm");
        TAP(A);
        settle();
        const SaveData* sv = &g_ctx.save;
        static const uint8_t want_cls[4] = { 0, 1, 4, 5 };
        static const uint8_t want_hp[4] = { 35, 30, 28, 25 };       /* lut_ClassStartingStats */
        int ok = in_pg && g_ctx.state == GS_WALK && Party_Gold(sv) == 400;
        for (int c = 0; c < 4; c++) {
            const CharacterStats* ch = &sv->characters[c];
            ok &= ch->class == want_cls[c] && ch->max_hp == want_hp[c] && ch->cur_hp == want_hp[c] &&
                  ch->level == 0 && ch->exp_to_next == 40;
            ok &= sv->character_magic[c].max_mp[0] == (c >= 2 ? 2 : 0);
        }
        ok &= sv->characters[0].name[0] == 0x8A && sv->characters[0].name[3] == 0x8A;   /* AAAA */
        ok &= sv->characters[1].name[0] == 0x94 && sv->characters[1].name[1] == 0x95 &&
              sv->characters[1].name[3] == 0x95;                                         /* KLLL */
        printf("%s: party generation: classes %d %d %d %d, HP %d/%d/%d/%d, gold %u, started %d\n",
               ok ? "PASS" : "FAIL", sv->characters[0].class, sv->characters[1].class,
               sv->characters[2].class, sv->characters[3].class, sv->characters[0].max_hp,
               sv->characters[1].max_hp, sv->characters[2].max_hp, sv->characters[3].max_hp,
               (unsigned)Party_Gold(sv), g_ctx.state == GS_WALK);
        #undef TAP
    }

    g_ctx.respond_rate = 5;           /* 15 frames per battle message (kept by Game_Init) */
    Game_Init(&g_ctx);                /* opens with a screen wipe */
    settle();
    shot(dir, "overworld");

    /* Coneria town: overworld entrance teleport 1.  Capture the wipe opening
     * on the town halfway through. */
    Game_EnterTeleport(&g_ctx, 1);
    while (g_ctx.fx_next != FXNEXT_WALK) step(&none, 1);   /* wipe closed, map loaded */
    step(&none, 28);
    shot(dir, "wipe");
    settle();
    shot(dir, "town");

    /* A dialogue box over the town (dialogue $02, the King's thanks). */
    g_ctx.text_count = Text_FormatDialogue(Text_Dialogue(0x02), &g_ctx.save, 0,
                                           g_ctx.text_glyphs, TEXT_MAX_GLYPHS);
    g_ctx.state = GS_DIALOGUE;
    shot(dir, "dialogue");
    g_ctx.state = GS_WALK;

    step(&start, 1);                 /* Start: palette cycles out, then the menu */
    step(&none, 9);
    shot(dir, "palcyc");
    while (g_ctx.state == GS_FX) step(&none, 1);
    shot(dir, "menu");
    {
        const InputState b = { BTN_B, BTN_B, 0 };
        step(&b, 1);                 /* B closes it: palette cycles back in */
    }
    settle();

    /* Leave town: one step south from the gate onto the grass, a warp tile,
     * returns to the overworld tile the party entered from. */
    {
        const InputState down = { BTN_DOWN, 0, 0 };
        int ret_x = g_ctx.ow_return_x, ret_y = g_ctx.ow_return_y;
        step(&down, 20);
        settle();
        int ok = g_ctx.mode == MODE_OW && g_ctx.player.tile_x == ret_x &&
                 g_ctx.player.tile_y == ret_y;
        printf("%s: leaving town by the south grass returns to the overworld (%d,%d)\n",
               ok ? "PASS" : "FAIL", g_ctx.player.tile_x, g_ctx.player.tile_y);
        shot(dir, "left_town");
    }

    /* A battle on the grass near Coneria, with its transition flash. */
    g_ctx.pending_formation = Battle_PickFormation(Battle_LandDomain(0x99, 0xA5));
    g_ctx.pending_backdrop_tile = OWMap_GetTileID(0x99, 0xA5);
    ScreenFx_Start(&g_ctx.fx, SCREENFX_BATTLE, NULL);
    g_ctx.fx_next = FXNEXT_BATTLE;
    g_ctx.state = GS_FX;
    step(&none, 6);
    shot(dir, "battle_flash");
    while (g_ctx.state == GS_FX) step(&none, 1);
    shot(dir, "battle");

    /* Fight it out: A every few frames picks FIGHT on the first enemy for
     * everyone, and closes the result. */
    {
        const InputState a = { BTN_A, BTN_A, 0 };
        int frames = 0;
        while (g_ctx.state == GS_BATTLE && frames < 20000) {
            step((frames % 8) == 0 ? &a : &none, 1);
            frames++;
        }
        int alive = 0;
        for (int i = 0; i < 4; i++) alive += g_ctx.save.characters[i].cur_hp > 0;
        printf("%s: auto-played battle finished in %d frames; party HP %u/%u/%u/%u, ailments %u/%u/%u/%u\n",
               g_ctx.state != GS_BATTLE && alive ? "PASS" : "FAIL", frames,
               g_ctx.save.characters[0].cur_hp, g_ctx.save.characters[1].cur_hp,
               g_ctx.save.characters[2].cur_hp, g_ctx.save.characters[3].cur_hp,
               g_ctx.save.characters[0].ailments, g_ctx.save.characters[1].ailments,
               g_ctx.save.characters[2].ailments, g_ctx.save.characters[3].ailments);
        settle();
    }

    /* A battle with the Fighter poisoned: poison persists through the battle
     * (in-battle bits and back) and drains 2 HP a round, and afterwards
     * ReSortPartyByAilment moves the Fighter to the back of the party.
     * (The order is put back for the scenarios that follow.) */
    {
        const InputState a = { BTN_A, BTN_A, 0 };
        CharacterStats chars[4];
        CharacterMagic magic[4];
        memcpy(chars, g_ctx.save.characters, sizeof(chars));
        memcpy(magic, g_ctx.save.character_magic, sizeof(magic));
        g_ctx.save.characters[0].ailments = 3;            /* out-of-battle poison */
        uint16_t hp_before = g_ctx.save.characters[0].cur_hp;
        g_ctx.pending_formation = Battle_PickFormation(Battle_LandDomain(0x99, 0xA5));
        ScreenFx_Start(&g_ctx.fx, SCREENFX_BATTLE, NULL);
        g_ctx.fx_next = FXNEXT_BATTLE;
        g_ctx.state = GS_FX;
        while (g_ctx.state == GS_FX) step(&none, 1);
        for (int frames = 0; g_ctx.state == GS_BATTLE && frames < 20000; frames++)
            step((frames % 8) == 0 ? &a : &none, 1);
        int slot = 0;
        for (int i = 0; i < 4; i++)
            if (g_ctx.save.characters[i].class == chars[0].class) slot = i;
        const CharacterStats* ft = &g_ctx.save.characters[slot];
        printf("%s: poisoned Fighter: HP %u -> %u, ailment %u after the battle, now in slot %d\n",
               ft->ailments == 3 && ft->cur_hp < hp_before && slot != 0 ? "PASS" : "FAIL",
               hp_before, ft->cur_hp, ft->ailments, slot + 1);
        memcpy(g_ctx.save.characters, chars, sizeof(chars));
        memcpy(g_ctx.save.character_magic, magic, sizeof(magic));
        settle();
    }

    /* Magic and potions: round 1 the first character drinks a HEAL, the next
     * two fight and the Red Mage (slot 4) casts FIRE on an enemy.  The potion
     * and the MP are spent on their turns; then the battle is auto-played. */
    {
        const InputState a = { BTN_A, BTN_A, 0 }, down = { BTN_DOWN, BTN_DOWN, 0 };
        CharacterMagic* rm = &g_ctx.save.character_magic[3];
        rm->spells[0] = 4 + 1;                            /* level 1, FIRE */
        rm->cur_mp[0] = rm->max_mp[0] = 2;
        g_ctx.save.items[25] = 2;                         /* HEAL potions */
        g_ctx.pending_formation = Battle_PickFormation(Battle_LandDomain(0x99, 0xA5));
        ScreenFx_Start(&g_ctx.fx, SCREENFX_BATTLE, NULL);
        g_ctx.fx_next = FXNEXT_BATTLE;
        g_ctx.state = GS_FX;
        while (g_ctx.state == GS_FX) step(&none, 1);

        const InputState* script[] = {
            &down, &down, &a, &a, &a,                     /* slot 1: DRINK, HEAL, self */
            &a, &a,                                       /* slot 2: FIGHT */
            &a, &a,                                       /* slot 3: FIGHT */
            &down, &a, &a, &a,                            /* Red Mage: MAGIC, FIRE, enemy */
        };
        for (unsigned k = 0; k < sizeof(script) / sizeof(script[0]); k++)
            press(script[k]);
        int frames = 0;
        while (g_ctx.state == GS_BATTLE && rm->cur_mp[0] == 2 && frames < 3000) {
            step(&none, 1);
            frames++;
        }
        step(&none, 20);                              /* into the result messages */
        shot(dir, "battle_magic");
        int mp = rm->cur_mp[0];
        for (frames = 0; g_ctx.state == GS_BATTLE && frames < 20000; frames++)
            step((frames % 8) == 0 ? &a : &none, 1);
        int potions = g_ctx.save.items[25];
        printf("%s: magic and potions: Red Mage MP 2 -> %d, HEAL potions 2 -> %d, battle %s\n",
               mp == 1 && potions == 1 && g_ctx.state != GS_BATTLE ? "PASS" : "FAIL",
               mp, potions, g_ctx.state != GS_BATTLE ? "over" : "still running");
        rm->spells[0] = 0;
        settle();
    }

    /* Victory with a level up: the first character is 1 EXP short of level
     * 11.  The reward screens are stepped through with A. */
    {
        const InputState a = { BTN_A, BTN_A, 0 };
        CharacterStats* c = &g_ctx.save.characters[0];
        uint32_t exp = 15016 - 1;                         /* lut_ExpToAdvance[9] - 1 */
        c->exp[0] = (uint8_t)exp; c->exp[1] = (uint8_t)(exp >> 8); c->exp[2] = 0;
        uint8_t level = c->level;
        uint16_t max_hp = c->max_hp;
        uint32_t gold = Party_Gold(&g_ctx.save);
        g_ctx.pending_formation = Battle_PickFormation(Battle_LandDomain(0x99, 0xA5));
        ScreenFx_Start(&g_ctx.fx, SCREENFX_BATTLE, NULL);
        g_ctx.fx_next = FXNEXT_BATTLE;
        g_ctx.state = GS_FX;
        while (g_ctx.state == GS_FX) step(&none, 1);
        int frames = 0;
        while (g_ctx.state == GS_BATTLE && !Battle_IsOver() && frames < 20000) {
            step((frames % 8) == 0 ? &a : &none, 1);
            frames++;
        }
        step(&a, 1); step(&none, 2);                      /* past the monsters-perished message */
        shot(dir, "battle_rewards");
        step(&a, 1); step(&none, 2);                      /* the level up */
        shot(dir, "battle_levelup");
        for (frames = 0; g_ctx.state == GS_BATTLE && frames < 2000; frames++)
            step((frames % 8) == 0 ? &a : &none, 1);
        printf("%s: victory: level %u -> %u, max HP %u -> %u, gold %u -> %u, %u EXP to next\n",
               c->level == level + 1 && c->max_hp > max_hp && Party_Gold(&g_ctx.save) > gold &&
               g_ctx.state != GS_BATTLE ? "PASS" : "FAIL",
               level + 1, c->level + 1, max_hp, c->max_hp, (unsigned)gold,
               (unsigned)Party_Gold(&g_ctx.save), c->exp_to_next);
        settle();
    }

    /* One formation of each type, as laid out by PrepareEnemyFormation:
     * 9 small, 4 large, mixed, a fiend and Chaos.  (Shown, then left.) */
    {
        static const struct { int formation; const char* name; } FORMS[] = {
            { 0x02, "formation_9small" }, { 0x06, "formation_4large" },
            { 0x07, "formation_mix" }, { 0x73, "formation_fiend" }, { 0x7B, "formation_chaos" },
        };
        int ok = 1;
        for (unsigned k = 0; k < sizeof(FORMS) / sizeof(FORMS[0]); k++) {
            g_ctx.pending_formation = FORMS[k].formation;
            ScreenFx_Start(&g_ctx.fx, SCREENFX_BATTLE, NULL);
            g_ctx.fx_next = FXNEXT_BATTLE;
            g_ctx.state = GS_FX;
            while (g_ctx.state == GS_FX) step(&none, 1);
            step(&none, 2);
            shot(dir, FORMS[k].name);
            ok &= g_ctx.state == GS_BATTLE;
            g_ctx.state = GS_WALK;                    /* leave without fighting */
        }
        printf("%s: formations of every type are built and drawn\n", ok ? "PASS" : "FAIL");
        settle();
    }

    /* The command screens: the command box, the enemy target cursor, and
     * the MAGIC, DRINK and ITEM boxes, each left with B.  (Shown, then the
     * battle is left.) */
    {
        const InputState a = { BTN_A, BTN_A, 0 }, b = { BTN_B, BTN_B, 0 },
                         down = { BTN_DOWN, BTN_DOWN, 0 };
        g_ctx.save.items[25] = 3;                         /* HEAL potions */
        g_ctx.pending_formation = 0x02;
        ScreenFx_Start(&g_ctx.fx, SCREENFX_BATTLE, NULL);
        g_ctx.fx_next = FXNEXT_BATTLE;
        g_ctx.state = GS_FX;
        while (g_ctx.state == GS_FX) step(&none, 1);
        wait_menu();                                      /* past the opening and the walk */
        shot(dir, "ui_command");
        press(&a); wait_menu();
        shot(dir, "ui_target");
        press(&b); press(&down); press(&a); wait_menu();
        shot(dir, "ui_magic");
        press(&b); press(&down); press(&a); wait_menu();
        shot(dir, "ui_drink");
        press(&b); press(&down); press(&a); wait_menu();
        shot(dir, "ui_item");
        g_ctx.state = GS_WALK;                            /* leave without fighting */
        printf("PASS: battle command screens drawn\n");
        settle();
    }

    /* A round of animations, one frame in three saved (anim_NNN): FIGHT for
     * the first three, FIRE from the Red Mage.  (Then the battle is left.) */
    if (getenv("FF1_ANIM_FRAMES")) {
        const InputState a = { BTN_A, BTN_A, 0 }, down = { BTN_DOWN, BTN_DOWN, 0 };
        CharacterMagic* rm = &g_ctx.save.character_magic[3];
        rm->spells[0] = 4 + 1;
        rm->cur_mp[0] = 2;
        g_ctx.pending_formation = 0x02;
        ScreenFx_Start(&g_ctx.fx, SCREENFX_BATTLE, NULL);
        g_ctx.fx_next = FXNEXT_BATTLE;
        g_ctx.state = GS_FX;
        while (g_ctx.state == GS_FX) step(&none, 1);
        const InputState* script[] = { &a, &a, &a, &a, &a, &a, &down, &a, &a, &a };
        for (unsigned k = 0; k < sizeof(script) / sizeof(script[0]); k++) press(script[k]);
        for (int f = 0; f < 900 && g_ctx.state == GS_BATTLE && !Battle_WaitingForInput(); f++) {
            step(&none, 1);
            if (f % 3 == 0) {
                char name[32];
                snprintf(name, sizeof(name), "anim_%03d", f);
                shot(dir, name);
            }
        }
        g_ctx.state = GS_WALK;
        rm->spells[0] = 0;
        settle();
    }

    /* Chaos (formation $7B) with 1 HP: the party fights, then ChaosDeath
     * dissolves him and the backdrop (shot mid-way) and the battle ends as
     * a win with no rewards. */
    {
        const InputState a = { BTN_A, BTN_A, 0 };
        uint16_t hp[4], maxhp[4];
        for (int i = 0; i < 4; i++) {
            hp[i] = g_ctx.save.characters[i].cur_hp;
            maxhp[i] = g_ctx.save.characters[i].max_hp;
            g_ctx.save.characters[i].cur_hp = g_ctx.save.characters[i].max_hp = 999;
        }
        uint32_t gold = Party_Gold(&g_ctx.save);
        g_ctx.pending_formation = 0x7B;
        ScreenFx_Start(&g_ctx.fx, SCREENFX_BATTLE, NULL);
        g_ctx.fx_next = FXNEXT_BATTLE;
        g_ctx.state = GS_FX;
        while (g_ctx.state == GS_FX) step(&none, 1);
        wait_menu();
        g_btl.enemy[0].hp = 1;
        shot(dir, "chaos");
        int frames = 0;
        while (g_ctx.state == GS_BATTLE && !Battle_IsOver() && frames < 20000) {
            if (Battle_WaitingForInput()) press(&a); else step(&none, 1);
            frames++;
        }
        int dissolve = 0;
        for (frames = 0; g_ctx.state == GS_BATTLE && frames < 4000; frames++) {
            step(&none, 1);
            if (frames == 130 + 110 + 1200) { shot(dir, "chaos_dissolve"); dissolve = 1; }
        }
        printf("%s: Chaos dissolves and the battle is won (gold %s)\n",
               dissolve && g_ctx.state != GS_BATTLE && Party_Gold(&g_ctx.save) == gold ? "PASS" : "FAIL",
               Party_Gold(&g_ctx.save) == gold ? "unchanged" : "changed");

        /* The ending: 21 pages over the epilogue picture (A/B can't hurry
         * them), then the empty box opens and "The End" is drawn a pixel a
         * frame; then it stays there. */
        int ending = g_ctx.state == GS_STORY;
        step(&none, 60);
        shot(dir, "ending");
        int k;
        for (k = 0; k < 600; k++) step((k % 8) == 0 ? &a : &none, 1);    /* A doesn't skip */
        int still_page_one = 1;    /* 32 + 512 + 32 frames a page: still on it */
        for (k = 0; k < 60000 && !Story_Finished(); k++) {
            step(&none, 1);
            if (k == 14000) shot(dir, "ending_theend_outline");
        }
        int finished = Story_Finished();
        shot(dir, "ending_theend");
        step(&a, 1);
        step(&none, 100);
        int stays = g_ctx.state == GS_STORY;
        printf("%s: ending: started %d, pages can't be skipped %d, \"The End\" drawn %d (%d more frames), stays %d\n",
               ending && still_page_one && finished && stays ? "PASS" : "FAIL",
               ending, still_page_one, finished, k, stays);
        g_ctx.state = GS_WALK;                        /* (the original never leaves it) */
        for (int i = 0; i < 4; i++) {
            g_ctx.save.characters[i].cur_hp = hp[i];
            g_ctx.save.characters[i].max_hp = maxhp[i];
            g_ctx.save.characters[i].ailments = 0;
        }
        settle();
    }

    /* Running: every character picks RUN (right to the RUN column, then A). */
    {
        const InputState right = { BTN_RIGHT, BTN_RIGHT, 0 }, a = { BTN_A, BTN_A, 0 };
        g_ctx.pending_formation = Battle_PickFormation(Battle_LandDomain(0x99, 0xA5));
        ScreenFx_Start(&g_ctx.fx, SCREENFX_BATTLE, NULL);
        g_ctx.fx_next = FXNEXT_BATTLE;
        g_ctx.state = GS_FX;
        while (g_ctx.state == GS_FX) step(&none, 1);
        int frames = 0;
        while (g_ctx.state == GS_BATTLE && frames < 20000) {
            press(&right);
            press(&a);
            step(&none, 1);
            frames += 3;
        }
        printf("%s: running away ended the battle after %d frames\n",
               g_ctx.state != GS_BATTLE ? "PASS" : "FAIL", frames);
        settle();
    }

    /* Coneria Castle (entrance 9): up the stairs (normal teleport 0) to the
     * throne room floor, through the door, and talk to the King. */
    g_ctx.state = GS_WALK;
    g_ctx.mode = MODE_OW;
    Game_EnterTeleport(&g_ctx, 9);
    settle();
    shot(dir, "castle");
    int stairs_x = -1, stairs_y = -1;
    for (int y = 0; y < 64 && stairs_x < 0; y++)
        for (int x = 0; x < 64; x++) {
            const OWMetatile* m = SM_GetMetatile(SM_GetTileID(x, y));
            if ((m->prop0 & 0xC0) == 0x80 && m->prop1 == 0) { stairs_x = x; stairs_y = y; break; }
        }
    walk_to(stairs_x, stairs_y);
    settle();
    int ok = SM_GetMapId() == 24;
    printf("%s: the castle stairs lead to the throne room floor (map %d)\n",
           ok ? "PASS" : "FAIL", SM_GetMapId());

    int king = -1;
    for (int i = 0; i < MAPOBJ_SLOTS; i++) if (g_ctx.objs.obj[i].id == 0x01) king = i;
    if (king >= 0) {
        int kx = g_ctx.objs.obj[king].phys_x, ky = g_ctx.objs.obj[king].phys_y;
        int there = walk_to(kx, ky + 1);
        shot(dir, "throne_room");
        InputState up = { BTN_UP, BTN_UP, 0 }, a = { BTN_A, BTN_A, 0 };
        step(&up, 1);                       /* face the King (blocked, so no step) */
        step(&none, 1);
        step(&a, 1);
        ok = there && g_ctx.state == GS_DIALOGUE && (g_ctx.inroom & 0x80);
        printf("%s: talking to the King in the throne room: \"%s\"\n", ok ? "PASS" : "FAIL",
               glyphs_ascii(g_ctx.text_glyphs, g_ctx.text_count));
        shot(dir, "king");
    } else {
        printf("FAIL: the King is not on map %d\n", SM_GetMapId());
    }

    /* The altar effect (an orb being lit), halfway through, over the throne room. */
    g_ctx.state = GS_WALK;
    ScreenFx_Start(&g_ctx.fx, SCREENFX_ALTAR, NULL);
    g_ctx.fx_next = FXNEXT_WALK;
    g_ctx.state = GS_FX;
    step(&none, 40);
    shot(dir, "altar");
    int frames = 40;
    while (g_ctx.state == GS_FX) { step(&none, 1); frames++; }
    printf("%s: the altar effect runs and returns to walking (%d frames)\n",
           g_ctx.state == GS_WALK ? "PASS" : "FAIL", frames);

    /* Temple of Fiends (entrance 13): pace between two random-battle floor
     * tiles and count the encounters (8/256 per step on this map). */
    g_ctx.mode = MODE_OW;
    Game_EnterTeleport(&g_ctx, 13);
    settle();
    int ax = -1, ay = -1;
    for (int y = 0; y < 64 && ax < 0; y++)
        for (int x = 0; x < 63; x++) {
            const OWMetatile* a = SM_GetMetatile(SM_GetTileID(x, y));
            const OWMetatile* b = SM_GetMetatile(SM_GetTileID(x + 1, y));
            if ((a->prop0 & 0x1F) == 0x0A && (a->prop1 & 0x80) &&
                (b->prop0 & 0x1F) == 0x0A && (b->prop1 & 0x80) &&
                path_dir(x, y)) { ax = x; ay = y; break; }
        }
    walk_to(ax, ay);
    int battles = 0;
    for (int n = 0; n < 300; n++) {
        walk_to((n & 1) ? ax : ax + 1, ay);
        if (g_ctx.state == GS_FX && g_ctx.fx_next == FXNEXT_BATTLE) {
            battles++;
            g_ctx.state = GS_WALK;            /* skip the battle itself */
        }
    }
    printf("%s: random encounters in the Temple of Fiends: %d in 300 steps\n",
           battles > 0 ? "PASS" : "FAIL", battles);

    /* With random encounters switched off (an option), the same pacing
     * meets none. */
    Game_SetRandomEncounters(0);
    int off_battles = 0;
    for (int n = 0; n < 300; n++) {
        walk_to((n & 1) ? ax : ax + 1, ay);
        if (g_ctx.state == GS_FX && g_ctx.fx_next == FXNEXT_BATTLE) {
            off_battles++;
            g_ctx.state = GS_WALK;
        }
    }
    Game_SetRandomEncounters(1);
    printf("%s: random encounters switched off: %d in 300 steps\n",
           off_battles == 0 ? "PASS" : "FAIL", off_battles);

    /* Game over (last: it restarts the game).  Only the first character is
     * standing, with 1 HP and poison, so the round's end finishes them:
     * the party-perished message, then the game starts over. */
    {
        const InputState a = { BTN_A, BTN_A, 0 };
        g_ctx.mode = MODE_OW;
        for (int i = 1; i < 4; i++) {
            g_ctx.save.characters[i].ailments = 1;        /* dead */
            g_ctx.save.characters[i].cur_hp = 0;
        }
        g_ctx.save.characters[0].ailments = 3;            /* poisoned */
        g_ctx.save.characters[0].cur_hp = 1;
        g_ctx.pending_formation = Battle_PickFormation(Battle_LandDomain(0x99, 0xA5));
        ScreenFx_Start(&g_ctx.fx, SCREENFX_BATTLE, NULL);
        g_ctx.fx_next = FXNEXT_BATTLE;
        g_ctx.state = GS_FX;
        while (g_ctx.state == GS_FX) step(&none, 1);
        int frames = 0;
        while (g_ctx.state == GS_BATTLE && !Battle_IsOver() && frames < 20000) {
            step((frames % 8) == 0 ? &a : &none, 1);
            frames++;
        }
        step(&none, 2);
        shot(dir, "game_over");
        for (frames = 0; g_ctx.state == GS_BATTLE && frames < 200; frames++)
            step((frames % 8) == 0 ? &a : &none, 1);
        /* GameStart: the title screen.  Right/left change the respond
         * rate; Continue with no save starts a new game. */
        int title = g_ctx.state == GS_TITLE;
        const InputState right = { BTN_RIGHT, BTN_RIGHT, 0 }, left = { BTN_LEFT, BTN_LEFT, 0 };
        step(&right, 1); step(&none, 1);
        shot(dir, "title");
        int rate_up = g_ctx.respond_rate == 6;
        step(&left, 1); step(&none, 1);
        step(&a, 1);
        int restarted = title && rate_up && g_ctx.respond_rate == 5 && g_ctx.state != GS_TITLE;
        for (int i = 0; i < 4; i++)
            restarted &= g_ctx.save.characters[i].ailments == 0 && g_ctx.save.characters[i].cur_hp > 0;
        printf("%s: game over: the title screen (%d, respond rate %d), then a new game\n",
               restarted ? "PASS" : "FAIL", title, g_ctx.respond_rate + 1);
    }

    /* Shops: Coneria's doors with a shop id, walked into; each type's loop
     * driven with the pad. */
    {
        SaveData* sv = &g_ctx.save;
        settle();
        Game_EnterTeleport(&g_ctx, 1);
        settle();
        Party_SetGold(sv, 1000);
        int door_x[16], door_y[16], door_id[16], ndoors = 0;
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64 && ndoors < 16; x++) {
                const OWMetatile* m = SM_GetMetatile(SM_GetTileID(x, y));
                if (m && m->prop1 && (m->prop0 & SM_TP_SPEC_MASK) == SM_TP_SPEC_DOOR) {
                    door_x[ndoors] = x; door_y[ndoors] = y; door_id[ndoors++] = m->prop1;
                }
            }
        printf("Coneria shop doors:");
        for (int i = 0; i < ndoors; i++)
            printf(" %d(type %d)@%d,%d", door_id[i], lut_ShopTypes[door_id[i]], door_x[i], door_y[i]);
        printf("\n");

        const InputState A = { BTN_A, BTN_A, 0 }, B = { BTN_B, BTN_B, 0 };
        const InputState D = { BTN_DOWN, BTN_DOWN, 0 };
        #define TAP(in) do { step(&(in), 1); step(&none, 1); } while (0)

        /* Walk into the shop of `type`; 1 once its screen is up. */
        #define ENTER(type) ({ int ok_ = 0; \
            for (int i_ = 0; i_ < ndoors && !ok_; i_++) \
                if (lut_ShopTypes[door_id[i_]] == (type)) { \
                    walk_to(door_x[i_], door_y[i_]); \
                    for (int k_ = 0; k_ < 300 && g_ctx.state != GS_SHOP; k_++) step(&none, 1); \
                    ok_ = g_ctx.state == GS_SHOP; } \
            ok_; })

        /* Weapon shop: buy the first weapon for character 0, sell it back. */
        int in = ENTER(0);
        shot(dir, "shop_weapon");
        uint32_t gold0 = Party_Gold(sv);
        uint8_t slot0 = sv->characters[0].weapons[3];
        sv->characters[0].weapons[3] = 0;
        TAP(A);                                   /* Buy */
        shot(dir, "shop_weapon_list");
        TAP(A);                                   /* the first weapon */
        shot(dir, "shop_weapon_confirm");
        TAP(A);                                   /* Yes */
        shot(dir, "shop_weapon_who");
        TAP(A);                                   /* character 0 */
        shot(dir, "shop_weapon_bought");
        uint32_t gold1 = Party_Gold(sv);
        int bought = gold1 < gold0 && sv->characters[0].weapons[3] != 0;
        TAP(D); TAP(A);                           /* Sell */
        TAP(A);                                   /* character 0 */
        for (int i = 0; i < 3; i++) TAP(D);       /* the 4th weapon: the new one */
        shot(dir, "shop_weapon_sell");
        TAP(A); TAP(A);                           /* that, Yes */
        int sold = Party_Gold(sv) == gold1 + (gold0 - gold1) / 2 && sv->characters[0].weapons[3] == 0;
        TAP(D); TAP(D); TAP(A);                   /* Exit */
        settle();
        printf("%s: weapon shop: entered %d, bought %d (%u -> %u G), sold %d, back walking %d\n",
               in && bought && sold && g_ctx.state == GS_WALK ? "PASS" : "FAIL",
               in, bought, (unsigned)gold0, (unsigned)gold1, sold, g_ctx.state == GS_WALK);
        sv->characters[0].weapons[3] = slot0;

        /* Item shop: buy a HEAL potion. */
        in = ENTER(6);
        uint8_t heals = sv->items[0x19];
        TAP(A); TAP(A);                           /* Buy, the first item */
        shot(dir, "shop_item_confirm");
        TAP(A);                                   /* Yes */
        shot(dir, "shop_item_bought");
        int got = sv->items[0x19] == heals + 1;
        TAP(B); TAP(D); TAP(A);                   /* Exit */
        settle();
        printf("%s: item shop: entered %d, HEAL %d -> %d\n",
               in && got && g_ctx.state == GS_WALK ? "PASS" : "FAIL", in, heals, sv->items[0x19]);

        /* White magic shop: the Fighter can't learn, a White Mage learns the
         * first spell. */
        int fighter = -1, mage = -1;
        for (int p = 0; p < 4; p++) {
            if (sv->characters[p].class == 0 && fighter < 0) fighter = p;
            if ((sv->characters[p].class == 4 || sv->characters[p].class == 3) && mage < 0) mage = p;
        }
        in = ENTER(2);
        shot(dir, "shop_wmagic");
        int refused = 0, learned = 0;
        if (in && fighter >= 0 && mage >= 0) {
            for (int i = 0; i < fighter; i++) TAP(D);
            TAP(A); TAP(A);                       /* the Fighter, the first spell */
            shot(dir, "shop_wmagic_cantlearn");
            refused = Shop_TakeSaveRequest() == 0 && sv->character_magic[fighter].spells[0] == 0;
            uint8_t before[3];
            memcpy(before, sv->character_magic[mage].spells, 3);
            memset(sv->character_magic[mage].spells, 0, 3);
            for (int i = 0; i < mage; i++) TAP(D);
            TAP(A);                               /* the mage */
            shot(dir, "shop_wmagic_list");
            TAP(A); TAP(A);                       /* the first spell, Yes */
            learned = sv->character_magic[mage].spells[0] == 1;
            memcpy(sv->character_magic[mage].spells, before, 3);
        }
        TAP(B);                                   /* leave */
        settle();
        printf("%s: white magic shop: entered %d, fighter refused %d, mage learned %d\n",
               in && refused && learned && g_ctx.state == GS_WALK ? "PASS" : "FAIL", in, refused, learned);

        /* Clinic: character 3 is dead and comes back with 1 HP. */
        sv->characters[3].ailments = 1;
        sv->characters[3].cur_hp = 0;
        in = ENTER(4);
        shot(dir, "shop_clinic");
        TAP(A); TAP(A);                           /* the dead one, Yes */
        shot(dir, "shop_clinic_revived");
        int revived = sv->characters[3].ailments == 0 && sv->characters[3].cur_hp == 1;
        TAP(A);                                   /* the key; "you don't need my help" */
        TAP(A);
        settle();
        printf("%s: clinic: entered %d, revived %d\n",
               in && revived && g_ctx.state == GS_WALK ? "PASS" : "FAIL", in, revived);

        /* Inn: stay, the party fades out and back, HP restored, saved. */
        sv->characters[0].cur_hp = 1;
        in = ENTER(5);
        shot(dir, "shop_inn");
        TAP(A);                                   /* Yes */
        shot(dir, "shop_inn_price");
        TAP(A);                                   /* Yes */
        step(&none, 30);
        shot(dir, "shop_inn_fading");
        step(&none, 200);
        shot(dir, "shop_inn_rested");
        int healed = sv->characters[0].cur_hp == sv->characters[0].max_hp;
        SaveData on_disk;
        int saved = Save_ReadFile(save_path, &on_disk) &&
                    on_disk.characters[0].cur_hp == on_disk.characters[0].max_hp;
        TAP(A);                                   /* fade back in */
        step(&none, 200);
        shot(dir, "shop_inn_goodbye");
        TAP(A);
        settle();
        printf("%s: inn: entered %d, healed %d, saved %d\n",
               in && healed && saved && g_ctx.state == GS_WALK ? "PASS" : "FAIL", in, healed, saved);
        #undef TAP
        #undef ENTER

        /* Continue: GameStart reads the inn's save back and starts on the
         * overworld tile the party entered Coneria from. */
        int ex = g_ctx.ow_return_x, ey = g_ctx.ow_return_y;
        uint32_t gold = Party_Gold(sv);
        sv->characters[0].cur_hp = 1;                 /* not saved */
        int cont = Game_Start(&g_ctx);
        settle();
        shot(dir, "continue");
        printf("%s: continue: from the save %d, at %d,%d (entered Coneria from %d,%d), gold %u, HP %d/%d\n",
               cont && g_ctx.mode == MODE_OW && g_ctx.player.tile_x == ex && g_ctx.player.tile_y == ey &&
               Party_Gold(sv) == gold && sv->characters[0].cur_hp == sv->characters[0].max_hp ? "PASS" : "FAIL",
               cont, g_ctx.player.tile_x, g_ctx.player.tile_y, ex, ey, (unsigned)Party_Gold(sv),
               sv->characters[0].cur_hp, sv->characters[0].max_hp);
    }

    /* The bridge north of Coneria: blocked until the King builds it, then
     * drawn, and walked across (no battle on it; bridgescene set). */
    {
        SaveData* sv = &g_ctx.save;
        settle();
        int bx = sv->bridge_x, by = sv->bridge_y;
        /* The bridge spans water between land on two opposite sides. */
        int dy = OWMap_CanMove(bx, by - 1, 1) && OWMap_CanMove(bx, by + 1, 1);   /* else across x */
        const InputState fwd = { (uint8_t)(dy ? BTN_UP : BTN_LEFT), (uint8_t)(dy ? BTN_UP : BTN_LEFT), 0 };
        int sx = bx + (dy ? 0 : 1), sy = by + (dy ? 1 : 0);        /* the land before it */
        int ex = bx - (dy ? 0 : 1), ey = by - (dy ? 1 : 0);        /* the land after it */
        g_ctx.mode = MODE_OW;
        g_ctx.player.tile_x = sx;
        g_ctx.player.tile_y = sy;
        g_ctx.player.vehicle = 1;
        sv->bridge_vis = 0;
        sv->bridgescene = 0;
        step(&fwd, 1);
        while (g_ctx.player.moving) step(&none, 1);
        int blocked = g_ctx.player.tile_x == sx && g_ctx.player.tile_y == sy;
        sv->bridge_vis = 1;                               /* Talk_Bridge */
        step(&none, 1);
        shot(dir, "bridge");
        step(&fwd, 1);
        while (g_ctx.player.moving) step(&none, 1);
        int on = g_ctx.player.tile_x == bx && g_ctx.player.tile_y == by && sv->bridgescene == 1;
        shot(dir, "bridge_on");

        /* The bridge scene: palette cycle out, then 4 story pages and 4
         * credits pages, each opened by the shutters, read (A here), and
         * closed; bridgescene becomes $80 and the overworld cycles back. */
        const InputState a = { BTN_A, BTN_A, 0 };
        int k;
        for (k = 0; k < 300 && g_ctx.state != GS_STORY; k++) step(&none, 1);
        int scene = g_ctx.state == GS_STORY;
        step(&none, 16);
        shot(dir, "bridge_scene_opening");
        step(&none, 30);
        shot(dir, "bridge_scene");
        int pages = 0;
        for (k = 0; k < 5000 && g_ctx.state == GS_STORY; k++) {
            step(&none, 1);
            if (k % 80 == 79) {                       /* past each opening */
                if (pages == 4) shot(dir, "bridge_scene_credits");
                step(&a, 1);
                pages++;
            }
        }
        settle();
        int done = g_ctx.state == GS_WALK && sv->bridgescene == 0x80;
        step(&fwd, 1);
        while (g_ctx.player.moving) step(&none, 1);
        settle();
        int across = g_ctx.player.tile_x == ex && g_ctx.player.tile_y == ey && g_ctx.state == GS_WALK;
        printf("%s: bridge at %d,%d: blocked before it's built %d, on it %d (no battle), scene %d "
               "(%d pages, bridgescene $%02X), across %d\n",
               blocked && on && scene && done && across ? "PASS" : "FAIL",
               bx, by, blocked, on, scene, pages, sv->bridgescene, across);
    }

    /* The ship at Pravoka: walk onto it (boarding), sail a tile out and
     * back at 2 pixels a frame, and step ashore where it can dock. */
    {
        SaveData* sv = &g_ctx.save;
        settle();
        static const int ddx[4] = { 1, -1, 0, 0 }, ddy[4] = { 0, 0, 1, -1 };
        static const uint8_t dbtn[4] = { BTN_RIGHT, BTN_LEFT, BTN_DOWN, BTN_UP };
        int shx = sv->ship_x, shy = sv->ship_y, land = -1, sea = -1;
        for (int d = 0; d < 4; d++) {
            int x = shx + ddx[d], y = shy + ddy[d];
            if (land < 0 && OWMap_CanMove(x, y, 1) &&
                (OWMap_GetTerrain(x, y) & 0x21) == 0x20) land = d;    /* walkable, the ship docks */
            else if (sea < 0 && OWMap_CanMove(x, y, 4)) sea = d;
        }
        int ok = land >= 0 && sea >= 0, boarded = 0, sailed = 0, docked = 0, frames = 0;
        if (ok) {
            sv->ship_vis = 1;
            g_ctx.mode = MODE_OW;
            g_ctx.player.tile_x = shx + ddx[land];
            g_ctx.player.tile_y = shy + ddy[land];
            g_ctx.player.vehicle = g_ctx.player.vehicle_next = 1;
            #define GO(d) do { const InputState in_ = { dbtn[d], dbtn[d], 0 }; step(&in_, 1); \
                while (g_ctx.player.moving) { step(&none, 1); } step(&none, 6); } while (0)
            GO(land ^ 1);                                  /* onto the ship */
            boarded = g_ctx.player.vehicle == 4 && g_ctx.player.tile_x == shx && g_ctx.player.tile_y == shy;
            shot(dir, "ship_board");
            const InputState out = { dbtn[sea], dbtn[sea], 0 };
            step(&out, 1);
            for (frames = 1; g_ctx.player.moving; frames++) step(&none, 1);
            step(&none, 6);
            sailed = g_ctx.player.vehicle == 4 && g_ctx.player.tile_x == shx + ddx[sea] &&
                     g_ctx.player.tile_y == shy + ddy[sea] && g_ctx.state == GS_WALK;
            shot(dir, "ship_sail");
            GO(sea ^ 1);                                   /* back to the harbor */
            GO(land);                                      /* ashore: the ship docks */
            docked = g_ctx.player.vehicle == 1 && sv->ship_x == shx && sv->ship_y == shy &&
                     g_ctx.player.tile_x == shx + ddx[land];
            shot(dir, "ship_docked");
            #undef GO
        }
        printf("%s: ship at %d,%d: boarded %d, sailed %d (%d frames a tile), docked %d\n",
               ok && boarded && sailed && frames <= 9 && docked ? "PASS" : "FAIL",
               shx, shy, boarded, sailed, frames, docked);
        sv->ship_vis = 0;
    }

    /* The canoe: from land onto a river (only with the canoe), along the
     * river, and back ashore. */
    {
        SaveData* sv = &g_ctx.save;
        settle();
        static const int ddx[4] = { 1, -1, 0, 0 }, ddy[4] = { 0, 0, 1, -1 };
        static const uint8_t dbtn[4] = { BTN_RIGHT, BTN_LEFT, BTN_DOWN, BTN_UP };
        /* a land tile L with a river tile R next to it, and R's opposite
         * neighbour R2 also river (L R R2 in a line) */
        int lx = -1, ly = -1, cd = -1;
        for (int y = 1; y < 255 && cd < 0; y++)
            for (int x = 1; x < 255 && cd < 0; x++) {
                if (!OWMap_CanMove(x, y, 1)) continue;
                for (int d = 0; d < 4; d++) {
                    int rx = x + ddx[d], ry = y + ddy[d];
                    if (!OWMap_CanMove(rx, ry, 1) && OWMap_CanMove(rx, ry, 2) &&
                        !OWMap_CanMove(rx + ddx[d], ry + ddy[d], 1) && OWMap_CanMove(rx + ddx[d], ry + ddy[d], 2)) {
                        lx = x; ly = y; cd = d;
                        break;
                    }
                }
            }
        int blocked = 0, boarded = 0, paddled = 0, ashore = 0;
        if (cd >= 0) {
            g_ctx.mode = MODE_OW;
            g_ctx.player.tile_x = lx;
            g_ctx.player.tile_y = ly;
            g_ctx.player.vehicle = g_ctx.player.vehicle_next = 1;
            #define GO(d) do { const InputState in_ = { dbtn[d], dbtn[d], 0 }; step(&in_, 1); \
                while (g_ctx.player.moving) { step(&none, 1); } step(&none, 6); \
                while (g_ctx.state != GS_WALK) { step(&none, 1); } } while (0)
            sv->has_canoe = 0;
            GO(cd);
            blocked = g_ctx.player.tile_x == lx && g_ctx.player.tile_y == ly;
            sv->has_canoe = 1;
            GO(cd);
            boarded = g_ctx.player.vehicle == 2;
            GO(cd);
            paddled = g_ctx.player.vehicle == 2 && g_ctx.player.tile_x == lx + 2 * ddx[cd] &&
                      g_ctx.player.tile_y == ly + 2 * ddy[cd];
            shot(dir, "canoe");
            GO(cd ^ 1); GO(cd ^ 1);
            ashore = g_ctx.player.vehicle == 1 && g_ctx.player.tile_x == lx && g_ctx.player.tile_y == ly;
            #undef GO
        }
        printf("%s: canoe at %d,%d: blocked without it %d, boarded %d, paddled %d, ashore %d\n",
               cd >= 0 && blocked && boarded && paddled && ashore ? "PASS" : "FAIL",
               lx, ly, blocked, boarded, paddled, ashore);
    }

    /* The airship: A on it takes off (a pixel every other frame), it flies
     * 4 pixels a frame over anything, and A lands it where it may land;
     * elsewhere it takes off again. */
    {
        SaveData* sv = &g_ctx.save;
        const InputState A = { BTN_A, BTN_A, 0 };
        const InputState R = { BTN_RIGHT, BTN_RIGHT, 0 }, L = { BTN_LEFT, BTN_LEFT, 0 };
        settle();
        int ax = sv->airship_x, ay = sv->airship_y;
        uint8_t vis = sv->airship_vis;
        sv->airship_vis = 0x1F;
        g_ctx.mode = MODE_OW;
        g_ctx.player.tile_x = ax;
        g_ctx.player.tile_y = ay;
        g_ctx.player.vehicle = g_ctx.player.vehicle_next = 1;
        step(&A, 1);
        int anim = g_ctx.state == GS_AIRSHIP;
        step(&none, 20);
        shot(dir, "airship_takeoff");
        int k;
        for (k = 0; k < 200 && g_ctx.state != GS_WALK; k++) step(&none, 1);
        int flying = g_ctx.player.vehicle == 8;
        step(&R, 1);
        int frames;
        for (frames = 1; g_ctx.player.moving; frames++) step(&none, 1);
        step(&R, 1);
        while (g_ctx.player.moving) step(&none, 1);
        shot(dir, "airship_fly");
        int moved = g_ctx.player.tile_x == ((ax + 2) & 0xFF) && g_ctx.state == GS_WALK;
        step(&L, 1); while (g_ctx.player.moving) step(&none, 1);
        /* Its starting desert (the Floater's) forbids landing; fly over the
         * nearest tile that allows it and land there. */
        int lx = -1, ly = -1;
        for (int r = 1; r < 40 && lx < 0; r++)
            for (int dy = -r; dy <= r && lx < 0; dy++)
                for (int dx = -r; dx <= r; dx++)
                    if (OWMap_CanMove((ax + dx) & 0xFF, (ay + dy) & 0xFF, 8)) {
                        lx = (ax + dx) & 0xFF; ly = (ay + dy) & 0xFF; break;
                    }
        g_ctx.player.tile_x = lx;
        g_ctx.player.tile_y = ly;
        step(&none, 1);
        step(&A, 1);
        for (k = 0; k < 200 && g_ctx.state != GS_WALK; k++) step(&none, 1);
        shot(dir, "airship_landed");
        int landed = g_ctx.player.vehicle == 1 && sv->airship_x == lx && sv->airship_y == ly;
        /* where it may not land (its own desert), it takes off again */
        step(&A, 1);                                  /* take off */
        for (k = 0; k < 200 && g_ctx.state != GS_WALK; k++) step(&none, 1);
        g_ctx.player.tile_x = ax;
        g_ctx.player.tile_y = ay;
        step(&none, 1);
        step(&A, 1);
        for (k = 0; k < 400 && g_ctx.state != GS_WALK; k++) step(&none, 1);
        int refused = g_ctx.player.vehicle == 8 && sv->airship_x == lx && sv->airship_y == ly;
        printf("%s: airship: takeoff %d, flying %d, %d frames a tile, moved %d, landed at %d,%d %d, "
               "refused at %d,%d %d\n",
               anim && flying && frames <= 5 && moved && landed && refused ? "PASS" : "FAIL",
               anim, flying, frames, moved, lx, ly, landed, ax, ay, refused);
        sv->airship_x = (uint8_t)ax;
        sv->airship_y = (uint8_t)ay;
        g_ctx.player.tile_x = ax;
        g_ctx.player.tile_y = ay;
        g_ctx.player.vehicle = g_ctx.player.vehicle_next = 1;
        sv->airship_vis = vis;
    }

    /* Bahamut with the TAIL: every class is promoted (+6), the TAIL is
     * used up, and when the box closes the map is re-entered (palette
     * cycled in).  Without the TAIL, and after, nothing changes. */
    {
        SaveData* sv = &g_ctx.save;
        const InputState A = { BTN_A, BTN_A, 0 };
        settle();
        SaveData keep = *sv;
        static MapObjects mo;
        memset(&mo, 0, sizeof(mo));
        mo.obj[0].id = mo.obj[0].rawid = 0x0E;            /* OBJID_BAHAMUT */
        sv->game_flags[0x0E] &= (uint8_t)~0x02;           /* not promoted yet */
        sv->items[13] = 0;                                /* no TAIL */
        TalkResult tr;
        Talk_ToObject(sv, &mo, 0, &tr);
        int no_tail = sv->characters[0].class == keep.characters[0].class && !tr.reentermap;
        sv->items[13] = 1;
        Talk_ToObject(sv, &mo, 0, &g_ctx.talk);
        int promoted = 1;
        for (int i = 0; i < 4; i++)
            promoted &= sv->characters[i].class == keep.characters[i].class + 6;
        promoted &= sv->items[13] == 0 && g_ctx.talk.reentermap;
        g_ctx.mode = MODE_OW;
        g_ctx.state = GS_DIALOGUE;
        g_ctx.text_count = 0;
        step(&A, 1);
        int reenter = g_ctx.state == GS_FX;
        settle();
        shot(dir, "class_change");
        Talk_ToObject(sv, &mo, 0, &tr);
        int once = sv->characters[0].class == keep.characters[0].class + 6 && !tr.reentermap;
        printf("%s: class change: refused without the TAIL %d, promoted %d, map re-entered %d, only once %d\n",
               no_tail && promoted && reenter && once ? "PASS" : "FAIL", no_tail, promoted, reenter, once);
        *sv = keep;
    }

    /* The minimap (Select with B held, on the overworld): drawn a pixel
     * row a frame, then the decorations and title; A/B leaves once it's
     * done (presses while drawing are ignored). */
    {
        const InputState selb = { BTN_SELECT | BTN_B, BTN_SELECT, 0 };
        const InputState A = { BTN_A, BTN_A, 0 };
        settle();
        g_ctx.mode = MODE_OW;
        step(&selb, 1);
        int k;
        for (k = 0; k < 200 && g_ctx.state != GS_MINIMAP; k++) step(&none, 1);
        int opened = g_ctx.state == GS_MINIMAP;
        step(&none, 60);
        shot(dir, "minimap_drawing");
        step(&A, 1);
        int ignored = g_ctx.state == GS_MINIMAP;
        step(&none, 150);
        shot(dir, "minimap");
        step(&A, 1);
        int left = g_ctx.state != GS_MINIMAP;
        settle();
        printf("%s: minimap: opened %d, A ignored while drawing %d, left %d, back walking %d\n",
               opened && ignored && left && g_ctx.state == GS_WALK ? "PASS" : "FAIL",
               opened, ignored, left, g_ctx.state == GS_WALK);
    }

    /* The sliding puzzle: in the ship, 54 B presses then the 55th with A
     * held.  Sliding a piece; a puzzle one move from solved, solved, pays
     * 100 G; B quits without a reward. */
    {
        SaveData* sv = &g_ctx.save;
        const InputState A = { BTN_A, BTN_A, 0 }, B = { BTN_B, BTN_B, 0 };
        const InputState AB = { BTN_A | BTN_B, BTN_B, 0 };
        settle();
        g_ctx.mode = MODE_OW;
        g_ctx.player.tile_x = sv->ship_x;               /* afloat at Pravoka */
        g_ctx.player.tile_y = sv->ship_y;
        g_ctx.player.vehicle = g_ctx.player.vehicle_next = 4;
        g_ctx.joy_b = 0;
        for (int i = 0; i < 54; i++) { step(&B, 1); step(&none, 1); }
        int early = g_ctx.state == GS_WALK;
        step(&AB, 1);
        int k;
        for (k = 0; k < 200 && g_ctx.state != GS_MINIGAME; k++) step(&none, 1);
        int opened = g_ctx.state == GS_MINIGAME;
        step(&none, 4);
        shot(dir, "minigame");
        static const uint8_t near[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0, 15 };
        Minigame_SetPuzzle(near, 15);
        uint32_t gold = Party_Gold(sv);
        step(&A, 1);                                    /* 15 slides left: solved */
        step(&none, 8);
        shot(dir, "minigame_slide");
        step(&none, 20);
        step(&A, 1);                                    /* leave the victory loop */
        for (k = 0; k < 300 && g_ctx.state != GS_WALK; k++) step(&none, 1);
        int paid = g_ctx.state == GS_WALK && Party_Gold(sv) == gold + 100;
        /* again, quitting with B */
        g_ctx.joy_b = 0;
        for (int i = 0; i < 54; i++) { step(&B, 1); step(&none, 1); }
        step(&AB, 1);
        for (k = 0; k < 200 && g_ctx.state != GS_MINIGAME; k++) step(&none, 1);
        step(&none, 2);
        step(&B, 1);
        for (k = 0; k < 300 && g_ctx.state != GS_WALK; k++) step(&none, 1);
        int quit = g_ctx.state == GS_WALK && Party_Gold(sv) == gold + 100;
        printf("%s: minigame: not before the 55th B %d, opened %d, solved pays 100 G %d, B quits %d\n",
               early && opened && paid && quit ? "PASS" : "FAIL", early, opened, paid, quit);
        g_ctx.player.tile_x = sv->ship_x + 1;
        g_ctx.player.vehicle = g_ctx.player.vehicle_next = 1;
    }

    /* Music: each place plays its track (PrepOverworld by vehicle,
     * PrepStandardMap by tileset, the menu, battle and back). */
    {
        SaveData* sv = &g_ctx.save;
        const InputState S = { BTN_START, BTN_START, 0 }, B = { BTN_B, BTN_B, 0 };
        settle();
        g_ctx.mode = MODE_OW;
        g_ctx.player.vehicle = g_ctx.player.vehicle_next = 1;
        Game_EnterTeleport(&g_ctx, 1);                 /* Coneria */
        settle(); step(&none, 4);
        uint8_t town = Music_CurrentSong();
        int town_ok = town == Music_TilesetTrack(SM_GetTileset());
        step(&S, 1);
        for (int k = 0; k < 200 && g_ctx.state != GS_MENU; k++) step(&none, 1);
        step(&none, 4);
        int menu_ok = Music_CurrentSong() == MUSIC_MENU;
        step(&B, 1); settle(); step(&none, 4);
        int back_ok = Music_CurrentSong() == town;
        g_ctx.pending_formation = 0;
        g_ctx.pending_backdrop_tile = 0;
        ScreenFx_Start(&g_ctx.fx, SCREENFX_BATTLE, NULL);
        g_ctx.fx_next = FXNEXT_BATTLE;
        g_ctx.state = GS_FX;
        while (g_ctx.state == GS_FX) step(&none, 1);
        step(&none, 4);
        int battle_ok = Music_CurrentSong() == MUSIC_BATTLE && g_music_followup == MUSIC_BATTLE;
        g_ctx.state = GS_WALK;                         /* leave the battle */
        g_music_followup = 0;
        /* out on the overworld, then the ship and the airship */
        Game_EnterTeleport(&g_ctx, 1);
        settle();
        const InputState down = { BTN_DOWN, BTN_DOWN, 0 };
        for (int k = 0; k < 60 && g_ctx.mode == MODE_SM; k++) {
            step(&down, 1);
            while (g_ctx.player.moving || g_ctx.state == GS_FX) step(&none, 1);
        }
        settle(); step(&none, 4);
        int ow_ok = g_ctx.mode == MODE_OW && Music_CurrentSong() == MUSIC_OVERWORLD;
        printf("%s: music: town $%02X %d, menu %d, back in town %d, battle %d, overworld %d\n",
               town_ok && menu_ok && back_ok && battle_ok && ow_ok ? "PASS" : "FAIL",
               town, town_ok, menu_ok, back_ok, battle_ok, ow_ok);
        (void)sv;
    }

    /* Lineup (Select): swap characters 0 and 1; then with character 1
     * dead, making it the leader is refused. */
    {
        SaveData* sv = &g_ctx.save;
        const InputState A = { BTN_A, BTN_A, 0 }, B = { BTN_B, BTN_B, 0 };
        const InputState D = { BTN_DOWN, BTN_DOWN, 0 }, SEL = { BTN_SELECT, BTN_SELECT, 0 };
        #define TAP(in) do { step(&(in), 1); step(&none, 1); } while (0)
        #define OPEN_LINEUP() do { settle(); step(&SEL, 1); \
            for (int k_ = 0; k_ < 200 && g_ctx.state != GS_LINEUP; k_++) step(&none, 1); } while (0)
        CharacterStats before[4];
        CharacterMagic mbefore[4];
        memcpy(before, sv->characters, sizeof(before));
        memcpy(mbefore, sv->character_magic, sizeof(mbefore));
        for (int i = 0; i < 4; i++) sv->characters[i].ailments = 0;
        uint8_t c0 = sv->characters[0].class, c1 = sv->characters[1].class;
        OPEN_LINEUP();
        int opened = g_ctx.state == GS_LINEUP;
        shot(dir, "lineup");
        TAP(A);
        step(&none, 30);                              /* slides left */
        TAP(D); TAP(A);
        step(&none, 20);
        shot(dir, "lineup_swap");
        step(&none, 120);                             /* both reach their new slots */
        TAP(B);
        settle();
        int swapped = g_ctx.state == GS_WALK && sv->characters[0].class == c1 &&
                      sv->characters[1].class == c0;

        /* Swap back, then try to make a dead character the leader. */
        OPEN_LINEUP();
        TAP(A); step(&none, 30); TAP(D); TAP(A); step(&none, 140); TAP(B);
        settle();
        int back = sv->characters[0].class == c0 && sv->characters[1].class == c1;
        sv->characters[1].ailments = 1;
        OPEN_LINEUP();
        TAP(A); step(&none, 30); TAP(D); TAP(A); step(&none, 140); TAP(B);
        settle();
        int refused = sv->characters[0].class == c0 && sv->characters[1].ailments == 1;
        memcpy(sv->characters, before, sizeof(before));
        memcpy(sv->character_magic, mbefore, sizeof(mbefore));
        printf("%s: lineup: opened %d, swapped %d, swapped back %d, dead leader refused %d\n",
               opened && swapped && back && refused ? "PASS" : "FAIL", opened, swapped, back, refused);
        #undef TAP
        #undef OPEN_LINEUP
    }

    /* The field menu, on the overworld: items (HEAL, TENT with a save),
     * magic (CURE), status, equipment, then EXIT from Coneria. */
    {
        SaveData* sv = &g_ctx.save;
        const InputState A = { BTN_A, BTN_A, 0 }, B = { BTN_B, BTN_B, 0 };
        const InputState D = { BTN_DOWN, BTN_DOWN, 0 };
        const InputState R = { BTN_RIGHT, BTN_RIGHT, 0 }, S = { BTN_START, BTN_START, 0 };
        #define TAP(in) do { step(&(in), 1); step(&none, 1); } while (0)
        #define OPEN_MENU() do { settle(); step(&S, 1); \
            for (int k_ = 0; k_ < 200 && g_ctx.state != GS_MENU; k_++) step(&none, 1); } while (0)
        /* Right presses from the first item to item `id` (the list is in id order). */
        #define ITEM_STEPS(id) ({ int n_ = 0; for (int i_ = 1; i_ < (id); i_++) \
            { if ((i_ < 18 || i_ > 21) && sv->items[i_]) n_++; } n_; })

        settle();
        sv->items[25] = 3;                            /* HEAL */
        sv->items[22] = 1;                            /* TENT */
        sv->characters[0].ailments = 0;
        sv->characters[0].cur_hp = 10;
        OPEN_MENU();
        shot(dir, "menu_main");
        int in_menu = g_ctx.state == GS_MENU;

        /* ITEM: HEAL on character 0. */
        TAP(A);
        shot(dir, "menu_item");
        for (int i = ITEM_STEPS(25); i > 0; i--) TAP(R);
        TAP(A);
        shot(dir, "menu_item_target");
        TAP(A);
        shot(dir, "menu_item_healed");
        TAP(A);
        int healed = sv->characters[0].cur_hp == 40 && sv->items[25] == 2;

        /* TENT: 30 HP each, then save. */
        remove(save_path);
        for (int i = ITEM_STEPS(22); i > 0; i--) TAP(R);
        sv->characters[1].cur_hp = 1;
        TAP(A);
        shot(dir, "menu_tent");
        TAP(A);                                       /* save */
        shot(dir, "menu_tent_saved");
        SaveData disk;
        int saved = Save_ReadFile(save_path, &disk) && disk.characters[1].cur_hp == 31 &&
                    disk.items[22] == 0;
        TAP(A);
        TAP(B);                                       /* back to the main menu */
        printf("%s: menu items: in the menu %d, HEAL 10 -> %d HP (%d left), TENT saved %d\n",
               in_menu && healed && saved ? "PASS" : "FAIL", in_menu,
               sv->characters[0].cur_hp, sv->items[25], saved);

        /* MAGIC: the Red Mage (character 3) casts CURE on character 0. */
        uint8_t spells[32], mp[8];
        CharacterMagic* rm = &sv->character_magic[3];
        memcpy(spells, rm->spells, 32);
        memcpy(mp, rm->cur_mp, 8);
        memset(rm->spells, 0, 32);
        rm->spells[0] = 1;                            /* CURE */
        rm->cur_mp[0] = 2;
        sv->characters[0].cur_hp = 5;
        TAP(D); TAP(A);                               /* MAGIC */
        TAP(R); TAP(D);                               /* character 3 */
        shot(dir, "menu_magic_who");
        TAP(A);
        shot(dir, "menu_magic");
        TAP(A);                                       /* CURE */
        TAP(A);                                       /* on character 0 */
        shot(dir, "menu_magic_cured");
        int cured = sv->characters[0].cur_hp >= 5 + 16 && sv->characters[0].cur_hp <= 5 + 31 &&
                    rm->cur_mp[0] == 1;
        TAP(A);
        TAP(B);
        memcpy(rm->spells, spells, 32);
        memcpy(rm->cur_mp, mp, 8);
        printf("%s: menu magic: CURE 5 -> %d HP, MP %d\n", cured ? "PASS" : "FAIL",
               sv->characters[0].cur_hp, rm->cur_mp[0]);

        /* STATUS of character 0. */
        for (int i = 0; i < 4; i++) TAP(D);
        TAP(A); TAP(A);
        shot(dir, "menu_status");
        TAP(A);

        /* WEAPON: unequip and re-equip character 0's first weapon. */
        TAP(D); TAP(D); TAP(A);
        shot(dir, "menu_weapon");
        uint8_t w0 = sv->characters[0].weapons[0];
        TAP(A); TAP(A);                               /* EQUIP mode, slot 0 */
        shot(dir, "menu_weapon_toggled");
        TAP(B); TAP(B);
        uint8_t w1 = sv->characters[0].weapons[0];
        TAP(D); TAP(D); TAP(A); TAP(A); TAP(A); TAP(B); TAP(B);   /* again */
        printf("%s: menu weapons: slot 0 %02X -> %02X -> %02X\n",
               (w0 ^ w1) == 0x80 && sv->characters[0].weapons[0] == w0 ? "PASS" : "FAIL",
               w0, w1, sv->characters[0].weapons[0]);
        TAP(B);                                       /* close */
        settle();
        int closed = g_ctx.state == GS_WALK;

        /* EXIT in Coneria: back out on the overworld. */
        Game_EnterTeleport(&g_ctx, 1);
        settle();
        CharacterMagic* m0 = &sv->character_magic[0];
        memcpy(spells, m0->spells, 32);
        memcpy(mp, m0->cur_mp, 8);
        memset(m0->spells, 0, 32);
        m0->spells[5 * 4] = 2;                        /* EXIT: level 6, 2nd spell */
        m0->cur_mp[5] = 1;
        int was_sm = g_ctx.mode == MODE_SM;
        OPEN_MENU();
        TAP(D); TAP(A); TAP(A);                       /* MAGIC, character 0 */
        TAP(A);                                       /* EXIT */
        shot(dir, "menu_exit");
        TAP(A);
        settle();
        int out = g_ctx.mode == MODE_OW && g_ctx.state == GS_WALK && m0->cur_mp[5] == 0;
        memcpy(m0->spells, spells, 32);
        memcpy(m0->cur_mp, mp, 8);
        printf("%s: menu closes with B %d; EXIT from Coneria (map %d) -> overworld %d at %d,%d\n",
               closed && was_sm && out ? "PASS" : "FAIL", closed, was_sm, out,
               g_ctx.player.tile_x, g_ctx.player.tile_y);
        #undef TAP
        #undef OPEN_MENU
        #undef ITEM_STEPS
    }
    /* Story NPCs that open the way: talk without the item (nothing), with
     * it (the NPC goes), then step where it stood.  The Black Orb in the
     * Temple of Fiends (all four orbs lit: the time loop), and Onrac's
     * submarine engineer (the OXYALE). */
    {
        SaveData* sv = &g_ctx.save;
        const InputState A = { BTN_A, BTN_A, 0 };
        /* then: the direction of the teleport after the NPC's tile (0: its
         * own tile teleports) */
        static const struct { const char* name; int entrance, map, ox, oy, n; uint8_t then; } npc[2] = {
            { "Black Orb", 13, 12, 20, 17, 4, 0 },
            { "submarine engineer", 7, 6, 45, 30, 1, BTN_RIGHT },
        };
        SaveData keep = *sv;
        for (int i = 0; i < 2; i++) {
            uint8_t* item = i == 0 ? &sv->items[18] : &sv->items[16];   /* the orbs / OXYALE */
            settle();
            Game_EnterTeleport(&g_ctx, (uint8_t)npc[i].entrance);
            settle();
            /* stand next to the NPC (below, else above, left, right) */
            static const int ax[4] = { 0, 0, -1, 1 }, ay[4] = { 1, -1, 0, 0 };
            static const uint8_t face[4] = { BTN_UP, BTN_DOWN, BTN_RIGHT, BTN_LEFT };
            /* stand below it (the Temple's floor has random encounters, so
             * the party is put there rather than walked) */
            int d = 0;
            g_ctx.player.tile_x = npc[i].ox + ax[d];
            g_ctx.player.tile_y = npc[i].oy + ay[d];
            int there = SM_CanWalk(g_ctx.player.tile_x, g_ctx.player.tile_y);
            memset(item, 0, npc[i].n);
            const InputState f = { face[d], face[d], 0 };
            step(&f, 1); step(&none, 1);                     /* turn to it (blocked) */
            int slot = -1;
            for (int k = 0; k < MAPOBJ_SLOTS; k++)
                if (g_ctx.objs.obj[k].rawid == (i == 0 ? 0xCA : 0x10)) slot = k;
            step(&A, 1); step(&none, 1); step(&A, 1); settle();
            int stays = slot >= 0 && g_ctx.objs.obj[slot].id != 0;
            memset(item, 1, npc[i].n);
            step(&A, 1); step(&none, 1);
            shot(dir, i == 0 ? "black_orb" : "sub_engineer");
            step(&A, 1); settle();
            int gone = slot >= 0 && g_ctx.objs.obj[slot].id == 0;
            int map0 = SM_GetMapId();
            step(&f, 1);
            for (int k = 0; k < 400 && (g_ctx.player.moving || g_ctx.state != GS_WALK); k++) step(&none, 1);
            if (npc[i].then) {
                const InputState t2 = { npc[i].then, npc[i].then, 0 };
                step(&t2, 1);
                for (int k = 0; k < 400 && (g_ctx.player.moving || g_ctx.state != GS_WALK); k++) step(&none, 1);
            }
            int moved = SM_GetMapId() != map0;
            printf("%s: %s (map %d): next to it %d, stays without the item %d, gone with it %d, "
                   "its tile leads on %d (map %d -> %d)\n",
                   there && stays && gone && moved ? "PASS" : "FAIL", npc[i].name, npc[i].map,
                   there, stays, gone, moved, map0, SM_GetMapId());
            *sv = keep;
        }
    }

    return 0;
}
