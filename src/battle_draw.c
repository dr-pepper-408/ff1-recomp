/**
 * @file battle_draw.c
 * @brief The battle screen, laid out as the original draws it.
 *
 * EnterBattle's frame (the enemy box, the party box, the four status boxes
 * and the backdrop rows), the formation (DrawFormation_* / fiend and Chaos
 * TSA), the party sprites, the status column (DrawCharacterStatus), and the
 * bottom message area: the roster and command boxes during input, the
 * MAGIC / ITEM / DRINK boxes, and the five combat boxes during turns; and
 * the animations' sprites (weapon swings, spells, explosions), the magic
 * flash and the screen shake, as the script in battle.c moves them.
 * Positions come from the original's tables (battle_ui.h).
 */

#include "battle_internal.h"
#include "ppu.h"
#include "ui.h"
#include "text.h"
#include "../data/generated/battle_palettes.h"
#include "../data/generated/enemy_names.h"
#include "../data/generated/fiend_tsa.h"
#include "../data/generated/battle_ui.h"

/* The message area (btl_msgbuffer) starts at nametable row 18. */
#define MSG_ROW 18

/* FormatBattleString makes two rows per line, a top row for kana marks
 * (blank in this version) over the letters, so a text block's letters land
 * one row below its position. */
#define TEXT_ROW (MSG_ROW + 1)

/* Item name ids (Constants.inc): spells start at MG_START. */
#define BTL_MG_START 0xB0

/* FF1 battle sprite palettes (bank_0F LoadBattleSpritePalettes).  Palette
 * 2 is the "variable" one (weapons, spells, explosions); palette 3 also
 * draws stoned characters. */
const uint8_t (*const btl_sprite_pals)[4] = (const uint8_t (*)[4])BattleSpritePalettes;

/* Battle sprite tiles ($1000 pattern table) in bank_09: the characters'
 * graphics from lut_BatSprCHR ($9000, tile $100 + class*32), tiles $80+
 * (weapons, spells, explosions, the cursor) from lut_BatObjCHR ($A800). */
#define SPR_OBJ(t)   (0x200 + (t))

/* Enemy graphic tile offsets within a battle BG CHR set (bank_0B
 * DrawSmallEnemy / DrawLargeEnemy).  graphic 0/2 = small (4x4 tiles),
 * 1/3 = large (6x6 tiles). */
static const int ENEMY_GFX_TILE[4] = { 0x12, 0x32, 0x22, 0x56 };
static const int ENEMY_GFX_SIZE[4] = { 4, 6, 4, 6 };

/* data_NothingText */
static const uint8_t NOTHING_TEXT[] = { 0x97, 0xB2, 0xB7, 0xAB, 0xAC, 0xB1, 0xAA };

static void draw_tiles(const uint8_t* t, int n, int tx, int ty) {
    for (int i = 0; i < n; i++) UI_DrawTile(t[i], tx + i, ty);
}

static void draw_text(const BtlText* t, int tx, int ty) {
    draw_tiles(t->t, t->len, tx, ty);
}

/* ---- Palettes ---- */

/* An enemy palette (NES colors) from the formation's palette id. */
static const uint8_t* enemy_palette(int palbit) {
    int id = palbit ? g_btl.enemy_pal1 : g_btl.enemy_pal0;
    return battle_palette_table[id >> 3] + (id & 7) * 4;
}

/* Backdrop in BG 0, the formation's two enemy palettes in BG 1/2, the
 * black border palette in BG 3 (LoadBattleBGPalettes) and the four battle
 * sprite palettes (LoadBattleSpritePalettes). */
static void load_palettes(void) {
    static const uint8_t border[4] = { 0x0F, 0x00, 0x0F, 0x30 };
    Ppu_SetPalette(0, g_btl.backdrop_pal);
    Ppu_SetPalette(1, enemy_palette(0));
    Ppu_SetPalette(2, enemy_palette(1));
    Ppu_SetPalette(3, border);
    for (int p = 0; p < 4; p++) Ppu_SetPalette(4 + p, p == 2 ? g_btl.varpal : btl_sprite_pals[p]);
    if (g_btl.bg_flash) g_ppu.palette[0] = g_btl.bg_flash;   /* DoMagicFlash */
}

/* A tile of the battle BG pattern table ($00-$7F: the backdrop and the
 * enemies), minus the pixel rows ChaosDeath has erased. */
static void bg_tile(ChrBank bank, int page_base, int t, int x, int y, int pal) {
    Ppu_DrawBgTile(bank, page_base + t, x, y, pal);
    uint8_t erased = g_btl.bg_erased[t & 0x7F];
    for (int r = 0; erased && r < 8; r++)
        if (erased & (1 << r)) Ppu_FillRect(x, y + r, 8, 1, 0);
}

/* ---- The frame (EnterBattle) ---- */

/* DrawBattleBackdropRow x4: 14 columns in the enemy box, then (after the
 * two box borders) 6 in the party box. */
static void draw_backdrop(void) {
    ChrBank bank = (g_btl.backdrop < 8) ? CHR_BANK_07 : CHR_BANK_08;
    int base = (g_btl.backdrop % 8) * 128;
    for (int row = 0; row < 4; row++) {
        for (int c = 0; c < 14; c++)
            bg_tile(bank, base, lut_BackdropLayout[c] + row * 4, (2 + c) * 8, (2 + row) * 8, 0);
        for (int c = 0; c < 6; c++)
            bg_tile(bank, base, lut_BackdropLayout[c] + row * 4, (18 + c) * 8, (2 + row) * 8, 0);
    }
}

static void draw_frame(void) {
    UI_DrawBox(1, 1, 16, 18);                 /* enemies */
    UI_DrawBox(17, 1, 8, 18);                 /* party */
    for (int y = 21; y >= 3; y -= 6)          /* status boxes, bottom up */
        UI_DrawBox(25, y, 6, 7);
    draw_backdrop();
}

/* ---- Enemies ---- */

/* A fiend's / Chaos's TSA: rows x cols tile ids from the enemy CHR page
 * (0 = blank), the palette of each 16x16 cell from the $10 attribute bytes
 * (attribute rows 1-4, columns 0-3; ApplyPalette_FiendChaos). */
static void draw_tsa(const uint8_t* tsa, int cols, int rows, const uint8_t* attr, int nes_x, int nes_y) {
    ChrBank bank = (g_btl.enemy_page < 8) ? CHR_BANK_07 : CHR_BANK_08;
    int base = (g_btl.enemy_page % 8) * 128;
    for (int ty = 0; ty < rows; ty++)
        for (int tx = 0; tx < cols; tx++) {
            uint8_t t = tsa[ty * cols + tx];
            if (!t) continue;
            int x = nes_x + tx * 8, y = nes_y + ty * 8;
            uint8_t a = attr[(y / 32 - 1) * 4 + x / 32];
            int pal = (a >> (((y % 32) / 16) * 4 + ((x % 32) / 16) * 2)) & 3;
            bg_tile(bank, base, t, x, y, pal);
        }
}

/* One enemy as background tiles: small 4x4 or large 6x6 (DrawSmallEnemy /
 * DrawLargeEnemy), or the fiend / Chaos; less the rows EraseEnemyGraphic
 * has cleared so far (from the enemy's top, or row 6 for a fiend). */
static void draw_enemy(int slot) {
    int erased = g_btl.enemy_erased[slot];
    if (g_btl.battle_type >= 3) {
        if (erased) Ppu_SetClip(0, 48 + erased * 8, PPU_WIDTH, PPU_HEIGHT);
        if (g_btl.battle_type == 3) {
            const uint8_t* t = fiend_tsa[g_btl.fiend_gfx & 3];
            draw_tsa(t, 8, 8, t + 0x40, 32, 64);
        } else {
            draw_tsa(chaos_tsa, 14, 12, chaos_tsa + 0xA8, 16, 48);
        }
        Ppu_ResetClip();
        return;
    }
    int x, y, w, h;
    btl_enemy_rect(slot, &x, &y, &w, &h);
    int gfx = g_btl.enemy[slot].gfx;
    int size = ENEMY_GFX_SIZE[gfx];
    ChrBank bank = (g_btl.enemy_page < 8) ? CHR_BANK_07 : CHR_BANK_08;
    int base = (g_btl.enemy_page % 8) * 128 + ENEMY_GFX_TILE[gfx];
    int pal = 1 + g_btl.enemy[slot].pal;          /* BG palettes 1/2 */
    for (int ty = erased; ty < size; ty++)
        for (int tx = 0; tx < size; tx++)
            Ppu_DrawBgTile(bank, base + ty * size + tx, x + tx * 8, y + ty * 8, pal);
}

/* ---- The party ---- */

/* DrawCharacter: a character's 16x24 sprite in their pose
 * (lut_CharacterPoseTSA, 2 tiles a row); lying down (24x16, from tile $1A,
 * one tile left and down) when dead; crouched in palette 3 when stoned. */
static void draw_character(const SaveData* save, int i) {
    if (g_btl.chr_hidden & (1 << i)) return;      /* FlashCharacterSprite */
    Ppu_SpriteGroup(1);                           /* (one cutout per character in 3D) */
    uint8_t cls = save->characters[i].class;
    int base = 0x100 + cls * 32;
    int pal = (cls < 12) ? lut_InBattleCharPaletteAssign[cls] : 0;
    int x = g_btl.chr_x[i], y = 0x30 + i * 0x18;
    uint8_t ail = g_btl.disp_ail[i];

    if (ail & BTL_AIL_DEAD) {
        for (int row = 0; row < 2; row++)
            for (int col = 0; col < 3; col++)
                Ppu_DrawSprite(CHR_BANK_09, base + 0x1A + row * 3 + col, 0xA8 + col * 8,
                               y + 8 + row * 8, pal, 0);
        return;
    }
    uint8_t pose = g_btl.chr_pose[i];
    if (pose == POSE_NATURAL) pose = btl_natural_pose(save, i);
    if (ail & BTL_AIL_STONE) {
        pal = 3;
        pose = POSE_CROUCH;
        x = 0xB0;
    }
    for (int row = 0; row < 3; row++) {
        int t = base + lut_CharacterPoseTSA[pose + row];
        Ppu_DrawSprite(CHR_BANK_09, t,     x,     y + row * 8, pal, 0);
        Ppu_DrawSprite(CHR_BANK_09, t + 1, x + 8, y + row * 8, pal, 0);
    }
}

/* The weapon (lut_WeaponSwingTSA: frame A as is, frame B flipped with its
 * lower-left tile hidden) or spell sprite (palette 2; its B frame is the
 * next 4 tiles). */
static void draw_attack_sprite(void) {
    if (!g_btl.atk_on) return;
    Ppu_SpriteGroup(0);
    int x = g_btl.atk_x, y = g_btl.atk_y;
    if (g_btl.atk_magic) {
        int t = g_btl.atk_tile + (g_btl.atk_frame_b ? 4 : 0);
        for (int k = 0; k < 4; k++)
            Ppu_DrawSprite(CHR_BANK_09, SPR_OBJ(t + k), x + (k & 1) * 8, y + (k >> 1) * 8, 2, 0);
        return;
    }
    const uint8_t* tsa = &lut_WeaponSwingTSA[g_btl.atk_frame_b ? 8 : 0];
    for (int k = 0; k < 4; k++) {
        if (k == 2 && g_btl.atk_hidell) continue;
        uint8_t attr = tsa[4 + k];
        Ppu_DrawSprite(CHR_BANK_09, SPR_OBJ(g_btl.atk_tile + tsa[k]), x + (k & 1) * 8,
                       y + (k >> 1) * 8, attr & 3, attr & (PPU_FLIP_X | PPU_FLIP_Y));
    }
}

/* DrawExplosion_Frame: up to 3 16x16 explosions (palette 2). */
static void draw_explosions(void) {
    for (int s = 0; s < 3; s++) {
        if (!g_btl.expl_on[s]) continue;
        Ppu_SpriteGroup(0);
        for (int k = 0; k < 4; k++)
            Ppu_DrawSprite(CHR_BANK_09, SPR_OBJ(g_btl.expl_tile[s] + k),
                           g_btl.expl_x[s] + (k & 1) * 8, g_btl.expl_y[s] + (k >> 1) * 8, 2, 0);
    }
}

/* DrawCharacterStatus: name, then the first ailment's label (battle message
 * $41 + bit) or "HP", then the HP (4 tiles, leading zeros blank). */
static void draw_status(const SaveData* save) {
    for (int i = 0; i < 4; i++) {
        const CharacterStats* c = &save->characters[i];
        uint8_t ail = g_btl.disp_ail[i];
        int addr = lut_CharStatusPPUAddr[i] - 0x2000;
        int tx = addr % 32, ty = addr / 32;

        BtlText t;
        btl_text_char_name(&t, c);
        draw_text(&t, tx, ty + 1);

        int bit = 0;
        while (bit < 7 && !(ail & (1 << bit))) bit++;
        btl_text_raw(&t, Text_BattleMessage(0x41 + bit));
        draw_tiles(t.t, t.len > 4 ? 4 : t.len, tx, ty + 3);

        uint8_t hp[4];
        unsigned v = g_btl.disp_hp[i];
        hp[0] = 0xFF;                                  /* no thousands digit */
        hp[1] = (uint8_t)(0x80 + (v / 100) % 10);
        hp[2] = (uint8_t)(0x80 + (v / 10) % 10);
        hp[3] = (uint8_t)(0x80 + v % 10);
        if (hp[1] == 0x80) {                           /* blank leading zeros */
            hp[1] = 0xFF;
            if (hp[2] == 0x80) hp[2] = 0xFF;
        }
        draw_tiles(hp, 4, tx, ty + 4);
    }
}

/* ---- The message area ---- */

static void draw_box_at(const uint8_t* box, int dy) {
    UI_DrawBox(box[1], MSG_ROW + box[2] + dy, box[3], box[4]);
}

/* DrawRosterBox: the enemy kinds, at rows 1,3,5,7 of the box. */
static void draw_roster(void) {
    draw_box_at(lut_EnemyRosterBox, 0);
    for (int k = 0; k < 4; k++)
        if (g_btl.roster[k] != 0xFF)
            UI_DrawAscii(enemy_name_table[g_btl.roster[k]], lut_EnemyRosterBox[1] + 2,
                         TEXT_ROW + lut_EnemyRosterBox[2] + 1 + k * 2);
}

/* DrawCommandBox */
static void draw_command_box(void) {
    draw_box_at(lua_BattleCommandBoxInfo, 0);
    for (int k = 0; k < 5; k++) {
        int n = 0;
        while (n < 4 && lut_BattleCommandText[k][n]) n++;
        draw_tiles(lut_BattleCommandText[k], n, lut_BattleCommandTextPos[k * 2],
                   TEXT_ROW + lut_BattleCommandTextPos[k * 2 + 1]);
    }
}

/* Append an item name (code $0E: at most 8 tiles). */
static int add_item(uint8_t* row, int n, uint8_t item_id) {
    return n + Text_ToTiles(Text_ItemName(item_id), row + n, 8);
}

static int add_spaces(uint8_t* row, int n, int count) {
    while (count-- > 0) row[n++] = 0xFF;
    return n;
}

/* DrawBattleMagicBox: one page of 4 levels -- "L#", the three spells (4
 * spaces for an empty slot) and the level's charges as one digit. */
static void draw_magic_box(const SaveData* save) {
    draw_box_at(lut_CombatItemMagicBox, 0);
    int p = g_btl.cur_char;
    int page = g_btl.magic_row / 4;
    for (int r = 0; r < 4; r++) {
        int level = page * 4 + r;
        uint8_t row[40];
        int n = 0;
        row[n++] = 0x95;                               /* 'L' */
        row[n++] = (uint8_t)(0x81 + level);
        for (int c = 0; c < 3; c++) {
            n = add_spaces(row, n, 1);
            int si = btl_spell_at(save, p, level, c);
            if (si >= 0) n = add_item(row, n, (uint8_t)(BTL_MG_START + si));
            else n = add_spaces(row, n, 4);
        }
        n = add_spaces(row, n, 1);
        row[n++] = (uint8_t)(0x80 + save->character_magic[p].cur_mp[level]);
        draw_tiles(row, n, lut_CombatItemMagicBox[1] + 1, TEXT_ROW + lut_CombatItemMagicBox[2] + 1 + r * 2);
    }
}

/* DrawBattleItemBox: per row, weapon slot (7 spaces if empty), two spaces,
 * then the armor slot. */
static void draw_item_box(const SaveData* save) {
    draw_box_at(lut_CombatItemMagicBox, 0);
    for (int r = 0; r < 4; r++) {
        uint8_t row[40];
        int n = add_spaces(row, 0, 1);
        uint8_t w = btl_equip_item(save, g_btl.cur_char, r);
        n = w ? add_item(row, n, w) : add_spaces(row, n, 7);
        n = add_spaces(row, n, 2);
        uint8_t a = btl_equip_item(save, g_btl.cur_char, 4 + r);
        if (a) n = add_item(row, n, a);
        draw_tiles(row, n, lut_CombatItemMagicBox[1] + 1, TEXT_ROW + lut_CombatItemMagicBox[2] + 1 + r * 2);
    }
}

/* DrawDrinkBox: " HEAL n" / " PURE n" for the potions not yet claimed. */
static void draw_drink_box(void) {
    draw_box_at(lut_CombatDrinkBox, 0);
    for (int r = 0; r < 2; r++) {
        if (!g_btl.potions[r]) continue;
        uint8_t row[24];
        int n = add_spaces(row, 0, 1);
        n = add_item(row, n, (uint8_t)(0x19 + r));     /* item_heal / item_pure */
        n = add_spaces(row, n, 1);
        BtlText num = { 0 };
        btl_text_add_number(&num, g_btl.potions[r]);
        for (int i = 0; i < num.len; i++) row[n++] = num.t[i];
        draw_tiles(row, n, lut_CombatDrinkBox[1] + 1, TEXT_ROW + lut_CombatDrinkBox[2] + 1 + r * 2);
    }
}

/* The combat boxes that are up (lut_CombatBoxes), in drawing order. */
static void draw_combat_boxes(void) {
    for (int b = 0; b < CBOX_COUNT; b++) {
        if (!(g_btl.cbox_shown & (1 << b))) continue;
        const uint8_t* cb = &lut_CombatBoxes[b * 8];
        UI_DrawBox(cb[1], MSG_ROW + cb[2], cb[3], cb[4]);
        draw_text(&g_btl.cbox[b], cb[6], TEXT_ROW + cb[7]);
    }
}

/* ---- Cursors ---- */

static void cursor_at(const uint8_t* lut, int i) {
    UI_DrawCursor(lut[i * 2], lut[i * 2 + 1]);
}

static void draw_enemy_cursor(void) {
    switch (g_btl.battle_type) {
    case 0:  cursor_at(lut_Target9SmallCursorPos, g_btl.target_curs); break;
    case 1:  cursor_at(lut_Target4LargeCursorPos, g_btl.target_curs); break;
    case 2:  cursor_at(lut_TargetMixCursorPos, g_btl.target_curs); break;
    default: {
        int x, y, w, h;
        btl_enemy_rect(0, &x, &y, &w, &h);
        UI_DrawCursor(x - 16, y);
        break;
    }
    }
}

/* ---- The screen ---- */

void Battle_Draw(const SaveData* save) {
    load_palettes();
    Ppu_Clear(0);
    g_ppu.shake_x = g_btl.shake_x;                 /* BattleScreenShake */
    g_ppu.shake_y = g_btl.shake_y;

    draw_frame();

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!g_btl.enemy[i].present || !g_btl.enemy_vis[i]) continue;
        draw_enemy(i);
        if (g_btl.battle_type >= 3) break;          /* the one fiend / Chaos */
    }

    /* Sprites, lowest priority first (later ones are drawn over): the
     * explosions (OAM $D0+), the party, the attack sprite (slot 0); the
     * cursor goes on top at the end. */
    int choosing = g_btl.phase >= BPHASE_COMMAND && g_btl.phase <= BPHASE_ITEM;
    draw_explosions();
    for (int i = 0; i < 4; i++) draw_character(save, i);
    draw_attack_sprite();

    draw_status(save);

    /* The message area. */
    if (choosing) {
        draw_roster();
        draw_command_box();
        if (g_btl.phase == BPHASE_MAGIC)      draw_magic_box(save);
        else if (g_btl.phase == BPHASE_ITEM)  draw_item_box(save);
        else if (g_btl.phase == BPHASE_DRINK) draw_drink_box();
    } else {
        draw_combat_boxes();
    }
    if (g_btl.nothing_box) {                       /* DoNothingMessageBox */
        const uint8_t* cb = &lut_CombatBoxes[CBOX_MESSAGE * 8];
        UI_DrawBox(cb[1], MSG_ROW + cb[2], cb[3], cb[4]);
        draw_tiles(NOTHING_TEXT, (int)sizeof(NOTHING_TEXT), cb[6], TEXT_ROW + cb[7]);
    }

    /* The cursor, while a menu is reading the pad (not during a walk). */
    if (!g_btl.nothing_box && g_btl.ev_i >= g_btl.ev_n) {
        switch (g_btl.phase) {
        case BPHASE_COMMAND:
            cursor_at(lut_MainCombatBoxCursorPos, g_btl.cmd_col * 4 + g_btl.cmd_row);
            break;
        case BPHASE_TARGET:
            draw_enemy_cursor();
            break;
        case BPHASE_MAGIC_TGT:
            if (g_btl.magic_target_enemy) draw_enemy_cursor();
            else cursor_at(lut_PlayerTargetCursorPos, g_btl.player_curs);
            break;
        case BPHASE_MAGIC:
            cursor_at(lut_MagicCursorPos, g_btl.magic_col * 4 + (g_btl.magic_row & 3));
            break;
        case BPHASE_ITEM:
            cursor_at(lut_ItemCursorPos, g_btl.item_cursor);
            break;
        case BPHASE_DRINK:
            cursor_at(lut_DrinkCursorPos, g_btl.drink_cursor);
            break;
        default:
            break;
        }
    }
}

void Battle_GetView(BattleView* v) {
    v->enemy_count = 0;
    for (int i = 0; i < MAX_ENEMIES && v->enemy_count < 9; i++) {
        if (!g_btl.enemy[i].present || !g_btl.enemy_vis[i]) continue;
        int x, y, w, h;
        btl_enemy_rect(i, &x, &y, &w, &h);
        v->enemy[v->enemy_count].x = x; v->enemy[v->enemy_count].y = y;
        v->enemy[v->enemy_count].w = w; v->enemy[v->enemy_count].h = h;
        v->enemy_count++;
        if (g_btl.battle_type >= 3) break;              /* the one fiend / Chaos */
    }
    v->backdrop_x = 16; v->backdrop_y = 16;              /* draw_backdrop's first copy */
    v->backdrop_w = 14 * 8; v->backdrop_h = 4 * 8;
    v->party_y = 0x30; v->party_row = 0x18;              /* draw_character */
    v->shake_x = g_btl.shake_x; v->shake_y = g_btl.shake_y;
    v->flash = g_btl.bg_flash ? g_btl.bg_flash : -1;
    v->boss = g_btl.no_run || g_btl.battle_type >= 3;
    int choosing = g_btl.phase >= BPHASE_COMMAND && g_btl.phase <= BPHASE_ITEM;
    v->stage_bottom = (choosing ? MSG_ROW : MSG_ROW + 1) * 8;
}
