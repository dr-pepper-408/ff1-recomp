/**
 * @file romdata.c
 * @brief Filling the game's tables from the player's ROM (romdata.h).
 *
 * The C counterpart of tools/extract_data.py: the same tables, byte for
 * byte (tests/dump_tables.c checks it).  Most tables are one run of the ROM
 * and are copied (rom_layout.h, from tools/find_slices.py); the others are
 * put together here.  Only offsets into the ROM are written in this file --
 * the game's data comes from the ROM.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "romdata.h"
#include "rom.h"
#include "../data/generated/tables_decl.h"

static const uint8_t* rom;

static const uint8_t* at(int bank, int offset) { return rom + bank * ROM_BANK_SIZE + offset; }
static unsigned word(const uint8_t* p) { return p[0] | p[1] << 8; }

/* Offsets of tables in the code banks (from the disassembly's listings). */
#define B0B_EXP_TO_ADVANCE      0x1000      /* lut_ExpToAdvance: 49 x 3 bytes */
#define B0B_BATTLE_MESSAGES     0x0C40      /* data_BattleMessages: text, pointers at +$310 */
#define B0B_NOTHING_HAPPENS     0x0FEC      /* message $4E, just past them */
#define B0B_ENEMY_NAMES         0x14E0      /* pointers, then names */
#define B0E_TALK_JUMP_TBL       0x10D3      /* lut_MapObjTalkJumpTbl: a routine per object */
#define B0E_COPYRIGHT           0x04A6      /* TitleScreen_Copyright @lut_Copyright */
#define B0E_SHOP_STRINGS        0x0000      /* lut_ShopStrings */
#define B0E_SHOP_DATA           0x0300      /* lut_ShopData */
#define B0E_MENU_TEXT           0x0500
#define B0F_COMMAND_BOX         0x3A1B      /* lua_BattleCommandBoxInfo */
#define B0F_DTE1                0x30A0
#define B0F_DTE2                0x3050
#define B00_CLASS_STATS         0x3040      /* lut_ClassStartingStats: $10 per class */
#define B01_OW_ROWS_END         0x3F40      /* the overworld rows end at MinimapDecompress */
#define B0D_STORY_TEXT          0x2800
#define B0D_CREDITS_TEXT        0x3B00
#define BATTLE_MSG_TEXT_SIZE    0x3AC       /* the messages and their pointers */

/* ---- Text: the game's strings as the extractor decodes them ---- */

/* The font: what each tile $80-$FF shows (the glyph layout; "@x" are the
 * icon tiles), as tools/extract_data.py's character tables have it. */
static const char* const charmap[128] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F",
    "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V",
    "W", "X", "Y", "Z", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l",
    "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "'", ",",
    ".", " ", "-", "..", "!", "?", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, "@S", "@H", "@K", "@X", "@F", "@N", "@A", "@s", "@h", "@G", "@B", "@T",
    "%", "@P", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, " ",
};

static char pool[16384];        /* the decoded strings */
static size_t pool_n;

static const char* glyph(uint8_t b) { return b >= 0x80 ? charmap[b - 0x80] : NULL; }

/* decode_dte: bytes from `p` (at most `len`) to a string: $05 a line break
 * (written "\n"), $1A-$69 a DTE pair, $80+ the font, anything else "\xNN";
 * up to `max` pieces or the $00. */
static const char* decode(const uint8_t* p, size_t len, int max) {
    char* s = pool + pool_n;
    size_t n = 0;
    int pieces = 0;
    for (size_t i = 0; i < len && p[i]; i++) {
        uint8_t b = p[i];
        char piece[8];
        if (b == 0x05) {
            strcpy(piece, "\\n");
        } else if (b >= 0x1A && b < 0x6A) {
            const char* a = glyph(at(0x0F, B0F_DTE1)[b - 0x1A]);
            const char* c = glyph(at(0x0F, B0F_DTE2)[b - 0x1A]);
            snprintf(piece, sizeof(piece), "%s%s", a ? a : "?", c ? c : "?");
        } else if (glyph(b)) {
            strcpy(piece, glyph(b));
        } else {
            snprintf(piece, sizeof(piece), "\\x%02X", b);
        }
        size_t k = strlen(piece);
        if (pool_n + n + k + 1 >= sizeof(pool)) break;
        memcpy(s + n, piece, k);
        n += k;
        if (++pieces >= max) break;
    }
    s[n] = 0;
    pool_n += n + 1;
    return s;
}

/* _collect_strings: the strings `ptrs` point to (in `data`, which starts at
 * address `base`), each up to and including its $00, into `blob` -- shared
 * pointers sharing bytes -- and where each starts into `offs`. */
static size_t collect(const uint8_t* data, size_t len, unsigned base, const uint8_t* ptrs, int count,
                      uint8_t* blob, uint16_t* offs) {
    size_t n = 0;
    for (int i = 0; i < count; i++) {
        unsigned ptr = word(ptrs + i * 2);
        int seen = -1;
        for (int k = 0; k < i; k++)
            if (word(ptrs + k * 2) == ptr) { seen = k; break; }
        if (seen >= 0) { offs[i] = offs[seen]; continue; }
        size_t start = ptr - base, end = start;
        while (end < len && data[end]) end++;
        offs[i] = (uint16_t)n;
        memcpy(blob + n, data + start, end - start + 1);
        n += end - start + 1;
    }
    return n;
}

/* ---- The tables that aren't one run of the ROM ---- */

static void load_text(void) {


    collect(at(0x0A, 0), ROM_BANK_SIZE, 0x8000, at(0x0A, 0x0000), 256, dialogue_bytes, dialogue_offsets);
    collect(at(0x0A, 0), ROM_BANK_SIZE, 0x8000, at(0x0A, 0x3700), 256, item_name_bytes, item_name_offsets);
    collect(at(0x0A, 0), ROM_BANK_SIZE, 0x8000, at(0x0A, 0x3600), ENEMY_ATTACK_NAME_COUNT,
            enemy_attack_bytes, enemy_attack_offsets);

    /* The battle messages: the text, its pointers at +$310, and message $4E
     * just past them -- the area runs on into the ROM, which is all one. */
    const uint8_t* msg = at(0x0B, B0B_BATTLE_MESSAGES);
    collect(msg, 0x0FEC - 0x0C40 + 64, 0x8C40, msg + 0x310, BATTLE_MSG_COUNT, battle_msg_bytes, battle_msg_offsets);

    const uint8_t* menu = at(0x0E, B0E_MENU_TEXT);
    for (int i = 0; i < MENU_STRING_COUNT; i++) menu_text_offsets[i] = (uint16_t)(word(menu + i * 2) - 0x8500);

    const uint8_t* story = at(0x0D, B0D_STORY_TEXT);
    for (unsigned i = 0; i < sizeof(story_text_offsets) / sizeof(story_text_offsets[0]); i++)
        story_text_offsets[i] = (uint16_t)(word(story + i * 2) - 0xA800);
    const uint8_t* credits = at(0x0D, B0D_CREDITS_TEXT);
    for (int i = 0; i < 4; i++) credits_text_offsets[i] = (uint16_t)(word(credits + i * 2) - 0xBB00);

    /* The shops' strings, and their inventories (5 bytes from each pointer). */
    const uint8_t* ss = at(0x0E, B0E_SHOP_STRINGS);
    collect(ss, 0x300, 0x8000, ss, SHOP_TEXT_COUNT, shop_text_bytes, shop_text_offsets);
    const uint8_t* sd = at(0x0E, B0E_SHOP_DATA);
    for (int i = 0; i < SHOP_COUNT; i++)
        memcpy(shop_inventory[i], sd + (word(sd + i * 2) - 0x8300), 5);

    /* Decoded strings: battle messages (1-based ids; the pointers stop
     * where they reach the table), then message $4E; enemy names. */
    int m = 0;
    for (; m < BATTLE_MESSAGE_COUNT - 1; m++) {
        unsigned ptr = word(msg + 0x310 + m * 2) - 0x8C40;
        if (ptr >= 0x310) break;
        battle_message_table[m] = decode(msg + ptr, 0x310 - ptr, 128);
    }
    battle_message_table[m] = decode(at(0x0B, B0B_NOTHING_HAPPENS), 64, 128);

    const uint8_t* names = at(0x0B, B0B_ENEMY_NAMES);
    for (int i = 0; i < ENEMY_NAME_COUNT; i++) {
        unsigned off = word(names + i * 2) - 0x94E0;
        enemy_name_table[i] = decode(names + off, 1136 - off, 256);
    }
}

static void load_others(void) {
    /* lut_ExpToAdvance: 3-byte values */
    const uint8_t* e = at(0x0B, B0B_EXP_TO_ADVANCE);
    for (int i = 0; i < 49; i++) lut_ExpToAdvance[i] = e[i * 3] | e[i * 3 + 1] << 8 | (uint32_t)e[i * 3 + 2] << 16;

    /* the 6 starting classes, 11 of each 16 bytes */
    for (int c = 0; c < CLASS_STARTING_STATS_COUNT; c++)
        memcpy(&class_starting_stats[c], at(0x00, B00_CLASS_STATS + c * 16), sizeof(class_starting_stats[c]));

    /* the overworld bank: its rows up to MinimapDecompress, then zeros */
    memset(rom_ow_bank, 0, sizeof(rom_ow_bank));
    memcpy(rom_ow_bank, at(0x01, 0), B01_OW_ROWS_END);

    /* the battle sounds: each a 3-byte header and 8 bytes per block, the
     * more of its square and noise blocks */
    unsigned off = 0;
    for (int k = 0; k < 3; k++) {
        lut_BattleSfxOffset[k] = (uint16_t)off;
        const uint8_t* h = data_BattleSoundEffects + off;
        off += 3 + 8 * (h[1] > h[2] ? h[1] : h[2]);
    }

    /* the command box: 5 text records after the box's, each x, y and a
     * pointer to a $00-terminated name */
    const uint8_t* rec = at(0x0F, B0F_COMMAND_BOX + 5);
    for (int k = 0; k < 5; k++) {
        lut_BattleCommandTextPos[k * 2] = rec[k * 5 + 1];
        lut_BattleCommandTextPos[k * 2 + 1] = rec[k * 5 + 2];
        const uint8_t* t = at(0x0F, (int)(word(rec + k * 5 + 3) - 0xC000));
        memset(lut_BattleCommandText[k], 0, 5);
        for (int i = 0; i < 5; i++) {
            lut_BattleCommandText[k][i] = t[i];
            if (!t[i]) break;
        }
    }

    /* the copyright lines: a screen address, then a $00-terminated string */
    const uint8_t* c = at(0x0E, B0E_COPYRIGHT);
    for (int k = 0; k < 2; k++) {
        unsigned addr = word(c);
        uint8_t* row = lut_Copyright[k];
        memset(row, 0, 18);
        row[0] = (uint8_t)((addr - 0x2000) % 32);
        row[1] = (uint8_t)((addr - 0x2000) / 32);
        int n = 0;
        while (c[2 + n]) n++;
        for (int i = 0; i <= n && 2 + i < 18; i++) row[2 + i] = c[2 + i];
        c += 2 + n + 1;
    }

    /* the talk routines: each distinct routine address in the jump table, in
     * order of first appearance, is one TALKFN_ */
    const uint8_t* j = at(0x0E, B0E_TALK_JUMP_TBL);
    unsigned seen[TALKFN_COUNT];
    int nseen = 0;
    for (int i = 0; i < MAPOBJ_ID_COUNT; i++) {
        unsigned a = word(j + i * 2);
        int k = 0;
        while (k < nseen && seen[k] != a) k++;
        if (k == nseen && nseen < TALKFN_COUNT) seen[nseen++] = a;
        lut_MapObjTalkFn[i] = (uint8_t)k;
    }
}

RomStatus RomData_Open(const char* path) {
    RomStatus s = Rom_LoadFile(path);
    if (s == ROM_OK && RomData_Load() != 0) s = ROM_ERR_FILE;
    return s;
}

RomStatus RomData_Find(const char* path, const char* const* fallbacks) {
    RomStatus s = ROM_ERR_FILE;
    if (path && (s = RomData_Open(path)) == ROM_OK) return s;
    const char* env = getenv("FF1_ROM");
    if (env && (s = RomData_Open(env)) == ROM_OK) return s;
    for (int i = 0; fallbacks && fallbacks[i]; i++)
        if ((s = RomData_Open(fallbacks[i])) == ROM_OK) return s;
    return s;
}

int RomData_Load(void) {
    rom = Rom_Data();
    if (!rom) return 1;
    pool_n = 0;
#define SLICE(name, off) memcpy(name, rom + (off), sizeof(name));
#include "rom_layout.h"
#undef SLICE
    load_text();
    load_others();
    return 0;
}
