/*
 * Map Object Data
 * Source: bank_00.dat ($B400 lut_MapObjects, $AE00 lut_MapObjGfx), bin/0E_95D5_objectdata.bin, bank_0E.asm lut_MapObjTalkJumpTbl
 * Generated: 2026-10-27
 * Townspeople and other standard-map objects.
 */

#pragma once

#include <stdint.h>

#define MAPOBJ_ID_COUNT 0xD0
#define MAPOBJ_PER_MAP 15

/* lut_MapObjects: per map, 15 objects x 3 bytes (id, x | flags, y) + 3 pad.
 * Flags in the x byte: $80 = in a room, $40 = stationary. */
#define MAPOBJ_MAP_COUNT 64
extern const uint8_t lut_MapObjects[3072];

/* lut_MapObjGfx: graphic id per object (CHR at bank_02 $A200 + gfx*$100). */
extern const uint8_t lut_MapObjGfx[208];

/* lut_MapObjTalkData: 4 bytes per object, used by its talk routine. */
extern const uint8_t lut_MapObjTalkData[832];

/* Talk routines (bank_0E TalkRoutines), in lut_MapObjTalkJumpTbl order. */
typedef enum {
    TALKFN_NONE,
    TALKFN_KINGCONERIA,
    TALKFN_GARLAND,
    TALKFN_PRINCESS1,
    TALKFN_BIKKE,
    TALKFN_ELFDOC,
    TALKFN_ELFPRINCE,
    TALKFN_ASTOS,
    TALKFN_NERRICK,
    TALKFN_SMITH,
    TALKFN_MATOYA,
    TALKFN_UNNE,
    TALKFN_VAMPIRE,
    TALKFN_SARDA,
    TALKFN_BAHAMUT,
    TALKFN_IFVIS,
    TALKFN_SUBENG,
    TALKFN_CUBEBOT,
    TALKFN_PRINCESS2,
    TALKFN_FAIRY,
    TALKFN_TITAN,
    TALKFN_CANOESAGE,
    TALKFN_NORM,
    TALKFN_REPLACE,
    TALKFN_FIGHT,
    TALKFN_UNUSED,
    TALKFN_IFITEM,
    TALKFN_INVIS,
    TALKFN_IFBRIDGE,
    TALKFN_IFEVENT,
    TALKFN_GOBRIDGE,
    TALKFN_4ORB,
    TALKFN_IFCANOE,
    TALKFN_IFCANAL,
    TALKFN_IFKEYTNT,
    TALKFN_IFEARTHVAMP,
    TALKFN_IFAIRSHIP,
    TALKFN_IFEARTHFIRE,
    TALKFN_COOGUY,
    TALKFN_CUBEBOTBAD,
    TALKFN_CHIME,
    TALKFN_BLACKORB,
    TALKFN_COUNT
} TalkFn;

/* lut_MapObjTalkJumpTbl: the talk routine for each object id. */
extern const uint8_t lut_MapObjTalkFn[MAPOBJ_ID_COUNT];

/* bank_0F lut_2x2MapObj_Right/Left/Up/Down: [facing][frame] = UL, DL, UR, DR
 * (tile, attribute) pairs; attribute bits 0-1 = sprite palette, $40 = flip. */
extern const uint8_t lut_2x2MapObj[4][2][8];
