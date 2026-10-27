/**
 * @file btl_core.c
 * @brief FF1 battle rules: in-battle character stats, the physical attack
 *        formula, spell effects, the run roll and EXP / level ups (ports from
 *        bank_0C / bank_0B).
 */

#include "btl_core.h"
#include "rng.h"
#include "../data/generated/weapon_data.h"
#include "../data/generated/battle_messages.h"
#include "../data/generated/level_up_data.h"

/* ---- In-battle character stats (LoadOneCharacterIBStats) ---- */

void Btl_LoadCharStats(const SaveData* save, int slot, BtlCharStats* out) {
    const CharacterStats* c = &save->characters[slot];
    *out = (BtlCharStats){ 0 };
    out->slotindex   = (uint8_t)slot;
    out->class       = c->class;
    out->hitrate     = c->hit_rate;
    out->magdef      = c->mag_def;
    out->evade       = c->evade;
    out->absorb      = c->absorb;
    out->dmg         = c->damage;
    out->elemresist  = c->elem_resist;
    out->numhitsmult = 1;
    out->numhits     = (uint8_t)((c->hit_rate >> 5) + 1);

    uint8_t weapon = 0;
    for (int i = 0; i < 4; i++)
        if (c->weapons[i] & 0x80) { weapon = c->weapons[i]; break; }
    /* BUGGED in the original: the critical rate is set to the weapon's index
     * instead of the weapon's crit rate stat. */
    out->critrate = weapon & 0x7F;
    if (out->critrate) {
        const WeaponData* w = &weapon_table[out->critrate - 1];
        out->wepgfx = w->battle_gfx;
        out->wepplt = w->palette;
    }
}

uint8_t Btl_AilmentObToIb(uint8_t ob) {
    static const uint8_t lut[4] = { 0, BTL_AIL_DEAD, BTL_AIL_STONE, BTL_AIL_POISON };
    return lut[ob & 3];
}

uint8_t Btl_AilmentIbToOb(uint8_t ib) {
    /* Only death, stone and poison persist out of battle, in that priority. */
    static const uint8_t lut[8] = { 0, 1, 2, 1, 3, 1, 2, 1 };
    return lut[ib & (BTL_AIL_DEAD | BTL_AIL_STONE | BTL_AIL_POISON)];
}

void Btl_PlayerAttacker(const BtlCharStats* c, uint8_t ob_ailments, BtlAttacker* out) {
    out->strength    = c->dmg;
    /* BUGGED in the original: category and element come from the character
     * (always 0) instead of the equipped weapon, so weapons never get their
     * bonus against weaknesses. */
    out->category    = c->category;
    out->element     = c->elemweak;
    out->hitrate     = c->hitrate;
    out->numhitsmult = c->numhitsmult;
    out->numhits     = c->numhits;
    out->critrate    = c->critrate;
    out->ailments    = ob_ailments;
    out->attackailment = 0;
}

/* ---- DoPhysicalAttack ---- */

static uint16_t cap255(uint16_t v) { return v > 255 ? 255 : v; }

void Btl_PhysicalAttack(const BtlAttacker* a, BtlDefender* d, int defender_is_player,
                        BtlAttackResult* r) {
    *r = (BtlAttackResult){ 0 };

    uint16_t hitchance = 168;
    uint16_t basedamage = a->strength;
    uint16_t critchance = a->critrate;

    /* Blindness: -40 to hit when the attacker is blind, +40 against a blind
     * defender (8-bit adds, as in the original). */
    if (a->ailments & BTL_AIL_DARK) hitchance = (uint8_t)(hitchance - 40);
    if (d->ailments & BTL_AIL_DARK) hitchance = (uint8_t)(hitchance + 40);

    /* A category or element weakness: +40 to hit, +4 damage (max 255). */
    if ((a->category & d->category) | (a->element & d->elemweakness)) {
        hitchance = cap255(hitchance + 40);
        basedamage = cap255(basedamage + 4);
    }

    if (d->ailments & (BTL_AIL_STUN | BTL_AIL_SLEEP)) {
        /* A sleeping or stunned defender takes 25% more damage.  (Then hit
         * rate and evade are skipped entirely; the disassembly asks whether
         * that is BUGGED.) */
        basedamage = cap255(basedamage + (basedamage >> 2));
    } else {
        hitchance = cap255(hitchance + a->hitrate);
        hitchance = (hitchance > d->evade) ? hitchance - d->evade : 0;
    }
    if (hitchance == 0) hitchance = 1;
    if (critchance >= hitchance) critchance = hitchance;

    int numhits = (uint8_t)(a->numhits * a->numhitsmult);
    if (numhits == 0) numhits = 1;

    if (d->ailments & (BTL_AIL_DEAD | BTL_AIL_STONE)) {
        r->ineffective = 1;
        return;
    }

    uint32_t total = 0;
    uint8_t base = (uint8_t)basedamage;
    for (; numhits > 0; numhits--) {
        uint8_t randhit = Rng_RandAX(0, 200);
        if (randhit == 200) continue;                     /* always a miss */

        uint16_t dmg = (uint16_t)(Rng_RandAX(0, base) + base);   /* [base, 2*base] */
        if (dmg > 255) dmg = 255;
        uint16_t dmgcalc = 0;

        if (hitchance >= randhit) {                       /* a hit */
            dmgcalc = (dmg > d->absorb) ? (uint16_t)(dmg - d->absorb) : 0;
            if (dmgcalc == 0) dmgcalc = 1;
            r->hits++;
        }
        if (critchance >= randhit) {                      /* a critical adds the damage again */
            dmgcalc = (uint16_t)(dmgcalc + dmg);
            r->crits++;
        }
        total += dmgcalc;

        /* Enemy attacks can inflict an ailment.  BUGGED in the original: the
         * check runs once ANY hit so far has connected, so later misses can
         * still inflict it. */
        if (defender_is_player && a->attackailment && r->hits) {
            uint16_t chance = 100;
            if (d->elemresist & a->element) chance = 0;
            chance = (chance > d->magdef) ? chance - d->magdef : 0;
            if (chance == 0) chance = 1;
            uint8_t roll = Rng_RandAX(0, 200);
            if (roll != 200 && chance >= roll) {
                uint8_t added = (uint8_t)(~d->ailments & a->attackailment);
                r->ailments_added |= added;
                d->ailments |= a->attackailment;
                if (added & BTL_AIL_DEAD) {
                    /* PrintPlayerAilmentMessageFromAttack: a death-touch kills
                     * outright (HP 0) and ends the attack; no damage is dealt
                     * or shown. */
                    d->hp = 0;
                    r->killed = 1;
                    r->ailment_death = 1;
                    return;
                }
            }
        }
    }

    r->damage = (uint16_t)(total > 0xFFFF ? 0xFFFF : total);
    d->hp = (d->hp > r->damage) ? (uint16_t)(d->hp - r->damage) : 0;
    if (d->hp == 0) {
        d->ailments = BTL_AIL_DEAD;                       /* replaces all other ailments */
        r->killed = 1;
    }
}

/* ---- Battle_PlayerTryRun ---- */

int Btl_TryRun(uint8_t luck, uint8_t bugged_level, int striking_first, int no_run) {
    if (striking_first) return 1;
    if (no_run) return 0;
    /* Run if luck > random[0, level + 15].  BUGGED in the original: "level"
     * is read from the in-battle stats instead of the character's stats, so
     * it is really another byte of battle RAM (the caller passes it). */
    uint8_t r = Rng_RandAX(0, (uint8_t)(bugged_level + 15));
    return luck > r;
}

/* ---- Magic: BtlMag_PerformSpellEffect and the effect routines ---- */

#define MAG_CATEGORY_UNDEAD 0x08

/* The magic math buffer entries the effects share.  16-bit, like the
 * original's: MathBuf_Add caps at $FFFF and MathBuf_Sub at 0. */
typedef struct {
    uint16_t hitchance, basedamage, randhit;
} MagMath;

static uint16_t add16(uint16_t a, uint16_t b) { return (a + b > 0xFFFF) ? 0xFFFF : (uint16_t)(a + b); }
static uint16_t sub16(uint16_t a, uint16_t b) { return (a > b) ? (uint16_t)(a - b) : 0; }
static uint8_t  add8cap(uint8_t a, uint8_t b) { return (a + b > 255) ? 255 : (uint8_t)(a + b); }

static void mag_say(BtlMagResult* r, uint8_t msg) {
    if (r->nmsgs < BTL_MAG_MAX_MSGS) r->msgs[r->nmsgs++] = msg;
}

/* BtlMag_PrepHitAndDamage: hit chance += spell hit rate - magdef; damage =
 * rand[damage, damage*2] (the roll uses the low byte only); randhit =
 * rand[0,200].  Effects that do no damage still run it on whatever damage
 * the buffer holds, which only matters for the RNG (one roll either way). */
static void mag_prep(MagMath* mm, const BtlMagic* m, const BtlMagDefender* d) {
    mm->hitchance = add16(mm->hitchance, m->hitrate);
    mm->hitchance = sub16(mm->hitchance, d->magdef);
    mm->basedamage = add16(mm->basedamage, Rng_RandAX(0, (uint8_t)mm->basedamage));
    mm->randhit = Rng_RandAX(0, 200);
}

/* BtlMag_CalcElemHitChance + BtlMag_PrepHitAndDamage (BtlMag_DoStatPrep):
 * base 148, 0 if the defender resists, +40 if weak. */
static void mag_stat_prep(MagMath* mm, const BtlMagic* m, const BtlMagDefender* d) {
    mm->hitchance = 148;
    if (m->element & d->elemresist) mm->hitchance = 0;
    if (m->element & d->elemweakness) mm->hitchance = add16(mm->hitchance, 40);
    mag_prep(mm, m, d);
}

/* BtlMag_DidSpellConnect, with the "200 always misses" check every caller
 * makes first. */
static int mag_connects(const MagMath* mm) {
    return mm->randhit != 200 && mm->hitchance >= mm->randhit;
}

/* BtlMag_ApplyDamage: a "critical" (the hit roll connecting) doubles the
 * damage (a 16-bit shift, no cap); HP drops, floored at 0. */
static void mag_apply_damage(MagMath* mm, BtlMagDefender* d, BtlMagResult* r) {
    if (mag_connects(mm)) mm->basedamage = (uint16_t)(mm->basedamage << 1);
    d->hp = sub16(d->hp, mm->basedamage);
    r->show_damage = 1;
    r->damage = mm->basedamage;
}

/* BtlMag_ApplyAilments: add the ailments, saying "Ineffective" if the
 * defender already had any of them (they are added regardless). */
static void mag_apply_ailments(const BtlMagic* m, BtlMagDefender* d, BtlMagResult* r) {
    if (m->effectivity & d->ailments) mag_say(r, BTLMSG_INEFFECTIVE);
    d->ailments |= m->effectivity;
}

/* The routines of jumptable_MagicEffect. */
static void mag_do_effect(MagMath* mm, const BtlMagic* m, BtlMagDefender* d, BtlMagResult* r) {
    switch (m->effect) {
    case 0x01:  /* BtlMag_Effect_Damage: always connects; the roll is a crit */
        r->connected = 1;
        mm->hitchance = 148;
        mm->basedamage = m->effectivity;
        if (m->element & d->elemresist) {
            mm->hitchance = 0;
            mm->basedamage >>= 1;
        }
        if (m->element & d->elemweakness) {
            mm->hitchance = add16(mm->hitchance, 40);
            mm->basedamage = add16(mm->basedamage >> 1, mm->basedamage);   /* x1.5 */
        }
        mag_prep(mm, m, d);
        mag_apply_damage(mm, d, r);
        break;

    case 0x02:  /* BtlMag_Effect_DamageUndead: no elemental checks */
        mm->hitchance = 148;
        mm->basedamage = m->effectivity;
        mag_prep(mm, m, d);
        if (!(d->category & MAG_CATEGORY_UNDEAD)) {
            mm->basedamage = 0;
            r->connected = 0;
            break;
        }
        r->connected = 1;
        mag_apply_damage(mm, d, r);
        break;

    case 0x03:  /* BtlMag_Effect_InflictAilment */
        mag_stat_prep(mm, m, d);
        if (mag_connects(mm)) {
            mag_apply_ailments(m, d, r);
            r->connected = 1;
        }
        break;

    case 0x04:  /* BtlMag_Effect_Slow */
        mag_stat_prep(mm, m, d);
        if (mag_connects(mm)) {
            r->connected = 1;
            /* Already at 0: undone, but still reported as connected (the
             * disassembly calls this a bug). */
            if (d->numhitsmult) d->numhitsmult--;
        }
        break;

    case 0x05:  /* BtlMag_Effect_LowerMorale */
        mag_stat_prep(mm, m, d);
        if (mag_connects(mm)) {
            d->morale = (d->morale > m->effectivity) ? (uint8_t)(d->morale - m->effectivity) : 0;
            r->connected = 1;
        }
        break;

    case 0x06:  /* BtlMag_Effect_RecoverHP */
    case 0x07: {
        r->connected = 1;
        if (d->ailments & BTL_AIL_DEAD) break;
        uint8_t heal = add8cap(Rng_RandAX(0, m->effectivity), m->effectivity);
        d->hp = add16(d->hp, heal);
        if (d->hp >= d->hpmax) d->hp = d->hpmax;
        break;
    }

    case 0x08:  /* BtlMag_Effect_CureAilment.  (It cures DEAD too, without
                 * restoring HP, and STONE, though no spell can in battle.) */
        if (d->ailments & m->effectivity) {
            d->ailments &= (uint8_t)~m->effectivity;
            r->connected = 1;
        }
        break;

    case 0x09:  /* BtlMag_Effect_AbsorbUp */
        d->absorb = add8cap(d->absorb, m->effectivity);
        r->connected = 1;
        break;

    case 0x0A:  /* BtlMag_Effect_ElemResist */
        d->elemresist |= m->effectivity;
        r->connected = 1;
        break;

    case 0x0B:  /* BtlMag_Effect_AttackUp */
    case 0x0D:  /* BtlMag_Effect_AttackUp2 (its hit-rate half writes to unused RAM) */
        d->strength = add8cap(d->strength, m->effectivity);
        r->connected = 1;
        break;

    case 0x0C:  /* BtlMag_Effect_Fast: up to x2, "Ineffective" beyond */
        r->connected = 1;
        if (++d->numhitsmult >= 3) {
            r->connected = 0;
            d->numhitsmult = 2;
        }
        break;

    case 0x0E:  /* BtlMag_Effect_EvadeDown.  BUGGED in the original: a JMP
                 * that should be a BEQ skips the effect, so it always misses
                 * (after rolling). */
        mag_stat_prep(mm, m, d);
        break;

    case 0x0F:  /* BtlMag_Effect_CureAll */
        d->hp = d->hpmax;
        d->ailments = 0;
        r->connected = 1;
        break;

    case 0x10:  /* BtlMag_Effect_EvadeUp */
        d->evade = add8cap(d->evade, m->effectivity);
        r->connected = 1;
        break;

    case 0x11: {  /* BtlMag_Effect_RemoveResist: its own elemental check */
        mm->hitchance = 148;
        if (m->element & d->elemresist) mm->hitchance = 0;
        if (m->element & d->elemweakness) mm->hitchance = 188;
        mm->hitchance = add16(mm->hitchance, m->hitrate);
        mm->hitchance = sub16(mm->hitchance, d->magdef);
        uint8_t roll = Rng_RandAX(0, 200);
        if (roll == 200) break;
        mm->randhit = roll;
        if (mm->hitchance >= mm->randhit) {
            d->elemresist = 0;
            r->connected = 1;
        }
        break;
    }

    case 0x12:  /* BtlMag_Effect_InflictAilment2: no roll, HP <= 300 */
        if (m->element & d->elemresist) break;
        if (d->hp <= 300) {
            mag_apply_ailments(m, d, r);
            r->connected = 1;
        }
        break;

    default:    /* $00: Battle_ShowIneffective */
        mag_say(r, BTLMSG_INEFFECTIVE);
        break;
    }
}

/* BtlMag_HandleAilmentChanges' messages, by ailment bit. */
static const uint8_t MAG_AIL_ADDED[8] = {
    0, BTLMSG_BROKENTOPIECES, BTLMSG_POISONED, BTLMSG_DARKNESS,
    BTLMSG_PARALYZED_A, BTLMSG_ASLEEP, BTLMSG_SILENCED, BTLMSG_CONFUSED,
};
static const uint8_t MAG_AIL_CURED[8] = {
    BTLMSG_SLAIN, BTLMSG_INEFFECTIVENOW, BTLMSG_NEUTRALIZED, BTLMSG_SIGHTRECOVERED,
    BTLMSG_CURED, BTLMSG_WOKEUP, BTLMSG_BREAKSILENCE, BTLMSG_CURED,
};

void Btl_MagicEffect(uint8_t attackid, const BtlMagic* m, BtlMagDefender* d,
                     int defender_is_player, BtlMagResult* r) {
    *r = (BtlMagResult){ 0 };
    MagMath mm = { 0 };
    uint8_t ail_orig = d->ailments;

    mag_do_effect(&mm, m, d, r);

    /* BtlMag_PrintMagicMessage */
    if (!r->connected) {
        mag_say(r, BTLMSG_INEFFECTIVE);
    } else if (attackid < 0x40 && lut_MagicBattleMessages[attackid]) {
        mag_say(r, lut_MagicBattleMessages[attackid]);
    } else if (attackid == 0x40) {
        mag_say(r, BTLMSG_HPUP);                          /* HEAL potion */
    } else {
        mag_say(r, 0);       /* no message (PURE, enemy attacks...): just the delay */
    }

    /* HP zero or "negative" (MathBuf_NZ) adds DEAD. */
    if (d->hp == 0 || (d->hp & 0x8000)) d->ailments |= BTL_AIL_DEAD;

    /* BtlMag_HandleAilmentChanges: bit 0 (death) first. */
    for (int bit = 0; bit < 8; bit++) {
        uint8_t mask = (uint8_t)(1 << bit);
        if (!((ail_orig ^ d->ailments) & mask)) continue;
        if (!(d->ailments & mask)) {
            mag_say(r, MAG_AIL_CURED[bit]);
        } else if (bit == 0) {
            mag_say(r, defender_is_player ? BTLMSG_SLAIN : BTLMSG_TERMINATED);
            break;                                        /* nothing else is reported */
        } else {
            uint8_t msg = MAG_AIL_ADDED[bit];
            /* ShowAltBattleMessage: players aren't "broken into pieces". */
            if (defender_is_player && msg == BTLMSG_BROKENTOPIECES) msg = BTLMSG_STOPPED;
            mag_say(r, msg);
        }
    }
}

/* ---- End of battle: EXP and level ups (bank_0B) ---- */

#define CLS_FT 0
#define CLS_TH 1
#define CLS_BB 2
#define CLS_KN 6
#define CLS_NJ 7
#define CLS_MA 8

uint16_t Btl_ExpShare(uint32_t exp_total, int survivors) {
    uint16_t share = survivors ? (uint16_t)((exp_total & 0xFFFF) / (unsigned)survivors) : 0;
    return share ? share : 1;
}

static uint32_t get_exp(const CharacterStats* c) {
    return c->exp[0] | ((uint32_t)c->exp[1] << 8) | ((uint32_t)c->exp[2] << 16);
}

static void set_exp(CharacterStats* c, uint32_t e) {
    c->exp[0] = (uint8_t)e;
    c->exp[1] = (uint8_t)(e >> 8);
    c->exp[2] = (uint8_t)(e >> 16);
}

static uint8_t cap200(unsigned v) { return (uint8_t)(v > 200 ? 200 : v); }

/* LvlUp_LevelUp (the caller has checked the level isn't 50). */
static void level_up(SaveData* save, int i, BtlLevelUp* out) {
    CharacterStats* c = &save->characters[i];
    CharacterMagic* m = &save->character_magic[i];
    uint8_t old = c->level;
    c->level = (uint8_t)(old + 1);
    /* Promoted classes share their base class's data. */
    const uint8_t* data = level_up_data[c->class % 6][old];
    uint8_t cls = c->class;

    c->hit_rate = cap200(c->hit_rate + lut_LvlUpHitRateBonus[cls]);
    c->mag_def  = cap200(c->mag_def + lut_LvlUpMagDefBonus[cls]);

    /* Spell charges.  Fighters and Thieves are skipped here because their
     * promoted classes share the data. */
    if (cls != CLS_FT && cls != CLS_TH) {
        for (int l = 0; l < 8; l++)
            if (data[1] & (1 << l)) m->max_mp[l]++;
        uint8_t cap = (cls == CLS_KN || cls == CLS_NJ) ? 4 : 9;
        for (int l = 0; l < 8; l++)
            if (m->max_mp[l] == cap + 1) m->max_mp[l]--;
    }

    /* Max HP: Vit/4 + 1, plus rand[20,25] on "strong" levels; at most 999.
     * (Vitality before this level's increase.) */
    uint8_t gain = (uint8_t)(c->vit / 4 + 1);
    if (data[0] & 0x20) gain = (uint8_t)(gain + Rng_RandAX(20, 25));
    unsigned hp = c->max_hp + gain;
    c->max_hp = (uint16_t)(hp >= 1000 ? 999 : hp);

    /* Str, Agi, Int, Vit, Luck: +1 if the level's bit is set, else a 1 in 4
     * chance.  A stat at 99 stays there, though it's still reported as up
     * (BUGGED in the original). */
    uint8_t* stats[5] = { &c->str, &c->agil, &c->intel, &c->vit, &c->luck };
    for (int k = 0; k < 5; k++) {
        uint8_t up = (data[0] & (0x10 >> k)) ? 1 : ((Rng_Battle() & 3) == 0);
        out->statup[k] = up;
        if (*stats[k] + up != 100) *stats[k] = (uint8_t)(*stats[k] + up);
    }

    /* Damage +1 when Strength went up to an even number; Evade +1 when
     * Agility went up (both capped at 200). */
    if (out->statup[0] && !(c->str & 1) && c->damage + 1 != 201) c->damage++;
    if (out->statup[1] && c->evade + 1 != 201) c->evade++;

    /* LvlUp_AdjustBBSubStats: an unarmed Black Belt / Master gets
     * Absorb = level and Damage = 2 x level.  BUGGED in the original (the
     * "BB armor bug"): it checks for no weapon, not no armor, so it
     * overwrites the Absorb that armor gives. */
    if (cls == CLS_BB || cls == CLS_MA) {
        int armed = 0;
        for (int w = 0; w < 4; w++) armed |= c->weapons[w] & 0x80;
        if (!armed) {
            uint8_t lv = (uint8_t)(c->level + 1);
            c->absorb = lv;
            c->damage = (uint8_t)(lv << 1);
        }
    }

    out->level = (uint8_t)(c->level + 1);
    out->max_hp = c->max_hp;
}

int Btl_AwardExp(SaveData* save, int i, uint32_t reward, BtlLevelUp* out) {
    CharacterStats* c = &save->characters[i];
    int leveled = 0;
    *out = (BtlLevelUp){ 0 };

    /* LvlUp_AwardExp: only the living (poisoned is fine) get EXP. */
    uint8_t ail = c->ailments & 3;
    if (ail == 0 || ail == 3) {
        uint32_t e = get_exp(c) + reward;
        set_exp(c, e >= 1000000 ? 999999 : e);
        if (c->level != 49 && get_exp(c) >= lut_ExpToAdvance[c->level]) {
            level_up(save, i, out);
            leveled = 1;
        }
    }

    /* ch_exptonext: what's left to the next level (low 16 bits); 0 at 50. */
    c->exp_to_next = (c->level == 49) ? 0
                   : (uint16_t)(lut_ExpToAdvance[c->level] - get_exp(c));
    return leveled;
}

/* ---- ReSortPartyByAilment (bank_0C) ---- */

void Btl_ReSortParty(SaveData* save) {
    static const uint8_t weight[4] = { 0x00, 0x40, 0x20, 0x10 };   /* lut_AilmentWeight */
    uint8_t order[4];                                               /* char_order_buf */
    for (int i = 0; i < 4; i++)
        order[i] = (uint8_t)(i + weight[save->characters[i].ailments & 3]);

    for (int pass = 0; pass < 4; pass++)
        for (int i = 0; i < 3; i++) {
            if (order[i] < order[i + 1]) continue;
            /* SwapCharsForSorting on the indices *stored* in the buffer. */
            int a = order[i] & 3, b = order[i + 1] & 3;
            CharacterStats ts = save->characters[a];
            save->characters[a] = save->characters[b];
            save->characters[b] = ts;
            CharacterMagic tm = save->character_magic[a];
            save->character_magic[a] = save->character_magic[b];
            save->character_magic[b] = tm;
            uint8_t t = order[a]; order[a] = order[b]; order[b] = t;
        }
}
