/*
 * Enemy AI Data
 * Source: bin/0C_9020_aidata.bin (bank $0C, ROM $9020)
 * Generated: 2026-10-27
 * Fixed 16-byte entries (lut_EnemyAi), indexed by enemy_data.ai_script. See bank_0C.asm Enemy_DoAi.
 */

#pragma once

#include <stdint.h>

#define ENEMY_AI_COUNT 44
#define ENEMY_AI_NO_SCRIPT 0xFF

typedef struct {
    uint8_t magic_rate;   // +0:  chance to cast magic ($00-$80)
    uint8_t special_rate; // +1:  chance to use special attack ($00-$80)
    uint8_t spells[8];    // +2..9: magic spell ids (0-based, $FF = end)
    uint8_t _pad_A;       // +10:  unused
    uint8_t special[4];   // +11..14: special attack ids (0-based, $FF = end)
    uint8_t _pad_F;       // +15:  unused
} EnemyAi;

extern EnemyAi enemy_ai_table[ENEMY_AI_COUNT];
