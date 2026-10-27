/*
 * Class Starting Stats
 * Source: bank_00.dat (lut_ClassStartingStats, ROM $B040)
 * Generated: 2026-10-27
 * New-game stats of the 6 starting classes (NewGame_LoadStartingStats).
 */

#pragma once

#include <stdint.h>

#define CLASS_STARTING_STATS_COUNT 6

typedef struct {
    uint8_t class_id;   // +0: unused copy of the class id
    uint8_t hp;         // +1: starting (and max) HP
    uint8_t str, agil, intel, vit, luck;   // +2..+6
    uint8_t damage;     // +7: ch_dmg
    uint8_t hit_rate;   // +8: ch_hitrate
    uint8_t evade;      // +9: ch_evade
    uint8_t mag_def;    // +A: ch_magdef
} ClassStartingStats;

extern ClassStartingStats class_starting_stats[CLASS_STARTING_STATS_COUNT];
