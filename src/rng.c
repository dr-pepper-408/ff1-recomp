/**
 * @file rng.c
 * @brief FF1's random number generators (ports of BattleStepRNG,
 *        GetBattleFormation's roll, BattleRNG and RandAX).
 *
 * The state starts at zero.  On the NES these bytes hold whatever RAM
 * contained at power-on; any fixed value is as valid.
 */

#include "rng.h"
#include "../data/generated/rng_table.h"

static RngState g_rng;

void Rng_SetState(const RngState* st) { g_rng = *st; }

void Rng_GetState(RngState* out) { *out = g_rng; }

/* Port of BattleStepRNG (bank_0F $C571). */
uint8_t Rng_BattleStep(void) {
    if (g_rng.battlestep_sign & 0x80) {
        g_rng.battlestep--;
    } else {
        g_rng.battlestep++;
    }
    if (g_rng.battlestep == 0) {
        /* A full cycle expired: add $A0 to the sign, which may flip the
         * direction of the next cycle. */
        g_rng.battlestep_sign = (uint8_t)(g_rng.battlestep_sign + 0xA0);
    }
    return rng_table[g_rng.battlestep];
}

/* GetBattleFormation (bank_0F $C54A): INC battlecounter / LDA lut_RNG,X. */
uint8_t Rng_FormationRoll(void) {
    g_rng.battlecounter++;
    return rng_table[g_rng.battlecounter];
}

/* Port of BattleRNG (bank_0F $FCE7). */
uint8_t Rng_Battle(void) {
    return battle_rng_table[g_rng.btl_rngstate++];
}

/* Port of RandAX (bank_0B $9F1D): lo + (BattleRNG() * range) / 256, where
 * range = hi + 1 - lo in 8 bits (so [0,255] gives range 0 -> always lo). */
uint8_t Rng_RandAX(uint8_t lo, uint8_t hi) {
    uint8_t range = (uint8_t)((uint8_t)(hi + 1) - lo);
    uint16_t product = (uint16_t)(Rng_Battle() * range);   /* MultiplyXA */
    return (uint8_t)((product >> 8) + lo);
}
