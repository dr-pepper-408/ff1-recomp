/*
 * Item Data
 * Source: bank_0A.dat (bank $0A, ROM $B700 for names) / bank_0D for prices
 * Generated: 2026-10-27
 * DTE-decoded item names and prices.
 */

#pragma once

#include <stdint.h>

#define ITEM_COUNT 112

typedef struct {
    const char *name;
    uint16_t price;
} ItemData;


