/*
 * Enemy Data
 * Source: bin/0C_8520_enemydata.bin (bank $0C, ROM $8520)
 * Generated: 2026-10-27
 * 20 bytes per enemy. See Constants.inc ENROMSTAT_* for layout.
 */

#pragma once

#include <stdint.h>

#define ENEMY_COUNT 128

typedef struct {
    uint16_t exp_reward;     // +0x00: Experience reward
    uint16_t gp_reward;      // +0x02: GP reward
    uint16_t max_hp;         // +0x04: Max HP
    uint8_t  morale;         // +0x06: Morale
    uint8_t  ai_script;      // +0x07: AI script index
    uint8_t  evade;          // +0x08: Evade/Agility
    uint8_t  absorb;         // +0x09: Absorb (Defense)
    uint8_t  num_hits;       // +0x0A: Number of hits
    uint8_t  hit_rate;       // +0x0B: Hit rate
    uint8_t  damage;         // +0x0C: Damage (Strength)
    uint8_t  crit_rate;      // +0x0D: Critical hit rate
    uint8_t  unknown_E;      // +0x0E: Unknown
    uint8_t  attack_ailment; // +0x0F: Attack ailment inflicted
    uint8_t  category;       // +0x10: Category bitmask
    uint8_t  mag_def;        // +0x11: Magic defense
    uint8_t  elem_weakness;  // +0x12: Elemental weakness bitmask
    uint8_t  elem_resist;    // +0x13: Elemental resist bitmask
} EnemyData;

extern EnemyData enemy_table[ENEMY_COUNT];
