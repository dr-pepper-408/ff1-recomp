/**
 * @file battle.c
 * @brief FF1 battles: the rules and the turn flow (bank_0B / bank_0C).
 *
 * Battle_Start() builds the formation (PrepareEnemyFormation) and rolls for
 * a first strike; Battle_Update() runs command input and the rounds
 * (DoBattleRound), one entity's turn at a time.  Physical attacks, spells,
 * potions and item spells go through the original's routines (the math in
 * btl_core.c); enemies pick actions with Enemy_DoAi.  Victory hands out EXP
 * and gold and levels characters up (EndOfBattleWrapUp); defeat is a game
 * over.
 *
 * A turn's rules run at once and record a script (battle_internal.h): the
 * combat boxes, RespondDelay waits and the animations -- walking forward,
 * weapon swings, spell sprites and flashes, explosions, the screen shake,
 * sprite blinks, enemies being erased -- in the order the original does
 * them.  The random parts of the animations are rolled while recording, at
 * the same points as the original, so the battle RNG runs the same course.
 * battle_draw.c renders what the script has put on screen.
 */

#include <stdio.h>
#include <string.h>
#include "battle_internal.h"
#include "music.h"
#include "sfx.h"
#include "config.h"
#include "rng.h"
#include "text.h"
#include "../data/generated/battle_formations.h"
#include "../data/generated/battle_domains.h"
#include "../data/generated/enemy_data.h"
#include "../data/generated/enemy_names.h"
#include "../data/generated/enemy_ai.h"
#include "../data/generated/magic_data.h"
#include "../data/generated/battle_backdrops.h"
#include "../data/generated/rng_table.h"
#include "../data/generated/weapon_data.h"
#include "../data/generated/armor_data.h"
#include "../data/generated/battle_messages.h"
#include "../data/generated/battle_ui.h"

/* Enemy category bit (Constants.inc CATEGORY_REGEN).  Declared locally
 * rather than pulling in constants.h, which defines RIGHT/LEFT/DOWN/UP/
 * BTN_START that clash with input.h. */
#define BTL_CATEGORY_REGEN  0x80

/* items[] index of the HEAL potion; PURE follows (variables.inc item_heal). */
#define BTL_ITEM_HEAL 25

/* Item name ids (Constants.inc): spells start at MG_START. */
#define BTL_MG_START 0xB0

/* The party's column and rows (EnterBattlePrepareSprites). */
#define CHR_X        0xB0
#define CHR_Y(i)     (0x30 + (i) * 0x18)

BattleState g_btl;

/* The respond rate (0-7) the next battle uses (respondrate). */
static int s_respond_rate = 0;

void Battle_SetRespondRate(int rate) {
    s_respond_rate = rate & 7;
}

static void battle_over(SaveData* save, BattleResult result);

/* ---- Text for the boxes ---- */

static uint8_t ascii_tile(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(0x80 + (c - '0'));
    if (c >= 'A' && c <= 'Z') return (uint8_t)(0x8A + (c - 'A'));
    if (c >= 'a' && c <= 'z') return (uint8_t)(0xA4 + (c - 'a'));
    switch (c) {
    case '\'': return 0xBE;
    case ',':  return 0xBF;
    case '.':  return 0xC0;
    case '-':  return 0xC2;
    case '!':  return 0xC4;
    case '?':  return 0xC5;
    }
    return 0xFF;
}

void btl_text_add_ascii(BtlText* t, const char* s) {
    for (; *s && t->len < BTL_TEXT_MAX; s++) t->t[t->len++] = ascii_tile(*s);
}

void btl_text_ascii(BtlText* t, const char* s) {
    t->len = 0;
    btl_text_add_ascii(t, s);
}

void btl_text_add_raw(BtlText* t, const uint8_t* raw, int max) {
    int room = BTL_TEXT_MAX - t->len;
    if (max > room) max = room;
    t->len = (uint8_t)(t->len + Text_ToTiles(raw, t->t + t->len, max));
}

void btl_text_raw(BtlText* t, const uint8_t* raw) {
    t->len = 0;
    btl_text_add_raw(t, raw, BTL_TEXT_MAX);
}

void btl_text_add_number(BtlText* t, unsigned n) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%u", n);
    btl_text_add_ascii(t, buf);
}

static const char* const CLASS_ABBR[12] = {
    "FT", "TH", "BB", "RM", "WM", "BM", "KN", "NJ", "MA", "RW", "WW", "BW"
};

/* A character's 4-tile name.  (An unnamed character -- the debug party --
 * shows its class abbreviation instead of blanks.) */
void btl_text_char_name(BtlText* t, const CharacterStats* c) {
    t->len = 0;
    if (!c->name[0]) {
        btl_text_add_ascii(t, (c->class < 12) ? CLASS_ABBR[c->class] : "??");
        return;
    }
    for (int i = 0; i < 4; i++) t->t[t->len++] = c->name[i];
}

static void text_enemy_name(BtlText* t, int slot) {
    btl_text_ascii(t, enemy_name_table[g_btl.enemy[slot].id]);
}

static void text_msg(BtlText* t, uint8_t btlmsg) {
    btl_text_raw(t, Text_BattleMessage(btlmsg));
}

/* An item / spell name as the battle boxes print it (at most 8 tiles,
 * DrawBattleSubString_Max8). */
static void text_item(BtlText* t, uint8_t item_id) {
    t->len = 0;
    btl_text_add_raw(t, Text_ItemName(item_id), 8);
}

/* An attack's name (code $0E): enemy attacks ($42+) from lut_EnemyAttack,
 * everything else from the item names. */
static void text_attack(BtlText* t, uint8_t attackid, int attacker_is_enemy) {
    if (attacker_is_enemy && attackid >= 0x42) {
        t->len = 0;
        btl_text_add_raw(t, Text_EnemyAttackName(attackid - 0x42), 8);
    } else {
        text_item(t, (uint8_t)(attackid + BTL_MG_START));
    }
}

/* ---- Recording the script ---- */

static BtlEvent* ev_add(BtlEventOp op) {
    if (g_btl.ev_i >= g_btl.ev_n) {               /* all played: start over */
        g_btl.ev_n = g_btl.ev_i = g_btl.ev_frame = 0;
        g_btl.expl_n = 0;
    }
    static BtlEvent overflow;
    BtlEvent* e = (g_btl.ev_n < BTL_EV_MAX) ? &g_btl.ev[g_btl.ev_n++] : &overflow;
    memset(e, 0, sizeof(*e));
    e->op = (uint8_t)op;
    return e;
}

static void ev_show(int box, const BtlText* t) {
    BtlEvent* e = ev_add(EV_SHOW);
    e->box = (uint8_t)box;
    e->text = *t;
}

static void ev_wait(void)             { ev_add(EV_WAIT); }
static void ev_wait_input(void)       { ev_add(EV_WAIT_INPUT); }
static void ev_hide_from(int box)     { ev_add(EV_HIDE_FROM)->box = (uint8_t)box; }
static void ev_clear_all(void)        { ev_hide_from(CBOX_ATTACKER); }

static void ev_show_msg(uint8_t btlmsg) {
    BtlText t;
    text_msg(&t, btlmsg);
    ev_show(CBOX_MESSAGE, &t);
}

/* ShowAltBattleMessage: the message box, a delay, the box undrawn. */
static void ev_msg(uint8_t btlmsg) {
    ev_show_msg(btlmsg);
    ev_wait();
    ev_hide_from(CBOX_MESSAGE);
}

/* DrawBtlMsg_ClearCombatBoxes: the message, a delay, every box undrawn. */
static void ev_msg_clear_all(uint8_t btlmsg) {
    ev_show_msg(btlmsg);
    ev_wait();
    ev_clear_all();
}

static void ev_show_char(int box, const SaveData* save, int p) {
    BtlText t;
    btl_text_char_name(&t, &save->characters[p]);
    ev_show(box, &t);
}

static void ev_show_enemy(int box, int slot) {
    BtlText t;
    text_enemy_name(&t, slot);
    ev_show(box, &t);
}

/* DrawCharacterStatus: what the status column (and the sprites' poses)
 * show from here on.  (It also sets the natural poses, which zeroes a dead
 * character's HP -- SetNaturalPose.) */
static void ev_status(SaveData* save) {
    BtlEvent* e = ev_add(EV_STATUS);
    for (int i = 0; i < 4; i++) {
        CharacterStats* c = &save->characters[i];
        if (c->ailments & BTL_AIL_DEAD) c->cur_hp = 0;
        e->text.t[i * 3 + 0] = (uint8_t)c->cur_hp;
        e->text.t[i * 3 + 1] = (uint8_t)(c->cur_hp >> 8);
        e->text.t[i * 3 + 2] = c->ailments;
    }
}

/* UpdateVariablePalette */
static void ev_varpal(uint8_t color, int magic) {
    BtlEvent* e = ev_add(EV_VARPAL);
    e->a = color;
    e->b = (uint8_t)magic;
}

static void ev_walk(int p, int right, int natural) {
    BtlEvent* e = ev_add(EV_WALK);
    e->a = (uint8_t)p;
    e->b = (uint8_t)right;
    e->c = (uint8_t)natural;
}

/* WalkForwardAndStrike: walk left, 8 swings (or casts), walk back. */
static void ev_walk_and_strike(int p, uint8_t gfx, int magic, int nodraw) {
    ev_walk(p, 0, 0);
    BtlEvent* e = ev_add(EV_STRIKE);
    e->a = (uint8_t)p;
    e->b = gfx;
    e->c = (uint8_t)magic;
    e->d = (uint8_t)nodraw;
    ev_walk(p, 1, 1);
}

static void ev_flash_char(int p) { ev_add(EV_FLASH_CHAR)->a = (uint8_t)p; }
static void ev_sfx(int id)       { ev_add(EV_SFX)->a = (uint8_t)id; }

/* Battle sound effects (PlayBattleSFX ids). */
enum { BSFX_MAGIC, BSFX_ENEMY_ATTACK, BSFX_PLAYER_ATTACK };

/* btlmag_playerhitsfx: the sound of a spell hitting a character (the
 * heal bleeps for the party's spells, "cha" for the enemies'). */
static int s_playerhitsfx;

/* Set by the frames whose loop doesn't call UpdateBattleSFX (the screen
 * shake, bank_0B's cheer and ChaosDeath): the effect is held. */
static int s_sfx_hold;
static void ev_erase(int slot)   { ev_add(EV_ERASE)->a = (uint8_t)slot; }

/* DoExplosionEffect on enemy `slot`: the explosions are rolled now -- per
 * frame a graphic ($F4/$F8/$FC), then x and y in the enemy's area -- in
 * DrawExplosions' order. */
static void ev_explosions(int slot) {
    ev_sfx(BSFX_PLAYER_ATTACK);       /* the "cha" (twice for spells: BtlMag_LoadEnemyDefenderStats) */
    int x, y, span, count;
    switch (g_btl.battle_type) {
    case 0:
        x = lut_ExplosionCoords_9Small[slot * 2]; y = lut_ExplosionCoords_9Small[slot * 2 + 1];
        span = 0x18; count = 6;
        break;
    case 1:
        x = lut_ExplosionCoords_4Large[slot * 2]; y = lut_ExplosionCoords_4Large[slot * 2 + 1];
        span = 0x28; count = 8;
        break;
    case 2:
        if (slot < 2) {      /* the large slots use the 4-large spots */
            x = lut_ExplosionCoords_4Large[slot * 2]; y = lut_ExplosionCoords_4Large[slot * 2 + 1];
            span = 0x28; count = 8;
        } else {             /* (15 rounds: the disassembly wonders if that's BUGGED) */
            x = lut_ExplosionCoords_6Small[(slot - 2) * 2]; y = lut_ExplosionCoords_6Small[(slot - 2) * 2 + 1];
            span = 0x18; count = 0x0F;
        }
        break;
    default:                 /* fiend / Chaos */
        x = 0x20; y = 0x40; span = -1; count = 0x14;
        break;
    }
    int x1 = (span < 0) ? 0x60 : x + span, y1 = (span < 0) ? 0x78 : y + span;

    BtlEvent* e = ev_add(EV_EXPLODE);
    int start = g_btl.expl_n;
    e->a = (uint8_t)start;
    e->b = (uint8_t)(start >> 8);
    e->c = (uint8_t)count;
    for (int k = 0; k < count * 3; k++) {
        uint8_t tile = (uint8_t)(0xF4 + Rng_RandAX(0, 2) * 4);
        uint8_t ex = Rng_RandAX((uint8_t)x, (uint8_t)x1);
        uint8_t ey = Rng_RandAX((uint8_t)y, (uint8_t)y1);
        if (g_btl.expl_n < BTL_EXPL_MAX) {
            g_btl.expl_pool[g_btl.expl_n][0] = tile;
            g_btl.expl_pool[g_btl.expl_n][1] = ex;
            g_btl.expl_pool[g_btl.expl_n][2] = ey;
            g_btl.expl_n++;
        }
    }
}

/* BattleScreenShake: 6 times, 2 frames then a random scroll of 0-3 in x
 * and y (rolled now).  The sixth is never seen. */
static void ev_shake(void) {
    ev_sfx(BSFX_ENEMY_ATTACK);        /* the "boom bash" */
    BtlEvent* e = ev_add(EV_SHAKE);
    for (int k = 0; k < 6; k++) {
        uint8_t sx = Rng_Battle() & 3;
        uint8_t sy = Rng_Battle() & 3;
        if (k < 5) { e->text.t[k * 2] = sx; e->text.t[k * 2 + 1] = sy; }
    }
}

/* ---- Playing the script ---- */

/* Play one frame of event `e` (`f` frames in).  @return 1 when it is over
 * (without using up this frame), 0 if it used the frame. */
static int ev_step(const BtlEvent* e, int f, const SaveData* save, const InputState* in) {
    switch (e->op) {
    case EV_SHOW:
        g_btl.cbox[e->box] = e->text;
        g_btl.cbox_shown |= (uint8_t)(1 << e->box);
        return 1;
    case EV_HIDE_FROM:
        g_btl.cbox_shown &= (uint8_t)((1 << e->box) - 1);
        return 1;
    case EV_WAIT:
        return f >= g_btl.respond_delay;
    case EV_WAIT_INPUT:
        return f > 0 && in && in->pressed;
    case EV_STATUS:
        for (int i = 0; i < 4; i++) {
            g_btl.disp_hp[i] = (uint16_t)(e->text.t[i * 3] | (e->text.t[i * 3 + 1] << 8));
            g_btl.disp_ail[i] = e->text.t[i * 3 + 2];
        }
        return 1;
    case EV_VARPAL: {
        /* Shades c, c-$10, c-$20 (white for magic). */
        g_btl.varpal[1] = e->a & 0x3F;
        g_btl.varpal[2] = (uint8_t)(e->a - 0x10) & 0x3F;
        g_btl.varpal[3] = e->b ? 0x30 : (uint8_t)(e->a - 0x20) & 0x3F;
        return 1;
    }
    case EV_WALK: {
        /* CharacterWalkAnimation: 8 steps of 2 pixels, one every 2 frames,
         * alternating the walk pose every 4. */
        int p = e->a;
        if (f < 16) {
            if (!(f & 1)) {
                int k = 8 - f / 2;
                g_btl.chr_pose[p] = (k & 2) ? POSE_WALK : POSE_STAND;
                g_btl.chr_x[p] = (uint8_t)(g_btl.chr_x[p] + (e->b ? 2 : -2));
            }
            return 0;
        }
        if (!e->c) return 1;
        g_btl.chr_pose[p] = POSE_NATURAL;           /* SetNaturalPose, 2 frames */
        return f >= 18;
    }
    case EV_STRIKE: {
        int p = e->a;
        int per = (e->c && !e->d) ? 6 : 2;          /* DoMagicFlash adds 4 frames */
        if (f < 8 * per) {
            int it = f / per, sub = f % per;
            if (sub == 0) {
                int k = 8 - it, a_frame = (k & 2) != 0;
                int x = g_btl.chr_x[p], y = CHR_Y(p);
                g_btl.atk_tile = e->b;
                g_btl.atk_magic = e->c;
                g_btl.atk_on = !e->d;
                if (e->c) {                          /* casting: cheer, the spell sprite */
                    g_btl.chr_pose[p] = POSE_CHEER;
                    g_btl.atk_x = x - 16;
                    g_btl.atk_y = y;
                    g_btl.atk_frame_b = !a_frame;
                    g_btl.atk_hidell = 0;
                } else if (a_frame) {                /* swing forward */
                    g_btl.chr_pose[p] = POSE_ATTACK_F;
                    g_btl.atk_x = x - 16;
                    g_btl.atk_y = y;
                    g_btl.atk_frame_b = 0;
                    g_btl.atk_hidell = 0;
                } else {                             /* weapon back behind the head */
                    g_btl.chr_pose[p] = POSE_ATTACK_B;
                    g_btl.atk_x = x + 8;
                    g_btl.atk_y = y - 8;
                    g_btl.atk_frame_b = 1;
                    g_btl.atk_hidell = 1;
                }
            }
            /* DoMagicFlash: a normal frame, two with the spell's color as
             * the background (white shown as grey), then normal again. */
            uint8_t fc = g_btl.varpal[1] == 0x20 ? 0x10 : g_btl.varpal[1];
            g_btl.bg_flash = (per == 6 && (sub == 1 || sub == 2)) ? fc : 0;
            return 0;
        }
        g_btl.atk_on = 0;                            /* sprite cleared, 1 frame */
        g_btl.bg_flash = 0;
        if (f == 8 * per) return 0;
        g_btl.chr_pose[p] = POSE_STAND;              /* standing, 2 frames */
        return f >= 8 * per + 3;
    }
    case EV_EXPLODE: {
        int start = e->a | (e->b << 8), n = e->c * 3;
        if (f < n) {
            int slot = f % 3, k = start + f;
            if (k < BTL_EXPL_MAX) {
                g_btl.expl_on[slot] = 1;
                g_btl.expl_tile[slot] = g_btl.expl_pool[k][0];
                g_btl.expl_x[slot] = g_btl.expl_pool[k][1];
                g_btl.expl_y[slot] = g_btl.expl_pool[k][2];
            }
            return 0;
        }
        memset(g_btl.expl_on, 0, sizeof(g_btl.expl_on));
        return f > n;                                /* 1 frame to clear them */
    }
    case EV_SFX:
        Sfx_BattlePlay(e->a);
        return 1;
    case EV_SHAKE:
        /* BattleScreenShake only updates the music, so the boom's first
         * noise block lasts 13 frames longer than its data says. */
        if (f >= 13) { g_btl.shake_x = g_btl.shake_y = 0; return 1; }
        s_sfx_hold = 1;
        if (f >= 2 && f < 12) {
            int k = f / 2 - 1;
            g_btl.shake_x = (int8_t)e->text.t[k * 2];
            g_btl.shake_y = (int8_t)e->text.t[k * 2 + 1];
        } else {
            g_btl.shake_x = g_btl.shake_y = 0;
        }
        return 0;
    case EV_FLASH_CHAR: {
        uint8_t bit = (uint8_t)(1 << e->a);
        if (f >= 16) { g_btl.chr_hidden &= (uint8_t)~bit; return 1; }
        if ((16 - f) & 2) g_btl.chr_hidden |= bit;
        else g_btl.chr_hidden &= (uint8_t)~bit;
        return 0;
    }
    case EV_ERASE: {
        int rows = btl_erase_rows(e->a);
        if (f >= rows) {
            if (rows) g_btl.enemy_vis[e->a] = 0;
            return 1;
        }
        g_btl.enemy_erased[e->a] = (uint8_t)(f + 1);  /* a row a frame */
        return 0;
    }
    case EV_CHEER:
        /* PlayFanfareAndCheer: 64 x 2 frames, cheering / standing every 8. */
        if (f == 0) g_music_track = g_music_followup = MUSIC_VICTORY;
        s_sfx_hold = 1;
        if (f < 128) {
            int k = 64 - f / 2;
            for (int i = 0; i < 4; i++) g_btl.chr_pose[i] = (k & 8) ? POSE_CHEER : POSE_STAND;
            return 0;
        }
        for (int i = 0; i < 4; i++) g_btl.chr_pose[i] = POSE_NATURAL;
        return f >= 130;
    case EV_CHAOS_DEATH: {
        /* ChaosDeath: 110 frames for the fanfare's jingle; the row table
         * filled with 256 rolls; then 8 x 256 frames, each erasing one
         * pixel row of a random BG tile $00-$79 (Chaos and the backdrop)
         * and shaking the screen with the tile numbers; then 2 seconds.
         * (The noise rumble comes with the audio.) */
        s_sfx_hold = 1;
        if (f < 110) return 0;
        if (f == 110) g_music_track = g_music_followup = 0x80;   /* stop the music */
        int k = f - 110;
        if (k == 0) {
            Sfx_ChaosRumbleStart();                     /* only the noise, at full volume */
            for (int i = 0; i < 256; i++) g_btl.chaos_rows[i] = Rng_Battle();
        }
        if ((k & 0xFF) == 0x80 && k < 8 * 256) Sfx_ChaosRumble();   /* halfway through each row */
        if (k && (k & 0xFF) == 0 && k <= 8 * 256) Sfx_ChaosRumble(); /* and after it */
        if (k < 8 * 256) {
            if (k && (k & 0xFF) == 0)                   /* next pixel row for every tile */
                for (int i = 0; i < 256; i++) g_btl.chaos_rows[i]++;
            uint8_t tile = Rng_RandAX(0, 0x79);
            g_btl.bg_erased[tile] |= (uint8_t)(1 << (g_btl.chaos_rows[tile] & 7));
            g_btl.shake_x = (int8_t)(g_btl.chaos_prev & 3);
            g_btl.shake_y = (int8_t)(tile & 3);
            g_btl.chaos_prev = tile;
            return 0;
        }
        g_btl.shake_x = g_btl.shake_y = 0;
        if (k < 8 * 256 + 120) return 0;
        Sfx_ChaosRumbleStop();                          /* the noise stops */
        return 1;
    }
    }
    (void)save;
    return 1;
}

/* Play the script for one frame.  @return 1 once it has run to the end. */
static int ev_run(const SaveData* save, const InputState* in) {
    while (g_btl.ev_i < g_btl.ev_n) {
        if (!ev_step(&g_btl.ev[g_btl.ev_i], g_btl.ev_frame, save, in)) {
            g_btl.ev_frame++;
            return 0;
        }
        g_btl.ev_i++;
        g_btl.ev_frame = 0;
    }
    return 1;
}

/* ---- Characters and enemy slots ---- */

/* Port of GetRandomPlayerTarget: slot 0 half the time, slot 1 a quarter,
 * slots 2 and 3 an eighth each, rerolled while the target is dead or stone. */
static int random_living_char(const SaveData* save) {
    int any = 0;
    for (int i = 0; i < 4; i++)
        if (!(save->characters[i].ailments & (BTL_AIL_DEAD | BTL_AIL_STONE))) any = 1;
    if (!any) return -1;
    for (;;) {
        uint8_t r = Rng_Battle();
        int t = (r < 0x20) + (r < 0x40) + (r < 0x80);
        if (!(save->characters[t].ailments & (BTL_AIL_DEAD | BTL_AIL_STONE))) return t;
    }
}

/* Is character `c` out of the fight (dead or stone)?  The original tracks
 * this by ailments (btl_drawflags), not HP: RUB leaves HP untouched. */
static int char_out(const SaveData* save, int c) {
    return (save->characters[c].ailments & (BTL_AIL_DEAD | BTL_AIL_STONE)) != 0;
}

/* Does enemy slot `e` still hold an enemy?  The original clears the slot's
 * id when an enemy dies, is stoned or runs; all of those set DEAD or STONE. */
int btl_enemy_exists(int e) {
    return e >= 0 && e < MAX_ENEMIES && g_btl.enemy[e].present &&
           !(g_btl.enemy[e].ail & (BTL_AIL_DEAD | BTL_AIL_STONE));
}

/* Can character `c` be given a command?  (Not dead, stone, asleep or
 * paralyzed: those get no command and use their turn to recover.) */
static int can_input(const SaveData* save, int c) {
    return !(save->characters[c].ailments & (BTL_AIL_DEAD | BTL_AIL_STONE | BTL_AIL_SLEEP | BTL_AIL_STUN));
}

static int first_living_char(const SaveData* save) {
    for (int i = 0; i < 4; i++)
        if (can_input(save, i)) return i;
    return -1;
}

static int first_living_enemy(void) {
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (btl_enemy_exists(i)) return i;
    return -1;
}

static int all_enemies_dead(void) {
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (btl_enemy_exists(i)) return 0;
    return 1;
}

/* CheckForBattleEnd: the party is defeated when all four are dead or stone. */
static int all_party_dead(const SaveData* save) {
    for (int i = 0; i < 4; i++)
        if (!char_out(save, i)) return 0;
    return 1;
}

/* SetNaturalPose: crouching when poisoned, paralyzed, asleep or below a
 * quarter of max HP, else standing (from what the status column shows). */
uint8_t btl_natural_pose(const SaveData* save, int p) {
    if (g_btl.disp_ail[p] & (BTL_AIL_POISON | BTL_AIL_STUN | BTL_AIL_SLEEP)) return POSE_CROUCH;
    uint16_t quarter = save->characters[p].max_hp >> 2;
    uint16_t hp = g_btl.disp_hp[p];
    if (hp >> 8) return POSE_STAND;                  /* (only the low bytes are compared) */
    return ((hp & 0xFF) < (quarter & 0xFF)) ? POSE_CROUCH : POSE_STAND;
}

/* Spell id (0..63) known by character `p` at (level row, slot col), or -1. */
int btl_spell_at(const SaveData* save, int p, int row, int col) {
    int pos = save->character_magic[p].spells[row * 4 + col];   /* 1..8, or 0 = empty */
    return pos ? row * 8 + (pos - 1) : -1;
}

/* Equipment slot `slot` (0-3 weapons, 4-7 armor) of character `p`: the
 * item id (TCITYPE), or 0 if the slot is empty. */
uint8_t btl_equip_item(const SaveData* save, int p, int slot) {
    const CharacterStats* c = &save->characters[p];
    uint8_t e = (slot < 4 ? c->weapons[slot] : c->armor[slot - 4]) & 0x7F;
    if (!e) return 0;
    return (uint8_t)((slot < 4 ? 0x1C : 0x44) + e - 1);   /* TCITYPE_WEPSTART / ARMSTART */
}

/* Where each enemy slot is drawn, by formation type (the nametable addresses
 * of DrawFormation_9Small / _4Large / _Mix and the fiend/Chaos TSA origins). */
void btl_enemy_rect(int slot, int* x, int* y, int* w, int* h) {
    static const int SMALL_ROW_Y[3] = { 80, 48, 112 };    /* middle, top, bottom */
    switch (g_btl.battle_type) {
    case 1:                                           /* 4 large */
        *x = 16 + (slot / 2) * 64; *y = 48 + (slot & 1) * 48; *w = *h = 48;
        break;
    case 2:                                           /* 2 large + 6 small */
        if (slot < 2) { *x = 16; *y = 48 + slot * 48; *w = *h = 48; }
        else { *x = 64 + ((slot - 2) / 3) * 32; *y = SMALL_ROW_Y[(slot - 2) % 3]; *w = *h = 32; }
        break;
    case 3:  *x = 32; *y = 64; *w = *h = 64; break;   /* fiend */
    case 4:  *x = 16; *y = 48; *w = 112; *h = 96; break;   /* Chaos */
    default:                                          /* 9 small */
        *x = 16 + (slot / 3) * 32; *y = SMALL_ROW_Y[slot % 3]; *w = *h = 32;
        break;
    }
}

/* DrawEnemyEffect's erase: 4 rows for a small enemy, 6 for a large one, 12
 * from $20C2 for a fiend; Chaos isn't erased (he dissolves). */
int btl_erase_rows(int slot) {
    switch (g_btl.battle_type) {
    case 1:  return 6;
    case 2:  return slot < 2 ? 6 : 4;
    case 3:  return 12;
    case 4:  return 0;
    default: return 4;
    }
}

/* EnemyTargetMenu_*: the cursor visits the slots in lut_EnemyIndex* order.
 * (Fiend and Chaos battles have one target.) */
int btl_target_count(void) {
    switch (g_btl.battle_type) {
    case 0:  return 9;
    case 1:  return 4;
    case 2:  return 8;
    default: return 1;
    }
}

int btl_target_slot(int i) {
    int slot;
    switch (g_btl.battle_type) {
    case 0:  slot = lut_EnemyIndex9Small[i]; break;
    case 1:  slot = lut_EnemyIndex4Large[i]; break;
    case 2:  slot = lut_EnemyIndexMix[i]; break;
    default: slot = 0; break;
    }
    return btl_enemy_exists(slot) ? slot : -1;
}

/* BattleTarget_Down / _Up: the next cursor index (wrapping) holding an enemy. */
static int target_step(int i, int dir) {
    int n = btl_target_count();
    for (int k = 0; k < n; k++) {
        i = (i + dir + n) % n;
        if (btl_target_slot(i) >= 0) return i;
    }
    return i;
}

/* RebuildEnemyRoster: the distinct enemy kinds still standing, in slot
 * order (up to 4). */
static void rebuild_roster(void) {
    memset(g_btl.roster, 0xFF, sizeof(g_btl.roster));
    int n = 0;
    for (int e = 0; e < MAX_ENEMIES && n < 4; e++) {
        if (!btl_enemy_exists(e)) continue;
        int dup = 0;
        for (int k = 0; k < n; k++) dup |= g_btl.roster[k] == g_btl.enemy[e].id;
        if (!dup) g_btl.roster[n++] = g_btl.enemy[e].id;
    }
}

/* ---- Formation -> enemy group ---- */

/* Fill enemy slot `slot` (btl_enemyIDs / btl_enemygfxplt, then the stats
 * LoadEnemyStats copies from ROM). */
static void place_enemy(int slot, uint8_t id, uint8_t gfx, uint8_t pal) {
    const EnemyData* ed = &enemy_table[id];
    BattleEnemy* en = &g_btl.enemy[slot];
    memset(en, 0, sizeof(*en));
    en->present = 1;
    en->id = id;
    en->gfx = gfx;
    en->pal = pal;
    en->maxhp = ed->max_hp;
    en->hp = ed->max_hp;
    en->absorb = ed->absorb;
    en->numhitsmult = 1;
    en->morale = ed->morale;
    en->evade = ed->evade;
    en->strength = ed->damage;
    g_btl.enemy_vis[slot] = 1;
    g_btl.enemy_count++;
}

/* A group's random size: RandAX(min, max) from the quantity byte. */
static int group_size(uint8_t qty) {
    return Rng_RandAX((uint8_t)(qty >> 4), (uint8_t)(qty & 0x0F));
}

/** Port of PrepareEnemyFormation: build the enemy slots from a formation
 *  byte (bit 7 = the "B" formation: groups 0-1 with the B quantities). */
static void build_enemies(int formation) {
    const BattleFormation* f = &battle_formation_table[formation & 0x7F];
    memset(g_btl.enemy, 0, sizeof(g_btl.enemy));
    g_btl.enemy_count = 0;
    g_btl.no_run = f->flags & 0x01;
    g_btl.battle_type = f->type_pattern >> 4;
    g_btl.enemy_page  = f->type_pattern & 0x0F;
    g_btl.enemy_pal0  = f->palette_0;
    g_btl.enemy_pal1  = f->palette_1;
    g_btl.fiend_gfx   = f->gfx_assign;

    uint8_t ids[4], qty[4];
    for (int g = 0; g < 4; g++) { ids[g] = f->enemy_ids[g]; qty[g] = f->qty_a[g]; }
    if (formation & 0x80) {
        qty[0] = f->qty_b[0]; qty[1] = f->qty_b[1];
        ids[2] = ids[3] = qty[2] = qty[3] = 0;
    }

    switch (g_btl.battle_type) {
    case 2: {   /* PrepareEnemyFormation_Mix: large in slots 0-1, small from 2 */
        int large = 2, small = 6, smallpos = 2;
        for (int g = 0; g < 4; g++) {
            uint8_t gfx = (f->gfx_assign >> (g * 2)) & 3;
            /* BUGGED in the original: group 2 reads palette bit 6 (group
             * 1's) instead of bit 5. */
            uint8_t pal = (f->flags >> (g == 2 ? 6 : 7 - g)) & 1;
            if (gfx & 1) {
                if (!large) continue;
                for (int n = group_size(qty[g]); n > 0; n--) {
                    place_enemy(large == 1 ? 1 : 0, ids[g], gfx, pal);
                    if (--large == 0) break;
                }
            } else {
                if (!small) continue;
                for (int n = group_size(qty[g]); n > 0; n--) {
                    place_enemy(smallpos++, ids[g], gfx, pal);
                    if (--small == 0) break;
                }
            }
        }
        break;
    }
    case 3:     /* Fiend / Chaos: the first enemy alone */
    case 4:
        place_enemy(0, ids[0], 0, 0);
        break;
    default: {  /* PrepareEnemyFormation_SmallLarge: 9 small or 4 large, in order */
        int slots[2] = { g_btl.battle_type == 1 ? 0 : 9, g_btl.battle_type == 1 ? 4 : 0 };
        for (int g = 0; g < 4; g++) {
            uint8_t gfx = (f->gfx_assign >> (g * 2)) & 3;
            uint8_t pal = (f->flags >> (7 - g)) & 1;
            int n = group_size(qty[g]);       /* rolled even for empty groups */
            for (; n > 0 && slots[gfx & 1]; n--) {
                slots[gfx & 1]--;
                place_enemy(g_btl.enemy_count, ids[g], gfx, pal);
            }
        }
        break;
    }
    }
}

int Battle_LandDomain(int tile_x, int tile_y) {
    return (((tile_y >> 5) & 7) << 3) | ((tile_x >> 5) & 7);
}

/* Each domain lists 8 formations; the slot is weighted by lut_FormationWeight. */
int Battle_PickFormation(int domain) {
    domain &= 0x7F;
    int slot = lut_FormationWeight[Rng_FormationRoll() & 0x3F];
    uint16_t v = battle_domain_table[domain * 4 + (slot >> 1)];
    return (slot & 1) ? (v >> 8) : (v & 0xFF);
}

/* InputCharacterBattleCommand: the character walks forward, then the
 * command box comes up. */
static void begin_commands(void) {
    rebuild_roster();
    g_btl.cmd_col = g_btl.cmd_row = 0;
    g_btl.phase = BPHASE_COMMAND;
    ev_walk(g_btl.cur_char, 0, 1);
}

void Battle_Start(SaveData* save, int formation, int ow_tile) {
    g_music_track = g_music_followup = MUSIC_BATTLE;   /* PrepBattleVarsAndEnterBattle */
    memset(&g_btl, 0, sizeof(g_btl));
    g_btl.formation = formation;
    g_btl.respond_delay = lut_RespondDelay[s_respond_rate];

    /* PrepBattleVarsAndEnterBattle: ailments become in-battle bits for the
     * battle (ConvertOBAilmentToIB), and the in-battle stats are loaded. */
    for (int i = 0; i < 4; i++) {
        save->characters[i].ailments = Btl_AilmentObToIb(save->characters[i].ailments);
        Btl_LoadCharStats(save, i, &g_btl.chars[i]);
        g_btl.disp_hp[i] = save->characters[i].cur_hp;
        g_btl.disp_ail[i] = save->characters[i].ailments;
        g_btl.chr_x[i] = CHR_X;
        g_btl.chr_pose[i] = POSE_NATURAL;
    }
    memcpy(g_btl.varpal, btl_sprite_pals[2], 4);

    /* Backdrop from the overworld tile (bank_0F lut_BtlBackdrops). */
    int b = backdrop_table[ow_tile & 0x7F];
    g_btl.backdrop = (b == 0xFF) ? 0 : b;
    for (int i = 0; i < 4; i++)
        g_btl.backdrop_pal[i] = backdrop_palettes[g_btl.backdrop][i];

    build_enemies(formation);
    /* Battle_AfterFadeIn: the potions DRINK can claim. */
    g_btl.potions[0] = save->items[BTL_ITEM_HEAL];
    g_btl.potions[1] = save->items[BTL_ITEM_HEAL + 1];
    for (int i = 0; i < 4; i++) { g_btl.char_cmd[i] = BTLC_NONE; g_btl.char_target[i] = -1; }
    if (g_btl.enemy_count == 0) {
        battle_over(save, BATTLE_RAN);
        return;
    }

    /* Battle_AfterFadeIn: unless the formation can't be run from,
     *   S = (leader's Agility + Luck, 8-bit) / 8
     *   V = random[S, 100] + S - surprise rate   (floored at 0, low byte)
     * V < 11: the monsters strike first; V >= 90: the party does. */
    if (!g_btl.no_run) {
        const CharacterStats* lead = &save->characters[0];
        uint8_t st = (uint8_t)(lead->agil + lead->luck) >> 3;
        unsigned v = st + Rng_RandAX(st, 100);
        const BattleFormation* f = &battle_formation_table[formation & 0x7F];
        v = (v > f->surprise_rate) ? v - f->surprise_rate : 0;
        v &= 0xFF;
        if (v < 11) {
            g_btl.surprised = 1;
            ev_msg_clear_all(BTLMSG_SURPRISED);
        } else if (v >= 90) {
            g_btl.striking_first = 1;
            ev_msg_clear_all(BTLMSG_STRIKEFIRST);
        }
    }
    if (g_btl.surprised || g_btl.striking_first) {
        g_btl.phase = BPHASE_OPENING;
    } else {
        g_btl.cur_char = 0;
        begin_commands();
    }
}

/* ---- Physical attacks (DoPhysicalAttack's boxes) ---- */

/* The result boxes of DoPhysicalAttack, after the attacker and defender
 * boxes went up.  Ends with RespondDelay_ClearCombatBoxes. */
static void physical_events(const BtlAttackResult* r, int defender_is_player) {
    if (r->ineffective) {
        ev_msg_clear_all(BTLMSG_INEFFECTIVE);
        return;
    }
    /* PrintPlayerAilmentMessageFromAttack, high bit first. */
    static const uint8_t AIL_MSG[8] = {
        0, BTLMSG_STOPPED, BTLMSG_POISONED, BTLMSG_DARKNESS,
        BTLMSG_PARALYZED_A, BTLMSG_ASLEEP, BTLMSG_SILENCED, BTLMSG_CONFUSED,
    };
    for (int bit = 7; bit >= 1; bit--)
        if (r->ailments_added & (1 << bit)) ev_msg(AIL_MSG[bit]);
    if (r->ailment_death) {
        ev_msg(BTLMSG_SLAIN);                 /* no damage shown */
        ev_wait();
        ev_clear_all();
        return;
    }

    BtlText t;
    if (r->hits >= 2) {                       /* "N Hits!" in the attack box */
        t.len = 0;
        btl_text_add_number(&t, (unsigned)r->hits);
        btl_text_add_raw(&t, Text_BattleMessage(BTLMSG_HITS), BTL_TEXT_MAX);
        ev_show(CBOX_ATTACK, &t);
    }
    if (r->damage) {                          /* "###DMG" or "Missed!" */
        t.len = 0;
        btl_text_add_number(&t, r->damage);
        btl_text_add_raw(&t, Text_BattleMessage(BTLMSG_DMG), BTL_TEXT_MAX);
    } else {
        text_msg(&t, BTLMSG_MISSED);
    }
    ev_show(CBOX_DAMAGE, &t);
    if (r->crits) {
        ev_show_msg(BTLMSG_CRITICALHIT);
        ev_wait();
    }
    if (r->killed) ev_show_msg(defender_is_player ? BTLMSG_SLAIN : BTLMSG_TERMINATED);
    ev_wait();
    ev_clear_all();
}

/* Port of EnemyAttackPlayer_Physical: enemy `eidx` attacks character `target`. */
static void enemy_physical_attack(SaveData* save, int eidx, int target) {
    BattleEnemy* en = &g_btl.enemy[eidx];
    const EnemyData* rom = &enemy_table[en->id];
    CharacterStats* c = &save->characters[target];
    const BtlCharStats* ib = &g_btl.chars[target];

    /* The screen shakes, then the target blinks (unless dead or stone). */
    ev_shake();
    if (!(c->ailments & (BTL_AIL_DEAD | BTL_AIL_STONE))) ev_flash_char(target);

    BtlAttacker a = {
        .strength    = en->strength,
        .category    = rom->category,
        /* BUGGED in the original: the enemy's elemental WEAKNESS is used as
         * its attack element (so e.g. FrWolves attack with fire). */
        .element     = rom->elem_weakness,
        .hitrate     = rom->hit_rate,
        .numhitsmult = en->numhitsmult,
        .numhits     = rom->num_hits,
        .critrate    = rom->crit_rate,
        .ailments    = en->ail,
        .attackailment = rom->attack_ailment,
    };
    BtlDefender d = {
        .ailments = c->ailments, .category = ib->category, .elemweakness = ib->elemweak,
        .evade = ib->evade, .absorb = ib->absorb, .magdef = ib->magdef,
        .elemresist = ib->elemresist, .hp = c->cur_hp,
    };
    BtlAttackResult r;
    Btl_PhysicalAttack(&a, &d, 1, &r);
    c->cur_hp = d.hp;
    c->ailments = d.ailments;
    /* RemoveDefenderAction: put to sleep, paralyzed, stoned or killed by the
     * attack, the character loses this round's command. */
    if (r.ailments_added & (BTL_AIL_SLEEP | BTL_AIL_STUN | BTL_AIL_STONE | BTL_AIL_DEAD))
        g_btl.char_cmd[target] = BTLC_NONE;
    physical_events(&r, 1);
}

/** Port of PlayerAttackEnemy_Physical: character `p` attacks enemy `eidx`. */
static void char_attack_enemy(SaveData* save, int p, int eidx) {
    BattleEnemy* en = &g_btl.enemy[eidx];
    const EnemyData* rom = &enemy_table[en->id];
    const BtlCharStats* ib = &g_btl.chars[p];

    /* Walk forward, swing the weapon (none when unarmed), walk back, then
     * explosions on the enemy (unless already dead or stone). */
    ev_varpal(ib->wepplt, 0);
    ev_walk_and_strike(p, ib->wepgfx, 0, ib->wepgfx == 0);
    if (!(en->ail & (BTL_AIL_DEAD | BTL_AIL_STONE))) ev_explosions(eidx);

    BtlAttacker a;
    Btl_PlayerAttacker(ib, save->characters[p].ailments, &a);
    BtlDefender d = {
        .ailments = en->ail, .category = rom->category,
        /* BUGGED in the original: the weakness comes from ROM, not the enemy's
         * battle stats, so spells that change it (XFER) don't affect attacks. */
        .elemweakness = rom->elem_weakness,
        .evade = en->evade, .absorb = en->absorb, .hp = en->hp,
    };
    BtlAttackResult r;
    Btl_PhysicalAttack(&a, &d, 0, &r);
    en->hp = d.hp;
    en->ail = d.ailments;
    physical_events(&r, 0);
}

/* ---- Enemy AI (bank_0C Enemy_DoAi) ---- */

/* Battle_CastMagicOnRandEnemy's pick: random slots [0,8] until one holds an
 * enemy. */
static int random_living_enemy(void) {
    if (all_enemies_dead()) return -1;
    for (;;) {
        int e = Rng_RandAX(0, 8);
        if (btl_enemy_exists(e)) return e;
    }
}

/* EnemyAi_ShouldPerformAction: rand[0,128] < rate. */
static int ai_should_perform(uint8_t rate) {
    return Rng_RandAX(0, 0x80) < rate;
}

/* Enemy_DoAi's choice: 1 and the spell/attack id if the enemy casts, 0 for
 * a physical attack.  Spells cycle through the AI's 8 slots, special
 * attacks through its 4 ($42+), an empty slot restarting the cycle. */
static int enemy_pick_action(int eidx, uint8_t* effect_id) {
    BattleEnemy* en = &g_btl.enemy[eidx];
    uint8_t ai = enemy_table[en->id].ai_script;
    if (ai == ENEMY_AI_NO_SCRIPT || ai >= ENEMY_AI_COUNT) return 0;
    const EnemyAi* a = &enemy_ai_table[ai];

    if (ai_should_perform(a->magic_rate)) {
        for (int tries = 0; tries < 16; tries++) {
            int pos = en->aimagpos & 7;
            en->aimagpos = (uint8_t)(pos + 1);
            if (a->spells[pos] != 0xFF) { *effect_id = a->spells[pos]; return 1; }
            en->aimagpos = 0;
        }
        return 0;
    }
    if (ai_should_perform(a->special_rate)) {
        for (int tries = 0; tries < 8; tries++) {
            int pos = en->aiatkpos & 3;
            en->aiatkpos = (uint8_t)(pos + 1);
            if (a->special[pos] != 0xFF) { *effect_id = (uint8_t)(a->special[pos] + 0x42); return 1; }
            en->aiatkpos = 0;
        }
    }
    return 0;
}

/* ---- Magic (bank_0C Player_DoMagicEffect / Enemy_DoMagicEffect) ---- */

/* Battle_PrepareMagic: the spell's effect parameters.  0 if it has no
 * battle effect ($FF = an item with no spell). */
static int btl_prepare_magic(uint8_t attackid, BtlMagic* out) {
    if (attackid == 0xFF || attackid >= MAGIC_TOTAL_COUNT) return 0;
    const MagicData* md = &magic_table[attackid];
    if (!md->effect) return 0;
    *out = (BtlMagic){ md->effect, md->element, md->hit_rate, md->effectivity };
    return 1;
}

/* BtlMag_LoadPlayerDefenderStats: HP, ailments and the "category" from the
 * out-of-battle stats, the rest from the in-battle ones. */
static void mag_load_player(const SaveData* save, int i, BtlMagDefender* d) {
    const CharacterStats* c = &save->characters[i];
    const BtlCharStats* ib = &g_btl.chars[i];
    *d = (BtlMagDefender){ 0 };
    /* BUGGED in the original: the class is used as the category, so the
     * classes with bit 3 set (Master and the three Wizards) count as undead
     * for HARM. */
    d->category = c->class;
    d->ailments = c->ailments;
    d->hp = c->cur_hp;
    d->hpmax = c->max_hp;
    d->numhitsmult = ib->numhitsmult;
    d->absorb = ib->absorb;
    d->evade = ib->evade;
    d->elemweakness = ib->elemweak;
    d->elemresist = ib->elemresist;
    d->magdef = ib->magdef;
    /* BUGGED in the original: strength isn't loaded (or saved), so TMPR and
     * SABR do nothing to characters. */
}

/* BtlMag_SavePlayerDefenderStats */
static void mag_save_player(SaveData* save, int i, const BtlMagDefender* d) {
    CharacterStats* c = &save->characters[i];
    BtlCharStats* ib = &g_btl.chars[i];
    c->ailments = d->ailments;
    c->mag_def = d->magdef;          /* written to the permanent stats (never changed) */
    c->cur_hp = d->hp;
    ib->elemresist = d->elemresist;
    ib->numhitsmult = d->numhitsmult;
    ib->absorb = d->absorb;
    ib->evade = d->evade;
}

/* BtlMag_LoadEnemyDefenderStats: a mix of battle RAM and ROM stats. */
static void mag_load_enemy(int e, BtlMagDefender* d) {
    const BattleEnemy* en = &g_btl.enemy[e];
    const EnemyData* rom = &enemy_table[en->id];
    *d = (BtlMagDefender){ 0 };
    d->hp = en->hp;
    d->hpmax = rom->max_hp;
    d->numhitsmult = en->numhitsmult;
    d->ailments = en->ail;
    d->category = rom->category;
    d->magdef = rom->mag_def;
    d->elemweakness = rom->elem_weakness;
    /* BUGGED in the original: resistance comes from ROM and isn't saved, so
     * XFER and the resist spells do nothing to enemies. */
    d->elemresist = rom->elem_resist;
    d->morale = en->morale;
    d->absorb = rom->absorb;         /* from ROM, but saved to RAM */
    d->evade = en->evade;
    d->strength = en->strength;
}

/* BtlMag_SaveEnemyDefenderStats */
static void mag_save_enemy(int e, const BtlMagDefender* d) {
    BattleEnemy* en = &g_btl.enemy[e];
    en->hp = d->hp;
    en->absorb = d->absorb;
    en->numhitsmult = d->numhitsmult;
    en->ail = d->ailments;
    en->morale = d->morale;
    en->evade = d->evade;
    en->strength = d->strength;
}

/* The effect on one defender whose box is up and whose stats were loaded
 * (with their explosions / blink): Battle_CastMagicOnEnemy /
 * _OnPlayer_NoLoad, then BtlMag_HandleAilmentChanges' bookkeeping. */
static void mag_effect_on(SaveData* save, uint8_t attackid, const BtlMagic* m, int is_player, int idx) {
    BtlMagDefender d;
    if (is_player) mag_load_player(save, idx, &d);
    else           mag_load_enemy(idx, &d);

    BtlMagResult r;
    Btl_MagicEffect(attackid, m, &d, is_player, &r);
    g_btl.spell_connected = r.connected;

    if (is_player) mag_save_player(save, idx, &d);
    else           mag_save_enemy(idx, &d);

    if (r.show_damage) {                      /* DrawDamageCombatBox */
        BtlText t = { 0 };
        btl_text_add_number(&t, r.damage);
        btl_text_add_raw(&t, Text_BattleMessage(BTLMSG_DMG), BTL_TEXT_MAX);
        ev_show(CBOX_DAMAGE, &t);
    }
    for (int i = 0; i < r.nmsgs; i++) {
        if (r.msgs[i]) ev_msg(r.msgs[i]);
        else ev_wait();                       /* a connected spell with no message */
    }

    /* A slain/stoned enemy is erased and leaves the battle; an immobilized
     * character loses their command for the round.  Either is recorded as
     * the turn's "defender" result, for the battle-end check. */
    if (!is_player) {
        if (d.ailments & (BTL_AIL_DEAD | BTL_AIL_STONE)) {
            ev_erase(idx);
            g_btl.fakeout_ail = d.ailments;
        }
    } else {
        uint8_t imm = d.ailments & (BTL_AIL_DEAD | BTL_AIL_STONE | BTL_AIL_STUN | BTL_AIL_SLEEP);
        if (imm) {
            g_btl.fakeout_ail = imm;
            g_btl.char_cmd[idx] = BTLC_NONE;
        }
    }
}

/* Battle_CastMagicOnEnemy: the defender box, the explosions, the effect. */
static void mag_cast_on_enemy(SaveData* save, uint8_t attackid, const BtlMagic* m, int e) {
    ev_show_enemy(CBOX_DEFENDER, e);
    ev_explosions(e);
    mag_effect_on(save, attackid, m, 0, e);
}

/* Battle_CastMagicOnPlayer: the target blinks, its box, the effect. */
static void mag_cast_on_player(SaveData* save, uint8_t attackid, const BtlMagic* m, int i) {
    ev_sfx(s_playerhitsfx);
    ev_flash_char(i);
    ev_show_char(CBOX_DEFENDER, save, i);
    mag_effect_on(save, attackid, m, 1, i);
}

/* RespondDelay_UndrawAllBut2Boxes, between the targets of a multi-target spell. */
static void ev_next_target(void) {
    ev_wait();
    ev_hide_from(CBOX_DEFENDER);
}

/* Player_DoMagicEffect: character `p` casts `attackid` from `source`
 * (0 magic, 1 drink, 2 item) at `target`.  The attacker and attack boxes
 * are already up. */
static void player_do_magic(SaveData* save, int p, uint8_t attackid, int source, int target) {
    g_btl.fakeout_ail = 0;
    s_playerhitsfx = BSFX_MAGIC;

    /* Muted: the silenced message, for MAGIC and also (arguably BUGGED) DRINK and ITEM.
     * The MP or potion is already spent. */
    if (save->characters[p].ailments & BTL_AIL_MUTE) {
        ev_msg_clear_all(BTLMSG_SILENCED);
        return;
    }

    BtlMagic m;
    if (!btl_prepare_magic(attackid, &m)) {
        ev_walk_and_strike(p, 0, 1, 1);                   /* the walk, no spell */
        ev_msg_clear_all(source == 2 ? BTLMSG_NOTHINGHAPPENS : BTLMSG_INEFFECTIVENOW);
        return;
    }

    /* PreparePlayerMagAttack -> WalkForwardAndCastMagic: the spell's palette
     * and sprite (none for potions and items). */
    /* (The bleeps start before the walk: slightly BUGGED, the disassembly
     * says -- WalkForwardAndCastMagic would be the place.) */
    if (source == 0) ev_sfx(BSFX_MAGIC);
    ev_varpal(g_btl.char_magicgfx[p][1], 1);
    ev_walk_and_strike(p, g_btl.char_magicgfx[p][0], 1, source != 0);

    /* Battle_PlayerMagic_CastOnTarget, by the lowest set target bit. */
    uint8_t t = magic_table[attackid].target;
    if (t & 0x01) {                                       /* all enemies */
        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (!btl_enemy_exists(e)) continue;
            mag_cast_on_enemy(save, attackid, &m, e);
            ev_next_target();
        }
    } else if (t & 0x02) {                                /* one enemy */
        if (btl_enemy_exists(target)) {
            mag_cast_on_enemy(save, attackid, &m, target);
        } else {                                          /* (its box, even if gone) */
            if (target >= 0 && target < MAX_ENEMIES && g_btl.enemy[target].present)
                ev_show_enemy(CBOX_DEFENDER, target);
            ev_msg(BTLMSG_INEFFECTIVE);
        }
    } else if (t & 0x04) {                                /* self */
        ev_show_char(CBOX_DEFENDER, save, p);
        ev_sfx(s_playerhitsfx);
        ev_flash_char(p);
        mag_effect_on(save, attackid, &m, 1, p);
    } else if (t & 0x08) {                                /* the party */
        for (int i = 0; i < 4; i++) {
            /* (The blink comes before checking for dead / stone.) */
            ev_show_char(CBOX_DEFENDER, save, i);
            ev_sfx(s_playerhitsfx);
            ev_flash_char(i);
            if (char_out(save, i)) ev_msg(BTLMSG_INEFFECTIVE);
            else mag_effect_on(save, attackid, &m, 1, i);
            ev_next_target();
            ev_status(save);
        }
    } else {                                              /* one character */
        int i = target & 3;
        ev_show_char(CBOX_DEFENDER, save, i);
        ev_sfx(s_playerhitsfx);
        ev_flash_char(i);
        if (char_out(save, i)) ev_msg(BTLMSG_INEFFECTIVE);
        else mag_effect_on(save, attackid, &m, 1, i);
    }
    ev_clear_all();                                       /* Battle_EndMagicTurn */
}

/* Enemy_DoMagicEffect: enemy `eidx` casts spell / special attack `attackid`. */
static void enemy_do_magic(SaveData* save, int eidx, uint8_t attackid) {
    g_btl.fakeout_ail = 0;
    s_playerhitsfx = BSFX_PLAYER_ATTACK;
    BtlMagic m;
    if (!btl_prepare_magic(attackid, &m)) {               /* never in the original data */
        ev_clear_all();
        return;
    }
    if (g_btl.enemy[eidx].ail & BTL_AIL_MUTE) {           /* muted: BTLMSG_INEFFECTIVE */
        ev_msg_clear_all(BTLMSG_INEFFECTIVE);
        return;
    }

    /* Battle_EnemyMagic_CastOnTarget */
    uint8_t t = magic_table[attackid].target;
    if (t & 0x01) {                                       /* the party */
        for (int i = 0; i < 4; i++) {
            if (char_out(save, i)) continue;
            mag_cast_on_player(save, attackid, &m, i);
            ev_next_target();
            ev_status(save);
        }
    } else if (t & 0x02) {                                /* one character */
        int i = random_living_char(save);
        if (i >= 0) mag_cast_on_player(save, attackid, &m, i);
    } else if (t & 0x04) {                                /* self */
        mag_cast_on_enemy(save, attackid, &m, eidx);
    } else if (t & 0x08) {                                /* all enemies... */
        /* BUGGED in the original: except the caster. */
        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (e == eidx || !btl_enemy_exists(e)) continue;
            mag_cast_on_enemy(save, attackid, &m, e);
            ev_next_target();
        }
    } else {                                              /* one enemy */
        int e = random_living_enemy();
        if (e >= 0) mag_cast_on_enemy(save, attackid, &m, e);
    }
    ev_clear_all();
}

/* ---- End of battle ---- */

/* lut_EOBText's battle message ids (bank_0B). */
#define EOBMSG_LEVUP    0x30   /* "Lev. up!" */
#define EOBMSG_HPMAX    0x31   /* "HP max" */
#define EOBMSG_PTS      0x32   /* "pts." */
#define EOBMSG_PERISHED 0x3C   /* (end-of-battle text) */
#define EOBMSG_MONSTERS 0x3D   /* (end-of-battle text) */
#define EOBMSG_PARTY    0x3E   /* " party " */
#define EOBMSG_EXPUP    0x49   /* "EXP up" */

/* RespondDelay_UndrawAllCombatBoxes after a WaitForAnyInput. */
static void ev_eob_next(void) {
    ev_wait_input();
    ev_wait();
    ev_clear_all();
}

/* EndOfBattleWrapUp: the monsters-perished message, the EXP share and gold, then each
 * character's EXP and level up, in the combat boxes. */
static void victory_rewards(SaveData* save) {
    BtlText t;
    text_msg(&t, EOBMSG_MONSTERS);
    btl_text_add_raw(&t, Text_BattleMessage(EOBMSG_PERISHED), BTL_TEXT_MAX);
    ev_show(CBOX_MESSAGE, &t);
    ev_eob_next();

    /* SumBattleReward over the enemies (one that ran had its rewards erased). */
    uint32_t exp = 0, gp = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {             /* all 9 slots */
        if (!g_btl.enemy[i].present || g_btl.enemy[i].fled) continue;
        exp += enemy_table[g_btl.enemy[i].id].exp_reward;
        gp  += enemy_table[g_btl.enemy[i].id].gp_reward;
    }
    int survivors = 0;
    for (int i = 0; i < 4; i++) survivors += !char_out(save, i);
    uint16_t share = Btl_ExpShare(exp, survivors);

    uint32_t gold = Party_Gold(save) + gp;                /* GiveRewardToParty */
    Party_SetGold(save, gold >= 1000000 ? 999999 : gold);

    /* Draw4EobBoxes: EXP up | ####P / GOLD | ####G. */
    text_msg(&t, EOBMSG_EXPUP);
    ev_show(0, &t);
    t.len = 0; btl_text_add_number(&t, share); btl_text_add_ascii(&t, "P");
    ev_show(1, &t);
    btl_text_ascii(&t, "GOLD");
    ev_show(2, &t);
    t.len = 0; btl_text_add_number(&t, gp & 0xFFFF); btl_text_add_ascii(&t, "G");   /* printed as 16 bits */
    ev_show(3, &t);
    ev_eob_next();

    /* The EXP is added as a 3-byte value whose top byte is left over from
     * the gold sum, until a level up's HP bonus clears it. */
    uint32_t hi = gp & 0xFF0000;
    for (int i = 0; i < 4; i++) {
        BtlLevelUp lv;
        if (!Btl_AwardExp(save, i, share | hi, &lv)) continue;
        hi = 0;
        /* Lev. up! | <Name> L## / HP max | ###pts. */
        text_msg(&t, EOBMSG_LEVUP);
        ev_show(0, &t);
        btl_text_char_name(&t, &save->characters[i]);
        btl_text_add_ascii(&t, " L");
        btl_text_add_number(&t, lv.level);
        ev_show(1, &t);
        text_msg(&t, EOBMSG_HPMAX);
        ev_show(2, &t);
        t.len = 0;
        btl_text_add_number(&t, lv.max_hp);
        btl_text_add_raw(&t, Text_BattleMessage(EOBMSG_PTS), BTL_TEXT_MAX);
        ev_show(3, &t);
        for (int k = 0; k < 5; k++) {         /* "Str. up" etc., one at a time */
            if (!lv.statup[k]) continue;
            text_msg(&t, (uint8_t)(BTLMSG_STR + k));
            btl_text_add_raw(&t, Text_BattleMessage(BTLMSG_UP), BTL_TEXT_MAX);
            ev_show(CBOX_MESSAGE, &t);
            ev_wait();
            ev_wait_input();
            ev_hide_from(CBOX_MESSAGE);
        }
        ev_wait();
        ev_clear_all();
    }
}

/* CheckForEndOfBattle / BattleOver_ProcessResult: the party cheers if it
 * won; ailments go back to out-of-battle ids; then the result's boxes. */
static void battle_over(SaveData* save, BattleResult result) {
    g_btl.result = result;
    g_btl.phase = BPHASE_DONE;
    if (result == BATTLE_WON) ev_add(EV_CHEER);
    for (int i = 0; i < 4; i++)                            /* ConvertIBStatsToOB */
        save->characters[i].ailments = Btl_AilmentIbToOb(save->characters[i].ailments);

    if (result == BATTLE_LOST) {                           /* GameOver */
        g_music_track = g_music_followup = MUSIC_GAME_OVER;
        BtlText t;
        btl_text_char_name(&t, &save->characters[0]);
        btl_text_add_raw(&t, Text_BattleMessage(EOBMSG_PARTY), BTL_TEXT_MAX);
        btl_text_add_raw(&t, Text_BattleMessage(EOBMSG_PERISHED), BTL_TEXT_MAX);
        ev_show(CBOX_MESSAGE, &t);
        ev_eob_next();
    } else if (result == BATTLE_WON && g_btl.formation == 0x7B) {
        ev_add(EV_CHAOS_DEATH);                            /* Chaos: no rewards, he dissolves */
        g_btl.result = BATTLE_ENDGAME;                     /* then the ending */
    } else if (result == BATTLE_WON) {
        victory_rewards(save);
    }
}

/* ---- Command input ---- */

static void round_start(SaveData* save);

/** UndoCharacterBattleCommand: B goes back to the previous character who
 *  can act, clearing their command (and returning a claimed potion). */
static void undo_prev_command(SaveData* save) {
    for (int c = g_btl.cur_char - 1; c >= 0; c--) {
        if (!can_input(save, c)) continue;
        ev_walk(g_btl.cur_char, 1, 1);                    /* walk back */
        if (g_btl.char_cmd[c] == BTLC_DRINK) g_btl.potions[g_btl.char_item[c] & 1]++;
        g_btl.char_cmd[c] = BTLC_NONE;
        g_btl.cur_char = c;
        begin_commands();
        return;
    }
}

/** SetCharacterBattleCommand: the character walks back; then the next
 *  character who can act, or the round (bank_0C DoBattleRound). */
static void advance_char(SaveData* save) {
    ev_walk(g_btl.cur_char, 1, 1);
    for (int i = 1; i <= 4; i++) {
        int c = (g_btl.cur_char + i) & 3;
        if (can_input(save, c) && g_btl.char_cmd[c] == BTLC_NONE) {
            g_btl.cur_char = c;
            begin_commands();
            return;
        }
    }
    g_btl.round_after_ev = 1;                             /* after the walk back */
}

/* CancelBattleAction: B out of a submenu walks the character back and
 * forward again. */
static void cancel_action(void) {
    ev_walk(g_btl.cur_char, 1, 1);
    ev_walk(g_btl.cur_char, 0, 1);
    g_btl.phase = BPHASE_COMMAND;
}

/* DoNothingMessageBox: "Nothing" until any button, then back to `ret`. */
static void nothing_box(BattlePhase ret) {
    g_btl.nothing_box = 1;
    g_btl.nothing_return = ret;
}

/* SelectEnemyTarget: start on the first cursor index holding an enemy. */
static void begin_enemy_target(BattlePhase phase) {
    g_btl.target_curs = target_step(-1, 1);
    g_btl.phase = phase;
}

/* ---- Turns ---- */

static void try_run(SaveData* save, int p);

/* Play the turn (Battle_DoTurn), recording its script.  turn_death is
 * Battle_DoTurn's defender output: whether the defender ended dead or
 * stone, which is the only time BattleTurnEnd_CheckForBattleEnd looks for
 * the battle's end. */
static void apply_action(SaveData* save) {
    int p = g_btl.pending.attacker;
    g_btl.turn_death = 0;
    switch (g_btl.pending.kind) {
    case PA_PLAYER_FIGHT: {
        int e = g_btl.pending.target;
        if (e >= 0) {
            char_attack_enemy(save, p, e);
            g_btl.turn_death = !btl_enemy_exists(e);
            if (g_btl.turn_death) ev_erase(e);            /* BattleTurnEnd */
        } else {
            ev_clear_all();
        }
        break;
    }
    case PA_PLAYER_DRINK:                                 /* Battle_DoPlayerTurn */
        save->items[BTL_ITEM_HEAL + g_btl.char_item[p]]--;
        player_do_magic(save, p, g_btl.pending.effect_id, 1, g_btl.pending.target);
        g_btl.turn_death = (g_btl.fakeout_ail & (BTL_AIL_DEAD | BTL_AIL_STONE)) != 0;
        break;
    case PA_PLAYER_CAST: {
        CharacterMagic* cm = &save->character_magic[p];
        cm->cur_mp[g_btl.pending.effect_id / 8]--;        /* one charge of its level */
        player_do_magic(save, p, g_btl.pending.effect_id, 0, g_btl.pending.target);
        g_btl.turn_death = (g_btl.fakeout_ail & (BTL_AIL_DEAD | BTL_AIL_STONE)) != 0;
        break;
    }
    case PA_PLAYER_ITEM:
        player_do_magic(save, p, g_btl.pending.effect_id, 2, g_btl.pending.target);
        g_btl.turn_death = (g_btl.fakeout_ail & (BTL_AIL_DEAD | BTL_AIL_STONE)) != 0;
        break;
    case PA_ENEMY_PHYSICAL: {
        int t = g_btl.pending.target;
        enemy_physical_attack(save, p, t);
        if (char_out(save, t)) {
            g_btl.char_cmd[t] = BTLC_NONE;                /* the dead don't act */
            g_btl.turn_death = 1;
        }
        break;
    }
    case PA_ENEMY_CAST:
        enemy_do_magic(save, p, g_btl.pending.effect_id);
        g_btl.turn_death = (g_btl.fakeout_ail & (BTL_AIL_DEAD | BTL_AIL_STONE)) != 0;
        break;
    case PA_PLAYER_RUN:
        try_run(save, p);
        break;
    case PA_PLAYER_WAKE: {
        /* Battle_PlayerTryWakeup: wake if max HP > random[0,$50]. */
        CharacterStats* c = &save->characters[p];
        if (c->max_hp > Rng_RandAX(0, 0x50)) {
            c->ailments &= (uint8_t)~BTL_AIL_SLEEP;
            ev_msg_clear_all(BTLMSG_WOKEUP);
        } else {
            ev_msg_clear_all(BTLMSG_SLEEPING);
        }
        break;
    }
    case PA_PLAYER_UNSTUN: {
        /* Battle_PlayerTryUnstun: 1 in 4. */
        CharacterStats* c = &save->characters[p];
        if ((Rng_Battle() & 3) == 0) {
            c->ailments &= (uint8_t)~BTL_AIL_STUN;
            ev_msg_clear_all(BTLMSG_CURED);
        } else {
            ev_msg_clear_all(BTLMSG_PARALYZED_B);
        }
        break;
    }
    case PA_ENEMY_STATUS:
        ev_msg_clear_all(g_btl.pending.status_msg);
        break;
    case PA_ENEMY_CONFUSED: {
        /* FIRE (magic $04) on a random enemy, the caster included, without
         * its attack box.  BUGGED in the original: the turn's defender
         * output isn't set, so a death here doesn't end the battle until a
         * later check. */
        BtlMagic m;
        btl_prepare_magic(4, &m);
        int e = random_living_enemy();
        if (e >= 0) mag_cast_on_enemy(save, 4, &m, e);
        ev_clear_all();
        break;
    }
    case PA_ENEMY_FLEE: {
        BattleEnemy* en = &g_btl.enemy[p];
        ev_msg(BTLMSG_RUNAWAY);
        ev_erase(p);
        en->ail = BTL_AIL_DEAD;
        en->fled = 1;
        g_btl.turn_death = 1;
        ev_clear_all();
        break;
    }
    default:
        ev_clear_all();
        break;
    }
    ev_status(save);                                      /* DrawCharacterStatus */
}

/* Plan character `p`'s queued command: g_btl.pending and the opening boxes. */
static void plan_player_turn(SaveData* save, int p) {
    g_btl.pending.attacker = p;
    g_btl.pending.target = g_btl.char_target[p];
    ev_show_char(CBOX_ATTACKER, save, p);
    BtlText t;
    switch (g_btl.char_cmd[p]) {
    case BTLC_FIGHT: {
        int eidx = g_btl.char_target[p];
        if (!btl_enemy_exists(eidx))
            eidx = first_living_enemy();   /* retarget if the chosen enemy fell */
        g_btl.pending.kind = PA_PLAYER_FIGHT;
        g_btl.pending.target = eidx;
        if (eidx >= 0) ev_show_enemy(CBOX_DEFENDER, eidx);
        break;
    }
    case BTLC_DRINK:
        g_btl.pending.kind = PA_PLAYER_DRINK;
        g_btl.pending.effect_id = (uint8_t)(0x40 + g_btl.char_item[p]);
        text_item(&t, (uint8_t)(BTL_ITEM_HEAL + g_btl.char_item[p]));
        ev_show(CBOX_ATTACK, &t);
        break;
    case BTLC_MAGIC:
        g_btl.pending.kind = PA_PLAYER_CAST;
        g_btl.pending.effect_id = g_btl.char_spell[p];
        text_attack(&t, g_btl.char_spell[p], 0);
        ev_show(CBOX_ATTACK, &t);
        break;
    case BTLC_ITEM:
        g_btl.pending.kind = PA_PLAYER_ITEM;
        g_btl.pending.effect_id = g_btl.char_spell[p];
        text_item(&t, g_btl.char_item[p]);
        ev_show(CBOX_ATTACK, &t);
        break;
    case BTLC_RUN:
        g_btl.pending.kind = PA_PLAYER_RUN;
        break;
    default:
        break;
    }
}

/* A character with no command: asleep or paralyzed (Battle_DoPlayerTurn's
 * fallback).  Returns 0 if there is nothing to do this turn. */
static int plan_player_recover(SaveData* save, int p) {
    uint8_t ail = save->characters[p].ailments;
    if (ail & BTL_AIL_SLEEP)      g_btl.pending.kind = PA_PLAYER_WAKE;
    else if (ail & BTL_AIL_STUN)  g_btl.pending.kind = PA_PLAYER_UNSTUN;
    else return 0;
    g_btl.pending.attacker = p;
    ev_show_char(CBOX_ATTACKER, save, p);
    return 1;
}

/* Plan enemy `eidx`'s turn (Battle_DoEnemyTurn / Enemy_DoAi). */
static void plan_enemy_turn(SaveData* save, int eidx) {
    BattleEnemy* en = &g_btl.enemy[eidx];
    g_btl.pending.attacker = eidx;
    ev_show_enemy(CBOX_ATTACKER, eidx);

    /* Asleep or paralyzed enemies use their turn to recover. */
    if (en->ail & (BTL_AIL_SLEEP | BTL_AIL_STUN)) {
        g_btl.pending.kind = PA_ENEMY_STATUS;
        if (en->ail & BTL_AIL_SLEEP) {
            /* BUGGED in the original: the wake-up check reads an uninitialized
             * value and subtracts from the wrong buffer, so a sleeping enemy
             * always wakes on its next turn (the roll still happens). */
            Rng_RandAX(0, 0x50);
            en->ail &= (uint8_t)~BTL_AIL_SLEEP;
            g_btl.pending.status_msg = BTLMSG_WOKEUP;
        } else if (Rng_Battle() < 25) {                   /* ~10%: paralysis wears off */
            en->ail &= (uint8_t)~BTL_AIL_STUN;
            g_btl.pending.status_msg = BTLMSG_CURED;
        } else {
            g_btl.pending.status_msg = BTLMSG_PARALYZED_B;
        }
        return;
    }
    if (en->ail & BTL_AIL_CONF) {
        if (Rng_Battle() < 0x40) {                        /* 25%: confusion wears off */
            en->ail &= (uint8_t)~BTL_AIL_CONF;
            g_btl.pending.kind = PA_ENEMY_STATUS;
            g_btl.pending.status_msg = BTLMSG_CURED;
        } else {                                          /* casts FIRE on its own side */
            g_btl.pending.kind = PA_ENEMY_CONFUSED;
        }
        return;
    }

    /* Morale: V = random[0,$32] + morale - 2 x the leader's level; an enemy
     * with V < $50 (or morale below 2 x level) runs away.  (ch_level is
     * 1-based during battle: ConvertOBStatsToIB.) */
    uint8_t twice_level = (uint8_t)((save->characters[0].level + 1) << 1);
    int flee;
    if (en->morale < twice_level) {
        flee = 1;
    } else {
        unsigned v = Rng_RandAX(0, 0x32) + (uint8_t)(en->morale - twice_level);
        flee = v < 0x50;
    }
    if (flee) {
        g_btl.pending.kind = PA_ENEMY_FLEE;
        return;
    }

    uint8_t effect_id;
    if (enemy_pick_action(eidx, &effect_id)) {
        g_btl.pending.kind = PA_ENEMY_CAST;
        g_btl.pending.effect_id = effect_id;
        BtlText t;
        text_attack(&t, effect_id, 1);
        ev_show(CBOX_ATTACK, &t);
    } else {
        int t = random_living_char(save);
        if (t < 0) t = 0;
        g_btl.pending.kind = PA_ENEMY_PHYSICAL;
        g_btl.pending.target = t;
        ev_show_char(CBOX_DEFENDER, save, t);
    }
}

/* Play the next live entity's turn.  Returns 1 if a turn started, 0 if the
 * round is over. */
static int round_begin_turn(SaveData* save) {
    while (g_btl.round_turn < g_btl.round_turns) {
        int e = g_btl.round_order[g_btl.round_turn++];
        if (e & 0x80) {
            int p = e & 0x03;
            /* Battle_DoPlayerTurn: no command (never given, or erased when
             * the character was slain or immobilized) means trying to wake
             * up or recover, or nothing. */
            if (g_btl.char_cmd[p] == BTLC_NONE) {
                if (!plan_player_recover(save, p)) continue;
            } else {
                plan_player_turn(save, p);
            }
        } else {
            if (!btl_enemy_exists(e)) continue;     /* empty slot */
            if (g_btl.striking_first) continue;     /* the party strikes first */
            plan_enemy_turn(save, e);
        }
        apply_action(save);
        return 1;
    }
    return 0;
}

/* CheckForBattleEnd: returns 1 (and ends the battle) if either side is out,
 * the party checked first. */
static int check_battle_end(SaveData* save) {
    if (all_party_dead(save)) {
        battle_over(save, BATTLE_LOST);
        return 1;
    }
    if (all_enemies_dead()) {
        battle_over(save, BATTLE_WON);
        return 1;
    }
    return 0;
}

/* ApplyEndOfRoundEffects: poison costs 2 HP (even when stoned -- the
 * original doesn't check), regenerating enemies recover 3. */
static void end_of_round(SaveData* save) {
    for (int i = 0; i < 4; i++) {
        CharacterStats* c = &save->characters[i];
        if (!(c->ailments & BTL_AIL_POISON)) continue;
        c->cur_hp = (c->cur_hp > 2) ? (uint16_t)(c->cur_hp - 2) : 0;
        if (c->cur_hp == 0) c->ailments = BTL_AIL_DEAD;
    }
    for (int i = 0; i < 4; i++) {                 /* DrawCharacterStatus */
        g_btl.disp_hp[i] = save->characters[i].cur_hp;
        g_btl.disp_ail[i] = save->characters[i].ailments;
    }
    /* (ApplyRegenToAllEnemies loops over the first btl_enemycount slots,
     * so in a mixed formation with a gap a regenerating enemy in a later
     * slot can be missed.) */
    for (int i = 0; i < g_btl.enemy_count; i++) {
        BattleEnemy* en = &g_btl.enemy[i];
        const EnemyData* rom = &enemy_table[en->id];
        if (!btl_enemy_exists(i) || !(rom->category & BTL_CATEGORY_REGEN)) continue;
        uint32_t hp = (uint32_t)en->hp + 3;
        en->hp = (uint16_t)(hp > rom->max_hp ? rom->max_hp : hp);
    }
}

/* The round is over: resolve victory/defeat, else command input for the
 * next round. */
static void round_end(SaveData* save) {
    g_btl.striking_first = 0;                /* enemies act from now on */
    end_of_round(save);
    if (check_battle_end(save)) return;      /* poison may have finished the party */
    for (int i = 0; i < 4; i++) { g_btl.char_cmd[i] = BTLC_NONE; g_btl.char_target[i] = -1; }
    g_btl.cur_char = first_living_char(save);
    if (g_btl.cur_char < 0) {                /* nobody can act: next round */
        g_btl.cur_char = 0;
        round_start(save);
        return;
    }
    begin_commands();
}

/* DoBattleRound: lut_InitialTurnOrder (enemy slots 0-8, then players
 * $80-$83), shuffled, then the first turn. */
static void round_start(SaveData* save) {
    int n = 0;
    for (int i = 0; i < 9; i++) g_btl.round_order[n++] = i;
    for (int i = 0; i < 4; i++) g_btl.round_order[n++] = 0x80 | i;
    g_btl.round_turns = n;

    /* 16 swaps of two random entries.  (The original notes this shuffle is
     * biased: entries are likely to stay put, leaving the party last.) */
    for (int k = 0; k < 16; k++) {
        int a = Rng_RandAX(0, 12);
        int b = Rng_RandAX(0, 12);
        int t = g_btl.round_order[b]; g_btl.round_order[b] = g_btl.round_order[a]; g_btl.round_order[a] = t;
    }

    g_btl.round_turn = 0;
    g_btl.phase = BPHASE_ROUND;
    if (!round_begin_turn(save))
        round_end(save);   /* no live entities (shouldn't happen here) */
}

static void try_run(SaveData* save, int p) {
    /* Battle_PlayerTryRun reads its "level" from the in-battle stats at the
     * ch_level offset, which lands on other battle RAM: the ailments of the
     * character two slots down (slots 0-1), btl_turnorder[2] (slot 2), or
     * btlmag_spellconnected (slot 3). */
    uint8_t level_byte;
    switch (p) {
    case 0:  level_byte = save->characters[2].ailments; break;
    case 1:  level_byte = save->characters[3].ailments; break;
    case 2:  level_byte = (uint8_t)g_btl.round_order[2]; break;
    default: level_byte = g_btl.spell_connected; break;
    }
    if (Btl_TryRun(save->characters[p].luck, level_byte, g_btl.striking_first, g_btl.no_run)) {
        ev_msg_clear_all(BTLMSG_CLOSECALL);
        g_btl.ran = 1;                                /* the party escapes after this turn */
    } else {
        ev_msg_clear_all(BTLMSG_CANTRUN);
    }
}

/* ---- Update ---- */

/* A spell chosen in the MAGIC box (BattleSubMenu_Magic): queue it, or go
 * pick its target. */
static void choose_spell(SaveData* save, int si) {
    int p = g_btl.cur_char;
    g_btl.char_spell[p] = (uint8_t)si;
    g_btl.char_target[p] = -1;
    g_btl.char_magicgfx[p][0] = magic_table[si].graphic;  /* btlcmd_magicgfx */
    g_btl.char_magicgfx[p][1] = magic_table[si].palette;
    g_btl.target_is_drink = 0;
    uint8_t t = magic_table[si].target;              /* by the lowest set target bit */
    if (t & 0x01) {                                  /* all enemies */
    } else if (t & 0x02) {                           /* one enemy */
        g_btl.magic_target_enemy = 1;
        begin_enemy_target(BPHASE_MAGIC_TGT);
        return;
    } else if (t & 0x04) {                           /* self */
        g_btl.char_target[p] = (int8_t)p;
    } else if (!(t & 0x08)) {                        /* one character */
        g_btl.magic_target_enemy = 0;
        g_btl.player_curs = 0;
        g_btl.phase = BPHASE_MAGIC_TGT;
        return;
    }                                                /* (0x08: the party) */
    g_btl.char_cmd[p] = BTLC_MAGIC;
    advance_char(save);
}

/* ---- Autopilot (for pictures and testing; not in the original) ----
 * Each character's command chosen as the menus would choose it: their
 * healing spell on the most hurt ally when one is under half HP, else
 * their attack spell while it has charges, else FIGHT the first enemy. */
static int ap_on, ap_wait;
static int8_t ap_attack[4], ap_heal[4];

void Battle_SetAutopilot(int on, const int8_t attack[4], const int8_t heal[4]) {
    ap_on = on;
    for (int i = 0; i < 4; i++) { ap_attack[i] = attack[i]; ap_heal[i] = heal[i]; }
}

static int ap_charged(const SaveData* save, int p, int si) {
    return si >= 0 && si < 64 && save->character_magic[p].cur_mp[si / 8] > 0;
}

static void autopilot(SaveData* save) {
    int p = g_btl.cur_char, si = -1, ally = -1;
    if (ap_charged(save, p, ap_heal[p])) {
        int worst = 50;                              /* (percent of max HP) */
        for (int c = 0; c < 4; c++) {
            const CharacterStats* ch = &save->characters[c];
            if (ch->ailments & (BTL_AIL_DEAD | BTL_AIL_STONE) || !ch->max_hp) continue;
            int pct = ch->cur_hp * 100 / ch->max_hp;
            if (pct < worst) { worst = pct; ally = c; }
        }
        if (ally >= 0) si = ap_heal[p];
    }
    if (si < 0 && ap_charged(save, p, ap_attack[p])) si = ap_attack[p];
    if (si >= 0) {
        choose_spell(save, si);
        if (g_btl.phase != BPHASE_MAGIC_TGT) return; /* (queued: all enemies, self, the party) */
        g_btl.char_target[p] = (int8_t)(g_btl.magic_target_enemy ? btl_target_slot(g_btl.target_curs)
                                                                  : (ally >= 0 ? ally : p));
        g_btl.char_cmd[p] = BTLC_MAGIC;
    } else {
        begin_enemy_target(BPHASE_TARGET);
        g_btl.char_target[p] = (int8_t)btl_target_slot(g_btl.target_curs);
        g_btl.char_cmd[p] = BTLC_FIGHT;
    }
    advance_char(save);
}

/* The command menus (BattleLogicLoop's input). */
static void update_input(SaveData* save, const InputState* in) {
    /* DoFrame_WithInput rolls (and throws away) a BattleRNG number every
     * time the menus read the pad: once per 2-frame menu loop, every frame
     * for the "Nothing" box. */
    if (g_btl.nothing_box || (++g_btl.input_frames & 1) == 0) Rng_Battle();

    if (g_btl.nothing_box) {                         /* DoNothingMessageBox */
        if (in->pressed) {
            g_btl.nothing_box = 0;
            if (g_btl.nothing_return == BPHASE_COMMAND) cancel_action();
            else g_btl.phase = g_btl.nothing_return;
        }
        return;
    }

    switch (g_btl.phase) {
    case BPHASE_COMMAND:
        if (!can_input(save, g_btl.cur_char)) { advance_char(save); break; }
        if (ap_on) {                                 /* (the menu shows a moment first) */
            if (++ap_wait >= 20) { ap_wait = 0; autopilot(save); }
            break;
        }
        /* The 2x4 command menu: FIGHT/MAGIC/DRINK/ITEM, and RUN alone in
         * the second column. */
        if (in->pressed & BTN_UP)    g_btl.cmd_row = (g_btl.cmd_row + 3) & 3;
        if (in->pressed & BTN_DOWN)  g_btl.cmd_row = (g_btl.cmd_row + 1) & 3;
        if (in->pressed & (BTN_LEFT | BTN_RIGHT)) g_btl.cmd_col ^= 1;
        if (in->pressed & BTN_A) {
            int p = g_btl.cur_char;
            if (g_btl.cmd_col == 1) {                /* RUN: tried on this character's turn */
                g_btl.char_cmd[p] = BTLC_RUN;
                advance_char(save);
                break;
            }
            switch (g_btl.cmd_row) {
            case 0:                                  /* FIGHT */
                begin_enemy_target(BPHASE_TARGET);
                break;
            case 1:                                  /* MAGIC */
                g_btl.magic_row = g_btl.magic_col = 0;
                g_btl.phase = BPHASE_MAGIC;
                break;
            case 2:                                  /* DRINK */
                if (!(g_btl.potions[0] | g_btl.potions[1])) {
                    nothing_box(BPHASE_COMMAND);
                } else {
                    g_btl.drink_cursor = 0;
                    g_btl.phase = BPHASE_DRINK;
                }
                break;
            default: {                               /* ITEM */
                int any = 0;
                for (int slot = 0; slot < 8; slot++) any |= btl_equip_item(save, p, slot);
                if (!any) {
                    nothing_box(BPHASE_COMMAND);
                } else {
                    g_btl.item_cursor = 0;
                    g_btl.phase = BPHASE_ITEM;
                }
                break;
            }
            }
        } else if (in->pressed & BTN_B) {
            undo_prev_command(save);
        }
        break;

    case BPHASE_TARGET:
    case BPHASE_MAGIC_TGT:
        if (in->pressed & BTN_B) {
            if (g_btl.phase == BPHASE_TARGET) cancel_action();
            else g_btl.phase = g_btl.target_is_drink ? BPHASE_DRINK : BPHASE_MAGIC;
            break;
        }
        if (g_btl.phase == BPHASE_TARGET || g_btl.magic_target_enemy) {
            /* EnemyTargetMenu: up/down through the cursor list. */
            if (in->pressed & BTN_UP)   g_btl.target_curs = target_step(g_btl.target_curs, -1);
            if (in->pressed & BTN_DOWN) g_btl.target_curs = target_step(g_btl.target_curs, 1);
        } else {
            if (in->pressed & BTN_UP)   g_btl.player_curs = (g_btl.player_curs + 3) & 3;
            if (in->pressed & BTN_DOWN) g_btl.player_curs = (g_btl.player_curs + 1) & 3;
        }
        if (in->pressed & BTN_A) {
            int p = g_btl.cur_char;
            int enemy = g_btl.phase == BPHASE_TARGET || g_btl.magic_target_enemy;
            g_btl.char_target[p] = (int8_t)(enemy ? btl_target_slot(g_btl.target_curs)
                                                  : g_btl.player_curs);
            if (g_btl.phase == BPHASE_TARGET) {
                g_btl.char_cmd[p] = BTLC_FIGHT;
            } else if (g_btl.target_is_drink) {
                /* The potion is claimed now and drunk on the character's turn. */
                g_btl.char_cmd[p] = BTLC_DRINK;
                g_btl.char_item[p] = (uint8_t)g_btl.drink_cursor;
                g_btl.potions[g_btl.drink_cursor]--;
            } else {
                g_btl.char_cmd[p] = BTLC_MAGIC;
            }
            advance_char(save);
        }
        break;

    case BPHASE_MAGIC:
        /* MenuSelection_Magic: 3 slots x 8 levels, shown 4 levels a page. */
        if (in->pressed & BTN_B) { cancel_action(); break; }
        if (in->pressed & BTN_UP)    g_btl.magic_row = (g_btl.magic_row + 7) & 7;
        if (in->pressed & BTN_DOWN)  g_btl.magic_row = (g_btl.magic_row + 1) & 7;
        if (in->pressed & BTN_LEFT)  g_btl.magic_col = (g_btl.magic_col + 2) % 3;
        if (in->pressed & BTN_RIGHT) g_btl.magic_col = (g_btl.magic_col + 1) % 3;
        if (in->pressed & BTN_A) {
            /* An empty slot or a level with no charges: "Nothing". */
            int si = btl_spell_at(save, g_btl.cur_char, g_btl.magic_row, g_btl.magic_col);
            if (si < 0 || save->character_magic[g_btl.cur_char].cur_mp[g_btl.magic_row] == 0)
                nothing_box(BPHASE_MAGIC);
            else
                choose_spell(save, si);
        }
        break;

    case BPHASE_DRINK:
        if (in->pressed & BTN_B) { cancel_action(); break; }
        if (in->pressed & (BTN_UP | BTN_DOWN)) g_btl.drink_cursor ^= 1;
        if (in->pressed & BTN_A) {
            if (g_btl.potions[g_btl.drink_cursor] == 0) {
                nothing_box(BPHASE_COMMAND);
            } else {
                g_btl.target_is_drink = 1;
                g_btl.magic_target_enemy = 0;
                g_btl.player_curs = 0;
                g_btl.phase = BPHASE_MAGIC_TGT;
            }
        }
        break;

    case BPHASE_ITEM:
        if (in->pressed & BTN_B) { cancel_action(); break; }
        if (in->pressed & BTN_UP)    g_btl.item_cursor = (g_btl.item_cursor & 4) | ((g_btl.item_cursor + 3) & 3);
        if (in->pressed & BTN_DOWN)  g_btl.item_cursor = (g_btl.item_cursor & 4) | ((g_btl.item_cursor + 1) & 3);
        if (in->pressed & (BTN_LEFT | BTN_RIGHT)) g_btl.item_cursor ^= 4;
        if (in->pressed & BTN_A) {
            int p = g_btl.cur_char;
            uint8_t id = btl_equip_item(save, p, g_btl.item_cursor);
            if (!id) {
                nothing_box(BPHASE_COMMAND);
                break;
            }
            /* The item's spell (1-based in the weapon/armor data; 0 = none
             * becomes $FF), always aimed at slot = the user's index. */
            uint8_t spell = (g_btl.item_cursor < 4) ? weapon_table[id - 0x1C].spell_id
                                                    : armor_table[id - 0x44].spell_id;
            g_btl.char_cmd[p] = BTLC_ITEM;
            g_btl.char_item[p] = id;
            g_btl.char_spell[p] = (uint8_t)(spell - 1);
            g_btl.char_target[p] = (int8_t)p;
            advance_char(save);
        }
        break;

    default:
        break;
    }
}

static BattleResult battle_update(SaveData* save, const InputState* in);

BattleResult Battle_Update(SaveData* save, const InputState* in) {
    s_sfx_hold = 0;
    BattleResult r = battle_update(save, in);
    if (!s_sfx_hold) Sfx_BattleUpdate();        /* BattleUpdateAudio */
    return r;
}

static BattleResult battle_update(SaveData* save, const InputState* in) {
    /* Whatever is playing (walks, a turn, the opening or closing boxes)
     * finishes before anything else happens. */
    if (!ev_run(save, in)) return BATTLE_CONTINUE;

    switch (g_btl.phase) {
    case BPHASE_OPENING:
        /* Surprised: BattleLogicLoop_DoCombat, the round runs with no
         * commands (characters still get their recovery turns). */
        if (g_btl.surprised) {
            round_start(save);
        } else {
            g_btl.cur_char = 0;
            begin_commands();
        }
        break;

    case BPHASE_ROUND: {
        /* The turn has played out: the boxes are down. */
        g_btl.cbox_shown = 0;
        for (int i = 0; i < MAX_ENEMIES; i++)    /* (anything gone is no longer drawn) */
            if (!btl_enemy_exists(i) && btl_erase_rows(i)) g_btl.enemy_vis[i] = 0;
        if (g_btl.ran) {
            battle_over(save, BATTLE_RAN);
            break;
        }
        /* BattleTurnEnd_CheckForBattleEnd: only after a turn whose defender
         * ended dead or stone. */
        if (g_btl.turn_death && check_battle_end(save)) break;
        if (!round_begin_turn(save)) round_end(save);
        break;
    }

    case BPHASE_DONE:
        /* The result's boxes are done; ExitBattle (which a game over doesn't
         * reach) re-sorts the party. */
        if (g_btl.result != BATTLE_LOST) Btl_ReSortParty(save);
        return g_btl.result;

    default:
        if (g_btl.round_after_ev) {                  /* every command is in */
            g_btl.round_after_ev = 0;
            round_start(save);
            break;
        }
        update_input(save, in);
        break;
    }
    return BATTLE_CONTINUE;
}

int Battle_IsOver(void) {
    return g_btl.phase == BPHASE_DONE;
}

int Battle_WaitingForInput(void) {
    return g_btl.phase >= BPHASE_COMMAND && g_btl.phase <= BPHASE_ITEM &&
           g_btl.ev_i >= g_btl.ev_n && !g_btl.round_after_ev;
}
