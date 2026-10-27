/*
 * Battle Messages
 * Source: bin/0B_8C40_battlemessages.bin (bank $0B, ROM $8C40)
 * Generated: 2026-10-27
 * DTE-decoded battle message strings.
 */

#pragma once

#include <stdint.h>

/* Indexed by BTLMSG id - 1. */
#define BATTLE_MESSAGE_COUNT 78

extern const char *const battle_message_table[BATTLE_MESSAGE_COUNT];

#define BTLMSG_HPUP 0x01
#define BTLMSG_ASLEEP 0x04
#define BTLMSG_SIGHTRECOVERED 0x06
#define BTLMSG_SILENCED 0x07
#define BTLMSG_DARKNESS 0x09
#define BTLMSG_NEUTRALIZED 0x0E
#define BTLMSG_BREAKSILENCE 0x11
#define BTLMSG_CONFUSED 0x13
#define BTLMSG_POISONED 0x14
#define BTLMSG_PARALYZED_A 0x17
#define BTLMSG_BROKENTOPIECES 0x1A
#define BTLMSG_SLAIN 0x20
#define BTLMSG_INEFFECTIVE 0x21
#define BTLMSG_STRIKEFIRST 0x22
#define BTLMSG_SURPRISED 0x23
#define BTLMSG_CANTRUN 0x24
#define BTLMSG_RUNAWAY 0x25
#define BTLMSG_CLOSECALL 0x26
#define BTLMSG_WOKEUP 0x27
#define BTLMSG_SLEEPING 0x28
#define BTLMSG_CURED 0x29
#define BTLMSG_PARALYZED_B 0x2A
#define BTLMSG_HITS 0x2B
#define BTLMSG_CRITICALHIT 0x2C
#define BTLMSG_DMG 0x2E
#define BTLMSG_STOPPED 0x2F
#define BTLMSG_STR 0x33
#define BTLMSG_AGI 0x34
#define BTLMSG_INT 0x35
#define BTLMSG_VIT 0x36
#define BTLMSG_LUCK 0x37
#define BTLMSG_UP 0x38
#define BTLMSG_TERMINATED 0x3F
#define BTLMSG_MISSED 0x40
#define BTLMSG_INEFFECTIVENOW 0x4A
#define BTLMSG_NOTHINGHAPPENS 0x4E

/* bank_0C lut_MagicBattleMessages: BTLMSG id per attack id (0 = none). */
extern const uint8_t lut_MagicBattleMessages[92];
