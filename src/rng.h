#pragma once
#include <stdint.h>

/*
 * rng.h — FF1's random number generators.
 *
 * FF1 has three independent generators, each a counter stepping through a
 * 256-byte scramble table.  They are ported separately because they advance
 * at different times; sharing one sequence would change every outcome.
 *
 *   Rng_BattleStep()    BattleStepRNG (bank_0F): encounter checks while walking.
 *   Rng_FormationRoll() GetBattleFormation (bank_0F): picks the formation slot.
 *   Rng_Battle()        BattleRNG (bank_0F): all in-battle randomness.
 *   Rng_RandAX()        RandAX (bank_0B/0C): a value in [lo, hi] via BattleRNG.
 *
 * No SDL dependency.
 */

/** Generator state (the NES RAM bytes the routines use). */
typedef struct {
    uint8_t battlestep;       ///< $F5: BattleStepRNG position.
    uint8_t battlestep_sign;  ///< $F6: bit 7 set = step backwards through the table.
    uint8_t battlecounter;    ///< $F7: GetBattleFormation position.
    uint8_t btl_rngstate;     ///< $688A: BattleRNG position.
} RngState;

/** Replace the generator state (e.g. a fixed seed for tests). */
void Rng_SetState(const RngState* st);

/** Copy out the current generator state. */
void Rng_GetState(RngState* out);

/** BattleStepRNG: step battlestep (direction flips every full cycle when
 *  battlestep_sign overflows) and return lut_RNG[battlestep]. */
uint8_t Rng_BattleStep(void);

/** GetBattleFormation's roll: ++battlecounter, return lut_RNG[battlecounter]. */
uint8_t Rng_FormationRoll(void);

/** BattleRNG: return the BattleRNG table at btl_rngstate, then increment it. */
uint8_t Rng_Battle(void);

/** RandAX: a random value in [lo, hi] (8-bit arithmetic, as on the NES). */
uint8_t Rng_RandAX(uint8_t lo, uint8_t hi);
