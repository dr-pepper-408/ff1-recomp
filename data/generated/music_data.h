/*
 * Music
 * Source: bin/0D_8000_scoredata.bin, bank_0D.asm
 * Generated: 2026-10-27
 * Score data and the music driver's tables.
 */

#pragma once

#include <stdint.h>

/* bank_0D lut_ScoreData ($8000): per song 4 channel pointers (sq1, sq2, tri,
 * unused), then the scores; pointers and loop targets are bank addresses. */
#define MUSIC_SCORE_BASE 0x8000
extern uint8_t lut_ScoreData[7584];

/* bank_0D $B2F9-$B5C8, as the ROM has them one after another (the driver
 * reads them through pointers, and the unused octaves $DC-$DF run past
 * lut_NoteFreqs into lut_NoteLengths). */
#define MUSIC_TBL_NOTEFREQS   0x000   /* 4 octaves x 12 notes, 2 bytes each */
#define MUSIC_TBL_NOTELENGTHS 0x060   /* 6 tempos x 16 lengths */
#define MUSIC_TBL_ENVSPEEDS   0x0C0   /* 16 envelope speeds */
#define MUSIC_TBL_ENVPATTERNS 0x0D0   /* 16 patterns x 32 volumes */
extern uint8_t music_tables[720];

/* bank_0F PrepOverworld @lut_VehicleMusic: the overworld's track by vehicle */
extern uint8_t lut_VehicleMusic[9];

/* bank_0F PrepStandardMap @lut_TilesetMusicTrack: a standard map's track by tileset */
extern uint8_t lut_TilesetMusicTrack[8];

/* bank_0D lut_OctaveOffset: byte offset of each octave in lut_NoteFreqs */
extern uint8_t lut_OctaveOffset[6];
