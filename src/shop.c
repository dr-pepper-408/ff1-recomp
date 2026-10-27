/**
 * @file shop.c
 * @brief Town shops: a port of bank_0E's EnterShop and its loops.
 *
 * Every shop is the same screen (DrawShop: the shopkeeper, the title and gold
 * boxes) with a dialogue box, a command box and an inventory box that come
 * and go, and one menu at a time (CommonShopLoop: up/down, A or B).  Each
 * shop type's loop (EnterShop_Equip / _Item / _Magic / _Inn / _Clinic) is a
 * small state machine here: a state says what to do with the result of the
 * menu (or key press) the player has just finished.
 */

#include <string.h>
#include "shop.h"
#include "ppu.h"
#include "ui.h"
#include "text.h"
#include "music.h"
#include "sfx.h"
#include "../data/generated/shop_data.h"
#include "../data/generated/battle_ui.h"

/* Item ids (Constants.inc TCITYPE_*, MG_START). */
#define WEP_START 0x1C
#define ARM_START 0x44
#define MG_START  0xB0

/* items[] index of the last real item; anything at 99 can't be bought. */
#define ITEM_MAX_QTY 99

/* Shop types (lut_ShopTypes). */
enum { SHOP_WEAPON, SHOP_ARMOR, SHOP_WMAGIC, SHOP_BMAGIC, SHOP_CLINIC, SHOP_INN, SHOP_ITEM, SHOP_CARAVAN };

/* Shop boxes (lut_ShopBox_*). */
enum { BOX_DIALOGUE, BOX_TITLE, BOX_LIST, BOX_CMD, BOX_GOLD };

/* Stock shop strings (lut_ShopStrings ids used by the loops). */
#define TXT_GOLD_G       0x08   /* " G" */
#define TXT_WELCOME      0x09
#define TXT_BUYSELLEXIT  0x0A
#define TXT_BUYEXIT      0x0B
#define TXT_NOROOM       0x0C
#define TXT_WHATWANT     0x0D
#define TXT_GOLDOK       0x0E
#define TXT_YESNO        0x0F
#define TXT_CANTAFFORD   0x10
#define TXT_WHOGETS      0x11
#define TXT_THANKYOU     0x13
#define TXT_WHOSESELL    0x14
#define TXT_WHOLEARNS    0x17
#define TXT_CANTLEARN    0x19
#define TXT_ALREADYKNOW  0x1A
#define TXT_INNWELCOME   0x1B
#define TXT_DONTFORGET   0x1C
#define TXT_HOLDRESET    0x1D
#define TXT_NOTHINGSELL  0x1E
#define TXT_WHOREVIVE    0x20
#define TXT_RETURNTOLIFE 0x21
#define TXT_LEVELFULL    0x22
#define TXT_DONTNEED     0x23
#define TXT_TOOBAD       0x25

/* What to do once the current menu / key wait is over. */
typedef enum {
    ST_EXIT,
    /* weapon / armor */
    ST_EQ_MAIN, ST_EQ_BUY_PICK, ST_EQ_BUY_YES, ST_EQ_BUY_CHAR,
    ST_EQ_SELL_CHAR, ST_EQ_SELL_PICK, ST_EQ_SELL_YES,
    /* item / caravan */
    ST_IT_MAIN, ST_IT_PICK, ST_IT_YES,
    /* magic */
    ST_MG_CHAR, ST_MG_PICK, ST_MG_YES,
    /* inn */
    ST_INN_STAY, ST_INN_PAY, ST_INN_RESTED, ST_INN_EXIT,
    /* clinic */
    ST_CL_START, ST_CL_PICK, ST_CL_YES,
} ShopState;

typedef enum { MENU_NONE, MENU_CMD, MENU_LIST, MENU_KEY } MenuKind;

static struct {
    int shop_id, type;
    uint8_t item_box[6];        /* inventory, sell list, or the inn/clinic price */
    /* What the boxes show. */
    const uint8_t* dlg;         /* dialogue box string (NULL: no box) */
    uint8_t dlg_extra[16];      /* drawn over it (a price), $00-terminated */
    int list_on, cmd_on;
    uint8_t list_str[48];       /* inventory box string */
    uint8_t cmd_str[16];        /* command box string */
    /* The menu being run. */
    MenuKind menu;
    int cursor, cursor_max;
    ShopState next;             /* what the menu's result is for */
    /* Purchase in progress. */
    uint8_t cur_item;
    uint16_t cur_price;
    int char_index;             /* chosen character (0-3) */
    int slot;                   /* equipment / spell slot */
    uint8_t spell;              /* level-based spell id (1-8) to learn */
    uint8_t clinic_chars[4];    /* the dead characters, by menu row */
    /* The inn's fade of the party sprites. */
    uint8_t spr_pal[8];         /* sprite palettes 0-1 as shown */
    int fading, fade_timer;     /* +1 out, -1 in, 0 none */
    ShopState after_fade;
    int saved;
    int done;
} S;

static const uint8_t* shop_text(int id) {
    return shop_text_bytes + shop_text_offsets[id];
}

/* ---- Menus and boxes ---- */

/* DrawShopDialogueBox */
static void dialogue(int id) {
    S.dlg = shop_text(id);
    S.dlg_extra[0] = 0;
}

/* CommonShopLoop_Cmd over the command box showing `str`. */
static void menu_cmd(const uint8_t* str, int max, ShopState next) {
    /* Copy up to the terminator; codes $02, $03 and $10-$13 take an argument
     * byte, which may be $00 ($10 $00: a character's name). */
    int n = 0;
    while (str[n] && n < (int)sizeof(S.cmd_str) - 2) {
        uint8_t c = str[n];
        S.cmd_str[n] = c; n++;
        if (c == 0x02 || c == 0x03 || (c >= 0x10 && c <= 0x13)) { S.cmd_str[n] = str[n]; n++; }
    }
    S.cmd_str[n] = 0;
    S.cmd_on = 1;
    S.menu = MENU_CMD;
    S.cursor = 0;
    S.cursor_max = max;
    S.next = next;
}

/* ShopLoop_CharNames */
static void menu_names(ShopState next) {
    static const uint8_t names[] = { 0x10, 0x00, 0x01, 0x11, 0x00, 0x01, 0x12, 0x00, 0x01, 0x13, 0x00, 0x00 };
    menu_cmd(names, 4, next);
}

static void menu_yesno(ShopState next) { menu_cmd(shop_text(TXT_YESNO), 2, next); }

/* ShopFrameNoCursor until A or B. */
static void wait_key(ShopState next) {
    S.menu = MENU_KEY;
    S.next = next;
}

/* LoadShopInventory */
static void load_inventory(void) {
    memcpy(S.item_box, shop_inventory[S.shop_id], 5);
    S.item_box[5] = 0;
}

/* ShopSelectBuyItem / ShopSelectBuyMagic: the inventory box lists the item
 * box ("name / price" per item, "name / L# price" for spells), the command
 * box is erased, and the list menu runs. */
static void menu_list(int magic, ShopState next) {
    int n = 0, count = 0;
    for (; count < 5 && S.item_box[count]; count++) {
        uint8_t id = S.item_box[count];
        S.list_str[n++] = 0x02; S.list_str[n++] = id; S.list_str[n++] = 0x01;
        if (magic) {
            S.list_str[n++] = 0xC6;                                   /* the 'L' tile */
            S.list_str[n++] = (uint8_t)(0x81 + ((id - MG_START) >> 3));
        } else {
            S.list_str[n++] = 0xFF; S.list_str[n++] = 0xFF;
        }
        S.list_str[n++] = 0x03; S.list_str[n++] = id; S.list_str[n++] = 0x01;
    }
    S.list_str[n] = 0;
    S.list_on = 1;
    S.cmd_on = 0;
    S.menu = MENU_LIST;
    S.cursor = 0;
    S.cursor_max = count;
    S.next = next;
}

/* DrawShopBuyItemConfirm: "Gold OK?" with the item's price over it. */
static void confirm_buy(void) {
    dialogue(TXT_GOLDOK);
    S.cur_item = S.item_box[S.cursor];
    S.cur_price = Text_ItemPrice(S.cur_item);
    S.dlg_extra[0] = 0x03;
    S.dlg_extra[1] = S.cur_item;
    S.dlg_extra[2] = 0;
}

/* "Gold OK?" with a 5-digit amount over it (PrintNumber_5Digit). */
static void confirm_amount(uint16_t amount) {
    dialogue(TXT_GOLDOK);
    Text_PrintNumber(amount, 5, S.dlg_extra);
    S.dlg_extra[5] = 0;
}

/* Shop_CanAfford / InnClinic_CanAfford: more than 65535 GP always will. */
static int can_afford(const SaveData* save, uint16_t price) {
    return save->gold[2] || (uint16_t)(save->gold[0] | (save->gold[1] << 8)) >= price;
}

/* ShopPayPrice */
static void pay(SaveData* save, uint16_t price) {
    Party_SetGold(save, Party_Gold(save) - price);
}

/* ---- The shop loops ---- */

static void equip_loop(void)  { menu_cmd(shop_text(TXT_BUYSELLEXIT), 3, ST_EQ_MAIN); }
static void item_loop(void)   { menu_cmd(shop_text(TXT_BUYEXIT), 2, ST_IT_MAIN); }
static void magic_start(void) { dialogue(TXT_WHOLEARNS); menu_names(ST_MG_CHAR); }

/* EquipShop_GiveItemToChar: the first empty slot, unequipped.  0 if full. */
static int give_equipment(SaveData* save, int p) {
    CharacterStats* c = &save->characters[p];
    uint8_t* slots = (S.type == SHOP_WEAPON) ? c->weapons : c->armor;
    uint8_t start = (S.type == SHOP_WEAPON) ? WEP_START : ARM_START;
    for (int i = 0; i < 4; i++)
        if (!slots[i]) {
            slots[i] = (uint8_t)(S.cur_item - (start - 1));
            return 1;
        }
    return 0;
}

/* EquipMenu_BuildSellBox: the character's list up to its first gap. */
static int build_sell_box(const SaveData* save, int p) {
    const CharacterStats* c = &save->characters[p];
    const uint8_t* slots = (S.type == SHOP_WEAPON) ? c->weapons : c->armor;
    uint8_t start = (S.type == SHOP_WEAPON) ? WEP_START : ARM_START;
    int n = 0;
    while (n < 4 && slots[n]) {
        S.item_box[n] = (uint8_t)((slots[n] & 0x7F) + start - 1);
        n++;
    }
    S.item_box[n] = 0;
    return n;
}

/* MagicShop_AssertLearn: the class's permissions, the spell not already
 * known on its level, and a free slot there.  Returns the message to show
 * on failure, or 0. */
static int assert_learn(const SaveData* save) {
    const CharacterStats* c = &save->characters[S.char_index];
    const CharacterMagic* m = &save->character_magic[S.char_index];
    int idx = S.cur_item - MG_START, level = idx >> 3, bit = idx & 7;
    if (c->class >= 12 || (lut_MagicPermis[c->class][level] & (0x80 >> bit))) return TXT_CANTLEARN;
    uint8_t id = (uint8_t)(bit + 1);
    const uint8_t* row = &m->spells[level * 4];
    if (row[0] == id || row[1] == id || row[2] == id) return TXT_ALREADYKNOW;
    for (int i = 0; i < 3; i++)
        if (!row[i]) {
            S.slot = level * 4 + i;
            S.spell = id;
            return 0;
        }
    return TXT_LEVELFULL;
}

/* ClinicBuildNameString: the dead characters' names.  Returns how many. */
static int build_clinic_names(const SaveData* save) {
    int n = 0, k = 0;
    for (int p = 0; p < 4; p++) {
        if (save->characters[p].ailments != 1) continue;
        S.clinic_chars[k++] = (uint8_t)p;
        S.cmd_str[n++] = (uint8_t)(0x10 + p);
        S.cmd_str[n++] = 0x00;
        S.cmd_str[n++] = 0x01;
    }
    S.cmd_str[n] = 0;
    return k;
}

static void clinic_start(const SaveData* save) {
    int dead = build_clinic_names(save);
    if (!dead) {
        dialogue(TXT_DONTNEED);                       /* nobody is dead */
        wait_key(ST_EXIT);
        return;
    }
    dialogue(TXT_WHOREVIVE);
    uint8_t names[16];
    memcpy(names, S.cmd_str, sizeof(names));
    menu_cmd(names, dead, ST_CL_PICK);
}

/* Inn / clinic price: the first two inventory bytes. */
static uint16_t service_price(void) {
    return (uint16_t)(S.item_box[0] | (S.item_box[1] << 8));
}

/* InnClinic_CanAfford's refusal: "you can't afford that", a key, and out. */
static void cant_afford_exit(void) {
    dialogue(TXT_CANTAFFORD);
    wait_key(ST_EXIT);
}

static void fade(int dir, ShopState then) {
    S.fading = dir;
    S.fade_timer = 0;
    S.after_fade = then;
    S.menu = MENU_NONE;
}

/* The player finished the menu S.next was for: `b` = pressed B. */
static void on_result(SaveData* save, int b) {
    int cur = S.cursor;
    S.menu = MENU_NONE;
    switch (S.next) {
    case ST_EXIT:
        S.done = 1;
        break;

    /* ---- EnterShop_Equip ---- */
    case ST_EQ_MAIN:
        if (b || cur == 2) { S.done = 1; break; }
        if (cur == 0) {                               /* Buy */
            load_inventory();
            dialogue(TXT_WHATWANT);
            menu_list(0, ST_EQ_BUY_PICK);
        } else {                                      /* Sell */
            dialogue(TXT_WHOSESELL);
            menu_names(ST_EQ_SELL_CHAR);
        }
        break;
    case ST_EQ_BUY_PICK:
        if (b) { equip_loop(); break; }
        confirm_buy();
        menu_yesno(ST_EQ_BUY_YES);
        break;
    case ST_EQ_BUY_YES:
        if (b || cur) { dialogue(TXT_TOOBAD); equip_loop(); break; }
        if (!can_afford(save, S.cur_price)) { dialogue(TXT_CANTAFFORD); equip_loop(); break; }
        dialogue(TXT_WHOGETS);
        menu_names(ST_EQ_BUY_CHAR);
        break;
    case ST_EQ_BUY_CHAR:
        if (b) { equip_loop(); break; }
        if (!give_equipment(save, cur)) { dialogue(TXT_NOROOM); equip_loop(); break; }
        pay(save, S.cur_price);
        dialogue(TXT_THANKYOU);
        equip_loop();
        break;
    case ST_EQ_SELL_CHAR:
        if (b) { equip_loop(); break; }
        S.char_index = cur;
        if (!build_sell_box(save, cur)) { dialogue(TXT_NOTHINGSELL); equip_loop(); break; }
        menu_list(0, ST_EQ_SELL_PICK);
        break;
    case ST_EQ_SELL_PICK:
        if (b) { equip_loop(); break; }
        /* DrawShopSellItemConfirm: half the price. */
        S.slot = cur;
        S.cur_price = Text_ItemPrice(S.item_box[cur]) >> 1;
        confirm_amount(S.cur_price);
        menu_yesno(ST_EQ_SELL_YES);
        break;
    case ST_EQ_SELL_YES: {
        if (b || cur) { dialogue(TXT_TOOBAD); equip_loop(); break; }
        /* Unadjust everyone's stats for this kind, drop the item, close
         * the gap, readjust: so selling something equipped is handled. */
        int armor = S.type == SHOP_ARMOR;
        CharacterStats* c = &save->characters[S.char_index];
        Party_UnadjustEquipStats(save, armor);
        (armor ? c->armor : c->weapons)[S.slot] = 0;
        Party_SortEquipment(save, armor);
        Party_ReadjustEquipStats(save, armor);
        Party_AddGold(save, S.cur_price);             /* AddGPToParty */
        equip_loop();
        break;
    }

    /* ---- EnterShop_Item (and the caravan) ---- */
    case ST_IT_MAIN:
        if (b || cur) { S.done = 1; break; }
        dialogue(TXT_WHATWANT);
        menu_list(0, ST_IT_PICK);
        break;
    case ST_IT_PICK:
        if (b) { item_loop(); break; }
        confirm_buy();
        menu_yesno(ST_IT_YES);
        break;
    case ST_IT_YES:
        if (b || cur) { dialogue(TXT_TOOBAD); item_loop(); break; }
        if (!can_afford(save, S.cur_price)) { dialogue(TXT_CANTAFFORD); item_loop(); break; }
        if (save->items[S.cur_item] >= ITEM_MAX_QTY) { dialogue(TXT_NOROOM); item_loop(); break; }
        save->items[S.cur_item]++;
        pay(save, S.cur_price);
        dialogue(TXT_THANKYOU);
        item_loop();
        break;

    /* ---- EnterShop_Magic ---- */
    case ST_MG_CHAR:
        if (b) { S.done = 1; break; }
        S.char_index = cur;
        load_inventory();
        menu_list(1, ST_MG_PICK);
        break;
    case ST_MG_PICK: {
        if (b) { menu_names(ST_MG_CHAR); break; }     /* MagicShop_Loop */
        S.cur_item = S.item_box[cur];
        int msg = assert_learn(save);
        if (msg) { dialogue(msg); menu_names(ST_MG_CHAR); break; }
        confirm_buy();
        menu_yesno(ST_MG_YES);
        break;
    }
    case ST_MG_YES:
        if (b || cur) { dialogue(TXT_TOOBAD); menu_names(ST_MG_CHAR); break; }
        if (!can_afford(save, S.cur_price)) { dialogue(TXT_CANTAFFORD); menu_names(ST_MG_CHAR); break; }
        pay(save, S.cur_price);
        save->character_magic[S.char_index].spells[S.slot] = S.spell;
        magic_start();
        break;

    /* ---- EnterShop_Inn ---- */
    case ST_INN_STAY:
        if (b || cur) { S.next = ST_INN_EXIT; on_result(save, 0); break; }
        confirm_amount(service_price());
        menu_yesno(ST_INN_PAY);
        break;
    case ST_INN_PAY:
        if (b || cur) { S.next = ST_INN_EXIT; on_result(save, 0); break; }
        if (!can_afford(save, service_price())) { cant_afford_exit(); break; }
        pay(save, service_price());
        /* MenuFillPartyHP / MenuRecoverPartyMP (not the dead or stoned),
         * then SaveGame. */
        for (int p = 0; p < 4; p++) {
            CharacterStats* c = &save->characters[p];
            if (c->ailments == 1 || c->ailments == 2) continue;
            c->cur_hp = c->max_hp;
            memcpy(save->character_magic[p].cur_mp, save->character_magic[p].max_mp, MP_LEVELS);
        }
        S.saved = 1;
        dialogue(TXT_DONTFORGET);
        S.cmd_on = 0;
        fade(+1, ST_INN_RESTED);                      /* the party fades out */
        break;
    case ST_INN_RESTED:                               /* a key: fade back in */
        fade(-1, ST_INN_EXIT);
        break;
    case ST_INN_EXIT:
        S.cmd_on = 0;
        dialogue(TXT_HOLDRESET);
        wait_key(ST_EXIT);
        break;

    /* ---- EnterShop_Clinic ---- */
    case ST_CL_START:
        clinic_start(save);
        break;
    case ST_CL_PICK:
        if (b) { S.done = 1; break; }
        S.char_index = S.clinic_chars[cur];
        confirm_amount(service_price());
        menu_yesno(ST_CL_YES);
        break;
    case ST_CL_YES: {
        if (b || cur) { clinic_start(save); break; }
        if (!can_afford(save, service_price())) { cant_afford_exit(); break; }
        pay(save, service_price());
        CharacterStats* c = &save->characters[S.char_index];
        c->ailments = 0;                              /* back to life with 1 HP */
        c->cur_hp = 1;
        dialogue(TXT_RETURNTOLIFE);
        S.cmd_on = 0;
        wait_key(ST_CL_START);
        break;
    }
    }
}

/* ---- Entry / update ---- */

void Shop_Open(SaveData* save, int shop_id) {
    memset(&S, 0, sizeof(S));
    S.shop_id = (shop_id < SHOP_COUNT) ? shop_id : 0;
    S.type = lut_ShopTypes[S.shop_id] & 7;
    load_inventory();                                 /* (DrawShop) */
    memcpy(S.spr_pal, BattleSpritePalettes, 8);

    switch (S.type) {                                 /* lut_ShopEntryJump */
    case SHOP_WEAPON:
    case SHOP_ARMOR:
        dialogue(TXT_WELCOME);
        equip_loop();
        break;
    case SHOP_WMAGIC:
    case SHOP_BMAGIC:
        magic_start();
        break;
    case SHOP_CLINIC:
        clinic_start(save);
        break;
    case SHOP_INN:
        dialogue(TXT_INNWELCOME);
        menu_yesno(ST_INN_STAY);
        break;
    default:                                          /* item, caravan */
        dialogue(TXT_WELCOME);
        item_loop();
        break;
    }
}

/* FadeOutBatSprPalettes / FadeInBatSprPalettes: every 8 frames each color
 * of sprite palettes 0-1 goes a shade darker (to black) or back. */
static void fade_step(void) {
    if (++S.fade_timer & 7) return;
    int changed = 0;
    for (int i = 1; i < 8; i++) {
        uint8_t c = S.spr_pal[i], want = BattleSpritePalettes[i];
        if (S.fading > 0) {
            if (c == 0x0F) continue;
            S.spr_pal[i] = (c < 0x10) ? 0x0F : (uint8_t)(c - 0x10);
        } else {
            if (c == want) continue;
            S.spr_pal[i] = (c == 0x0F) ? (uint8_t)(want & 0x0F) : (uint8_t)(c + 0x10);
        }
        changed = 1;
    }
    if (!changed) {
        int dir = S.fading;
        S.fading = 0;
        if (dir > 0) wait_key(S.after_fade);          /* faded out: wait for a key */
        else { S.next = S.after_fade; on_result(NULL, 0); }
    }
}

int Shop_Update(SaveData* save, const InputState* in) {
    if (g_music_track == 0x81) g_music_track = MUSIC_SHOP;   /* after the save jingle */
    if (!S.fading) Sfx_JoyFrame(in, 0x0F);      /* ShopFrame's select / move sounds */
    if (S.fading) {
        fade_step();
        return S.done;
    }
    switch (S.menu) {
    case MENU_KEY:
        if (in->pressed & (BTN_A | BTN_B)) on_result(save, 0);
        break;
    case MENU_CMD:
    case MENU_LIST:
        /* CommonShopLoop: up/down (wrapping), A or B. */
        if (in->pressed & BTN_B) { on_result(save, 1); break; }
        if (in->pressed & BTN_A) { on_result(save, 0); break; }
        if (S.cursor_max > 0) {
            if (in->pressed & BTN_UP)   S.cursor = (S.cursor + S.cursor_max - 1) % S.cursor_max;
            if (in->pressed & BTN_DOWN) S.cursor = (S.cursor + 1) % S.cursor_max;
        }
        break;
    default:
        break;
    }
    return S.done;
}

int Shop_TakeSaveRequest(void) {
    int s = S.saved;
    S.saved = 0;
    return s;
}

/* ---- Drawing ---- */

/* The palette a background tile uses (lut_ShopAttributes). */
static int attr_palette(int tx, int ty) {
    uint8_t a = lut_ShopAttributes[(ty / 4) * 8 + tx / 4];
    return (a >> (((ty % 4) / 2) * 4 + ((tx % 4) / 2) * 2)) & 3;
}

static void put_tile(uint8_t t, int tx, int ty) {
    Ppu_DrawBgTile(CHR_BANK_09, t, tx * 8, ty * 8, attr_palette(tx, ty));
}

/* DrawBox, in the shop's palettes. */
static void draw_box(int b) {
    int x = lut_ShopBox_X[b], y = lut_ShopBox_Y[b], w = lut_ShopBox_Wd[b], h = lut_ShopBox_Ht[b];
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            uint8_t t;
            if (r == 0)          t = (c == 0) ? 0xF7 : (c == w - 1) ? 0xF9 : 0xF8;
            else if (r == h - 1) t = (c == 0) ? 0xFC : (c == w - 1) ? 0xFE : 0xFD;
            else                 t = (c == 0) ? 0xFA : (c == w - 1) ? 0xFB : 0xFF;
            put_tile(t, x + c, y + r);
        }
}

/* DrawComplexString at (tx, ty). */
static void draw_string(const uint8_t* s, const SaveData* save, int tx, int ty) {
    TextGlyph g[256];
    int n = Text_FormatComplex(s, save, g, 256);
    int x = tx, y = ty;
    for (int i = 0; i < n; i++) {
        if (g[i] == TEXT_NEWLINE) { x = tx; y++; continue; }
        put_tile((uint8_t)g[i], x++, y);
    }
}

/* Text starts 1 right and 2 down from a box's corner (DrawBox's dest). */
static void draw_in_box(int b, const uint8_t* s, const SaveData* save) {
    draw_string(s, save, lut_ShopBox_X[b] + 1, lut_ShopBox_Y[b] + 2);
}

/* DrawOBSprite: standing, crouched when poisoned, crouched and grey when
 * stoned, nothing when dead. */
static void draw_party_member(const SaveData* save, int p, int x, int y) {
    const CharacterStats* c = &save->characters[p];
    if (c->ailments == 1) return;
    int pal = (c->class < 12) ? lut_InBattleCharPaletteAssign[c->class] : 0;
    int t = 0x100 + c->class * 32;
    if (c->ailments == 3) t += 0x14;
    if (c->ailments == 2) { t += 0x14; pal = 3; }
    for (int k = 0; k < 6; k++)
        Ppu_DrawSprite(CHR_BANK_09, t + k, x + (k & 1) * 8, y + (k >> 1) * 8, pal, 0);
}

void Shop_Draw(const SaveData* save) {
    static const uint8_t border_blue[4] = { 0x0F, 0x00, 0x01, 0x30 };   /* LoadBorderPalette_Blue */
    Ppu_SetPalette(0, shopkeep_palettes[S.type]);
    Ppu_SetPalette(1, lut_ShopPalettes);
    Ppu_SetPalette(2, lut_ShopPalettes + 4);
    Ppu_SetPalette(3, border_blue);
    Ppu_SetPalette(4, S.spr_pal);
    Ppu_SetPalette(5, S.spr_pal + 4);
    Ppu_SetPalette(6, BattleSpritePalettes + 8);
    Ppu_SetPalette(7, BattleSpritePalettes + 12);
    Ppu_Clear(0);

    /* DrawShop: the shopkeeper (10x10 at 11,6), the title and gold boxes. */
    for (int r = 0; r < 10; r++)
        for (int c = 0; c < 10; c++) {
            uint8_t t = lut_ShopkeepImage[r * 10 + c];
            if (t) put_tile((uint8_t)(t + lut_ShopkeepAdditive[S.type]), 11 + c, 6 + r);
        }
    draw_box(BOX_TITLE);
    draw_in_box(BOX_TITLE, shop_text(S.type), save);
    draw_box(BOX_GOLD);
    static const uint8_t gold[] = { 0x04, 0x00 };     /* PrintGold */
    draw_in_box(BOX_GOLD, gold, save);
    draw_string(shop_text(TXT_GOLD_G), save, lut_ShopBox_X[BOX_GOLD] + 1 + 6, lut_ShopBox_Y[BOX_GOLD] + 2);

    if (S.dlg) {
        draw_box(BOX_DIALOGUE);
        draw_in_box(BOX_DIALOGUE, S.dlg, save);
        if (S.dlg_extra[0]) draw_in_box(BOX_DIALOGUE, S.dlg_extra, save);
    }
    if (S.list_on) {
        draw_box(BOX_LIST);
        draw_in_box(BOX_LIST, S.list_str, save);
    }
    if (S.cmd_on) {
        draw_box(BOX_CMD);
        draw_in_box(BOX_CMD, S.cmd_str, save);
    }

    /* DrawShopPartySprites */
    draw_party_member(save, 1, 0x98, 0x38);
    draw_party_member(save, 2, 0x98, 0x50);
    draw_party_member(save, 3, 0x98, 0x68);
    draw_party_member(save, 0, 0x88, 0x50);

    /* DrawShopCursor */
    if (S.menu == MENU_CMD)
        UI_DrawCursor(lut_ShopCurs_Cmd[S.cursor * 2], lut_ShopCurs_Cmd[S.cursor * 2 + 1]);
    else if (S.menu == MENU_LIST)
        UI_DrawCursor(lut_ShopCurs_List[S.cursor * 2], lut_ShopCurs_List[S.cursor * 2 + 1]);
}
