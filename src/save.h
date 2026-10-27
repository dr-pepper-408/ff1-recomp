#pragma once
#include <stdint.h>
#include "../data/generated/types.h"

/*
 * save.h -- the battery-backed save (SaveGame, VerifyChecksum, GameStart's
 * Continue).  The file is the cartridge's 8 KB SRAM ($6000-$7FFF), as an
 * emulator's .sav: $6000 holds unsram (the game's working copy) and $6400
 * sram (the saved copy, guarded by $55/$AA and a checksum).  Only the sram
 * copy is read back.
 */

#define SAVE_FILE_SIZE  0x2000   ///< $6000-$7FFF
#define SAVE_SRAM_OFS   0x0400   ///< sram ($6400) in the file

/** The ADC-with-carry sum of the $400-byte block that VerifyChecksum and
 *  SaveGame compute (carry kept between additions, the last one lost). */
uint8_t Save_Sum(const SaveData* block);

/** SaveGame: record the overworld scroll and vehicle in `unsram`, then copy
 *  it to `sram` with the assertion bytes and the checksum. */
void Save_Game(SaveData* unsram, uint8_t ow_scroll_x, uint8_t ow_scroll_y,
               uint8_t vehicle, SaveData* sram);

/** GameStart's checks: $55/$AA assertion bytes, then VerifyChecksum.
 *  @return 1 if `sram` holds a valid save. */
int Save_Valid(const SaveData* sram);

/** Write the 8 KB SRAM file.  @return 1 on success. */
int Save_WriteFile(const char* path, const SaveData* unsram, const SaveData* sram);

/** Read the sram copy out of an 8 KB SRAM file and validate it.
 *  @return 1 if `out` now holds a valid save. */
int Save_ReadFile(const char* path, SaveData* out);
