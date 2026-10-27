#pragma once

/*
 * romdata.h -- the game's tables, filled from the player's ROM.
 *
 * The tables (declared in the headers in data/generated, their storage in
 * romdata_storage.c) are empty until RomData_Load copies them out of the
 * ROM loaded by rom.c: most are one run of the ROM (rom_layout.h), the rest
 * are put together as tools/extract_data.py describes them.
 */

/** Fill every table from the loaded ROM (Rom_Data() must be set).
 *  @return 0 on success. */
int RomData_Load(void);

#include "rom.h"

/** Load the ROM at `path`, check it and fill the tables: Rom_LoadFile +
 *  RomData_Load.  @return ROM_OK, or why not (Rom_StatusText). */
RomStatus RomData_Open(const char* path);

/** RomData_Open on the first of these that is a ROM of ours: `path` (may be
 *  NULL), $FF1_ROM, then each of `fallbacks` (NULL-terminated).  @return
 *  the status of the last one tried, ROM_OK if one worked. */
RomStatus RomData_Find(const char* path, const char* const* fallbacks);
