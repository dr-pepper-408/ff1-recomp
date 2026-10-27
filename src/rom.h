#pragma once
#include <stddef.h>
#include <stdint.h>

/*
 * rom.h -- the player's copy of the game.
 *
 * The port ships without the game's data: it reads it from a ROM of Final
 * Fantasy (USA) the player supplies.  A ROM file is accepted with or
 * without its 16-byte iNES header (dumps differ in their headers); what is
 * checked is the 256 KB after it: 16 banks of 16 KB.
 */

#define ROM_BANK_SIZE  0x4000
#define ROM_BANKS      16
#define ROM_SIZE       (ROM_BANK_SIZE * ROM_BANKS)

/* The SHA-1 of the 256 KB of Final Fantasy (USA). */
#define ROM_SHA1 "c9cfbf5455085e198dce039298b083cd6fc88bce"

typedef enum {
    ROM_OK = 0,
    ROM_ERR_FILE,       ///< can't be read
    ROM_ERR_SIZE,       ///< not 256 KB (with or without a 16-byte header)
    ROM_ERR_HASH,       ///< the right size, but another game or version
} RomStatus;

/** Check a ROM image in memory and keep it.  @return ROM_OK if it's ours. */
RomStatus Rom_LoadMemory(const uint8_t* data, size_t size);

/** Rom_LoadMemory from a file. */
RomStatus Rom_LoadFile(const char* path);

/** The loaded ROM's 256 KB (no header), or NULL. */
const uint8_t* Rom_Data(void);

/** A byte span in bank `bank` at offset `offset` within it. */
const uint8_t* Rom_Bank(int bank, int offset);

/** A message for a status, for the player. */
const char* Rom_StatusText(RomStatus s);

/** SHA-1 of `size` bytes as 40 hex digits (and a NUL) into `out`. */
void Rom_Sha1Hex(const uint8_t* data, size_t size, char out[41]);
