/**
 * @file menu.c
 * @brief The field menu: a port of bank_0E's EnterMainMenu and sub menus.
 *
 * The original is a set of nested loops, each drawing its sprites, doing a
 * MenuFrame and then acting on the pad.  Here each loop is a state: a state
 * names the loop (or MenuWaitForBtn) that is waiting for its next frame, and
 * on_frame() runs what the loop does after that frame, up to the next one.
 *
 * Drawing goes into a nametable and an OAM list, as the routines' PPU
 * writes do, so the screen keeps whatever was drawn last (a description box
 * left open, a target screen showing the HP from before an item was used).
 *
 * Sound effects and music are played as the original does (sfx.c, music.c).
 * Not ported: menustall (boxes drawn a row per frame while the
 * screen is on): everything here is drawn at once.
 */

#include <string.h>
#include "menu.h"
#include "text.h"
#include "ui.h"
#include "ppu.h"
#include "party.h"
#include "music.h"
#include "sfx.h"
#include "../data/generated/menu_data.h"
#include "../data/generated/battle_ui.h"

/* Item ids (items[] index, Constants.inc ITEM_*) */
enum {
    IT_LUTE = 1, IT_CROWN, IT_CRYSTAL, IT_HERB, IT_KEY, IT_TNT, IT_ADAMANT, IT_SLAB,
    IT_RUBY, IT_ROD, IT_FLOATER, IT_CHIME, IT_TAIL, IT_CUBE, IT_BOTTLE, IT_OXYALE,
    IT_CANOE, IT_ORB_START, IT_QTY_START = IT_ORB_START + 4,
    IT_TENT = IT_QTY_START, IT_CABIN, IT_HOUSE, IT_HEAL, IT_PURE, IT_SOFT, IT_STOP
};

/* Spells castable outside battle (Constants.inc MG_*). */
#define MG_START 0xB0
enum {
    MG_CURE = MG_START + 0x00, MG_CUR2 = MG_START + 0x10, MG_HEAL = MG_START + 0x13,
    MG_PURE = MG_START + 0x18, MG_CUR3 = MG_START + 0x20, MG_LIFE = MG_START + 0x21,
    MG_HEL2 = MG_START + 0x23, MG_WARP = MG_START + 0x26, MG_SOFT = MG_START + 0x28,
    MG_EXIT = MG_START + 0x29, MG_CUR4 = MG_START + 0x30, MG_HEL3 = MG_START + 0x33,
    MG_LIF2 = MG_START + 0x38,
};

/* Map objects and tile properties the key items look at (Constants.inc). */
#define OBJID_FAIRY        0x13
#define OBJID_RODPLATE     0x16
#define OBJID_LUTEPLATE    0x17
#define TP_SPEC_MASK       0x1E
#define TP_SPEC_USEROD     0x14
#define TP_SPEC_USELUTE    0x16
#define OWTP_SPEC_MASK     0xC0
#define OWTP_SPEC_FLOATER  0xC0

/* Out-of-battle ailments (ch_ailments). */
enum { AIL_DEAD = 1, AIL_STONE = 2, AIL_POISON = 3 };

/* lut_MainItemBoxes ids. */
enum { BOX_ORBS, BOX_CHAR0, BOX_GOLD = 5, BOX_OPTIONS, BOX_TITLE, BOX_DESC, BOX_LIST };

/* ---- The nametable and OAM ---- */

#define NT_W 32
#define NT_H 30

static uint8_t nt[NT_H][NT_W];
static uint8_t attr[64];            /* $23C0-$23FF */
static int dest_x, dest_y;          /* DrawBox's output text position */

typedef struct { int ob; int x, y; int who; } Sprite;   /* ob: 0 cursor, 1 OB sprite */
static Sprite oam[16];
static int oam_n;

static SaveData* g_save;

/* ClearNT */
static void clear_nt(void) {
    memset(nt, 0, sizeof(nt));
    memset(attr, 0xFF, sizeof(attr));
}

static void put_tile(uint8_t t, int x, int y) {
    if (x >= 0 && x < NT_W && y >= 0 && y < NT_H) nt[y][x] = t;
}

/* DrawBox: sets dest_x/dest_y to where the box's text starts. */
static void draw_box(int x, int y, int w, int h) {
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            uint8_t t;
            if (r == 0)          t = (c == 0) ? 0xF7 : (c == w - 1) ? 0xF9 : 0xF8;
            else if (r == h - 1) t = (c == 0) ? 0xFC : (c == w - 1) ? 0xFE : 0xFD;
            else                 t = (c == 0) ? 0xFA : (c == w - 1) ? 0xFB : 0xFF;
            put_tile(t, x + c, y + r);
        }
    dest_x = x + 1;
    dest_y = y + 2;
}

/* DrawMainItemBox */
static void draw_main_item_box(int id) {
    const uint8_t* b = &lut_MainItemBoxes[id * 4];
    draw_box(b[0], b[1], b[2], b[3]);
}

/* EraseBox (box 8, EraseDescBox) */
static void erase_box(int id) {
    const uint8_t* b = &lut_MainItemBoxes[id * 4];
    for (int r = 0; r < b[3]; r++)
        for (int c = 0; c < b[2]; c++) put_tile(0, b[0] + c, b[1] + r);
}

/* DrawComplexString at dest_x, dest_y. */
static void draw_complex(const uint8_t* s) {
    static TextGlyph g[TEXT_MAX_GLYPHS];
    int n = Text_FormatComplex(s, g_save, g, TEXT_MAX_GLYPHS);
    int x = dest_x, y = dest_y;
    for (int i = 0; i < n; i++) {
        if (g[i] == TEXT_NEWLINE) { x = dest_x; y++; continue; }
        put_tile((uint8_t)g[i], x++, y);
    }
}

/* DrawMenuString */
static void draw_menu_string(int id) { draw_complex(Text_MenuString(id)); }

static int submenu_targ;            /* the character a sub menu is for */

/* DrawCharMenuString / _Len: Y = `len` (copies len+1 bytes). */
static void draw_char_menu_string(int id, int len) {
    static uint8_t buf[0x100];
    Text_CharMenuString(id, submenu_targ, buf, len + 1);
    draw_complex(buf);
}

static void clear_oam(void) { oam_n = 0; }

/* DrawCursor */
static void draw_cursor(int x, int y) {
    if (oam_n < 16) oam[oam_n++] = (Sprite){ 0, x, y, 0 };
}

/* DrawOBSprite */
static void draw_ob_sprite(int who, int x, int y) {
    if (oam_n < 16) oam[oam_n++] = (Sprite){ 1, x, y, who };
}

/* ---- Menu state ---- */

typedef enum {
    M_MAIN, M_SUB, M_ITEM, M_TARGET, M_MAGIC, M_WAIT,
    M_EQ_MODE, M_EQ_EQUIP, M_EQ_TRADE, M_EQ_TRADE2, M_EQ_DROP, M_EQ_DROP_CONFIRM,
} MenuState;

/* What follows a MenuWaitForBtn, or a target chosen with ItemTargetMenuLoop. */
typedef enum {
    C_RESUME_MAIN,          /* return to the main menu (ResumeMainMenu) */
    C_ENTER_ITEM,           /* JMP EnterItemMenu */
    C_ENTER_MAGIC,          /* JMP EnterMagicMenu */
    C_ITEM_HEAL_USED,       /* UseItem_Heal: DEC item_heal, then EnterItemMenu */
    C_HEAL_SPELL_ANSWER,    /* UseMagic_HealFamily: A casts it, B doesn't */
    C_SAVE_ANSWER,          /* MenuSaveConfirm: A saves, B doesn't */
    C_SAVED,                /* MenuSaveConfirm saved: the HOUSE recovers MP, EnterItemMenu */
    C_WARP,                 /* UseMagic_WARP's exit */
    C_EXIT,                 /* UseMagic_EXIT's exit */
} Cont;

/* What a target from ItemTargetMenuLoop is for. */
typedef enum {
    T_ITEM_HEAL, T_ITEM_PURE, T_ITEM_SOFT,
    T_CURE, T_CUR4, T_LIFE, T_LIF2, T_PURE, T_SOFT,
} TargetUse;

static struct {
    MenuEnv env;
    MenuState state;
    int cursor, cursor_max, cursor2;
    uint8_t joy_prevdir;
    int descboxopen;
    int sub_magic;              /* MainMenuSubTarget is for MAGIC (else STATUS) */
    uint8_t item_box[0x20];     /* the item list / the 16 pieces of equipment */
    /* MenuWaitForBtn */
    Cont cont;
    int wait_sfx;               /* MenuWaitForBtn_SFX (else MenuWaitForBtn) */
    /* ItemTargetMenuLoop */
    TargetUse target_use;
    uint8_t hp_recovery;
    int mp_required;            /* the MP level the spell uses (ch_curmp index) */
    int save_item;              /* TENT / CABIN / HOUSE being used */
    /* equip menu */
    int armor;                  /* equipoffset: 0 weapons, 1 armor */
    int eq_modecurs;
    int save_request;
    MenuResult result;
} M;

/* ---- Shared loops ---- */

/* MenuWaitForBtn(_SFX), then `c`. */
static void wait_btn(Cont c) {
    M.state = M_WAIT;
    M.cont = c;
    M.wait_sfx = c != C_HEAL_SPELL_ANSWER && c != C_SAVE_ANSWER;
}

/* The pad as `joy` (held) and joy_prevdir see it: only new presses of the
 * directions in `mask` count, and only when they change. */
static int new_dir(const InputState* in, uint8_t mask) {
    uint8_t d = in->held & mask;
    if (d == M.joy_prevdir) return 0;
    M.joy_prevdir = d;
    return d;
}

/* ---- Main menu ---- */

/* DrawOrbBox */
static void draw_orb_box(void) {
    static const struct { int x, y; uint8_t tile; int item; } orbs[4] = {
        { 4, 4, 0x64, IT_ORB_START + 0 },    /* fire  ($2084) */
        { 6, 4, 0x68, IT_ORB_START + 1 },    /* water ($2086) */
        { 4, 6, 0x6C, IT_ORB_START + 2 },    /* air   ($20C4) */
        { 6, 6, 0x70, IT_ORB_START + 3 },    /* earth ($20C6) */
    };
    draw_main_item_box(BOX_ORBS);
    uint8_t a = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t t = orbs[i].tile;
        a >>= 2;
        if (!g_save->items[orbs[i].item]) {    /* unlit: tiles $76, palette 3 */
            t = 0x76;
            a |= 0xC0;
        }
        put_tile(t, orbs[i].x, orbs[i].y);
        put_tile((uint8_t)(t + 1), orbs[i].x + 1, orbs[i].y);
        put_tile((uint8_t)(t + 2), orbs[i].x, orbs[i].y + 1);
        put_tile((uint8_t)(t + 3), orbs[i].x + 1, orbs[i].y + 1);
    }
    attr[0x09] = a;                            /* $23C9 */
}

/* DrawMainMenuCharBoxBody */
static void draw_char_box_body(int c) {
    static uint8_t str[0x20];
    const CharacterMagic* m = &g_save->character_magic[c];
    int any = 0;
    for (int i = 0; i < MP_LEVELS; i++) any |= m->max_mp[i];
    if (any) {
        for (int i = 0; i < MP_LEVELS; i++) {
            str[i * 2] = (uint8_t)(m->cur_mp[i] | 0x80);
            str[i * 2 + 1] = 0x7A;              /* '/' */
        }
        str[0x0F] = 0;
        dest_y += 0x0A;
        draw_complex(str + 8);                  /* levels 5-8 */
        str[8] = 0;
        dest_y--;
        draw_complex(str);                      /* levels 1-4 */
        str[0] = 0x52; str[1] = 0x4A; str[2] = 0x8C; str[3] = 0;   /* "MA" "GI" "C" */
        dest_y--;
        draw_complex(str);
        dest_y -= 8;
    }
    uint8_t code = (uint8_t)(0x10 | c);
    uint8_t hp[6] = { code, 0x05, 0x7A, code, 0x06, 0x00 };          /* cur HP / max HP */
    dest_y += 5;
    draw_complex(hp);
    dest_y -= 5;
    uint8_t rest[10] = { code, 0x00, 0x01, 0xC6, code, 0x03, 0x01, code, 0x02, 0x00 };
    draw_complex(rest);                         /* name, L level, ailment blurb */
}

/* DrawMainMenu */
static void draw_main_menu(void) {
    clear_nt();
    draw_orb_box();
    draw_main_item_box(BOX_GOLD);               /* DrawMainMenuGoldBox */
    draw_menu_string(0x01);
    draw_main_item_box(BOX_OPTIONS);            /* DrawMainMenuOptionBox */
    dest_y++;
    draw_menu_string(0x02);
    for (int c = 0; c < 4; c++) {
        draw_main_item_box(BOX_CHAR0 + c);
        draw_char_box_body(c);
    }
}

/* DrawMainMenuCharSprites */
static void draw_main_menu_char_sprites(void) {
    draw_ob_sprite(0, 0x88, 0x18);
    draw_ob_sprite(1, 0xD8, 0x18);
    draw_ob_sprite(3, 0xD8, 0x88);
    draw_ob_sprite(2, 0x88, 0x88);
}

/* MainMenuLoop's top */
static void main_loop(void) {
    clear_oam();
    draw_cursor(0x10, lut_MainMenuCursor_Y[M.cursor]);
    draw_main_menu_char_sprites();
    M.state = M_MAIN;
}

/* ResumeMainMenu */
static void resume_main_menu(void) {
    draw_main_menu();
    clear_oam();
    M.cursor = 0;
    M.joy_prevdir = 0;
    M.cursor_max = 5;
    main_loop();
}

/* MainMenuSubTarget's loop top */
static void sub_loop(void) {
    clear_oam();
    draw_main_menu_char_sprites();
    draw_cursor(lut_MainMenuSubCursor[M.cursor * 2], lut_MainMenuSubCursor[M.cursor * 2 + 1]);
    M.state = M_SUB;
}

static void sub_target(int magic) {
    M.sub_magic = magic;
    M.cursor = 0;
    sub_loop();
}

/* ---- Description box ---- */

/* DrawItemDescBox(_Fanfare) */
static void draw_desc_box(int id) {
    draw_main_item_box(BOX_DESC);
    M.descboxopen++;
    draw_menu_string(id);
}

/* CloseDescBox_Sfx: close the box if it's open. */
static void close_desc_box_sfx(void) {
    Sfx_MenuMove();
    if (M.descboxopen) {
        M.descboxopen = 0;
        erase_box(BOX_DESC);
    }
}

/* ---- Status menu ---- */

/* EnterStatusMenu */
static void enter_status_menu(void) {
    clear_nt();
    for (int i = 0; i < 6; i++) {
        const uint8_t* b = &lutStatusBoxes[i * 4];
        draw_box(b[0], b[1], b[2], b[3]);
        draw_char_menu_string(0x23 + i, 0x7F);
    }
    clear_oam();
    draw_ob_sprite(submenu_targ, 0x58, 0x20);
    wait_btn(C_RESUME_MAIN);
}

/* ---- Target menu (heal potions, CURE, LIFE...) ---- */

/* DrawItemTargetMenu */
static void draw_item_target_menu(void) {
    clear_nt();
    draw_box(0x01, 0x0B, 0x1E, 0x08);
    dest_x = 0x04;
    dest_y = 0x11; draw_complex(str_TargetMaxHP);
    dest_y = 0x10; draw_complex(str_TargetCurHP);
    dest_y = 0x0F; draw_complex(str_TargetAilment);
    dest_y = 0x0D; draw_complex(str_TargetName);
    clear_oam();
}

/* ItemTargetMenuLoop's top */
static void target_loop_top(void) {
    clear_oam();
    draw_cursor(lut_ItemTargetCursorX[M.cursor], 0x68);
    M.state = M_TARGET;
}

static void target_loop(TargetUse use) {
    M.target_use = use;
    M.cursor = 0;
    target_loop_top();
}

/* MenuRecoverHP_Abs */
static void recover_hp_abs(CharacterStats* c, uint8_t amount) {
    uint16_t hp = (uint16_t)(c->cur_hp + amount);
    c->cur_hp = (hp >= c->max_hp) ? c->max_hp : hp;
    g_music_track = MUSIC_HP_JINGLE;            /* the gain HP jingle */
}

/* MenuRecoverPartyHP: everyone not dead or stone, then the target menu. */
static void recover_party_hp(uint8_t amount) {
    for (int p = 0; p < 4; p++) {
        CharacterStats* c = &g_save->characters[p];
        if (c->ailments == AIL_DEAD || c->ailments == AIL_STONE) continue;   /* MenuRecoverHP */
        recover_hp_abs(c, amount);
    }
    draw_item_target_menu();
}

/* CureOBAilment: 1 if the character had `ail` (now cured). */
static int cure_ob_ailment(int p, uint8_t ail) {
    CharacterStats* c = &g_save->characters[p];
    if (c->ailments != ail) return 0;
    c->ailments = 0;
    return 1;
}

static void spend_mp(void) {
    g_save->character_magic[submenu_targ].cur_mp[M.mp_required]--;
}

/* ---- Item menu ---- */

static void enter_item_menu(void);
static void resume_item_menu(void);

/* ItemMenu_Loop's top */
static void item_loop(void) {
    clear_oam();
    draw_cursor(lut_ItemMenuCursor[M.cursor * 2], lut_ItemMenuCursor[M.cursor * 2 + 1]);
    M.state = M_ITEM;
}

/* DrawItemBox: the list of items the party has.  0 if none. */
static int draw_item_box(void) {
    draw_box(0x01, 0x03, 0x1E, 0x13);
    int n = 0;
    for (int id = 1; id < IT_STOP; id++) {
        if (id >= IT_ORB_START && id < IT_QTY_START) continue;   /* no orbs */
        if (g_save->items[id]) M.item_box[n++] = (uint8_t)id;
    }
    if (!n) return 0;
    M.cursor_max = n;
    M.item_box[n] = 0;
    for (int i = 0; i < n; i++) {
        uint8_t id = M.item_box[i];
        uint8_t str[8];
        memcpy(str, Text_ItemName(id), 7);       /* 7 bytes, terminator or not */
        str[7] = 0;
        if (id >= IT_QTY_START) {
            uint8_t qty[2];
            Text_PrintNumber(g_save->items[id], 2, qty);   /* PrintNumber_2Digit */
            str[5] = qty[0];
            str[6] = qty[1];
        }
        dest_x = lutItemBoxStrPos[i * 2];
        dest_y = lutItemBoxStrPos[i * 2 + 1];
        draw_complex(str);
    }
    M.cursor = n;
    return 1;
}

/* ResumeItemMenu */
static void resume_item_menu(void) {
    int any = draw_item_box();
    draw_main_item_box(BOX_TITLE);              /* DrawItemTitleBox */
    draw_menu_string(0x03);
    clear_oam();
    if (!any) {
        draw_desc_box(0x04);                    /* "You have nothing." */
        wait_btn(C_RESUME_MAIN);
        return;
    }
    M.cursor = 0;
    M.joy_prevdir = 0;
    item_loop();
}

/* EnterItemMenu */
static void enter_item_menu(void) {
    M.descboxopen = 0;
    clear_nt();
    resume_item_menu();
}

/* MoveItemMenuCurs */
static void move_item_cursor(const InputState* in) {
    int d = new_dir(in, 0x0F);
    if (!d) return;
    int a, cur = M.cursor, max = M.cursor_max;
    if (d == 0x04) {                            /* down */
        a = cur + 3;
        if (a >= max && a >= 3)
            do a -= 3; while (a >= 3);
    } else if (d >= 0x04) {                     /* up */
        a = cur - 3;
        if (a < 0) {
            a = cur;
            if (max >= 3) {
                do a += 3; while (a < max);
                a -= 3;
            }
        }
    } else if (d == 0x01) {                     /* right */
        a = cur + 1;
        if (a >= max) a = 0;
    } else {                                    /* left */
        a = cur - 1;
        if (a < 0) a = max - 1;
    }
    M.cursor = a;
    close_desc_box_sfx();
}

/* MenuSaveConfirm (TENT, CABIN, HOUSE): description, then A or B. */
static void menu_save_confirm(int desc) {
    draw_desc_box(desc);
    clear_oam();
    wait_btn(C_SAVE_ANSWER);
}

/* UseItem_Tent / _Cabin / _House */
static void use_rest_item(int id) {
    static const struct { uint8_t hp; int desc, cant; } rest[3] = {
        { 30, 0x1A, 0x1B }, { 60, 0x1C, 0x1D }, { 120, 0x1E, 0x1F },
    };
    int k = id - IT_TENT;
    if (M.env.on_sm) {                          /* not on the overworld */
        draw_desc_box(rest[k].cant);
        item_loop();
        return;
    }
    g_save->items[id]--;
    recover_party_hp(rest[k].hp);
    M.save_item = id;
    menu_save_confirm(rest[k].desc);
}

/* The A button on an item: @ItemJumpTable. */
static void use_item(void) {
    uint8_t id = M.item_box[M.cursor];
    switch (id) {
    case IT_CROWN:   draw_desc_box(0x07); break;          /* UseItem_SetDesc */
    case IT_CRYSTAL: draw_desc_box(0x08); break;
    case IT_HERB:    draw_desc_box(0x09); break;
    case IT_KEY:     draw_desc_box(0x0A); break;
    case IT_TNT:     draw_desc_box(0x0B); break;
    case IT_ADAMANT: draw_desc_box(0x0C); break;
    case IT_SLAB:    draw_desc_box(0x0D); break;
    case IT_RUBY:    draw_desc_box(0x0E); break;
    case IT_CHIME:   draw_desc_box(0x13); break;
    case IT_TAIL:    draw_desc_box(0x14); break;
    case IT_CUBE:    draw_desc_box(0x15); break;
    case IT_OXYALE:  draw_desc_box(0x18); break;
    case IT_CANOE:   draw_desc_box(0x19); break;

    case IT_BOTTLE:                                       /* UseItem_Bottle */
        if (g_save->game_flags[OBJID_FAIRY] & 1) {
            draw_desc_box(0x17);                          /* (the empty bottle's text) */
            break;
        }
        g_save->items[IT_BOTTLE] = 0;
        if (M.env.objs) MapObj_Show(M.env.objs, g_save, OBJID_FAIRY);   /* ShowMapObject */
        else g_save->game_flags[OBJID_FAIRY] |= 1;
        g_music_track = MUSIC_FANFARE;                    /* DrawItemDescBox_Fanfare */
        draw_desc_box(0x16);
        resume_item_menu();                               /* the box stays on screen */
        return;

    case IT_ROD:                                          /* UseItem_Rod */
    case IT_LUTE: {                                       /* UseItem_Lute */
        int rod = id == IT_ROD;
        uint8_t obj = rod ? OBJID_RODPLATE : OBJID_LUTEPLATE;
        if (M.env.on_sm &&
            (M.env.tileprop_now & TP_SPEC_MASK) == (rod ? TP_SPEC_USEROD : TP_SPEC_USELUTE) &&
            (g_save->game_flags[obj] & 1)) {
            if (M.env.objs) MapObj_Hide(M.env.objs, g_save, obj);   /* HideMapObject */
            else g_save->game_flags[obj] &= (uint8_t)~1;
            g_music_track = MUSIC_FANFARE;                /* DrawItemDescBox_Fanfare */
            draw_desc_box(rod ? 0x0F : 0x05);
        } else {
            draw_desc_box(rod ? 0x10 : 0x06);
        }
        break;
    }

    case IT_FLOATER:                                      /* UseItem_Floater */
        if (!M.env.on_sm && (M.env.tileprop_now & OWTP_SPEC_MASK) == OWTP_SPEC_FLOATER &&
            !g_save->airship_vis) {
            g_save->airship_vis++;
            g_music_track = MUSIC_FANFARE;                /* DrawItemDescBox_Fanfare */
            draw_desc_box(0x11);
        } else {
            draw_desc_box(0x12);
        }
        break;

    case IT_TENT: case IT_CABIN: case IT_HOUSE:
        use_rest_item(id);
        return;

    case IT_HEAL:                                         /* UseItem_Heal */
        draw_item_target_menu();
        draw_desc_box(0x20);
        target_loop(T_ITEM_HEAL);
        return;
    case IT_PURE:
        draw_item_target_menu();
        draw_desc_box(0x21);
        target_loop(T_ITEM_PURE);
        return;
    case IT_SOFT:
        draw_item_target_menu();
        draw_desc_box(0x22);
        target_loop(T_ITEM_SOFT);
        return;

    default:                                              /* UseItem_Bad */
        break;
    }
    item_loop();
}

/* ---- Magic menu ---- */

static void enter_magic_menu(void);

static uint8_t spell_at(int cursor) {
    return g_save->character_magic[submenu_targ].spells[cursor & 0x1F];
}

/* MagicMenu_Loop's top: DrawMagicMenuCursor */
static void magic_loop(void) {
    clear_oam();
    draw_cursor((M.cursor & 3) * 48 + 0x50, ((M.cursor << 2) & 0xF0) + 0x28);
    M.state = M_MAGIC;
}

/* DrawMagicMenuMainBox: 0 if the character has no spells. */
static int draw_magic_menu_main_box(void) {
    draw_main_item_box(BOX_LIST);
    draw_char_menu_string(0x2A, 0xC0);          /* every level's spells and MP */
    for (int i = 0; i < 0x20; i++) {
        if ((i & 3) == 3) continue;
        if (spell_at(i)) {
            M.cursor = i;
            return 1;
        }
    }
    return 0;
}

/* EnterMagicMenu */
static void enter_magic_menu(void) {
    M.descboxopen = 0;
    clear_nt();
    int any = draw_magic_menu_main_box();
    draw_main_item_box(BOX_TITLE);
    draw_char_menu_string(0x29, 0x7F);          /* the character's name */
    clear_oam();
    if (!any) {
        wait_btn(C_RESUME_MAIN);
        return;
    }
    M.joy_prevdir = 0;
    magic_loop();
}

/* MoveMagicMenuCursor: skipping empty slots (and the unused 4th column). */
static void move_magic_cursor(const InputState* in) {
    int d = new_dir(in, 0x0F);
    if (!d) return;
    if (d >= 0x04) {
        int step = (d == 0x04) ? 4 : -4;        /* down / up */
        do M.cursor = (M.cursor + step) & 0x1F; while (!spell_at(M.cursor));
    } else if (d == 0x01) {                     /* right */
        do {
            if (((M.cursor + 1) & 3) == 3) M.cursor &= 0x1C;
            else M.cursor++;
        } while (!spell_at(M.cursor));
    } else {                                    /* left */
        do {
            if (((M.cursor - 1) & 3) == 3) M.cursor = (M.cursor & 0x1C) | 2;
            else M.cursor--;
        } while (!spell_at(M.cursor));
    }
    close_desc_box_sfx();
}

/* The A button on a spell. */
static void cast_spell(uint8_t* fc) {
    /* UseMagic_GetRequiredMP */
    M.mp_required = M.cursor >> 2;
    if (!g_save->character_magic[submenu_targ].cur_mp[M.mp_required]) {
        draw_desc_box(0x32);                    /* that level's spells are exhausted */
        magic_loop();
        return;
    }
    /* The spell id: (level*8 | spell) + MG_START-1.  BUGGED in the
     * original: ORA instead of ADC makes a level's 8th spell look like the
     * one before it on even levels, which never matters (they're all black
     * magic, not castable here). */
    uint8_t id = (uint8_t)((((M.cursor << 1) & 0x38) | spell_at(M.cursor)) + MG_START - 1);
    uint8_t f = *fc;
    switch (id) {
    case MG_CURE: M.hp_recovery = (uint8_t)((f & 0x0F) | 0x10); goto cure_family;
    case MG_CUR2: M.hp_recovery = (uint8_t)((f & 0x1F) | 0x20); goto cure_family;
    case MG_CUR3: M.hp_recovery = (uint8_t)((f & 0x3F) | 0x40);
    cure_family:                                /* UseMagic_CureFamily */
        draw_item_target_menu();
        draw_desc_box(0x2B);
        target_loop(T_CURE);
        return;
    case MG_CUR4:
        draw_item_target_menu();
        draw_desc_box(0x2B);
        target_loop(T_CUR4);
        return;
    case MG_HEAL: M.hp_recovery = (uint8_t)((f & 0x07) + 0x10); goto heal_family;
    case MG_HEL2: M.hp_recovery = (uint8_t)((f & 0x0F) + 0x20); goto heal_family;
    case MG_HEL3: M.hp_recovery = (uint8_t)((f & 0x1F) + 0x40);
    heal_family:                                /* UseMagic_HealFamily */
        draw_desc_box(0x2C);
        clear_oam();
        wait_btn(C_HEAL_SPELL_ANSWER);
        return;
    case MG_LIFE: draw_item_target_menu(); draw_desc_box(0x2E); target_loop(T_LIFE); return;
    case MG_LIF2: draw_item_target_menu(); draw_desc_box(0x2E); target_loop(T_LIF2); return;
    case MG_PURE: draw_item_target_menu(); draw_desc_box(0x2D); target_loop(T_PURE); return;
    case MG_SOFT: draw_item_target_menu(); draw_desc_box(0x30); target_loop(T_SOFT); return;
    case MG_WARP:
        draw_desc_box(0x2F);
        spend_mp();
        wait_btn(C_WARP);
        return;
    case MG_EXIT:
        draw_desc_box(0x31);
        spend_mp();
        wait_btn(C_EXIT);
        return;
    default:
        draw_desc_box(0x33);                    /* can't cast that here */
        magic_loop();
        return;
    }
}

/* A target picked in ItemTargetMenuLoop. */
static void use_on_target(void) {
    int p = M.cursor;
    CharacterStats* c = &g_save->characters[p];
    int ok;
    switch (M.target_use) {
    case T_ITEM_HEAL:                           /* UseItem_Heal */
    case T_CURE:                                /* UseMagic_CureFamily */
        ok = c->ailments != AIL_DEAD && c->ailments != AIL_STONE;
        if (ok) recover_hp_abs(c, M.target_use == T_CURE ? M.hp_recovery : 30);
        break;
    case T_CUR4:
        /* BUGGED in the original: no ailment check, so the dead and stoned
         * get their HP refilled too (and stay dead / stone). */
        c->cur_hp = c->max_hp;
        ok = 1;
        break;
    case T_ITEM_PURE: case T_PURE: ok = cure_ob_ailment(p, AIL_POISON); break;
    case T_ITEM_SOFT: case T_SOFT: ok = cure_ob_ailment(p, AIL_STONE); break;
    case T_LIFE:
        ok = cure_ob_ailment(p, AIL_DEAD);
        if (ok) c->cur_hp = 1;
        break;
    case T_LIF2:
    default:
        ok = cure_ob_ailment(p, AIL_DEAD);
        if (ok) c->cur_hp = c->max_hp;
        break;
    }
    if (!ok) {                                  /* PlaySFX_Error, and choose again */
        Sfx_Error();
        target_loop(M.target_use);
        return;
    }
    switch (M.target_use) {
    case T_ITEM_HEAL:
        draw_item_target_menu();
        wait_btn(C_ITEM_HEAL_USED);             /* the potion goes after the wait */
        return;
    case T_ITEM_PURE: g_save->items[IT_PURE]--; break;
    case T_ITEM_SOFT: g_save->items[IT_SOFT]--; break;
    default: spend_mp(); break;
    }
    draw_item_target_menu();
    wait_btn(M.target_use <= T_ITEM_SOFT ? C_ENTER_ITEM : C_ENTER_MAGIC);
}

/* ---- Equip menu ---- */

/* DrawEquipMenuStrings */
static void draw_equip_menu_strings(void) {
    uint8_t add = M.armor ? 0x44 - 1 : 0x1C - 1;
    for (int i = 0; i < 16; i++) {
        uint8_t str[10];
        memset(str, 0xFF, 9);
        str[9] = 0;
        uint8_t it = M.item_box[i];
        if (it & 0x7F) {
            memcpy(str + 2, Text_ItemName((uint8_t)((it & 0x7F) + add)), 7);
            if (it & 0x80) { str[0] = 0xC7; str[1] = 0xC2; }   /* "E-" */
        }
        dest_x = lut_EquipStringPositions[i * 2];
        dest_y = lut_EquipStringPositions[i * 2 + 1];
        draw_complex(str);
    }
}

static void equip_box(int id) {
    const uint8_t* b = &lut_EquipBoxes[id * 4];
    draw_box(b[0], b[1], b[2], b[3]);
}

/* DrawEquipMenu */
static void draw_equip_menu(void) {
    clear_nt();
    equip_box(0);
    draw_menu_string(M.armor ? 0x3E : 0x34);    /* "ARMOR" / "WEAPON" */
    equip_box(1);
    draw_menu_string(0x35);                     /* "EQUIP  TRADE  DROP" */
    for (int c = 0; c < 4; c++) {
        equip_box(3 + c * 2);
        equip_box(2 + c * 2);
        draw_menu_string(0x36 + c * 2);         /* the name */
    }
    attr[0] = 0x7F;                             /* useless: palettes 1 and 3 match */
    attr[1] = 0xDF;
}

static uint8_t* equip_list(int c) {
    CharacterStats* ch = &g_save->characters[c];
    return M.armor ? ch->armor : ch->weapons;
}

/* EquipMenu @Start */
static void equip_mode_loop(void) {
    clear_oam();
    draw_cursor(lut_EquipModeCursorX[M.eq_modecurs], 0x14);
    M.state = M_EQ_MODE;
}

/* EnterEquipMenu */
static void enter_equip_menu(int armor) {
    M.armor = armor;
    Party_SortEquipment(g_save, armor);         /* SortEquipmentList */
    draw_equip_menu();
    for (int c = 0; c < 4; c++) memcpy(&M.item_box[c * 4], equip_list(c), 4);   /* CopyEquipToItemBox */
    Party_UnadjustEquipStats(g_save, armor);
    clear_oam();
    draw_equip_menu_strings();
    M.eq_modecurs = 0;
    equip_mode_loop();
}

/* The loops of the three modes, and their confirm / second-pick loops. */
static void equip_sub_loop(MenuState st, const uint8_t* fc) {
    clear_oam();
    int x, y;
    switch (st) {
    case M_EQ_TRADE2:                           /* DrawEquipMenuCursSecondary: they take turns */
        if ((*fc >> 1) & 1) goto primary;
        x = lut_EquipMenuCurs[M.cursor2 * 2] | 4;
        y = lut_EquipMenuCurs[M.cursor2 * 2 + 1];
        draw_cursor(x, y);
        break;
    case M_EQ_DROP_CONFIRM:                     /* flickers */
        if ((*fc >> 1) & 1) break;
        /* fall through */
    default:
    primary:
        draw_cursor(lut_EquipMenuCurs[M.cursor * 2], lut_EquipMenuCurs[M.cursor * 2 + 1]);
        break;
    }
    M.state = st;
}

/* MoveEquipMenuCurs */
static void move_equip_cursor(const InputState* in) {
    int d = new_dir(in, 0x0F);
    if (!d) return;
    if (d < 0x04) M.cursor ^= 1;
    else if (d == 0x04) M.cursor = (M.cursor + 2) & 0x0F;
    else M.cursor = (M.cursor - 2) & 0x0F;
}

/* IsEquipLegal: 1 if the character in the cursor's row can equip `it`
 * (1-based id); if so, the pieces it replaces are unequipped. */
static int is_equip_legal(uint8_t it) {
    int c = (M.cursor >> 2) & 3;
    uint8_t cls = g_save->characters[c].class;
    uint16_t bit = lut_ClassEquipBit[cls < 12 ? cls : 11];
    int id = (it - 1) & 0x3F;
    if (!M.armor) {
        if (lut_WeaponPermissions[id < 40 ? id : 39] & bit) return 0;
        for (int k = 0; k < 4; k++) M.item_box[(M.cursor & 0x0C) + k] &= 0x7F;
        return 1;
    }
    if (lut_ArmorPermissions[id < 40 ? id : 39] & bit) return 0;
    uint8_t type = lut_ArmorTypes[id < 40 ? id : 39];
    for (int k = 0; k < 4; k++) {
        int i = (M.cursor & 0x0C) + k;
        if (i == M.cursor || !(M.item_box[i] & 0x80)) continue;
        int other = (M.item_box[i] - 0x81) & 0x3F;
        if (lut_ArmorTypes[other < 40 ? other : 39] == type) M.item_box[i] &= 0x7F;
    }
    return 1;
}

/* ---- Frames ---- */

static void after_wait(int a_held) {
    switch (M.cont) {
    case C_RESUME_MAIN: resume_main_menu(); break;
    case C_ENTER_ITEM:  enter_item_menu(); break;
    case C_ENTER_MAGIC: enter_magic_menu(); break;
    case C_ITEM_HEAL_USED:
        g_save->items[IT_HEAL]--;
        enter_item_menu();
        break;
    case C_HEAL_SPELL_ANSWER:
        if (!a_held) { enter_magic_menu(); break; }
        recover_party_hp(M.hp_recovery);
        spend_mp();
        wait_btn(C_ENTER_MAGIC);
        break;
    case C_SAVE_ANSWER:
        if (!a_held) {
            M.descboxopen = 0;                  /* CloseDescBox */
            erase_box(BOX_DESC);
            enter_item_menu();
            break;
        }
        draw_desc_box(0x3F);                    /* "Now saving..!" */
        M.save_request = 1;                     /* SaveGame */
        wait_btn(C_SAVED);
        break;
    case C_SAVED:
        /* The HOUSE recovers MP after saving (some would call it BUGGED:
         * the save has the MP from before). */
        if (M.save_item == IT_HOUSE)
            for (int p = 0; p < 4; p++) {       /* MenuRecoverPartyMP */
                CharacterStats* c = &g_save->characters[p];
                if (c->ailments == AIL_DEAD || c->ailments == AIL_STONE) continue;
                memcpy(g_save->character_magic[p].cur_mp, g_save->character_magic[p].max_mp, MP_LEVELS);
            }
        enter_item_menu();
        break;
    case C_WARP: M.result = MENU_WARP; break;
    case C_EXIT: M.result = MENU_EXIT; break;
    }
}

static void on_frame(const InputState* in, uint8_t* fc) {
    int a = (in->pressed & BTN_A) != 0, b = (in->pressed & BTN_B) != 0;
    switch (M.state) {
    case M_WAIT:                                /* MenuWaitForBtn */
        if (a || b) {
            if (M.wait_sfx) Sfx_MenuSel();
            after_wait((in->held & BTN_A) != 0);
        }
        break;

    case M_MAIN:
        if (a) {
            Sfx_MenuSel();
            switch (M.cursor) {
            case 0: enter_item_menu(); break;
            case 1: sub_target(1); break;
            case 2: enter_equip_menu(0); break;
            case 3: enter_equip_menu(1); break;
            default: sub_target(0); break;
            }
        } else if (b) {
            M.result = MENU_CLOSED;
        } else {
            int d = new_dir(in, 0x0C);          /* MoveCursorUpDown */
            if (d == 0x04) M.cursor = (M.cursor + 1 < M.cursor_max) ? M.cursor + 1 : 0;
            else if (d) M.cursor = (M.cursor > 0) ? M.cursor - 1 : M.cursor_max - 1;
            if (d) Sfx_MenuMove();
            main_loop();
        }
        break;

    case M_SUB:                                 /* MainMenuSubTarget */
        if (a) {
            submenu_targ = M.cursor;
            if (!M.sub_magic) { enter_status_menu(); break; }
            uint8_t ail = g_save->characters[submenu_targ].ailments;
            if (ail == AIL_DEAD || ail == AIL_STONE) { Sfx_Error(); sub_target(1); break; }
            enter_magic_menu();
        } else if (b) {                         /* @EscapeSubTarget */
            M.cursor = 0;
            M.cursor_max = 5;
            main_loop();
        } else {
            int d = new_dir(in, 0x0F);          /* MoveMainMenuSubCursor */
            if (d) { M.cursor ^= (d < 0x04) ? 1 : 2; Sfx_MenuMove(); }
            sub_loop();
        }
        break;

    case M_ITEM:
        if (a) { Sfx_MenuSel(); use_item(); }
        else if (b) resume_main_menu();
        else { move_item_cursor(in); item_loop(); }
        break;

    case M_TARGET:                              /* ItemTargetMenuLoop */
        if (a || b) Sfx_MenuSel();
        if (a) use_on_target();
        else if (b) {
            if (M.target_use <= T_ITEM_SOFT) enter_item_menu();
            else enter_magic_menu();
        } else {
            int d = new_dir(in, 0x03);
            if (d) { M.cursor = (M.cursor + (d == 0x01 ? 1 : -1)) & 3; Sfx_MenuMove(); }
            target_loop_top();
        }
        break;

    case M_MAGIC:
        if (a) { Sfx_MenuSel(); cast_spell(fc); }
        else if (b) resume_main_menu();
        else { move_magic_cursor(in); magic_loop(); }
        break;

    case M_EQ_MODE:
        if (a) {                                /* @GoToSubMenu */
            Sfx_MenuSel();
            M.cursor = M.cursor2 = 0;
            equip_sub_loop(M.eq_modecurs == 0 ? M_EQ_EQUIP : M.eq_modecurs == 1 ? M_EQ_TRADE : M_EQ_DROP, fc);
        } else if (b) {
            for (int c = 0; c < 4; c++) memcpy(equip_list(c), &M.item_box[c * 4], 4);   /* CopyEquipFromItemBox */
            Party_SortEquipment(g_save, M.armor);
            Party_ReadjustEquipStats(g_save, M.armor);
            resume_main_menu();
        } else {
            int d = new_dir(in, 0x03);          /* MoveEquipMenuModeCurs */
            if (d == 0x01) M.eq_modecurs = (M.eq_modecurs + 1) % 3;
            else if (d) M.eq_modecurs = (M.eq_modecurs + 2) % 3;
            equip_mode_loop();
        }
        break;

    case M_EQ_EQUIP:
        if (a) {
            uint8_t it = M.item_box[M.cursor];
            if (it && ((it & 0x80) || is_equip_legal(it))) {
                M.item_box[M.cursor] ^= 0x80;
                draw_equip_menu_strings();
            } else {
                Sfx_Error();
            }
            equip_sub_loop(M_EQ_EQUIP, fc);
        } else if (b) {
            M.eq_modecurs = 0;
            equip_mode_loop();
        } else { move_equip_cursor(in); equip_sub_loop(M_EQ_EQUIP, fc); }
        break;

    case M_EQ_TRADE:
        if (a) { M.cursor2 = M.cursor; equip_sub_loop(M_EQ_TRADE2, fc); }
        else if (b) { M.eq_modecurs = 0; equip_mode_loop(); }
        else { move_equip_cursor(in); equip_sub_loop(M_EQ_TRADE, fc); }
        break;

    case M_EQ_TRADE2:
        if (a) {                                /* @DoTrade: both end up unequipped */
            uint8_t t = M.item_box[M.cursor];
            M.item_box[M.cursor] = M.item_box[M.cursor2] & 0x7F;
            M.item_box[M.cursor2] = t & 0x7F;
            draw_equip_menu_strings();
            equip_sub_loop(M_EQ_TRADE, fc);
        } else if (b) {
            equip_sub_loop(M_EQ_TRADE, fc);
        } else { move_equip_cursor(in); equip_sub_loop(M_EQ_TRADE2, fc); }
        break;

    case M_EQ_DROP:
        if (a && M.item_box[M.cursor]) equip_sub_loop(M_EQ_DROP_CONFIRM, fc);
        else if (a) { Sfx_Error(); equip_sub_loop(M_EQ_DROP, fc); }
        else if (b) { M.eq_modecurs = 0; equip_mode_loop(); }
        else { move_equip_cursor(in); equip_sub_loop(M_EQ_DROP, fc); }
        break;

    case M_EQ_DROP_CONFIRM:
        if (a) {                                /* @DoDrop */
            M.item_box[M.cursor] = 0;
            draw_equip_menu_strings();
            equip_sub_loop(M_EQ_DROP, fc);
        } else if (b) {
            equip_sub_loop(M_EQ_DROP, fc);
        } else {
            equip_sub_loop(M_EQ_DROP_CONFIRM, fc);
        }
        break;
    }
}

/* ---- API ---- */

void Menu_Open(SaveData* save, const MenuEnv* env) {
    memset(&M, 0, sizeof(M));
    M.env = *env;
    g_save = save;
    resume_main_menu();                         /* EnterMainMenu */
}

MenuResult Menu_Update(SaveData* save, const InputState* in, uint8_t* framecounter) {
    g_save = save;
    M.result = MENU_CONTINUE;
    int equip = M.state >= M_EQ_MODE;
    /* MenuFrame (not EquipMenuFrame): after a jingle, the menu music */
    if (!equip && (g_music_track & 0x80)) g_music_track = MUSIC_MENU;
    if (equip) {
        /* EquipMenuFrame: UpdateEquipMenuModeAttrib (the mode's attribute
         * bytes; palettes 2 and 3 match, so it shows nothing) and a
         * second INC framecounter. */
        memcpy(&attr[3], &lut_EquipModeAttr[M.eq_modecurs * 4], 4);
        (*framecounter)++;
        Sfx_JoyFrame(in, 0x0F);                 /* its select / move sounds */
    }
    on_frame(in, framecounter);
    return M.result;
}

int Menu_TakeSaveRequest(void) {
    int r = M.save_request;
    M.save_request = 0;
    return r;
}

void Menu_Draw(const SaveData* save) {
    /* LoadMenuCHRPal + lutMenuPalettes: BG 0 lit orbs, 1-2 unused, 3 the
     * blue border; sprites: the battle sprite palettes. */
    static const uint8_t border_blue[4] = { 0x0F, 0x00, 0x01, 0x30 };
    Ppu_SetPalette(0, lutMenuPalettes);
    Ppu_SetPalette(1, lutMenuPalettes + 4);
    Ppu_SetPalette(2, lutMenuPalettes + 8);
    Ppu_SetPalette(3, border_blue);
    for (int i = 0; i < 4; i++) Ppu_SetPalette(4 + i, BattleSpritePalettes + i * 4);
    Ppu_Clear(0);

    for (int y = 0; y < NT_H; y++)
        for (int x = 0; x < NT_W; x++) {
            uint8_t t = nt[y][x];
            if (t < 0x60) continue;              /* tile 0: blank */
            uint8_t a = attr[(y / 4) * 8 + x / 4];
            int pal = (a >> (((y % 4) / 2) * 4 + ((x % 4) / 2) * 2)) & 3;
            if (t < 0x80) Ppu_DrawBgTile(CHR_BANK_ORB, t - 0x60, x * 8, y * 8, pal);
            else          Ppu_DrawBgTile(CHR_BANK_09, t, x * 8, y * 8, pal);
        }

    /* OAM: earlier sprites are in front, so draw from the back. */
    for (int i = oam_n - 1; i >= 0; i--) {
        const Sprite* s = &oam[i];
        if (!s->ob) { UI_DrawCursor(s->x, s->y); continue; }
        const CharacterStats* c = &save->characters[s->who];
        if (c->ailments == AIL_DEAD) continue;
        int pal = lutClassBatSprPalette[c->class < 12 ? c->class : 0];
        int t = 0x100 + c->class * 32;
        if (c->ailments == AIL_POISON) t += 0x14;
        else if (c->ailments == AIL_STONE) { t += 0x14; pal = 3; }
        for (int k = 0; k < 6; k++)
            Ppu_DrawSprite(CHR_BANK_09, t + k, s->x + (k & 1) * 8, s->y + (k >> 1) * 8, pal, 0);
    }
}
