/*
 * Spell Names
 * Source: bank_0A.dat (bank $0A, ROM $B700 item-name table IDs $B0-$EF)
 * Generated: 2026-10-27
 * DTE-decoded spell names, indexed by magic_table spell id 0..63.
 */

#pragma once

#include <stdint.h>

#define SPELL_NAME_COUNT 64

extern const char *const spell_names[SPELL_NAME_COUNT];
