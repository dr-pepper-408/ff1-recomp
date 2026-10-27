/**
 * @file test_main.c
 * @brief Unit tests for the SDL-free game logic (run with `ctest`).
 *
 * Expected values come from the disassembly (routine names cited per test)
 * or from known in-game behaviour.
 */

#include <stdio.h>
#include <string.h>
#include "text.h"
#include "rng.h"
#include "party.h"
#include "screen_fx.h"
#include "ppu.h"
#include "btl_core.h"
#include "save.h"
#include "apu.h"
#include "music.h"
#include "sfx.h"
#include "rom.h"
#include "romdata.h"
#include <stdlib.h>
#include <math.h>
#include "../data/generated/rng_table.h"
#include "../data/generated/magic_data.h"
#include "../data/generated/battle_messages.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do {                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            g_failures++;                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

/* Font tiles -> ASCII for readable comparisons ('#' = no ASCII equivalent). */
static char tile_ascii(TextGlyph g) {
    if (g == TEXT_NEWLINE) return '|';
    if (g >= 0x80 && g <= 0x89) return (char)('0' + g - 0x80);
    if (g >= 0x8A && g <= 0xA3) return (char)('A' + g - 0x8A);
    if (g >= 0xA4 && g <= 0xBD) return (char)('a' + g - 0xA4);
    switch (g) {
    case 0xFF: return ' ';
    case 0xC0: return '.';
    case 0xBF: return ',';
    case 0xC2: return '-';
    case 0xC4: return '!';
    case 0xC5: return '?';
    }
    return '#';
}

static const char* to_ascii(const TextGlyph* g, int n) {
    static char buf[TEXT_MAX_GLYPHS + 1];
    for (int i = 0; i < n; i++) buf[i] = tile_ascii(g[i]);
    buf[n] = '\0';
    return buf;
}

static const char* number(uint32_t value, int digits) {
    uint8_t tiles[6];
    TextGlyph g[6];
    int n = Text_PrintNumber(value, digits, tiles);
    for (int i = 0; i < n; i++) g[i] = tiles[i];
    return to_ascii(g, n);
}

/* ---- PrintNumber / FormatNumber / TrimZeros (bank_0E) ---- */
static void test_print_number(void) {
    CHECK(strcmp(number(67, 4), "  67") == 0);
    CHECK(strcmp(number(0, 3), "  0") == 0);           /* ones digit never trimmed */
    CHECK(strcmp(number(100, 3), "100") == 0);
    CHECK(strcmp(number(999999, 6), "999999") == 0);
    CHECK(strcmp(number(50000, 5), "50000") == 0);
    CHECK(strcmp(number(7, 1), "7") == 0);
    /* Overflow quirks of the original: */
    CHECK(strcmp(number(100, 2), "A0") == 0);          /* 10 tens -> tile $8A 'A' */
    CHECK(strcmp(number(12, 1), "C") == 0);            /* 12 | $80 = $8C 'C' */
    CHECK(strcmp(number(1500, 3), "988") == 0);        /* 900 taken, then low byte of 600 = 88 */
}

/* ---- DrawDialogueString / DrawComplexString (bank_0F) ---- */
/* Game text is checked by its length and a hash, so the repository doesn't
 * contain the game's text: text_is(s, len, fnv1a, exact) -- the whole
 * string (exact) or its first `len` characters. */
static int text_is(const char* s, size_t len, uint32_t hash, int exact) {
    if (exact ? strlen(s) != len : strlen(s) < len) return 0;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return h == hash;
}

static void test_text(void) {
    static SaveData save;
    TextGlyph g[TEXT_MAX_GLYPHS];
    int n;

    memset(&save, 0, sizeof(save));

    n = Text_FormatDialogue(Text_Dialogue(0x00), &save, 0, g, TEXT_MAX_GLYPHS);
    CHECK(text_is(to_ascii(g, n), 13, 0xA7006E9Au, 1));

    n = Text_FormatDialogue(Text_Dialogue(0x02), &save, 0, g, TEXT_MAX_GLYPHS);
    CHECK(text_is(to_ascii(g, n), 34, 0xD4B24BAFu, 0));

    /* Chest text: $02 prints dlg_itemid's name and takes no argument byte. */
    n = Text_FormatDialogue(Text_Dialogue(0xF0), &save, 0x19, g, TEXT_MAX_GLYPHS);
    CHECK(text_is(to_ascii(g, n), 20, 0x230FA631u, 0));
    CHECK(strstr(to_ascii(g, n), "|HEAL") != NULL);
    n = Text_FormatDialogue(Text_Dialogue(0xF0), &save, 0x6C, g, TEXT_MAX_GLYPHS);
    CHECK(strstr(to_ascii(g, n), "|10 G") != NULL);

    /* Item names: potions end in the potion icon tile ($E1). */
    n = Text_FormatComplex(Text_ItemName(0x19), &save, g, TEXT_MAX_GLYPHS);
    CHECK(n == 5 && g[4] == 0xE1);
    n = Text_FormatComplex(Text_ItemName(0xF0), &save, g, TEXT_MAX_GLYPHS);
    CHECK(strcmp(to_ascii(g, n), "FIGHTER") == 0);

    /* Control codes: $04 gold (6 digits), $10 xx character stats. */
    Party_SetGold(&save, 400);
    const uint8_t gold[] = { 0x04, 0x00 };
    n = Text_FormatComplex(gold, &save, g, TEXT_MAX_GLYPHS);
    CHECK(strcmp(to_ascii(g, n), "   400") == 0);

    save.characters[1].cur_hp = 35;
    save.characters[1].level = 4;                          /* 0-based: shows 5 */
    save.characters[1].name[0] = 0x9D;                     /* "TEST" */
    save.characters[1].name[1] = 0x8E;
    save.characters[1].name[2] = 0x9C;
    save.characters[1].name[3] = 0x9D;
    const uint8_t stats[] = { 0x11, 0x00, 0x05, 0x11, 0x03, 0x11, 0x05, 0x00 };
    n = Text_FormatComplex(stats, &save, g, TEXT_MAX_GLYPHS);
    CHECK(strcmp(to_ascii(g, n), "TEST| 5 35") == 0);

    /* $03 xx: item price, 5 digits.  TENT = 75 GP. */
    const uint8_t price[] = { 0x03, 0x16, 0x00 };
    n = Text_FormatComplex(price, &save, g, TEXT_MAX_GLYPHS);
    CHECK(strcmp(to_ascii(g, n), "   75") == 0);
    CHECK(Text_ItemPrice(0x18) == 3000);                   /* HOUSE */

    /* $01 is a double line break in complex strings. */
    const uint8_t brk[] = { 0x8A, 0x01, 0x8B, 0x00 };
    n = Text_FormatComplex(brk, &save, g, TEXT_MAX_GLYPHS);
    CHECK(strcmp(to_ascii(g, n), "A||B") == 0);

    /* Output is truncated at `max`, never overrun. */
    n = Text_FormatDialogue(Text_Dialogue(0x02), &save, 0, g, 4);
    CHECK(n == 4);
}

/* ---- RNG (bank_0F BattleStepRNG / BattleRNG, bank_0B RandAX) ---- */
static void test_rng(void) {
    RngState st = { 0, 0, 0, 0 };

    Rng_SetState(&st);
    for (int i = 0; i < 300; i++)
        CHECK(Rng_Battle() == battle_rng_table[i & 0xFF]);

    /* BattleStepRNG counts up; after a full cycle the sign gains $A0, which
     * sets bit 7, so the next cycle counts down. */
    Rng_SetState(&st);
    CHECK(Rng_BattleStep() == rng_table[1]);
    for (int i = 0; i < 254; i++) Rng_BattleStep();
    CHECK(Rng_BattleStep() == rng_table[0]);                 /* wrapped to 0 */
    Rng_GetState(&st);
    CHECK(st.battlestep_sign == 0xA0);
    CHECK(Rng_BattleStep() == rng_table[0xFF]);              /* now stepping down */

    /* RandAX stays in range; [x, x] always gives x. */
    st = (RngState){ 0, 0, 0, 0 };
    Rng_SetState(&st);
    int in_range = 1;
    for (int i = 0; i < 512; i++) {
        uint8_t v = Rng_RandAX(10, 20);
        if (v < 10 || v > 20) in_range = 0;
    }
    CHECK(in_range);
    CHECK(Rng_RandAX(42, 42) == 42);

    /* GetBattleFormation increments its counter before reading. */
    st = (RngState){ 0, 0, 0, 0 };
    Rng_SetState(&st);
    CHECK(Rng_FormationRoll() == rng_table[1]);
    CHECK(Rng_FormationRoll() == rng_table[2]);
}

/* ---- Party (bank_0F AddGPToParty) ---- */
static void test_gold(void) {
    static SaveData save;
    memset(&save, 0, sizeof(save));

    Party_AddGold(&save, 500);
    CHECK(Party_Gold(&save) == 500);

    Party_SetGold(&save, 998000);
    Party_AddGold(&save, 5000);
    CHECK(Party_Gold(&save) == 999999);                      /* capped */

    /* The original's byte-wise check misses totals whose middle byte is
     * below $42 once the high byte reaches $10 (here 1,048,576). */
    Party_SetGold(&save, 0x0FFFFF);
    Party_AddGold(&save, 1);
    CHECK(Party_Gold(&save) == 0x100000);
}

/* ---- Screen transitions (bank_0F ScreenWipe / CyclePalettes / BattleTransition) ---- */
static int fx_frames(ScreenFx* fx) {
    int frames = 1;                       /* the frame shown right after Start */
    while (!ScreenFx_Step(fx)) frames++;
    return frames;
}

static void test_screen_fx(void) {
    ScreenFx fx;
    uint8_t pal[16] = { 0x0F, 0x1A, 0x29, 0x30, 0x0F, 0x0F, 0x0F, 0x0F,
                        0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F };

    /* Open: visible lines 1, 5, ... 221 (56 frames); close: 221 ... 5 (55). */
    ScreenFx_Start(&fx, SCREENFX_WIPE_OPEN, NULL);
    CHECK(fx.wipe_rows == 1 && fx.wipe_top - 11 == 122);
    CHECK(fx_frames(&fx) == 56);
    ScreenFx_Start(&fx, SCREENFX_WIPE_CLOSE, NULL);
    CHECK(fx.wipe_rows == 221 && fx.wipe_top - 11 == 10);
    CHECK(fx_frames(&fx) == 55);

    /* BattleTransition: counter $00-$40, new emphasis every 4 frames. */
    ScreenFx_Start(&fx, SCREENFX_BATTLE, NULL);
    CHECK(fx_frames(&fx) == 0x41);
    ScreenFx_Start(&fx, SCREENFX_BATTLE, NULL);
    for (int i = 0; i < 9; i++) ScreenFx_Step(&fx);
    ScreenFx_Apply(&fx);
    CHECK(g_ppu.emphasis == 2);           /* counter 9: (9 << 3) & $E0 = $40 */

    /* Cycling out: a hue step every 4th frame; $x9 -> $xA -> $xB -> $xC -> black. */
    ScreenFx_Start(&fx, SCREENFX_PALCYC_OUT, pal);
    for (int i = 0; i < 3; i++) ScreenFx_Step(&fx);   /* frames 2-4: one step */
    CHECK(fx.pal[2] == 0x2A && fx.pal[1] == 0x1B && fx.pal[3] == 0x31);
    while (!ScreenFx_Step(&fx)) {}
    int all_black = 1;
    for (int i = 0; i < 16; i++) if (fx.pal[i] != 0x0F) all_black = 0;
    CHECK(all_black);

    /* Cycling in: non-black colors start at $xC and step down to the palette. */
    ScreenFx_Start(&fx, SCREENFX_PALCYC_IN, pal);
    CHECK(fx.pal[0] == 0x0F && fx.pal[1] == 0x1C && fx.pal[2] == 0x2C && fx.pal[3] == 0x3C);
    while (!ScreenFx_Step(&fx)) {}
    CHECK(memcmp(fx.pal, pal, 16) == 0);
}

/* ---- Save layout (types.h) ---- */
static void test_save_layout(void) {
    static SaveData save;
    memset(&save, 0, sizeof(save));
    const uint8_t* ram = (const uint8_t*)&save;

    save.characters[2].cur_hp = 0x1234;                      /* ch_curhp, char 2 */
    CHECK(ram[0x100 + 2 * 0x40 + 0x0A] == 0x34 && ram[0x100 + 2 * 0x40 + 0x0B] == 0x12);
    save.character_magic[3].max_mp[7] = 9;                   /* ch_maxmp, char 3 */
    CHECK(ram[0x300 + 3 * 0x40 + 0x28 + 7] == 9);
    CHECK(sizeof(SaveData) == 0x400);
}

/* ---- Battle rules (bank_0C DoPhysicalAttack etc.) ---- */
static void seed_battle_rng(uint8_t state) {
    RngState st = { 0, 0, 0, state };
    Rng_SetState(&st);
}

static void test_battle_rules(void) {
    BtlAttacker a = { .strength = 20, .hitrate = 10, .numhits = 1, .numhitsmult = 1 };
    BtlAttackResult r;

    /* Damage per hit is [base, 2*base] - absorb, at least 1.  (A roll of 0
     * is a critical even with crit rate 0, which adds the damage again.) */
    int in_range = 1, any_hit = 0;
    for (int seed = 0; seed < 256; seed++) {
        BtlDefender d = { .absorb = 5, .hp = 500 };
        seed_battle_rng((uint8_t)seed);
        Btl_PhysicalAttack(&a, &d, 0, &r);
        if (r.hits && !r.crits) {
            any_hit = 1;
            if (r.damage < 15 || r.damage > 35) in_range = 0;
        }
        if (d.hp != 500 - r.damage) in_range = 0;
    }
    CHECK(any_hit && in_range);

    /* Evade 255 leaves a hit chance of 1: nearly everything misses ... */
    int hits = 0;
    for (int seed = 0; seed < 256; seed++) {
        BtlDefender d = { .evade = 255, .hp = 500 };
        seed_battle_rng((uint8_t)seed);
        Btl_PhysicalAttack(&a, &d, 0, &r);
        hits += r.hits;
    }
    CHECK(hits <= 4);
    /* ... unless the defender sleeps: then evade isn't applied at all, and
     * damage gets +25%. */
    hits = 0;
    for (int seed = 0; seed < 256; seed++) {
        BtlDefender d = { .evade = 255, .ailments = BTL_AIL_SLEEP, .hp = 500 };
        seed_battle_rng((uint8_t)seed);
        Btl_PhysicalAttack(&a, &d, 0, &r);
        hits += r.hits;
        if (r.hits && !r.crits && (r.damage < 25 || r.damage > 50)) hits = -1000;
    }
    CHECK(hits > 200);

    /* A dead or stoned defender: BTLMSG_INEFFECTIVE, nothing changes. */
    BtlDefender dead = { .ailments = BTL_AIL_STONE, .hp = 7 };
    Btl_PhysicalAttack(&a, &dead, 0, &r);
    CHECK(r.ineffective && dead.hp == 7 && dead.ailments == BTL_AIL_STONE);

    /* Death replaces every other ailment. */
    int killed = 0;
    for (int seed = 0; seed < 64 && !killed; seed++) {
        BtlDefender d = { .ailments = BTL_AIL_POISON | BTL_AIL_DARK, .hp = 1 };
        seed_battle_rng((uint8_t)seed);
        Btl_PhysicalAttack(&a, &d, 0, &r);
        if (r.killed) { killed = 1; CHECK(d.hp == 0 && d.ailments == BTL_AIL_DEAD); }
    }
    CHECK(killed);

    /* In-battle stats: hits = hit rate / 32 + 1; crit rate = weapon index (bug). */
    static SaveData save;
    memset(&save, 0, sizeof(save));
    save.characters[0].hit_rate = 70;
    save.characters[0].weapons[1] = 0x80 | 5;
    BtlCharStats cs;
    Btl_LoadCharStats(&save, 0, &cs);
    CHECK(cs.numhits == 3 && cs.critrate == 5 && cs.numhitsmult == 1);

    /* Ailments in and out of battle. */
    CHECK(Btl_AilmentObToIb(3) == BTL_AIL_POISON);
    CHECK(Btl_AilmentIbToOb(BTL_AIL_POISON | BTL_AIL_SLEEP) == 3);
    CHECK(Btl_AilmentIbToOb(BTL_AIL_DEAD | BTL_AIL_POISON) == 1);   /* death wins */

    /* Running. */
    CHECK(Btl_TryRun(0, 0, 1, 1) == 1);          /* striking first always escapes */
    CHECK(Btl_TryRun(255, 0, 0, 1) == 0);        /* "no run" formations */
    seed_battle_rng(0);
    CHECK(Btl_TryRun(0, 10, 0, 0) == 0);         /* luck 0 never beats the roll */
}

/* ---- Magic (bank_0C BtlMag_PerformSpellEffect) ---- */
static BtlMagic spell(int id) {
    const MagicData* m = &magic_table[id];
    return (BtlMagic){ m->effect, m->element, m->hit_rate, m->effectivity };
}

static int has_msg(const BtlMagResult* r, uint8_t msg) {
    for (int i = 0; i < r->nmsgs; i++) if (r->msgs[i] == msg) return 1;
    return 0;
}

enum { SP_CURE = 0, SP_HARM = 1, SP_FIRE = 4, SP_SLEP = 5, SP_LOCK = 6, SP_TMPR = 14,
       SP_SLOW = 15, SP_FAST = 29, SP_BRAK = 53, SP_XXXX = 63, POT_PURE = 0x41 };

static void test_magic(void) {
    BtlMagResult r;

    /* FIRE: always "connects"; damage rand[10,20], doubled when the roll is a
     * crit (148 + 24 - magdef).  A weakness makes it x1.5 (base 15). */
    int ok = 1, crit = 0, plain = 0;
    for (int seed = 0; seed < 256; seed++) {
        BtlMagic m = spell(SP_FIRE);
        BtlMagDefender d = { .hp = 500, .hpmax = 500, .magdef = 100 };
        seed_battle_rng((uint8_t)seed);
        Btl_MagicEffect(SP_FIRE, &m, &d, 0, &r);
        if (!r.connected || !r.show_damage || d.hp != 500 - r.damage) ok = 0;
        if (r.damage >= 10 && r.damage <= 20) plain++;
        else if (r.damage >= 20 && r.damage <= 40) crit++;
        else ok = 0;
    }
    CHECK(ok && plain > 0 && crit > 0);
    ok = 1;
    for (int seed = 0; seed < 64; seed++) {
        BtlMagic m = spell(SP_FIRE);
        BtlMagDefender d = { .hp = 500, .elemweakness = 0x10, .magdef = 255 };
        seed_battle_rng((uint8_t)seed);
        Btl_MagicEffect(SP_FIRE, &m, &d, 0, &r);
        if (r.damage < 15 || r.damage > 60) ok = 0;
    }
    CHECK(ok);

    /* Killing: DEAD is added, BTLMSG_TERMINATED (enemies) / BTLMSG_SLAIN (players). */
    {
        BtlMagic m = spell(SP_FIRE);
        BtlMagDefender d = { .hp = 3, .ailments = BTL_AIL_POISON };
        seed_battle_rng(0);
        Btl_MagicEffect(SP_FIRE, &m, &d, 0, &r);
        CHECK(d.hp == 0 && (d.ailments & BTL_AIL_DEAD) && has_msg(&r, BTLMSG_TERMINATED));
        BtlMagDefender pd = { .hp = 3 };
        Btl_MagicEffect(SP_FIRE, &m, &pd, 1, &r);
        CHECK(has_msg(&r, BTLMSG_SLAIN));
    }

    /* HARM only hurts undead (category bit 3), else BTLMSG_INEFFECTIVE. */
    {
        BtlMagic m = spell(SP_HARM);
        BtlMagDefender d = { .hp = 100, .category = 0 };
        Btl_MagicEffect(SP_HARM, &m, &d, 0, &r);
        CHECK(!r.connected && d.hp == 100 && has_msg(&r, BTLMSG_INEFFECTIVE));
        BtlMagDefender u = { .hp = 100, .category = 0x08 };
        Btl_MagicEffect(SP_HARM, &m, &u, 0, &r);
        CHECK(r.connected && u.hp < 100);
    }

    /* CURE: rand[16,32] HP, capped at max, "HP up!"; nothing for the dead. */
    {
        BtlMagic m = spell(SP_CURE);
        BtlMagDefender d = { .hp = 10, .hpmax = 100 };
        Btl_MagicEffect(SP_CURE, &m, &d, 1, &r);
        CHECK(d.hp >= 26 && d.hp <= 42 && has_msg(&r, BTLMSG_HPUP));
        BtlMagDefender full = { .hp = 95, .hpmax = 100 };
        Btl_MagicEffect(SP_CURE, &m, &full, 1, &r);
        CHECK(full.hp == 100);
    }

    /* SLEP on a sleeper: the ailment is "added" again with BTLMSG_INEFFECTIVE. */
    {
        BtlMagic m = spell(SP_SLEP);
        int asleep = 0, again = 0;
        for (int seed = 0; seed < 32; seed++) {
            BtlMagDefender d = { .hp = 50 };
            seed_battle_rng((uint8_t)seed);
            Btl_MagicEffect(SP_SLEP, &m, &d, 0, &r);
            if ((d.ailments & BTL_AIL_SLEEP) && has_msg(&r, BTLMSG_ASLEEP)) asleep++;
            BtlMagDefender s2 = { .hp = 50, .ailments = BTL_AIL_SLEEP };
            seed_battle_rng((uint8_t)seed);
            Btl_MagicEffect(SP_SLEP, &m, &s2, 0, &r);
            if (r.connected && has_msg(&r, BTLMSG_INEFFECTIVE) && !has_msg(&r, BTLMSG_ASLEEP)) again++;
        }
        CHECK(asleep > 16 && again == asleep);
        /* Resisting the element zeroes the hit chance to 0 + 24 - magdef. */
        int hit = 0;
        for (int seed = 0; seed < 256; seed++) {
            BtlMagDefender d = { .hp = 50, .elemresist = 0x01, .magdef = 24 };
            seed_battle_rng((uint8_t)seed);
            Btl_MagicEffect(SP_SLEP, &m, &d, 0, &r);
            hit += r.connected;
        }
        CHECK(hit <= 3);                      /* only rolls of 0 (0, 0 and 1 in the table) */
    }

    /* LOCK (EvadeDown) never works: the BUGGED JMP. */
    {
        BtlMagic m = spell(SP_LOCK);
        int any = 0;
        for (int seed = 0; seed < 64; seed++) {
            BtlMagDefender d = { .hp = 50, .evade = 40 };
            seed_battle_rng((uint8_t)seed);
            Btl_MagicEffect(SP_LOCK, &m, &d, 0, &r);
            any |= r.connected || d.evade != 40;
        }
        CHECK(!any);
    }

    /* FAST stops at x2 (BTLMSG_INEFFECTIVE); SLOW at 0 stays 0 but "works". */
    {
        BtlMagic m = spell(SP_FAST);
        BtlMagDefender d = { .hp = 50, .numhitsmult = 1 };
        Btl_MagicEffect(SP_FAST, &m, &d, 1, &r);
        CHECK(r.connected && d.numhitsmult == 2);
        Btl_MagicEffect(SP_FAST, &m, &d, 1, &r);
        CHECK(!r.connected && d.numhitsmult == 2 && has_msg(&r, BTLMSG_INEFFECTIVE));
        BtlMagic sl = spell(SP_SLOW);
        int ok0 = 1;
        for (int seed = 0; seed < 32; seed++) {
            BtlMagDefender z = { .hp = 50, .numhitsmult = 0 };
            seed_battle_rng((uint8_t)seed);
            Btl_MagicEffect(SP_SLOW, &sl, &z, 0, &r);
            if (z.numhitsmult != 0) ok0 = 0;
        }
        CHECK(ok0);
    }

    /* TMPR raises strength (the caller doesn't save it for characters). */
    {
        BtlMagic m = spell(SP_TMPR);
        BtlMagDefender d = { .hp = 50, .strength = 250 };
        Btl_MagicEffect(SP_TMPR, &m, &d, 1, &r);
        CHECK(r.connected && d.strength == 255);
    }

    /* XXXX (InflictAilment2): death when HP <= 300, no roll; HP is left as
     * it is.  BRAK stone on a character reads "Stopped". */
    {
        BtlMagic m = spell(SP_XXXX);
        BtlMagDefender d = { .hp = 300 };
        Btl_MagicEffect(SP_XXXX, &m, &d, 0, &r);
        CHECK(r.connected && d.hp == 300 && (d.ailments & BTL_AIL_DEAD) &&
              has_msg(&r, BTLMSG_TERMINATED));
        BtlMagDefender big = { .hp = 301 };
        Btl_MagicEffect(SP_XXXX, &m, &big, 0, &r);
        CHECK(!r.connected && big.ailments == 0);

        BtlMagic b = spell(SP_BRAK);
        int stopped = 0;
        for (int seed = 0; seed < 32 && !stopped; seed++) {
            BtlMagDefender pd = { .hp = 50 };
            seed_battle_rng((uint8_t)seed);
            Btl_MagicEffect(SP_BRAK, &b, &pd, 1, &r);
            if (pd.ailments & BTL_AIL_STONE)
                stopped = has_msg(&r, BTLMSG_STOPPED) && !has_msg(&r, BTLMSG_BROKENTOPIECES);
        }
        CHECK(stopped);
    }

    /* PURE potion: cures poison, BTLMSG_NEUTRALIZED, and no HP-up message. */
    {
        const MagicData* pm = &magic_table[POT_PURE];
        BtlMagic m = { pm->effect, pm->element, pm->hit_rate, pm->effectivity };
        BtlMagDefender d = { .hp = 20, .hpmax = 30, .ailments = BTL_AIL_POISON };
        Btl_MagicEffect(POT_PURE, &m, &d, 1, &r);
        CHECK(d.ailments == 0 && r.nmsgs == 2 && r.msgs[0] == 0 && r.msgs[1] == BTLMSG_NEUTRALIZED);
    }

    /* Message ids index the ROM table (1-based). */
    CHECK(text_is(battle_message_table[BTLMSG_INEFFECTIVE - 1], 11, 0xEA202A17u, 1));
    CHECK(text_is(battle_message_table[BTLMSG_NOTHINGHAPPENS - 1], 15, 0x700389CFu, 1));
}

/* ---- End of battle (bank_0B EndOfBattleWrapUp, LvlUp_xxx) ---- */
static uint32_t exp_of(const CharacterStats* c) {
    return c->exp[0] | ((uint32_t)c->exp[1] << 8) | ((uint32_t)c->exp[2] << 16);
}

static void test_level_up(void) {
    /* DivideRewardBySurvivors: 16-bit dividend, at least 1. */
    CHECK(Btl_ExpShare(100, 3) == 33);
    CHECK(Btl_ExpShare(0, 4) == 1);
    CHECK(Btl_ExpShare(0x12345, 1) == 0x2345);

    static SaveData save;
    Party_Init(&save);                           /* Fighter, Thief, Black Belt, Red Mage */
    CHECK(save.characters[0].exp_to_next == 40);
    BtlLevelUp lv;

    /* Fighter level 1 -> 2 at 40 EXP.  Fighter level 2 data is $3A: strong
     * (+20..25 HP), Str/Agi/Vit guaranteed. */
    CharacterStats* ft = &save.characters[0];
    CharacterStats before = *ft;
    seed_battle_rng(0);
    CHECK(Btl_AwardExp(&save, 0, 39, &lv) == 0 && ft->exp_to_next == 1);
    CHECK(Btl_AwardExp(&save, 0, 1, &lv) == 1);
    int gain = ft->max_hp - before.max_hp;
    CHECK(ft->level == 1 && lv.level == 2 && lv.max_hp == ft->max_hp);
    CHECK(gain >= before.vit / 4 + 1 + 20 && gain <= before.vit / 4 + 1 + 25);
    CHECK(lv.statup[0] && lv.statup[1] && lv.statup[3]);
    CHECK(ft->str == before.str + 1 && ft->agil == before.agil + 1 && ft->vit == before.vit + 1);
    CHECK(ft->hit_rate == before.hit_rate + 3 && ft->mag_def == before.mag_def + 3);
    CHECK(ft->evade == before.evade + 1);
    CHECK(ft->damage == before.damage + ((ft->str & 1) ? 0 : 1));
    CHECK(ft->exp_to_next == 196 - 40);
    CHECK(save.character_magic[0].max_mp[0] == 0);           /* Fighters get no MP */

    /* Enough EXP for several levels still gives just one (BUGGED). */
    CHECK(Btl_AwardExp(&save, 0, 5000, &lv) == 1 && ft->level == 2);
    CHECK(Btl_AwardExp(&save, 0, 0, &lv) == 1 && ft->level == 3);    /* the next battle */

    /* The dead and stoned get nothing, but exptonext is refreshed. */
    CharacterStats* th = &save.characters[1];
    th->ailments = 1;
    th->exp_to_next = 7;
    CHECK(Btl_AwardExp(&save, 1, 100, &lv) == 0 && exp_of(th) == 0 && th->exp_to_next == 40);
    th->ailments = 3;                                        /* poisoned is fine */
    CHECK(Btl_AwardExp(&save, 1, 100, &lv) == 1);
    th->ailments = 0;

    /* A stat at 99 stays there but is still reported as up. */
    th->str = 99;
    th->level = 1;
    seed_battle_rng(0);
    uint32_t need = 196;
    th->exp[0] = (uint8_t)need; th->exp[1] = 0; th->exp[2] = 0;
    Btl_AwardExp(&save, 1, 0, &lv);
    CHECK(th->str == 99);

    /* An unarmed Black Belt: Absorb = level, Damage = 2 x level. */
    CharacterStats* bb = &save.characters[2];
    for (int w = 0; w < 4; w++) bb->weapons[w] = 0;
    Btl_AwardExp(&save, 2, 40, &lv);
    CHECK(bb->level == 1 && bb->absorb == 2 && bb->damage == 4);

    /* The Red Mage's level 2 MP gain, capped at 9 per level. */
    CharacterMagic* rm = &save.character_magic[3];
    uint8_t mp0 = rm->max_mp[0];
    rm->max_mp[1] = 9;
    Btl_AwardExp(&save, 3, 40, &lv);
    CHECK(rm->max_mp[0] >= mp0 && rm->max_mp[1] == 9);

    /* Level 50 is the end; EXP stops at 999999. */
    ft->level = 49;
    ft->exp[0] = 0x3F; ft->exp[1] = 0x42; ft->exp[2] = 0x0F;  /* 999999 */
    CHECK(Btl_AwardExp(&save, 0, 500, &lv) == 0 && exp_of(ft) == 999999 && ft->exp_to_next == 0);
}

/* ---- ReSortPartyByAilment (bank_0C) ---- */
static void party_order(const SaveData* s, char out[5]) {
    for (int i = 0; i < 4; i++) out[i] = (char)('A' + s->characters[i].class);
    out[4] = '\0';
}

static void test_resort(void) {
    static SaveData s;
    char order[5];
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < 4; i++) {                /* tag A-D by class, B-D's magic too */
        s.characters[i].class = (uint8_t)i;
        s.character_magic[i].max_mp[0] = (uint8_t)i;
    }
    Btl_ReSortParty(&s);
    party_order(&s, order);
    CHECK(strcmp(order, "ABCD") == 0);           /* nobody hurt: unchanged */

    /* Leader dead: to the back, but the BUGGED swap reorders the others. */
    s.characters[0].ailments = 1;
    Btl_ReSortParty(&s);
    party_order(&s, order);
    CHECK(strcmp(order, "DBCA") == 0);
    CHECK(s.characters[3].ailments == 1 && s.character_magic[0].max_mp[0] == 3);

    /* Last character poisoned: already at the back, nothing moves. */
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < 4; i++) s.characters[i].class = (uint8_t)i;
    s.characters[3].ailments = 3;
    Btl_ReSortParty(&s);
    party_order(&s, order);
    CHECK(strcmp(order, "ABCD") == 0);
}

/* ---- Equipment stats (bank_0F Unadjust/ReadjustEquipStats) ---- */
static void test_equip_stats(void) {
    static SaveData save;
    Party_Init(&save);                           /* Fighter, Thief, Black Belt, Red Mage */
    CHECK(save.characters[0].max_hp == 35 && save.characters[0].damage == 10);
    CHECK(save.character_magic[3].max_mp[0] == 2);   /* Red Mage starts with 2 MP */

    CharacterStats* ft = &save.characters[0];
    uint8_t dmg = ft->damage, hit = ft->hit_rate;
    ft->weapons[0] = 0x80 | 1;                   /* equip weapon 1 */
    Party_ReadjustEquipStats(&save, 0);
    CHECK(ft->damage != dmg || ft->hit_rate != hit);
    Party_UnadjustEquipStats(&save, 0);
    CHECK(ft->damage == dmg && ft->hit_rate == hit);

    /* An unarmed Black Belt hits for 2 x level. */
    CharacterStats* bb = &save.characters[2];
    bb->level = 4;                               /* 0-based: level 5 */
    Party_UnadjustEquipStats(&save, 0);
    Party_ReadjustEquipStats(&save, 0);
    CHECK(bb->damage == 10);
}

/* SaveGame / VerifyChecksum and the 8 KB SRAM file. */
static void test_save_game(void) {
    static SaveData block, unsram, sram, loaded;

    /* The sum carries: $FF + $01 leaves 0 with carry, which the next
     * addition ($6600,X) picks up. */
    memset(&block, 0, sizeof(block));
    ((uint8_t*)&block)[0x000] = 0xFF;
    ((uint8_t*)&block)[0x100] = 0x01;
    CHECK(Save_Sum(&block) == 0x01);

    Party_Init(&unsram);
    Party_SetGold(&unsram, 12345);
    unsram.characters[2].cur_hp = 77;
    Save_Game(&unsram, 0x92, 0x9E, 1, &sram);
    CHECK(unsram.ow_scroll_x == 0x92 && unsram.ow_scroll_y == 0x9E && unsram.vehicle == 1);
    CHECK(sram.assert_55 == 0x55 && sram.assert_AA == 0xAA);
    CHECK(Save_Sum(&sram) == 0xFF);
    CHECK(Save_Valid(&sram));
    loaded = sram;
    loaded.characters[0].str ^= 1;                    /* corrupted */
    CHECK(!Save_Valid(&loaded));
    loaded = sram;
    loaded.assert_AA = 0;
    CHECK(!Save_Valid(&loaded));

    /* The file: unsram at $6000, sram at $6400, 8 KB. */
    const char* path = "ff1-tests.sav";
    remove(path);
    CHECK(!Save_ReadFile(path, &loaded));             /* no file: new game */
    CHECK(Save_WriteFile(path, &unsram, &sram));
    FILE* f = fopen(path, "rb");
    static uint8_t file[SAVE_FILE_SIZE + 1];
    size_t n = f ? fread(file, 1, sizeof(file), f) : 0;
    if (f) fclose(f);
    CHECK(n == SAVE_FILE_SIZE);
    CHECK(memcmp(file + SAVE_SRAM_OFS, &sram, sizeof(sram)) == 0);
    CHECK(memcmp(file, &unsram, sizeof(unsram)) == 0);
    memset(&loaded, 0, sizeof(loaded));
    CHECK(Save_ReadFile(path, &loaded));
    CHECK(memcmp(&loaded, &sram, sizeof(sram)) == 0);
    CHECK(Party_Gold(&loaded) == 12345 && loaded.characters[2].cur_hp == 77);
    file[SAVE_SRAM_OFS + 0x150] ^= 0x10;               /* a bad byte in the saved copy */
    f = fopen(path, "wb");
    if (f) { fwrite(file, 1, SAVE_FILE_SIZE, f); fclose(f); }
    CHECK(!Save_ReadFile(path, &loaded));
    remove(path);
}

/* The APU, and the music driver writing to it. */
static int zero_crossings(const int16_t* s, int n) {
    int z = 0;
    for (int i = 1; i < n; i++) z += (s[i - 1] < 0) != (s[i] < 0);
    return z;
}

static double rms(const int16_t* s, int n) {
    double sum = 0;
    for (int i = 0; i < n; i++) sum += (double)s[i] * s[i];
    return n ? sqrt(sum / n) : 0;
}

static void test_audio(void) {
    static int16_t buf[44100];

    /* Pulse 1 at 50% duty, constant volume 15, period $0FD:
     * 1789773 / (16 * 254) = 440 Hz, about 880 zero crossings a second. */
    Apu_Reset();
    Apu_Write(0x4015, 0x01);
    Apu_Write(0x4000, 0xBF);
    Apu_Write(0x4001, 0x08);                          /* sweep off (negate bit only) */
    Apu_Write(0x4002, 0xFD);
    Apu_Write(0x4003, 0x00);
    Apu_Render(buf, 44100, 44100);
    int z = zero_crossings(buf + 4410, 44100 - 4410) * 10 / 9;
    CHECK(z > 860 && z < 900);
    CHECK(rms(buf, 44100) > 1000);

    /* Silence: no channel enabled. */
    Apu_Reset();
    Apu_Render(buf, 4410, 44100);
    CHECK(rms(buf, 4410) < 50);

    /* The prelude plays (all three channels primed in 3 frames, then sound). */
    Apu_Reset();
    g_music_track = MUSIC_PRELUDE;
    for (int f = 0; f < 3; f++) Music_Play();
    CHECK(g_music_track == 0);
    double loud = 0;
    for (int f = 0; f < 120; f++) {
        Music_Play();
        Apu_Render(buf, 735, 44100);
        loud += rms(buf, 735);
    }
    CHECK(loud / 120 > 500);

    /* The "game saved" jingle ends by itself and silences the channels. */
    Apu_Reset();
    g_music_track = MUSIC_FANFARE_SAVED;
    int f;
    for (f = 0; f < 60 * 30 && g_music_track != 0x81; f++) Music_Play();
    CHECK(g_music_track == 0x81);
    /* (the triangle only stops when its length counter runs out: up to
     * ~2 s on the NES too) */
    for (int k = 0; k < 180; k++) { Music_Play(); Apu_Render(buf, 735, 44100); }
    Apu_Render(buf, 4410, 44100);
    CHECK(rms(buf, 4410) < 300);
}

static void test_sfx(void) {
    static int16_t buf[735 * 4];

    /* The menu sounds hold square 2 from the music for their length. */
    Apu_Reset();
    Apu_Write(0x4015, 0x0F);                        /* (as the music leaves it) */
    g_sq2_sfx = 0;
    Sfx_MenuSel();
    CHECK(g_sq2_sfx == 0x1F);
    Apu_Render(buf, 735, 44100);
    CHECK(rms(buf, 735) > 500);
    Sfx_MenuMove();
    CHECK(g_sq2_sfx == 0x10);

    /* PlaySFX_Error: 16 frames of its own, then nothing. */
    Sfx_Error();
    int frames = 0;
    while (Sfx_ErrorFrame()) frames++;
    CHECK(frames == 16);
    CHECK(g_sq2_sfx == 1);
    CHECK(Sfx_ErrorFrame() == 0);

    /* The frame routines: A or B selects; only a new direction moves. */
    InputState in = { BTN_A, BTN_A, 0 };
    g_sq2_sfx = 0;
    Sfx_JoyFrame(&in, 0x0F);
    CHECK(g_sq2_sfx == 0x1F);
    InputState down = { BTN_DOWN, BTN_DOWN, 0 };
    g_sq2_sfx = 0;
    Sfx_JoyFrame(&down, 0x0F);
    CHECK(g_sq2_sfx == 0x10);
    InputState still = { BTN_DOWN, 0, 0 };          /* held since last frame */
    g_sq2_sfx = 0;
    Sfx_JoyFrame(&still, 0x0F);
    CHECK(g_sq2_sfx == 0);
    Sfx_JoyFrame(&down, 0x03);                      /* not one of its directions */
    CHECK(g_sq2_sfx == 0);

    /* The dialogue box sweeps. */
    Sfx_DialogueOpen();
    CHECK(g_sq2_sfx == 53);
    Sfx_DialogueClose();
    CHECK(g_sq2_sfx == 37);

    /* The battle transition's noise is audible, and the APU is off after. */
    Apu_Reset();
    ScreenFx fx;
    uint8_t pal[16] = { 0 };
    ScreenFx_Start(&fx, SCREENFX_BATTLE, pal);
    double loud = 0;
    int n = 0;
    while (!ScreenFx_Step(&fx)) { Apu_Render(buf, 735, 44100); loud += rms(buf, 735); n++; }
    CHECK(n > 0 && loud / n > 300);
    for (int k = 0; k < 4; k++) Apu_Render(buf + k * 735, 735, 44100);
    CHECK(rms(buf + 735 * 3, 735) < 300);

    /* Battle: the "cha" holds square 2 for its 12 frames, is heard, and
     * its last block lets the noise die away. */
    Apu_Reset();
    Sfx_BattlePlay(2);
    CHECK(g_sq2_sfx == 12);
    loud = 0;
    for (int k = 0; k < 12; k++) { Sfx_BattleUpdate(); Apu_Render(buf, 735, 44100); loud += rms(buf, 735); }
    CHECK(loud / 12 > 300);
    for (int k = 0; k < 10; k++) { Sfx_BattleUpdate(); Apu_Render(buf, 735, 44100); }
    CHECK(rms(buf, 735) < 300);
    Sfx_BattlePlay(1);
    CHECK(g_sq2_sfx == 15);
    Sfx_BattlePlay(0);
    CHECK(g_sq2_sfx == 60);
}

static void test_rom(void) {
    /* SHA-1: the standard examples */
    char hex[41];
    Rom_Sha1Hex((const uint8_t*)"", 0, hex);
    CHECK(strcmp(hex, "da39a3ee5e6b4b0d3255bfef95601890afd80709") == 0);
    Rom_Sha1Hex((const uint8_t*)"abc", 3, hex);
    CHECK(strcmp(hex, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);

    /* A file of the wrong size, and the right size with other contents */
    static uint8_t junk[ROM_SIZE + 16];
    CHECK(Rom_LoadMemory(junk, 1000) == ROM_ERR_SIZE);
    CHECK(Rom_LoadMemory(junk, ROM_SIZE + 16) == ROM_ERR_HASH);
    CHECK(Rom_LoadFile("no such file.nes") == ROM_ERR_FILE);

    /* The reference ROM, when built (tools/build_rom.py): with its header
     * and without it */
    const char* path = getenv("FF1_ROM") ? getenv("FF1_ROM") : "../FinalFantasy.nes";
    FILE* f = fopen(path, "rb");
    if (!f) { printf("(test_rom: no ROM at %s, skipped)\n", path); return; }
    size_t n = fread(junk, 1, sizeof(junk), f);
    fclose(f);
    CHECK(n == ROM_SIZE + 16);
    CHECK(Rom_LoadMemory(junk, n) == ROM_OK);
    CHECK(Rom_LoadMemory(junk + 16, ROM_SIZE) == ROM_OK);
    CHECK(Rom_Data() != NULL && Rom_Bank(1, 0)[0] == junk[16 + ROM_BANK_SIZE]);
}

int main(void) {
    /* The game's data comes from a ROM: $FF1_ROM, or the reference one
     * tools/build_rom.py writes beside the project. */
    static const char* const roms[] = { "../FinalFantasy.nes", "../../FinalFantasy.nes", NULL };
    RomStatus st = RomData_Find(NULL, roms);
    if (st != ROM_OK) {
        printf("No ROM of Final Fantasy (USA): set FF1_ROM. Only the ROM checks run.\n");
        test_rom();
        printf("%d checks, %d failed\n", g_checks, g_failures);
        return g_failures ? 1 : 0;
    }
    test_print_number();
    test_text();
    test_rng();
    test_gold();
    test_screen_fx();
    test_save_layout();
    test_save_game();
    test_audio();
    test_sfx();
    test_rom();
    test_battle_rules();
    test_equip_stats();
    test_magic();
    test_level_up();
    test_resort();

    printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
