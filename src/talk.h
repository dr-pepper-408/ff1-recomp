#pragma once
#include <stdint.h>
#include "mapobj.h"

/*
 * talk.h — Talking to map objects (no SDL).
 *
 * Port of TalkToObject and the TalkRoutines (bank_0E): each object id has a
 * routine (lut_MapObjTalkJumpTbl) and 4 bytes of data (lut_MapObjTalkData)
 * that pick the dialogue and may change the game state — give items, set
 * event flags, show/hide objects, or start a battle or teleport once the
 * dialogue box closes.
 */

typedef struct {
    uint8_t dialogue;    ///< Dialogue id to show.
    uint8_t sfx;         ///< dlgsfx: 0 none, 1 fanfare, 2 treasure jingle.
    int battle;          ///< Formation to fight when the box closes (TalkBattle), or -1.
    int normal_tele;     ///< Normal teleport when the box closes (TalkNormTeleport), or -1.
    int reentermap;      ///< dlgflg_reentermap: re-enter the map when the box closes
                         ///< (the class change, to reload the party's graphics).
} TalkResult;

/** Talk to the object in slot `slot` of `mo` (found with
 *  MapObj_FindTalkTarget). */
void Talk_ToObject(SaveData* save, MapObjects* mo, int slot, TalkResult* out);
