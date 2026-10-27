/**
 * @file text.c
 * @brief FF1 text engine: ports of DrawComplexString / DrawDialogueString
 *        (bank_0F) and PrintCharStat / PrintNumber / PrintGold / PrintPrice
 *        (bank_0E), expanding strings into glyph tokens.
 *
 * Character data is read by its NES RAM offset (SaveData mirrors unsram byte
 * for byte, see types.h), the same way the original indexes ch_stats.
 */

#include "text.h"
#include "../data/generated/text_data.h"

/* Offsets within the save block (variables.inc). */
#define OFS_GOLD        0x01C
#define OFS_CH_STATS    0x100
#define OFS_CH_MAGIC    0x300
#define CH_CLASS        0x00
#define CH_AILMENTS     0x01
#define CH_NAME         0x02
#define CH_EXP          0x07
#define CH_CURHP        0x0A
#define CH_MAXHP        0x0C
#define CH_STR          0x10
#define CH_AGIL         0x11
#define CH_INT          0x12
#define CH_VIT          0x13
#define CH_LUCK         0x14
#define CH_EXPTONEXT    0x16
#define CH_WEAPONS      0x18
#define CH_ARMOR        0x1C
#define CH_SUBSTATS     0x20
#define CH_LEVEL        0x26

/* Item-name ids for the class names and ailment blurbs. */
#define ITEMNAME_CLASS_BASE    0xF0
#define ITEMNAME_AILMENT_BASE  0xFC

/* Substrings nest at most a couple of levels deep (string -> item name). */
#define TEXT_MAX_DEPTH 4

/* ---- String tables ---- */

const uint8_t* Text_Dialogue(uint8_t id) {
    return dialogue_bytes + dialogue_offsets[id];
}

const uint8_t* Text_ItemName(uint8_t id) {
    return item_name_bytes + item_name_offsets[id];
}

const uint8_t* Text_MenuString(int id) {
    if (id < 0 || id >= MENU_STRING_COUNT) return NULL;
    return menu_text_bytes + menu_text_offsets[id];
}

void Text_CharMenuString(int id, int c, uint8_t* out, int len) {
    const uint8_t* s = Text_MenuString(id);
    int avail = s ? (int)(menu_text_bytes + sizeof(menu_text_bytes) - s) : 0;
    for (int i = 0; i < len; i++) {
        uint8_t b = (i < avail) ? s[i] : 0;
        out[i] = (b == 0x10) ? (uint8_t)(0x10 | c) : b;
    }
}

const uint8_t* Text_EnemyAttackName(int index) {
    if (index < 0 || index >= ENEMY_ATTACK_NAME_COUNT) return NULL;
    return enemy_attack_bytes + enemy_attack_offsets[index];
}

const uint8_t* Text_BattleMessage(int id) {
    if (id < 1 || id > BATTLE_MSG_COUNT) return NULL;
    return battle_msg_bytes + battle_msg_offsets[id - 1];
}

int Text_ToTiles(const uint8_t* src, uint8_t* out, int max) {
    int n = 0;
    for (; src && *src && n < max; src++) {
        uint8_t b = *src;
        if (b >= 0x80) {
            out[n++] = b;
        } else if (b >= 0x1A && b < 0x7A) {
            out[n++] = lut_DTE1[b - 0x1A];
            if (n < max) out[n++] = lut_DTE2[b - 0x1A];
        }
    }
    return n;
}

int Text_ToAscii(const uint8_t* src, char* out, int size) {
    int n = 0;
    for (; *src && n < size - 1; src++) {
        uint8_t b = *src;
        char c = 0;
        if (b >= 0x80 && b <= 0x89)      c = (char)('0' + (b - 0x80));
        else if (b >= 0x8A && b <= 0xA3) c = (char)('A' + (b - 0x8A));
        else if (b >= 0xA4 && b <= 0xBD) c = (char)('a' + (b - 0xA4));
        else switch (b) {
            case 0xBF: c = ','; break;
            case 0xC0: c = '.'; break;
            case 0xC2: c = '-'; break;
            case 0xC4: c = '!'; break;
            case 0xC5: c = '?'; break;
            case 0xFF: c = ' '; break;
        }
        if (c) out[n++] = c;
    }
    out[n] = '\0';
    return n;
}

uint16_t Text_ItemPrice(uint8_t id) {
    return item_prices[id];
}

/* ---- Token output ---- */

typedef struct {
    TextGlyph* out;
    int n, max;
    const SaveData* save;
    uint8_t item_id;      /* dialogue: dlg_itemid */
} Emit;

static void emit(Emit* e, TextGlyph g) {
    if (e->n < e->max) e->out[e->n++] = g;
}

static void emit_tiles(Emit* e, const uint8_t* tiles, int count) {
    for (int i = 0; i < count; i++) emit(e, tiles[i]);
}

/* A DTE pair (bytes $1A-$79).  Bytes $6A-$79 run past the 80-entry tables,
 * reading the ROM bytes that follow them (see text_data.h). */
static void emit_dte(Emit* e, uint8_t c) {
    emit(e, lut_DTE1[c - 0x1A]);
    emit(e, lut_DTE2[c - 0x1A]);
}

/* ---- Numbers (bank_0E PrintNumber / FormatNumber / TrimZeros) ---- */

/* One FormatNumber digit stage: the largest k in 1..9 with value >= k*place
 * (comparing only the bytes the original compares, `mask`).  Subtracts it and
 * returns the digit tile ($80 + k), or $80 if none. */
static uint8_t format_digit(uint32_t* value, uint32_t place, uint32_t mask) {
    uint32_t v = *value & mask;
    for (uint32_t k = 9; k >= 1; k--) {
        if (v >= k * place) {
            *value = (*value & ~mask) | ((v - k * place) & mask);
            return (uint8_t)(TILE_DIGIT_0 + k);
        }
    }
    return TILE_DIGIT_0;
}

int Text_PrintNumber(uint32_t value, int digits, uint8_t* out) {
    if (digits < 1) digits = 1;
    if (digits > 6) digits = 6;

    if (digits == 1) {                          /* PrintNumber_1Digit */
        out[0] = (uint8_t)((value & 0xFF) | 0x80);
        return 1;
    }

    /* FormatNumber_6..3Digits: 6/5-digit stages compare all three bytes,
     * 4/3-digit stages only the low two. */
    static const uint32_t place[7] = { 0, 0, 0, 100, 1000, 10000, 100000 };
    uint32_t v = value & 0xFFFFFF;
    int pos = 0;
    for (int d = digits; d >= 3; d--) {
        out[pos++] = format_digit(&v, place[d], d >= 5 ? 0xFFFFFF : 0xFFFF);
    }

    /* FormatNumber_2Digits: count tens in the low byte (the tens "digit" can
     * exceed 9, which shows as a letter, as in the original). */
    uint8_t lo = (uint8_t)(v & 0xFF);
    uint8_t tens = 0;
    while (lo >= 10) { lo -= 10; tens++; }
    out[pos++] = (uint8_t)(tens | 0x80);
    out[pos++] = (uint8_t)(lo | 0x80);

    /* TrimZeros: blank leading '0's, never the ones digit. */
    for (int i = 0; i < digits - 1 && out[i] == TILE_DIGIT_0; i++)
        out[i] = TILE_SPACE;
    return digits;
}

static void emit_number(Emit* e, uint32_t value, int digits) {
    uint8_t buf[6];
    int n = Text_PrintNumber(value, digits, buf);
    emit_tiles(e, buf, n);
}

/* ---- DrawComplexString ---- */

static void format_complex(Emit* e, const uint8_t* src, int depth);

static void emit_item_name(Emit* e, uint8_t id, int depth) {
    format_complex(e, Text_ItemName(id), depth + 1);
}

/* Port of PrintCharStat (bank_0E $8D70) for stat codes $03-$0B and $2C-$FF.
 * `ch` points at the character's ch_stats entry, `mp` at its ch_magicdata. */
static void emit_char_stat(Emit* e, const uint8_t* ch, const uint8_t* mp, uint8_t code) {
    switch (code) {
    case 0x03: emit_number(e, (uint8_t)(ch[CH_LEVEL] + 1), 2); return;
    case 0x04: emit_number(e, ch[CH_EXP] | (ch[CH_EXP + 1] << 8) | ((uint32_t)ch[CH_EXP + 2] << 16), 6); return;
    case 0x05: emit_number(e, ch[CH_CURHP] | (ch[CH_CURHP + 1] << 8), 3); return;
    case 0x06: emit_number(e, ch[CH_MAXHP] | (ch[CH_MAXHP + 1] << 8), 3); return;
    case 0x07: emit_number(e, ch[CH_STR], 2); return;
    case 0x08: emit_number(e, ch[CH_AGIL], 2); return;
    case 0x09: emit_number(e, ch[CH_INT], 2); return;
    case 0x0A: emit_number(e, ch[CH_VIT], 2); return;
    case 0x0B: emit_number(e, ch[CH_LUCK], 2); return;
    }
    if (code >= 0x2C && code < 0x3C) {          /* cur/max MP, 1 digit */
        emit_number(e, mp[0x20 + (code - 0x2C)], 1);
    } else if (code >= 0x3C && code < 0x42) {   /* damage..mag def, 3 digits */
        emit_number(e, ch[CH_SUBSTATS + (code - 0x3C)], 3);
    } else {                                    /* $42+: exp to next, 5 digits */
        emit_number(e, ch[CH_EXPTONEXT] | (ch[CH_EXPTONEXT + 1] << 8), 5);
    }
}

/* lutMagic: spell index i (0-23) -> its byte in the 32-byte ch_spells
 * layout, 4 per level with the 4th unused. */
static uint8_t lut_magic_slot(int i) { return (uint8_t)(i / 3 * 4 + i % 3); }

/* Control codes $10-$13: stat `code` of character `c`. */
static void emit_stat_code(Emit* e, int c, uint8_t code, int depth) {
    const uint8_t* ram = (const uint8_t*)e->save;
    const uint8_t* ch = ram + OFS_CH_STATS + c * CH_STATS_SIZE;
    const uint8_t* mp = ram + OFS_CH_MAGIC + c * 0x40;

    if (code == 0x00) {                         /* name: 4 tiles as a substring */
        uint8_t name[5] = { ch[CH_NAME], ch[CH_NAME + 1], ch[CH_NAME + 2], ch[CH_NAME + 3], 0 };
        format_complex(e, name, depth + 1);
    } else if (code == 0x01) {                  /* class name */
        emit_item_name(e, (uint8_t)(ITEMNAME_CLASS_BASE + ch[CH_CLASS]), depth);
    } else if (code == 0x02) {                  /* ailment blurb */
        emit_item_name(e, (uint8_t)(ITEMNAME_AILMENT_BASE + ch[CH_AILMENTS]), depth);
    } else if (code < 0x0C || code >= 0x2C) {
        emit_char_stat(e, ch, mp, code);
    } else if (code < 0x14) {
        /* $0C-$13: weapon/armor slot.  The original routine is bugged: its
         * PPU writes leave two blanks, then it draws the item-name string
         * indexed by the raw equipment id instead of the item's name.  The
         * game never uses these codes; reproduced as-is. */
        uint8_t raw = (code < 0x10) ? ch[CH_WEAPONS + (code & 3)] : ch[CH_ARMOR + (code & 3)];
        if (raw == 0) return;
        emit(e, TILE_SPACE);
        emit(e, TILE_SPACE);
        emit_item_name(e, (uint8_t)(raw & 0x7F), depth);
    } else {                                    /* $14-$2B: spell name */
        uint8_t slot = lut_magic_slot(code - 0x14);
        uint8_t spell = mp[slot];
        if (spell == 0) return;
        uint8_t level8 = (uint8_t)((slot * 2) & 0x38);
        emit_item_name(e, (uint8_t)(spell + level8 + 0xAF), depth);
    }
}

static void format_complex(Emit* e, const uint8_t* src, int depth) {
    if (!src || depth > TEXT_MAX_DEPTH) return;
    for (;;) {
        uint8_t c = *src++;
        if (c == 0x00) return;
        if (c >= 0x7A) { emit(e, c); continue; }
        if (c >= 0x1A) { emit_dte(e, c); continue; }

        switch (c) {
        case 0x01:                              /* double line break */
            emit(e, TEXT_NEWLINE);
            emit(e, TEXT_NEWLINE);
            break;
        case 0x02:                              /* item name */
            emit_item_name(e, *src++, depth);
            break;
        case 0x03:                              /* item price (5 digits) */
            emit_number(e, Text_ItemPrice(*src++), 5);
            break;
        case 0x04: {                            /* gold (6 digits) */
            const uint8_t* g = (const uint8_t*)e->save + OFS_GOLD;
            emit_number(e, g[0] | (g[1] << 8) | ((uint32_t)g[2] << 16), 6);
            break;
        }
        case 0x10: case 0x11: case 0x12: case 0x13:
            emit_stat_code(e, c - 0x10, *src++, depth);
            break;
        default:                                /* $05-$0F, $14-$19 */
            emit(e, TEXT_NEWLINE);
            break;
        }
    }
}

int Text_FormatComplex(const uint8_t* src, const SaveData* save,
                       TextGlyph* out, int max) {
    Emit e = { out, 0, max, save, 0 };
    format_complex(&e, src, 0);
    return e.n;
}

/* ---- DrawDialogueString ---- */

static void format_dialogue(Emit* e, const uint8_t* src, int depth) {
    if (!src || depth > TEXT_MAX_DEPTH) return;
    for (;;) {
        uint8_t c = *src++;
        if (c == 0x00) return;
        if (c >= 0x7A) { emit(e, c); continue; }
        if (c >= 0x1A) { emit_dte(e, c); continue; }

        if (c == 0x02) {
            format_dialogue(e, Text_ItemName(e->item_id), depth + 1);
        } else if (c == 0x03) {
            /* Lead character's name.  The original switches the string
             * pointer to the name without saving it, so drawing stops after
             * the name (bugged, and unused by the game). */
            const uint8_t* ch = (const uint8_t*)e->save + OFS_CH_STATS;
            uint8_t name[5] = { ch[CH_NAME], ch[CH_NAME + 1], ch[CH_NAME + 2], ch[CH_NAME + 3], 0 };
            format_dialogue(e, name, depth + 1);
            return;
        } else {
            emit(e, TEXT_NEWLINE);
        }
    }
}

int Text_FormatDialogue(const uint8_t* src, const SaveData* save,
                        uint8_t item_id, TextGlyph* out, int max) {
    Emit e = { out, 0, max, save, item_id };
    format_dialogue(&e, src, 0);
    return e.n;
}
