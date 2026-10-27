/**
 * @file rom.c
 * @brief Loading and checking the player's ROM (rom.h).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rom.h"

static uint8_t* g_rom;

/* ---- SHA-1 (FIPS 180-1) ---- */

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void sha1_block(uint32_t h[5], const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void Rom_Sha1Hex(const uint8_t* data, size_t size, char out[41]) {
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    size_t i = 0;
    for (; i + 64 <= size; i += 64) sha1_block(h, data + i);
    uint8_t tail[128] = { 0 };
    size_t rest = size - i;
    memcpy(tail, data + i, rest);
    tail[rest] = 0x80;
    size_t blocks = rest + 9 > 64 ? 2 : 1;
    uint64_t bits = (uint64_t)size * 8;
    for (int k = 0; k < 8; k++) tail[blocks * 64 - 1 - k] = (uint8_t)(bits >> (8 * k));
    for (size_t b = 0; b < blocks; b++) sha1_block(h, tail + b * 64);
    for (int k = 0; k < 5; k++) sprintf(out + k * 8, "%08x", (unsigned)h[k]);
}

/* ---- Loading ---- */

RomStatus Rom_LoadMemory(const uint8_t* data, size_t size) {
    if (size == ROM_SIZE + 16) { data += 16; size -= 16; }      /* (an iNES header) */
    if (size != ROM_SIZE) return ROM_ERR_SIZE;
    char hex[41];
    Rom_Sha1Hex(data, size, hex);
    if (strcmp(hex, ROM_SHA1) != 0) return ROM_ERR_HASH;
    if (!g_rom) g_rom = malloc(ROM_SIZE);
    if (!g_rom) return ROM_ERR_FILE;
    memcpy(g_rom, data, ROM_SIZE);
    return ROM_OK;
}

RomStatus Rom_LoadFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return ROM_ERR_FILE;
    uint8_t* buf = malloc(ROM_SIZE + 16 + 1);
    size_t n = buf ? fread(buf, 1, ROM_SIZE + 16 + 1, f) : 0;
    fclose(f);
    RomStatus s = buf ? Rom_LoadMemory(buf, n) : ROM_ERR_FILE;
    free(buf);
    return s;
}

const uint8_t* Rom_Data(void) { return g_rom; }

const uint8_t* Rom_Bank(int bank, int offset) {
    return g_rom ? g_rom + bank * ROM_BANK_SIZE + offset : NULL;
}

const char* Rom_StatusText(RomStatus s) {
    switch (s) {
    case ROM_OK:       return "OK";
    case ROM_ERR_FILE: return "The file can't be read.";
    case ROM_ERR_SIZE: return "This isn't a Final Fantasy (NES) ROM: its size is wrong.";
    case ROM_ERR_HASH: return "This isn't Final Fantasy (USA): another version, or a modified copy.";
    }
    return "?";
}
