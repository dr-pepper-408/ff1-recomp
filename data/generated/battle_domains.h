/*
 * Battle Domains
 * Source: bin/0B_8000_battledomains.bin (bank $0B, ROM $8000)
 * Generated: 2026-10-27
 * 256 entries × 2 bytes each, pointers to battle formations.
 */

#pragma once

#include <stdint.h>

#define BATTLE_DOMAIN_COUNT 512

extern uint16_t battle_domain_table[BATTLE_DOMAIN_COUNT];
