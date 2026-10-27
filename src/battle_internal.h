#pragma once
/*
 * battle_internal.h -- state shared by the battle logic (battle.c) and its
 * presentation (battle_draw.c).  Not for use outside the battle module.
 */
#include <stdint.h>
#include "battle.h"
#include "btl_core.h"

#define MAX_ENEMIES 9

/* Battle phases. */
typedef enum {
    BPHASE_OPENING,   /* the strike-first / surprised message */
    BPHASE_COMMAND,   /* player picks a command for the current character */
    BPHASE_TARGET,    /* choosing a target (FIGHT) */
    BPHASE_MAGIC,     /* choosing a spell from the character's list */
    BPHASE_MAGIC_TGT, /* choosing a target for a spell/item */
    BPHASE_DRINK,     /* choosing a potion (HEAL / PURE) */
    BPHASE_ITEM,      /* choosing a piece of equipment to use */
    BPHASE_ROUND,     /* the round's turns play out */
    BPHASE_DONE,      /* end-of-battle boxes; buttons step through them */
} BattlePhase;

/* Queued per-character command for the current round (FF1 btl_charcmdbuf). */
typedef enum {
    BTLC_NONE = 0,   /* no command chosen yet */
    BTLC_FIGHT,      /* attack an enemy target */
    BTLC_DRINK,      /* drink a potion */
    BTLC_MAGIC,      /* cast a spell */
    BTLC_ITEM,       /* use a piece of equipment's spell */
    BTLC_RUN,        /* try to run (on this character's turn) */
} BtlCommand;

/* A turn about to be played (Battle_DoTurn's branches). */
typedef enum {
    PA_PLAYER_FIGHT,   /* a character attacks an enemy */
    PA_PLAYER_DRINK,   /* a character drinks a potion (Player_DoDrink) */
    PA_PLAYER_CAST,    /* a character casts a spell (Player_DoMagic) */
    PA_PLAYER_ITEM,    /* a character uses equipment (Player_DoItem) */
    PA_ENEMY_PHYSICAL, /* an enemy attacks a character */
    PA_ENEMY_CAST,     /* an enemy casts a spell/special attack */
    PA_PLAYER_RUN,     /* a character tries to run (Battle_PlayerTryRun) */
    PA_PLAYER_WAKE,    /* a sleeping character tries to wake (Battle_PlayerTryWakeup) */
    PA_PLAYER_UNSTUN,  /* a paralyzed character tries to recover (Battle_PlayerTryUnstun) */
    PA_ENEMY_STATUS,   /* an asleep/paralyzed/confused enemy's turn (Battle_DoEnemyTurn) */
    PA_ENEMY_CONFUSED, /* a confused enemy casts FIRE on its own side */
    PA_ENEMY_FLEE,     /* an enemy runs away (morale check) */
} PendingKind;

typedef struct {
    PendingKind kind;
    int attacker;       /* player index (0-3) or enemy index (0-8) */
    int target;         /* enemy index (FIGHT, one-enemy spells) or player index */
    uint8_t effect_id;  /* magic_table index (btl_attackid); $FF = no effect */
    uint8_t status_msg; /* PA_ENEMY_STATUS: the BTLMSG decided while planning */
} PendingAction;

/* One enemy slot (btl_enemyIDs + btl_enemystats; the en_xxx fields). */
typedef struct {
    uint8_t  present;       /* the slot was filled (btl_enemyIDs != $FF) */
    uint8_t  id;            /* enemy_table index */
    uint16_t hp;
    uint16_t maxhp;
    uint8_t  gfx;           /* 2-bit graphic (0/2 small, 1/3 large) */
    uint8_t  pal;           /* palette bit (0/1) */
    uint8_t  ail;           /* en_ailments (BTL_AIL_*) */
    uint8_t  absorb;        /* en_defense */
    uint8_t  numhitsmult;   /* en_numhitsmult: 1; FAST/SLOW move it in 0..2 */
    uint8_t  morale;        /* en_morale (FEAR lowers it) */
    uint8_t  evade;         /* en_evade */
    uint8_t  strength;      /* en_strength */
    uint8_t  aimagpos;      /* AI round-robin magic slot */
    uint8_t  aiatkpos;      /* AI round-robin attack slot */
    uint8_t  fled;          /* ran away: no EXP or gold */
} BattleEnemy;

/* The five combat boxes (bank_0F lut_CombatBoxes). */
enum { CBOX_ATTACKER, CBOX_ATTACK, CBOX_DEFENDER, CBOX_DAMAGE, CBOX_MESSAGE, CBOX_COUNT };

/* A line of font tiles. */
#define BTL_TEXT_MAX 24
typedef struct {
    uint8_t len;
    uint8_t t[BTL_TEXT_MAX];
} BtlText;

/* Character poses (Constants.inc CHARPOSE_*): offsets into
 * lut_CharacterPoseTSA.  POSE_NATURAL = SetNaturalPose's choice. */
#define POSE_STAND    0x00
#define POSE_WALK     0x04
#define POSE_ATTACK_B 0x08
#define POSE_ATTACK_F 0x0C
#define POSE_CHEER    0x10
#define POSE_CROUCH   0x18
#define POSE_NATURAL  0xFF

/* The script a turn (or the battle's start and end) plays: combat boxes
 * going up and down with RespondDelay waits, as the original's DrawCombatBox
 * / RespondDelay / UndrawNBattleBlocks calls do, and the animations, frame by
 * frame.  The rules run first and record the script; anything random in an
 * animation (explosions, the screen shake) is rolled at that point, in the
 * original's order. */
typedef enum {
    EV_SHOW,        /* draw combat box `box` with `text` */
    EV_HIDE_FROM,   /* undraw boxes `box`..4 */
    EV_WAIT,        /* RespondDelay */
    EV_WAIT_INPUT,  /* WaitForAnyInput */
    EV_STATUS,      /* DrawCharacterStatus: `text` holds HP lo,hi + ailments x4 */
    EV_VARPAL,      /* UpdateVariablePalette: a = color, b = 1 for magic */
    EV_WALK,        /* CharacterWalkAnimation: a = char, b = 1 right / 0 left,
                       c = 1: then the natural pose and 2 frames */
    EV_STRIKE,      /* WalkForwardAndStrike's swing: a = char, b = graphic,
                       c = 1 magic, d = 1 no sprite (btlattackspr_nodraw) */
    EV_EXPLODE,     /* DrawExplosions: a,b = pool start (lo,hi), c = count */
    EV_SHAKE,       /* BattleScreenShake: `text` holds 5 x,y scroll pairs */
    EV_FLASH_CHAR,  /* FlashCharacterSprite: a = char */
    EV_ERASE,       /* EraseEnemyGraphic: a = slot */
    EV_CHEER,       /* PlayFanfareAndCheer */
    EV_CHAOS_DEATH, /* ChaosDeath: the dissolve (rolls as it plays; nothing else does) */
    EV_SFX,         /* PlayBattleSFX: a = the effect */
} BtlEventOp;

typedef struct {
    uint8_t op, box, a, b, c, d;
    BtlText text;
} BtlEvent;

#define BTL_EV_MAX   320
#define BTL_EXPL_MAX 640

/* Everything that lives for one battle.  Cleared by Battle_Start(). */
typedef struct {
    BtlCharStats chars[4];      /* btl_chstats (LoadAllCharacterIBStats) */
    int turn_death;             /* the turn's defender ended dead/stone: check for the end */
    uint8_t spell_connected;    /* btlmag_spellconnected (read by the run bug) */
    uint8_t fakeout_ail;        /* btlmag_fakeout_ailments: a magic turn's defender result */
    uint8_t potions[2];         /* btl_potion_heal/pure: not yet claimed by a DRINK */
    int formation;
    int backdrop;               /* backdrop id (0..15) */
    uint8_t backdrop_pal[4];    /* backdrop palette (NES colors) */
    int enemy_count;            /* btl_enemycount: enemies generated (slots may have gaps) */
    int striking_first;         /* btl_strikingfirst: enemies skip the first round */
    int surprised;              /* the party skips its first round of commands */
    BattleEnemy enemy[MAX_ENEMIES];
    uint8_t roster[4];          /* btl_enemyroster: enemy ids named in the roster box ($FF none) */
    int battle_type;            /* formation type: 0=9small 1=4large 2=mix 3=fiend 4=chaos */
    int fiend_gfx;              /* fiend graphic (formation gfx byte) for type 3 */
    int enemy_page;             /* enemy CHR page id (0-15) */
    uint8_t enemy_pal0, enemy_pal1;
    int no_run;
    int respond_delay;          /* frames per RespondDelay */

    /* Command input. */
    BattlePhase phase;
    int cur_char;               /* character whose command is being chosen */
    int cmd_col, cmd_row;       /* command box cursor (2x4 menu; column 1 is RUN) */
    int target_curs;            /* enemy target cursor index (EnemyTargetMenu order) */
    int magic_row;              /* magic cursor: spell level 0-7 (rows 0-3 / 4-7 are the two pages) */
    int magic_col;              /* magic cursor: slot 0-2 */
    int magic_target_enemy;     /* MAGIC_TGT picks an enemy (1) or a character (0) */
    int player_curs;            /* character target cursor (0-3) */
    int item_cursor;            /* equipment cursor: 0-3 weapons, 4-7 armor */
    int drink_cursor;           /* potion cursor: 0 HEAL, 1 PURE */
    int target_is_drink;        /* MAGIC_TGT is choosing a DRINK target */
    BattlePhase nothing_return; /* "Nothing" box up (DoNothingMessageBox): phase to go back to */
    int nothing_box;
    int input_frames;           /* frames spent in the menus (DoFrame_WithInput's RNG roll) */
    int round_after_ev;         /* start the round once the walk back is over */
    uint8_t char_cmd[4];        /* queued command per character (BtlCommand) */
    int8_t  char_target[4];     /* FIGHT target / MAGIC target index */
    uint8_t char_spell[4];      /* MAGIC: spell id (0..63); ITEM: the item's spell ($FF none) */
    uint8_t char_item[4];       /* DRINK: potion (0 HEAL, 1 PURE); ITEM: item id */
    uint8_t char_magicgfx[4][2];/* btlcmd_magicgfx: the chosen spell's graphic and palette */

    /* Round resolution. */
    int round_order[13];        /* shuffled turn order (bank_0C btl_turnorder) */
    int round_turns;            /* number of entries in round_order */
    int round_turn;             /* index of the turn being resolved */
    PendingAction pending;      /* the turn being played */
    int ran;                    /* the party escaped this turn */

    /* The script and what it has put on screen. */
    BtlEvent ev[BTL_EV_MAX];
    int ev_n, ev_i, ev_frame;
    uint8_t expl_pool[BTL_EXPL_MAX][3];   /* explosion tile, x, y per frame */
    int expl_n;
    BtlText cbox[CBOX_COUNT];
    uint8_t cbox_shown;         /* bit per CBOX_* */
    uint16_t disp_hp[4];        /* the status column and sprites as last drawn */
    uint8_t disp_ail[4];
    uint8_t chr_x[4];           /* btl_chardraw_x */
    uint8_t chr_pose[4];        /* btl_chardraw_pose, or POSE_NATURAL */
    uint8_t chr_hidden;         /* bit per character (FlashCharacterSprite) */
    uint8_t enemy_vis[MAX_ENEMIES];     /* still drawn */
    uint8_t enemy_erased[MAX_ENEMIES];  /* rows erased so far */
    uint8_t varpal[4];          /* sprite palette 2 (weapon / magic / explosions) */
    int atk_on, atk_x, atk_y, atk_tile, atk_frame_b, atk_magic, atk_hidell;
    uint8_t bg_flash;           /* DoMagicFlash: 0, or the background flash color */
    uint8_t expl_on[3], expl_x[3], expl_y[3], expl_tile[3];
    int8_t shake_x, shake_y;
    uint8_t bg_erased[0x80];    /* ChaosDeath: erased pixel rows (bit per row) of BG tiles $00-$7F */
    uint8_t chaos_rows[256];    /* ChaosDeath's @tilerowtbl */
    uint8_t chaos_prev;         /* its previous random tile (the next x shake) */

    /* End of battle. */
    BattleResult result;
} BattleState;

extern BattleState g_btl;

/* The four battle sprite palettes (LoadBattleSpritePalettes; battle_draw.c). */
extern const uint8_t (*const btl_sprite_pals)[4];

/* Shared helpers (battle.c). */
int  btl_enemy_exists(int e);
int  btl_spell_at(const SaveData* save, int p, int row, int col);   /* spell id or -1 */
uint8_t btl_equip_item(const SaveData* save, int p, int slot);     /* item id or 0 */
/* Enemy slot `slot`'s rectangle on screen (DrawFormation_* positions). */
void btl_enemy_rect(int slot, int* x, int* y, int* w, int* h);
/* Rows EraseEnemyGraphic clears for this formation's enemies (0 for Chaos). */
int  btl_erase_rows(int slot);
/* Enemy target cursor: slot under cursor index `i`, or -1 (EnemyTargetMenu). */
int  btl_target_slot(int i);
int  btl_target_count(void);
/* SetNaturalPose, from the displayed HP / ailments. */
uint8_t btl_natural_pose(const SaveData* save, int p);
/* Text builders. */
void btl_text_raw(BtlText* t, const uint8_t* raw);           /* DTE string -> tiles */
void btl_text_add_raw(BtlText* t, const uint8_t* raw, int max);
void btl_text_ascii(BtlText* t, const char* s);
void btl_text_add_ascii(BtlText* t, const char* s);
void btl_text_add_number(BtlText* t, unsigned n);
void btl_text_char_name(BtlText* t, const CharacterStats* c);
