/*
 * RNG Tables
 * Source: bin/0F_F100_rngtable.bin, bin/0F_FCF1_rngtable.bin, bank_0F.asm
 * Generated: 2026-10-27
 * Scramble tables used by FF1's random number generators.
 */

#pragma once

#include <stdint.h>

#define RNG_TABLE_SIZE 256

/* lut_RNG ($F100): BattleStepRNG and GetBattleFormation. */
extern const uint8_t rng_table[256];

/* BattleRNG's own scramble table ($FCF1): all in-battle randomness. */
extern const uint8_t battle_rng_table[256];

/* lut_FormationWeight ($C58C): (random & $3F) -> formation slot 0-7
 * within a domain (12,12,12,12,6,6,3,1 out of 64). */
extern const uint8_t lut_FormationWeight[64];

