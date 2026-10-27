/**
 * @file save.c
 * @brief SaveGame (bank_0E), VerifyChecksum and GameStart's Continue
 *        (bank_0F), and the 8 KB SRAM file.
 */

#include <stdio.h>
#include <string.h>
#include "save.h"

_Static_assert(sizeof(SaveData) == 0x400, "SaveData must be the $400-byte unsram block");

uint8_t Save_Sum(const SaveData* block) {
    /* ADC $6400,X / $6500,X / $6600,X / $6700,X for X = 0..$FF, carry
     * carried from each addition into the next. */
    const uint8_t* b = (const uint8_t*)block;
    unsigned a = 0, c = 0;
    for (int x = 0; x < 0x100; x++)
        for (int page = 0; page < 4; page++) {
            a += b[page * 0x100 + x] + c;
            c = a >> 8;
            a &= 0xFF;
        }
    return (uint8_t)a;
}

void Save_Game(SaveData* unsram, uint8_t ow_scroll_x, uint8_t ow_scroll_y,
               uint8_t vehicle, SaveData* sram) {
    unsram->ow_scroll_x = ow_scroll_x;
    unsram->ow_scroll_y = ow_scroll_y;
    unsram->vehicle = vehicle;
    *sram = *unsram;
    sram->assert_55 = 0x55;
    sram->assert_AA = 0xAA;
    sram->checksum = 0;
    sram->checksum = (uint8_t)(Save_Sum(sram) ^ 0xFF);   /* so the sum comes to $FF */
}

int Save_Valid(const SaveData* sram) {
    return sram->assert_55 == 0x55 && sram->assert_AA == 0xAA && Save_Sum(sram) == 0xFF;
}

int Save_WriteFile(const char* path, const SaveData* unsram, const SaveData* sram) {
    static uint8_t file[SAVE_FILE_SIZE];
    /* Keep whatever else the file holds ($6800-$7FFF is unused by the game,
     * but an emulator's file may have it). */
    memset(file, 0, sizeof(file));
    FILE* f = fopen(path, "rb");
    if (f) {
        size_t n = fread(file, 1, sizeof(file), f);
        (void)n;
        fclose(f);
    }
    memcpy(file, unsram, sizeof(SaveData));
    memcpy(file + SAVE_SRAM_OFS, sram, sizeof(SaveData));
    f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(file, 1, sizeof(file), f) == sizeof(file);
    return (fclose(f) == 0) && ok;
}

int Save_ReadFile(const char* path, SaveData* out) {
    static uint8_t file[SAVE_FILE_SIZE];
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(file, 1, sizeof(file), f);
    fclose(f);
    if (n < SAVE_SRAM_OFS + sizeof(SaveData)) return 0;
    SaveData sram;
    memcpy(&sram, file + SAVE_SRAM_OFS, sizeof(sram));
    if (!Save_Valid(&sram)) return 0;
    *out = sram;
    return 1;
}
