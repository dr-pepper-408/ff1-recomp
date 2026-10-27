/*
 * Story Screens
 * Source: bin/0B_A800_endingbridge_chrnt.bin, bin/0D_A800_storytext.bin, bin/0D_BB00_credittext.bin, bank_0D.asm
 * Generated: 2026-10-27
 * Bridge scene (and ending) data: nametable, text, palette.
 */

#pragma once

#include <stdint.h>

/* bank_0B data_BridgeNT ($BC00): the bridge picture, a full nametable
 * (960 tiles + 64 attribute bytes).  Its CHR is chr_bridge (chr_data.h). */
extern uint8_t data_BridgeNT[1024];

/* bank_0D.asm lut_BridgeBGPal: the bridge scene's BG palettes */
extern uint8_t lut_BridgeBGPal[16];

/* bank_0D.asm lut_StorySecondaryAttrib: top half of the $2400 nametable's attributes (the closed shutters: the box in palette 0) */
extern uint8_t lut_StorySecondaryAttrib[32];

/* bank_0D.asm lut_Bridge_LastPage: bridge scene story pages: 0 to this - 1 */
extern uint8_t lut_Bridge_LastPage[1];

/* bank_0D.asm lut_Ending_LastPage: ending scene story pages: lut_Bridge_LastPage to this - 1 */
extern uint8_t lut_Ending_LastPage[1];

/* bank_0D lut_StoryText ($A800): one complex string per page, as ROM bytes */
extern uint8_t story_text_bytes[1536];

extern uint16_t story_text_offsets[25];

/* bank_0D lut_CreditsText ($BB00): 4 pages of (PPU address, tiles, $01 or $00) */
extern uint8_t credits_text_bytes[142];

extern uint16_t credits_text_offsets[4];

/* bank_0D lut_MinigameCHR: 16 puzzle pieces of 2x2 tiles, 1bpp (8 bytes a tile) */
extern uint8_t lut_MinigameCHR[512];

/* bank_0D.asm lut_PuzzleStart: the pieces before the shuffle */
extern int8_t lut_PuzzleStart[16];

/* bank_0D.asm lut_MGDirection_Y: slide y per frame: right, left, down, up */
extern int8_t lut_MGDirection_Y[4];

/* bank_0D.asm lut_MGDirection_X: slide x per frame */
extern int8_t lut_MGDirection_X[4];

/* bank_0B data_EpilogueCHR ($A800, 8 rows) and data_EpilogueNT ($B000) */
extern uint8_t data_EpilogueCHR[2048];

extern uint8_t data_EpilogueNT[1024];

/* bank_0D.asm lut_EndingBGPal: the ending's BG palettes */
extern uint8_t lut_EndingBGPal[16];

/* bank_0D data_TheEndDrawData: the outline's moves, a byte a pixel (bit 0 right,
 * 1 down, 2 left, 3 up), $00-terminated */
extern uint8_t data_TheEndDrawData[1024];

/* bank_0D lut_TheEndPixelPosition_Ylo/_Yhi/_X ($50 each) and lut_TheEndPixelMasks:
 * the CHR address of pixel (x, y) is X[x] + Ylo[y] + Yhi[y] * 256 */
extern uint8_t lut_TheEndPixelPosition[248];

/* bank_0D.asm lut_TheEnd_AttrTable: attribute bytes written at $23CA */
extern uint8_t lut_TheEnd_AttrTable[19];

/* bank_0D.asm @lut_FillGradient: rows the fill may fill */
extern uint8_t lut_FillGradient[80];

