/*
 * Game Flags Initial State
 * Source: bank_00.dat (bank $00, ROM $AF00 / $B000)
 * Generated: 2026-10-27
 * Initial values for game flags and first page of unsram (save RAM).
 */

#pragma once

#include <stdint.h>

#define GAME_FLAGS_SIZE 256
extern const uint8_t init_game_flags[];

#define UNSRAM_FIRST_PAGE_SIZE 256
extern const uint8_t init_unsram_first_page[];
