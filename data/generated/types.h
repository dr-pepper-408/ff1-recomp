/*
 * Types and Save Structure
 * Source: variables.inc
 * Generated: 2026-10-27
 * C structs matching the FF1 save RAM layout byte for byte.
 */

#pragma once

#include <stdint.h>

#include <stddef.h>

/*
 * The structs below mirror the NES save RAM byte for byte (unsram $6000-$63FF,
 * copied to battery SRAM $6400-$67FF on save), so a SaveData can be read from
 * or written to an emulator .sav file directly.  Offsets are checked at
 * compile time against variables.inc.  Multi-byte fields are little-endian,
 * as on the NES (the host must be little-endian too: x86, ARM).
 */

#define CH_STATS_SIZE 0x40
#define MAX_PARTY_SIZE 4

// ch_stats (unsram + $100): $40 bytes per character.
typedef struct {
    uint8_t  class;          // +$00 ch_class
    uint8_t  ailments;       // +$01 ch_ailments (out-of-battle ailment id)
    uint8_t  name[4];        // +$02 ch_name: 4 font tiles (not NUL-terminated)
    uint8_t  _pad_06;
    uint8_t  exp[3];         // +$07 ch_exp
    uint16_t cur_hp;         // +$0A ch_curhp
    uint16_t max_hp;         // +$0C ch_maxhp
    uint8_t  _pad_0E[2];
    uint8_t  str;            // +$10 ch_str
    uint8_t  agil;           // +$11 ch_agil
    uint8_t  intel;          // +$12 ch_int
    uint8_t  vit;            // +$13 ch_vit
    uint8_t  luck;           // +$14 ch_luck
    uint8_t  _pad_15;
    uint16_t exp_to_next;    // +$16 ch_exptonext (display only)
    uint8_t  weapons[4];     // +$18 ch_weapons (0 = none, bit 7 = equipped)
    uint8_t  armor[4];       // +$1C ch_armor   (0 = none, bit 7 = equipped)
    uint8_t  damage;         // +$20 ch_dmg
    uint8_t  hit_rate;       // +$21 ch_hitrate
    uint8_t  absorb;         // +$22 ch_absorb
    uint8_t  evade;          // +$23 ch_evade
    uint8_t  elem_resist;    // +$24 ch_resist
    uint8_t  mag_def;        // +$25 ch_magdef
    uint8_t  level;          // +$26 ch_level (0-based out of battle)
    uint8_t  _pad_27[0x19];
} CharacterStats;

// ch_magicdata (unsram + $300): $40 bytes per character.
#define SPELL_SLOTS 0x20
#define MP_LEVELS 8

typedef struct {
    uint8_t spells[SPELL_SLOTS];  // +$00 ch_spells: 8 levels x 4 slots (4th unused)
    uint8_t cur_mp[MP_LEVELS];    // +$20 ch_curmp
    uint8_t max_mp[MP_LEVELS];    // +$28 ch_maxmp
    uint8_t _pad_30[0x10];
} CharacterMagic;

#define ITEMS_SIZE 0x30          // items[] region (unsram + $20)
#define GAME_FLAGS_SIZE 0x100

// The full $400-byte save block.
typedef struct {
    // ---- page 0: overworld state, gold, items ----
    uint8_t  ship_vis;           // +$00
    uint8_t  ship_x;             // +$01
    uint8_t  ship_y;             // +$02
    uint8_t  _pad_03;
    uint8_t  airship_vis;        // +$04
    uint8_t  airship_x;          // +$05
    uint8_t  airship_y;          // +$06
    uint8_t  _pad_07;
    uint8_t  bridge_vis;         // +$08
    uint8_t  bridge_x;           // +$09
    uint8_t  bridge_y;           // +$0A
    uint8_t  _pad_0B;
    uint8_t  canal_vis;          // +$0C
    uint8_t  canal_x;            // +$0D
    uint8_t  canal_y;            // +$0E
    uint8_t  _pad_0F;
    uint8_t  ow_scroll_x;        // +$10 unsram_ow_scroll_x
    uint8_t  ow_scroll_y;        // +$11 unsram_ow_scroll_y
    uint8_t  has_canoe;          // +$12
    uint8_t  _pad_13;
    uint8_t  vehicle;            // +$14 unsram_vehicle
    uint8_t  _pad_15;
    uint8_t  bridgescene;        // +$16
    uint8_t  _pad_17[5];
    uint8_t  gold[3];            // +$1C (little-endian)
    uint8_t  _pad_1F;
    uint8_t  items[ITEMS_SIZE];  // +$20 key items, orbs, consumable counts
    uint8_t  _pad_50[0xAD];
    uint8_t  checksum;           // +$FD sram_checksum
    uint8_t  assert_55;          // +$FE sram_assert_55
    uint8_t  assert_AA;          // +$FF sram_assert_AA
    // ---- pages 1-3 ----
    CharacterStats characters[MAX_PARTY_SIZE];       // +$100 ch_stats
    uint8_t  game_flags[GAME_FLAGS_SIZE];            // +$200 game_flags
    CharacterMagic character_magic[MAX_PARTY_SIZE];  // +$300 ch_magicdata
} SaveData;

#define TYPES_CHECK(t, f, off) _Static_assert(offsetof(t, f) == (off), #t "." #f " offset")
TYPES_CHECK(CharacterStats, exp,         0x07);
TYPES_CHECK(CharacterStats, cur_hp,      0x0A);
TYPES_CHECK(CharacterStats, max_hp,      0x0C);
TYPES_CHECK(CharacterStats, str,         0x10);
TYPES_CHECK(CharacterStats, exp_to_next, 0x16);
TYPES_CHECK(CharacterStats, weapons,     0x18);
TYPES_CHECK(CharacterStats, armor,       0x1C);
TYPES_CHECK(CharacterStats, damage,      0x20);
TYPES_CHECK(CharacterStats, level,       0x26);
TYPES_CHECK(CharacterMagic, cur_mp,      0x20);
TYPES_CHECK(CharacterMagic, max_mp,      0x28);
TYPES_CHECK(SaveData, gold,              0x1C);
TYPES_CHECK(SaveData, items,             0x20);
TYPES_CHECK(SaveData, checksum,          0xFD);
TYPES_CHECK(SaveData, characters,        0x100);
TYPES_CHECK(SaveData, game_flags,        0x200);
TYPES_CHECK(SaveData, character_magic,   0x300);
_Static_assert(sizeof(CharacterStats) == 0x40, "CharacterStats size");
_Static_assert(sizeof(CharacterMagic) == 0x40, "CharacterMagic size");
_Static_assert(sizeof(SaveData) == 0x400, "SaveData size");
#undef TYPES_CHECK
