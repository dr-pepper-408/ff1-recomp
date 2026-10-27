#pragma once
/*
 * party.h — Party/character data model.
 *
 * The foundation layer that owns the FF1 party.  It wraps types.h's SaveData
 * as the single source of truth: one struct holds the unsram header, the four
 * party members' stats and spell data, the game flags, and the SRAM footer.
 *
 * Character class ids mirror constants.h's CharacterClass enum.  We redeclare
 * them locally (rather than including constants.h) because constants.h also
 * defines RIGHT/LEFT/DOWN/UP and BTN_START with values that clash with
 * input.h; this is the same deconfliction convention used by sm_map.h.
 */

#include <stdint.h>
#include "../data/generated/types.h"

typedef enum {
    PARTY_CLASS_FT = 0x00,   /* Fighter      */
    PARTY_CLASS_TH = 0x01,   /* Thief        */
    PARTY_CLASS_BB = 0x02,   /* Black Belt   */
    PARTY_CLASS_RM = 0x03,   /* Red Mage     */
    PARTY_CLASS_WM = 0x04,   /* White Mage   */
    PARTY_CLASS_BM = 0x05,   /* Black Mage   */
    PARTY_CLASS_KN = 0x06,   /* Knight       */
    PARTY_CLASS_NJ = 0x07,   /* Ninja        */
    PARTY_CLASS_MA = 0x08,   /* Master       */
    PARTY_CLASS_RW = 0x09,   /* Red Wizard   */
    PARTY_CLASS_WW = 0x0A,   /* White Wizard */
    PARTY_CLASS_BW = 0x0B,   /* Black Wizard */
} PartyClass;

/** A new game (GameStart + NewGame_LoadStartingStats): game flags, unsram
 *  header, and the party of the given classes (0-5) and names (4 tiles). */
void Party_NewGame(SaveData* s, const uint8_t classes[MAX_PARTY_SIZE],
                   const uint8_t names[MAX_PARTY_SIZE][4]);

/** Party_NewGame with the default party (Fighter, Thief, Black Belt, Red
 *  Mage named ARGO, BEA, CID, DUNE), for --new. */
void Party_Init(SaveData* s);

/** Access the i-th party member's stats (0..MAX_PARTY_SIZE-1), or NULL. */
CharacterStats* Party_Character(SaveData* s, int slot);

/** Access the i-th party member's magic data (0..MAX_PARTY_SIZE-1), or NULL. */
CharacterMagic* Party_Magic(SaveData* s, int slot);

/** Convert a character's 4-tile FF1 name to ASCII (out: 4 chars + NUL).
 *  Tiles without an ASCII equivalent become '?'. */
void Party_NameToAscii(const CharacterStats* c, char out[5]);

/** Gold in GP (little-endian 3-byte field). */
uint32_t Party_Gold(const SaveData* s);

/** Port of AddGPToParty (bank_0F): add GP, capping at 999,999 with the
 *  original's byte-wise check (which misses some totals of 1,048,576+). */
void Party_AddGold(SaveData* s, uint32_t amount);

/** Set gold in GP (clamped to 24 bits). */
void Party_SetGold(SaveData* s, uint32_t gold);

/** Highest inventory item id + 1 (item ids 1..ITEM_STOP-1 are the inventory;
 *  ids 18..21 are the orbs, which the menu skips). */
#define ITEM_STOP 28

/** Give the party one of `item_id` (caps at 99).  Returns 0 on success,
 *  -1 if the id is out of range or the party already holds 99. */
int Party_AddItem(SaveData* s, uint8_t item_id);

/** Equip `weapon_id` (1-based) into the first empty weapon slot across the
 *  party.  Returns 0 on success, -1 if every slot is full. */
int Party_AddWeapon(SaveData* s, uint8_t weapon_id);

/** Equip `armor_id` (1-based) into the first empty armor slot across the
 *  party.  Returns 0 on success, -1 if every slot is full. */
int Party_AddArmor(SaveData* s, uint8_t armor_id);

/** UnadjustEquipStats (bank_0F): remove the weapon (armor = 0) or armor
 *  (armor = 1) bonuses from every character's stats, before equipping. */
void Party_UnadjustEquipStats(SaveData* s, int armor);

/** ReadjustEquipStats: add the equipped weapons' or armor's bonuses back
 *  (with the Black Belt / Master bare-handed rules). */
void Party_ReadjustEquipStats(SaveData* s, int armor);

/** SortEquipmentList (bank_0F): close the gaps in every character's weapon
 *  (armor = 0) or armor (armor = 1) list, keeping the order. */
void Party_SortEquipment(SaveData* s, int armor);

/** Debug loadout: seed a level-10 party with items, equipment, magic and gold
 *  (see FF1_DEBUG_STATE in config.h).  Called right after Party_Init(). */
void Party_ApplyDebug(SaveData* s);
