/*
 * Encounter Data
 * Source: bin/0B_8C00_mapencounterrates.bin (bank_0B $8C00, lut_BattleRates)
 * Generated: 2026-10-27
 * Random encounter rates (X/256 per step).
 */

#pragma once

#include <stdint.h>

/* lut_BattleRates: entry 0 is the overworld (unused: its rate is
 * hard-coded), entry map+1 is standard map `map`. */
extern const uint8_t lut_BattleRates[64];

