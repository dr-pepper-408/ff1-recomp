/*
 * Shop Data
 * Source: bin/0E_8300_shopdata.bin, bin/0E_8000_shopstrings.bin, bank_0E/0F/00 tables
 * Generated: 2026-10-27
 * Shop inventories (5 bytes, $00-terminated; inn/clinic: 2-byte price),
 * shop types, screen layout, magic permissions, palettes, stock text.
 */

#pragma once

#include <stdint.h>

#define SHOP_COUNT 71

/* lut_ShopData: 5 inventory bytes per shop id (LoadShopInventory). */
extern const uint8_t shop_inventory[SHOP_COUNT][5];

/* bank_0F.asm lut_ShopTypes: shop type per shop id (0 weapon .. 7 caravan) */
extern const uint8_t lut_ShopTypes[71];

/* bank_0E.asm lut_ShopkeepAdditive: shopkeeper tile offset per shop type */
extern const uint8_t lut_ShopkeepAdditive[8];

/* bank_0E.asm lut_ShopAttributes: the shop screen's attribute table */
extern const uint8_t lut_ShopAttributes[64];

/* bank_0E.asm lut_ShopkeepImage: 10x10 shopkeeper image (0 = blank) */
extern const uint8_t lut_ShopkeepImage[100];

/* bank_0E.asm lut_ShopBox_X: boxes: 0 dialogue, 1 title, 2 inventory, 3 command, 4 gold */
extern const uint8_t lut_ShopBox_X[5];

extern const uint8_t lut_ShopBox_Y[5];

extern const uint8_t lut_ShopBox_Wd[5];

extern const uint8_t lut_ShopBox_Ht[5];

/* bank_0E.asm lut_ShopCurs_Cmd: cursor x,y in the command box */
extern const uint8_t lut_ShopCurs_Cmd[8];

/* bank_0E.asm lut_ShopCurs_List: cursor x,y in the inventory box */
extern const uint8_t lut_ShopCurs_List[10];

/* bank_0F.asm lut_ShopPalettes: title and gold box palettes */
extern const uint8_t lut_ShopPalettes[8];

/* bank_0E magic permissions, per class and spell level: bit 7 = the
 * level's first spell; a set bit means the class can't learn it. */
extern const uint8_t lut_MagicPermis[12][8];

/* bank_00 lut_BackdropPal $40+: the shopkeeper's palette per shop type. */
extern const uint8_t shopkeep_palettes[8][4];

/* lut_ShopStrings (bank_0E $8000): titles 0-7, " G", dialogue */
#define SHOP_TEXT_COUNT 38

extern const uint16_t shop_text_offsets[SHOP_TEXT_COUNT];

extern const uint8_t shop_text_bytes[660];

