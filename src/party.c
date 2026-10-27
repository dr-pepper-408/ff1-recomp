/*
 * party.c — Party initialization and accessors.
 */

#include <string.h>

/* Pull in the raw ROM init data (game flags + unsram first page) before
 * party.h/types.h.  game_flags_init.h defines GAME_FLAGS_SIZE as 256 while
 * types.h defines it as 0x100 (same value, different tokens), so #undef the
 * former to avoid a macro-redefinition warning.  We never rely on either
 * macro here — sizeof() is used for the copies. */
#include "../data/generated/game_flags_init.h"
#undef GAME_FLAGS_SIZE

#include "party.h"
#include "../data/generated/class_starting_stats.h"
#include "../data/generated/weapon_data.h"
#include "../data/generated/armor_data.h"
#include "../data/generated/level_up_data.h"

/* Number of unsram bytes that are meaningful at game start: the ship/
 * airship/bridge/canal positions, overworld scroll, gold, and the item list.
 * The remainder of the $100-byte unsram page is padding that starts as zero. */
#define UNSRAM_HEADER_BYTES 0x50

/* Port of NewGame_LoadStartingStats (bank_0F): a new character's stats from
 * lut_ClassStartingStats.  Red/White/Black Mages also start with 2 level-1 MP. */
static void load_starting_stats(SaveData* s, int slot, uint8_t cls) {
    CharacterStats* c = &s->characters[slot];
    const ClassStartingStats* st = &class_starting_stats[cls];
    c->class    = cls;
    c->cur_hp   = c->max_hp = st->hp;
    c->str      = st->str;
    c->agil     = st->agil;
    c->intel    = st->intel;
    c->vit      = st->vit;
    c->luck     = st->luck;
    c->damage   = st->damage;
    c->hit_rate = st->hit_rate;
    c->evade    = st->evade;
    c->mag_def  = st->mag_def;
    if (cls >= PARTY_CLASS_RM && cls < PARTY_CLASS_KN) {
        s->character_magic[slot].cur_mp[0] = 2;
        s->character_magic[slot].max_mp[0] = 2;
    }
}

/* GameStart's new-game setup, then NewGamePartyGeneration's
 * @RecordClassAndName and NewGame_LoadStartingStats. */
void Party_NewGame(SaveData* s, const uint8_t classes[MAX_PARTY_SIZE],
                   const uint8_t names[MAX_PARTY_SIZE][4]) {
    memset(s, 0, sizeof(*s));

    /* Game flags (chests opened, events, etc.). */
    memcpy(s->game_flags, init_game_flags, sizeof(s->game_flags));

    /* Unsram header: positions, gold, items. */
    memcpy(&s->ship_vis, init_unsram_first_page, UNSRAM_HEADER_BYTES);

    for (int i = 0; i < MAX_PARTY_SIZE; i++) {
        s->characters[i].exp_to_next = 40;           /* to reach level 2 */
        memcpy(s->characters[i].name, names[i], 4);
        load_starting_stats(s, i, classes[i]);
    }

    /* SRAM validity markers (checksum is computed when saving). */
    s->checksum  = 0;
    s->assert_55 = 0x55;
    s->assert_AA = 0xAA;
}

void Party_Init(SaveData* s) {
    /* The default party (--new, which skips the title and party creation):
     * Fighter, Thief, Black Belt, Red Mage, with placeholder names (font
     * tiles: 'A' = $8A, $FF = blank). */
    static const uint8_t classes[MAX_PARTY_SIZE] = {
        PARTY_CLASS_FT, PARTY_CLASS_TH, PARTY_CLASS_BB, PARTY_CLASS_RM
    };
    static const char* text[MAX_PARTY_SIZE] = { "ARGO", "BEA", "CID", "DUNE" };
    uint8_t names[MAX_PARTY_SIZE][4];
    for (int i = 0; i < MAX_PARTY_SIZE; i++)
        for (int k = 0; k < 4; k++) {
            char ch = text[i][k];
            names[i][k] = ch ? (uint8_t)(0x8A + ch - 'A') : 0xFF;
        }
    Party_NewGame(s, classes, names);
}

CharacterStats* Party_Character(SaveData* s, int slot) {
    if (slot < 0 || slot >= MAX_PARTY_SIZE) return NULL;
    return &s->characters[slot];
}

CharacterMagic* Party_Magic(SaveData* s, int slot) {
    if (slot < 0 || slot >= MAX_PARTY_SIZE) return NULL;
    return &s->character_magic[slot];
}

void Party_NameToAscii(const CharacterStats* c, char out[5]) {
    /* Font tiles: $80-$89 digits, $8A-$A3 A-Z, $A4-$BD a-z, $FF space. */
    for (int i = 0; i < 4; i++) {
        uint8_t t = c->name[i];
        char ch = '?';
        if (t >= 0x80 && t <= 0x89)      ch = (char)('0' + (t - 0x80));
        else if (t >= 0x8A && t <= 0xA3) ch = (char)('A' + (t - 0x8A));
        else if (t >= 0xA4 && t <= 0xBD) ch = (char)('a' + (t - 0xA4));
        else if (t == 0xFF)              ch = ' ';
        out[i] = ch;
    }
    out[4] = '\0';
}

/* ---- Equipment stats (bank_0F UnadjustEquipStats / ReadjustEquipStats) ----
 * The derived stats are kept incrementally: equipping adds a piece's bonuses,
 * unequipping subtracts them (8-bit arithmetic, as on the NES). */

static int is_black_belt(const CharacterStats* c) {
    return c->class == PARTY_CLASS_BB || c->class == PARTY_CLASS_MA;
}

/* Add (sign +1) or remove (sign -1) one equipped piece's bonuses. */
static void adjust_piece(CharacterStats* c, uint8_t id, int armor, int sign) {
    if (!(id & 0x80)) return;                       /* not equipped */
    int idx = (id & 0x7F) - 1;                      /* ids are 1-based */
    if (idx < 0) return;
    if (!armor) {
        if (idx >= WEAPON_COUNT) return;
        const WeaponData* w = &weapon_table[idx];
        c->hit_rate = (uint8_t)(c->hit_rate + sign * w->hit_rate);
        c->damage   = (uint8_t)(c->damage + sign * w->damage);
    } else {
        if (idx >= ARMOR_COUNT) return;
        const ArmorData* a = &armor_table[idx];
        c->evade = (uint8_t)(c->evade - sign * a->evade_penalty);
        if (sign > 0) {                             /* removal zeroes these instead */
            c->absorb = (uint8_t)(c->absorb + a->absorb_boost);
            c->elem_resist |= a->elem_defense;
        }
    }
}

void Party_UnadjustEquipStats(SaveData* s, int armor) {
    for (int i = 0; i < MAX_PARTY_SIZE; i++) {
        CharacterStats* c = &s->characters[i];
        const uint8_t* slots = armor ? c->armor : c->weapons;
        for (int j = 0; j < 4; j++) adjust_piece(c, slots[j], armor, -1);
        /* UnadjustBBEquipStats */
        if (!armor) {
            if (is_black_belt(c)) c->damage = 0;
        } else {
            c->absorb = 0;
            c->elem_resist = 0;
        }
    }
}

void Party_ReadjustEquipStats(SaveData* s, int armor) {
    for (int i = 0; i < MAX_PARTY_SIZE; i++) {
        CharacterStats* c = &s->characters[i];
        const uint8_t* slots = armor ? c->armor : c->weapons;
        for (int j = 0; j < 4; j++) adjust_piece(c, slots[j], armor, +1);
        /* ReadjustBBEquipStats: Black Belts and Masters fight best bare-handed. */
        if (!is_black_belt(c)) continue;
        if (!armor) {
            if (c->damage)                          /* a weapon is equipped */
                c->damage = (uint8_t)(c->damage + (c->str >> 1));
            else                                    /* unarmed: 2 x level */
                c->damage = (uint8_t)((c->level + 1) * 2);
        } else if (c->absorb == 0) {                /* no armor: absorb = level */
            c->absorb = (uint8_t)(c->level + 1);
        }
    }
}

void Party_SortEquipment(SaveData* s, int armor) {
    for (int i = 0; i < MAX_PARTY_SIZE; i++) {
        uint8_t* slots = armor ? s->characters[i].armor : s->characters[i].weapons;
        uint8_t tmp[4];
        memcpy(tmp, slots, 4);
        memset(slots, 0, 4);
        int n = 0;
        for (int j = 0; j < 4; j++)
            if (tmp[j]) slots[n++] = tmp[j];
    }
}

uint32_t Party_Gold(const SaveData* s) {
    return (uint32_t)s->gold[0]
         | ((uint32_t)s->gold[1] << 8)
         | ((uint32_t)s->gold[2] << 16);
}

void Party_SetGold(SaveData* s, uint32_t gold) {
    s->gold[0] = (uint8_t)(gold & 0xFF);
    s->gold[1] = (uint8_t)((gold >> 8) & 0xFF);
    s->gold[2] = (uint8_t)((gold >> 16) & 0xFF);
}

void Party_AddGold(SaveData* s, uint32_t amount) {
    uint32_t total = (Party_Gold(s) + (amount & 0xFFFFFF)) & 0xFFFFFF;
    uint8_t hi = (uint8_t)(total >> 16), md = (uint8_t)(total >> 8), lo = (uint8_t)total;
    /* Compare against 1,000,000 ($0F4240) the way the original does: if the
     * high byte is >= $0F but the middle byte is below $42 it exits without
     * capping, even when the high byte is $10 or more. */
    int over = 0;
    if (hi >= 0x0F) {
        if (md > 0x42)       over = 1;
        else if (md == 0x42) over = (lo >= 0x40);
    }
    Party_SetGold(s, over ? 999999 : total);
}

int Party_AddItem(SaveData* s, uint8_t item_id) {
    if (!s || item_id >= ITEM_STOP) return -1;
    if (s->items[item_id] >= 99) return -1;
    s->items[item_id]++;
    return 0;
}

int Party_AddWeapon(SaveData* s, uint8_t weapon_id) {
    if (!s || weapon_id == 0) return -1;
    for (int i = 0; i < MAX_PARTY_SIZE; i++) {
        for (int j = 0; j < 4; j++) {
            if (s->characters[i].weapons[j] == 0) {
                s->characters[i].weapons[j] = weapon_id;
                return 0;
            }
        }
    }
    return -1;
}

int Party_AddArmor(SaveData* s, uint8_t armor_id) {
    if (!s || armor_id == 0) return -1;
    for (int i = 0; i < MAX_PARTY_SIZE; i++) {
        for (int j = 0; j < 4; j++) {
            if (s->characters[i].armor[j] == 0) {
                s->characters[i].armor[j] = armor_id;
                return 0;
            }
        }
    }
    return -1;
}

/*
 * Debug loadout (FF1_DEBUG_STATE in config.h): seed a level-10 party with a
 * spread of items, equipment, magic and gold so the ITEM/MAGIC/WEAPON/ARMOR
 * menus can be exercised without playing through the game.
 *
 * Equipment uses FF1's native encoding: a slot value of 0 is empty, a value
 * of 1..40 is an unequipped weapon/armor id, and bit 7 (0x80) marks a slot as
 * equipped.
 */
/* Teach `m` the given spells (global ids) and grant full MP for each level
 * that ends up with at least one spell. */
static void debug_learn(CharacterMagic* m, const uint8_t* spells, int count) {
    for (int i = 0; i < count; i++) {
        int id = spells[i];
        m->spells[(id / 8) * 4 + (i % 3)] = (uint8_t)((id % 8) + 1);
    }
    for (int lvl = 0; lvl < 8; lvl++) {
        int has = 0;
        for (int col = 0; col < 3; col++)
            if (m->spells[lvl * 4 + col]) { has = 1; break; }
        if (has) { m->max_mp[lvl] = 9; m->cur_mp[lvl] = 9; }
    }
}

void Party_ApplyDebug(SaveData* s) {
    Party_SetGold(s, 50000);

    /* Level-10 party: Fighter, White Mage, Black Mage, Red Mage. */
    static const struct {
        uint8_t cls; uint16_t hp; uint8_t str, agil, intel, vit, luck;
    } DBG[4] = {
        { PARTY_CLASS_FT, 260, 34, 14,  1, 20,  9 },   /* Fighter    */
        { PARTY_CLASS_WM, 180, 10, 12, 34, 12, 14 },   /* White Mage */
        { PARTY_CLASS_BM, 150,  8, 12, 34, 10, 14 },   /* Black Mage */
        { PARTY_CLASS_RM, 170, 14, 14, 14, 10, 14 },   /* Red Mage   */
    };
    for (int i = 0; i < 4; i++) {
        CharacterStats* c = &s->characters[i];
        load_starting_stats(s, i, DBG[i].cls);
        c->level  = 9;               /* 0-based: 9 == "LV 10" */
        /* Just reached level 10: the EXP for it, and what's left to 11. */
        uint32_t exp = lut_ExpToAdvance[8];
        c->exp[0] = (uint8_t)exp; c->exp[1] = (uint8_t)(exp >> 8); c->exp[2] = (uint8_t)(exp >> 16);
        c->exp_to_next = (uint16_t)(lut_ExpToAdvance[9] - exp);
        c->max_hp = DBG[i].hp;
        c->cur_hp = DBG[i].hp;
        c->str    = DBG[i].str;
        c->agil   = DBG[i].agil;
        c->intel  = DBG[i].intel;
        c->vit    = DBG[i].vit;
        c->luck   = DBG[i].luck;
    }

    /* Key items (ids 1..17), one each. */
    for (int id = 1; id <= 17; id++) s->items[id] = 1;

    /* Consumables (ids 22..27) with quantities. */
    s->items[22] = 2;    /* TENT   */
    s->items[23] = 2;    /* CABIN  */
    s->items[24] = 1;    /* HOUSE  */
    s->items[25] = 10;   /* HEAL@P */
    s->items[26] = 5;    /* PURE@P */
    s->items[27] = 3;    /* SOFT@P */

    /* Weapons: slot 0 equipped (bit 7), spare pieces carried. */
    static const uint8_t W[4][4] = {
        { 0x80 | 13, 17, 29, 0 },   /* FT: Long Sword, Silver Sword, Dragon Sword */
        { 0x80 |  3,  0,  0, 0 },   /* WM: Wooden Staff                           */
        { 0x80 |  3,  0,  0, 0 },   /* BM: Wooden Staff                           */
        { 0x80 |  4, 12,  0, 0 },   /* RM: Rapier, Sabre                          */
    };
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            s->characters[i].weapons[j] = W[i][j];

    /* Armor: body/shield/helmet/gloves, all equipped. */
    static const uint8_t A[4][4] = {
        { 0x80 | 3, 0x80 | 17, 0x80 | 26, 0x80 | 33 },  /* FT: Chain, shield, cap, gloves */
        { 0x80 | 1, 0,         0,         0 },          /* WM: Cloth                      */
        { 0x80 | 1, 0,         0,         0 },          /* BM: Cloth                      */
        { 0x80 | 1, 0x80 | 17, 0x80 | 26, 0x80 | 33 },  /* RM: Cloth, shield, cap, gloves */
    };
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            s->characters[i].armor[j] = A[i][j];

    /* Equipment bonuses, as leaving the equip menus applies them. */
    Party_ReadjustEquipStats(s, 0);
    Party_ReadjustEquipStats(s, 1);

    /* Magic: the White Mage, Black Mage and Red Mage each learn three spells
     * per level so every spell family can be exercised in battle.  A slot in
     * ch_spells stores the spell's position within its level (1..8); its global
     * spell id is level*8 + (position-1). */
    /* White Mage: heal / protect / revive spells. */
    static const uint8_t WM_SPELLS[24] = {
         0,  2,  3,   8,  9, 10,  16, 18, 19,  24, 26, 27,
        32, 33, 35,  40, 42, 43,  48, 50, 51,  56, 58, 59,
    };
    /* Black Mage: damage / status spells. */
    static const uint8_t BM_SPELLS[24] = {
         1,  4,  5,  12, 13, 15,  17, 20, 22,  25, 28, 30,
        34, 36, 37,  44, 45, 46,  49, 52, 53,  57, 60, 61,
    };
    /* Red Mage: a first-three-per-level mixed spread (as before). */
    static const uint8_t RM_SPELLS[12] = {
         0,  1,  2,   8,  9, 10,  16, 17, 18,  24, 25, 26,
    };

    debug_learn(&s->character_magic[1], WM_SPELLS, 24);
    debug_learn(&s->character_magic[2], BM_SPELLS, 24);
    debug_learn(&s->character_magic[3], RM_SPELLS, 12);
}
