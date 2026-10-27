#pragma once
#include <stdint.h>
#include "../data/generated/types.h"

/*
 * btl_core.h — FF1 battle rules (no SDL, no presentation).
 *
 * Ports of the bank_0C battle math: LoadOneCharacterIBStats,
 * DoPhysicalAttack (with PlayerAttackEnemy_Physical /
 * EnemyAttackPlayer_Physical's stat setup), the spell effects
 * (BtlMag_PerformSpellEffect) and Battle_PlayerTryRun.  The
 * original's documented bugs are reproduced; see the comments at each one.
 * Randomness comes from rng.h (RandAX via BattleRNG), in the original order.
 */

/* In-battle ailment bits (Constants.inc AIL_*). */
#define BTL_AIL_DEAD   0x01
#define BTL_AIL_STONE  0x02
#define BTL_AIL_POISON 0x04
#define BTL_AIL_DARK   0x08
#define BTL_AIL_STUN   0x10
#define BTL_AIL_SLEEP  0x20
#define BTL_AIL_MUTE   0x40
#define BTL_AIL_CONF   0x80

/** A character's in-battle stats (btl_chstats, $12 bytes). */
typedef struct {
    uint8_t slotindex, class;
    uint8_t hitrate, magdef, evade, absorb, dmg, elemresist;
    uint8_t numhitsmult;   ///< 1, or 2 under FAST
    uint8_t numhits;       ///< hit rate / 32 + 1
    uint8_t category;      ///< always 0 (players have no category)
    uint8_t elemweak;      ///< always 0
    uint8_t critrate;      ///< the equipped weapon's index (bug, see btl_core.c)
    uint8_t wepgfx, wepplt;
} BtlCharStats;

/** Port of LoadOneCharacterIBStats. */
void Btl_LoadCharStats(const SaveData* save, int slot, BtlCharStats* out);

/** Out-of-battle ailment id <-> in-battle ailment bits
 *  (ConvertOBAilmentToIB / ConvertIBAilmentToOB, bank_0B). */
uint8_t Btl_AilmentObToIb(uint8_t ob);
uint8_t Btl_AilmentIbToOb(uint8_t ib);

/** The attacker's side of a physical attack (btl_attacker_*). */
typedef struct {
    uint8_t strength, category, element, hitrate;
    uint8_t numhitsmult, numhits, critrate;
    uint8_t ailments;        ///< the attacker's own ailments (DARK lowers hit chance)
    uint8_t attackailment;   ///< ailment the attack inflicts (enemies only)
} BtlAttacker;

/** The defender's side (btl_defender_*). hp/ailments are updated in place. */
typedef struct {
    uint8_t ailments, category, elemweakness, evade, absorb, magdef, elemresist;
    uint16_t hp;
} BtlDefender;

/** What happened, for the battle messages. */
typedef struct {
    int ineffective;         ///< the defender was already dead or stone
    int hits;                ///< hits that connected
    int crits;               ///< critical hits
    uint16_t damage;         ///< total damage
    uint8_t ailments_added;  ///< ailments newly inflicted by the attack
    int killed;              ///< the defender's HP reached 0
    int ailment_death;       ///< killed by the attack's DEAD ailment (no damage shown)
} BtlAttackResult;

/** Port of DoPhysicalAttack's math.  `defender_is_player` enables the
 *  attack-ailment roll (only enemies inflict ailments with attacks). */
void Btl_PhysicalAttack(const BtlAttacker* a, BtlDefender* d, int defender_is_player,
                        BtlAttackResult* r);

/** PlayerAttackEnemy_Physical's attacker setup from a character. */
void Btl_PlayerAttacker(const BtlCharStats* c, uint8_t ob_ailments, BtlAttacker* out);

/* ---- Magic (bank_0C BtlMag_xxx) ---- */

/** A spell's battle parameters (Battle_PrepareMagic: btlmag_effect etc.,
 *  from magic_data.h). */
typedef struct {
    uint8_t effect, element, hitrate, effectivity;
} BtlMagic;

/** The magic defender (btlmag_defender_xxx).  The caller loads it the way the
 *  original does (BtlMag_LoadPlayer/EnemyDefenderStats) and saves back only
 *  what the original saves. */
typedef struct {
    uint16_t hp, hpmax;
    uint8_t ailments, category, magdef, elemweakness, elemresist;
    uint8_t morale, absorb, evade, strength, numhitsmult;
} BtlMagDefender;

#define BTL_MAG_MAX_MSGS 12

/** What a spell did to one defender, in display order. */
typedef struct {
    uint8_t  connected;      ///< btlmag_spellconnected
    uint8_t  show_damage;    ///< the "###DMG" box was drawn
    uint16_t damage;         ///< its number
    uint8_t  nmsgs;
    uint8_t  msgs[BTL_MAG_MAX_MSGS];   ///< BTLMSG ids (0 = a pause with no message)
} BtlMagResult;

/** Port of BtlMag_PerformSpellEffect: the effect, its message, the DEAD
 *  ailment on HP 0 and BtlMag_HandleAilmentChanges' messages.  `attackid`
 *  is the magic_data index ($00-$3F spells, $40/$41 potions, $42+ enemy
 *  attacks). */
void Btl_MagicEffect(uint8_t attackid, const BtlMagic* m, BtlMagDefender* d,
                     int defender_is_player, BtlMagResult* r);

/* ---- End of battle (bank_0B EndOfBattleWrapUp / LvlUp_xxx) ---- */

/** DivideRewardBySurvivors: the low 16 bits of the summed EXP divided by the
 *  number of characters not dead or stone; at least 1. */
uint16_t Btl_ExpShare(uint32_t exp_total, int survivors);

/** What one level up changed, for the "Lev. up!" boxes. */
typedef struct {
    uint8_t  level;        ///< new level, 1-based (as printed)
    uint16_t max_hp;       ///< new max HP
    uint8_t  statup[5];    ///< Str, Agi, Int, Vit, Luck went up (even if capped at 99)
} BtlLevelUp;

/** LvlUp_AwardAndUpdateExp for character `i`: a living or poisoned
 *  character gets `reward` EXP (capped at 999999) and levels up if it
 *  reached the next level -- only once per battle (BUGGED in the
 *  original).  ch_exptonext is updated for every character.  Run after the
 *  ailments are back to out-of-battle ids.  @return 1 if `out` describes a
 *  level up. */
int Btl_AwardExp(SaveData* save, int i, uint32_t reward, BtlLevelUp* out);

/** ReSortPartyByAilment (ExitBattle): move dead, stoned and poisoned
 *  characters toward the back of the party (stats and magic swap places).
 *  BUGGED in the original: the swap uses the character index stored in the
 *  sort buffer instead of the slots it compared, so some orders come out
 *  wrong; that is reproduced.  Ailments must be out-of-battle ids. */
void Btl_ReSortParty(SaveData* save);

/** Port of Battle_PlayerTryRun's roll: 1 if the party escapes.
 *  `bugged_level` is the byte the original reads as the level (see btl_core.c);
 *  `luck` is the character's luck. */
int Btl_TryRun(uint8_t luck, uint8_t bugged_level, int striking_first, int no_run);
