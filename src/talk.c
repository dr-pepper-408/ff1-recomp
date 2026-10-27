/**
 * @file talk.c
 * @brief Ports of TalkToObject and the Talk_* routines (bank_0E).
 *
 * Each routine receives the object's 4 bytes of talk data, called [0]-[3]
 * here as in the disassembly's comments: [0] is usually an object id or item
 * index used in a condition, [1]-[3] are dialogue ids.
 */

#include "talk.h"
#include "party.h"
#include "../data/generated/constants.h"
#include "../data/generated/mapobj_data.h"

/* items[] indices (variables.inc: item_lute = items + 1, ...). */
enum {
    ITEM_LUTE = 0x01, ITEM_CROWN, ITEM_CRYSTAL, ITEM_HERB, ITEM_MYSTICKEY, ITEM_TNT,
    ITEM_ADAMANT, ITEM_SLAB, ITEM_RUBY, ITEM_ROD, ITEM_FLOATER, ITEM_CHIME, ITEM_TAIL,
    ITEM_CUBE, ITEM_BOTTLE, ITEM_OXYALE, ITEM_CANOE,
    ORB_FIRE = 0x12, ORB_WATER, ORB_AIR, ORB_EARTH,
};

/* State shared by the routines (tmp..tmp+3, X, and the outputs). */
typedef struct {
    SaveData* s;
    MapObjects* mo;
    int slot;            /* X: the object's slot (for HideThisMapObject) */
    uint8_t d[4];        /* [0]..[3] */
    TalkResult* out;
} Talk;

static int is_visible(const Talk* t, uint8_t id)    { return t->s->game_flags[id] & GMFLG_OBJVISIBLE; }
static int event_flag(const Talk* t, uint8_t id)    { return (t->s->game_flags[id] & GMFLG_EVENT) != 0; }
static void set_event(Talk* t, uint8_t id)          { t->s->game_flags[id] |= GMFLG_EVENT; }
static void hide_this(Talk* t, uint8_t id)          { MapObj_HideThis(t->mo, t->s, id, t->slot); }
static void show(Talk* t, uint8_t id)               { MapObj_Show(t->mo, t->s, id); }
static int all_orbs_lit(const Talk* t) {
    const uint8_t* it = t->s->items;
    return (it[ORB_FIRE] & it[ORB_WATER] & it[ORB_AIR] & it[ORB_EARTH]) != 0;
}

/* Each routine returns the dialogue id (A on exit). */

static uint8_t talk_king_coneria(Talk* t) {
    if (!is_visible(t, OBJID_PRINCESS_2)) return t->d[1];   /* princess still kidnapped */
    if (t->s->bridge_vis) return t->d[3];
    t->s->bridge_vis++;                                      /* build the bridge */
    t->out->sfx++;
    return t->d[2];
}

static uint8_t talk_garland(Talk* t) {
    hide_this(t, OBJID_GARLAND);
    t->out->battle = BTL_GARLAND;
    return t->d[1];
}

static uint8_t talk_princess1(Talk* t) {
    hide_this(t, OBJID_PRINCESS_1);
    show(t, OBJID_PRINCESS_2);
    t->out->normal_tele = NORMTELE_SAVEDPRINCESS;            /* back to Coneria Castle */
    return t->d[1];
}

static uint8_t talk_bikke(Talk* t) {
    if (!event_flag(t, OBJID_BIKKE)) {
        set_event(t, OBJID_BIKKE);
        t->out->battle = BTL_BIKKE;
        return t->d[1];
    }
    if (t->s->ship_vis) return t->d[3];
    t->s->ship_vis++;                                        /* the ship is yours */
    show(t, OBJID_PIRATETERR_1);
    show(t, OBJID_PIRATETERR_2);
    show(t, OBJID_PIRATETERR_3);
    t->out->sfx++;
    return t->d[2];
}

static uint8_t talk_elf_doc(Talk* t) {
    if (event_flag(t, OBJID_ELFPRINCE)) return t->d[3];     /* prince awake */
    if (!t->s->items[ITEM_HERB]) return t->d[1];
    t->s->items[ITEM_HERB]--;
    set_event(t, OBJID_ELFPRINCE);
    t->out->sfx++;
    return t->d[2];
}

static uint8_t talk_elf_prince(Talk* t) {
    if (!event_flag(t, OBJID_ELFPRINCE)) return t->d[3];    /* still asleep */
    if (t->s->items[ITEM_MYSTICKEY]) return t->d[2];
    t->s->items[ITEM_MYSTICKEY]++;
    t->out->sfx++;
    return t->d[1];
}

static uint8_t talk_astos(Talk* t) {
    if (!t->s->items[ITEM_CROWN]) return t->d[1];
    t->s->items[ITEM_CRYSTAL]++;
    hide_this(t, OBJID_ASTOS);
    t->out->battle = BTL_ASTOS;
    t->out->sfx++;
    return t->d[2];
}

static uint8_t talk_nerrick(Talk* t) {
    if (!t->s->items[ITEM_TNT]) return t->d[1];
    t->s->items[ITEM_TNT]--;
    t->s->canal_vis = 0;                                     /* the canal is open */
    hide_this(t, OBJID_NERRICK);
    t->out->sfx++;
    return t->d[2];
}

static uint8_t talk_smith(Talk* t) {
    if (event_flag(t, OBJID_SMITH)) return t->d[3];
    if (!t->s->items[ITEM_ADAMANT]) return t->d[1];
    if (Party_AddWeapon(t->s, WPNID_XCALBUR) != 0) return DLGID_DONTBEGREEDY;
    set_event(t, OBJID_SMITH);
    t->s->items[ITEM_ADAMANT]--;
    t->out->sfx++;
    return t->d[2];
}

static uint8_t talk_matoya(Talk* t) {
    if (t->s->items[ITEM_HERB]) return t->d[3];
    if (t->s->items[ITEM_CRYSTAL]) {                         /* crystal for herb */
        t->s->items[ITEM_HERB]++;
        t->s->items[ITEM_CRYSTAL]--;
        t->out->sfx++;
        return t->d[2];
    }
    return event_flag(t, OBJID_ELFPRINCE) ? t->d[3] : t->d[1];
}

static uint8_t talk_unne(Talk* t) {
    if (event_flag(t, OBJID_UNNE)) return t->d[3];
    if (!t->s->items[ITEM_SLAB]) return t->d[1];
    t->s->items[ITEM_SLAB]--;
    set_event(t, OBJID_UNNE);
    t->out->sfx++;
    return t->d[2];
}

static uint8_t talk_vampire(Talk* t) {
    hide_this(t, OBJID_VAMPIRE);
    t->out->battle = BTL_VAMPIRE;
    return t->d[1];
}

static uint8_t talk_sarda(Talk* t) {
    if (t->s->items[ITEM_ROD] || is_visible(t, OBJID_VAMPIRE)) return t->d[2];
    t->s->items[ITEM_ROD]++;
    t->out->sfx++;
    return t->d[1];
}

/* DoClassChange (bank_0E): every party member gets the promoted class. */
static void do_class_change(SaveData* s) {
    for (int i = 0; i < MAX_PARTY_SIZE; i++) s->characters[i].class += 6;
}

static uint8_t talk_bahamut(Talk* t) {
    if (event_flag(t, OBJID_BAHAMUT)) return t->d[3];
    if (!t->s->items[ITEM_TAIL]) return t->d[1];
    t->s->items[ITEM_TAIL]--;
    set_event(t, OBJID_BAHAMUT);
    do_class_change(t->s);
    t->out->reentermap++;                                    /* DoClassChange's INC */
    t->out->sfx++;
    return t->d[2];
}

static uint8_t talk_ifvis(Talk* t) { return is_visible(t, t->d[0]) ? t->d[2] : t->d[1]; }

static uint8_t talk_subeng(Talk* t) {
    if (!t->s->items[ITEM_OXYALE]) return t->d[1];
    hide_this(t, OBJID_SUBENGINEER);
    return t->d[2];
}

static uint8_t talk_cubebot(Talk* t) {
    if (t->s->items[ITEM_CUBE]) return t->d[2];
    t->s->items[ITEM_CUBE]++;
    t->out->sfx++;
    return t->d[1];
}

static uint8_t talk_princess2(Talk* t) {
    if (t->s->items[ITEM_LUTE]) return t->d[2];
    t->s->items[ITEM_LUTE]++;
    t->out->sfx++;
    return t->d[1];
}

static uint8_t talk_fairy(Talk* t) {
    if (t->s->items[ITEM_OXYALE]) return t->d[2];
    t->s->items[ITEM_OXYALE]++;
    t->out->sfx++;
    return t->d[1];
}

static uint8_t talk_titan(Talk* t) {
    if (!t->s->items[ITEM_RUBY]) return t->d[1];
    t->s->items[ITEM_RUBY]--;
    hide_this(t, OBJID_TITAN);
    t->out->sfx++;
    return t->d[2];
}

static uint8_t talk_canoe_sage(Talk* t) {
    if (t->s->has_canoe || !t->s->items[ORB_EARTH]) return t->d[2];
    t->s->has_canoe++;
    t->out->sfx++;
    return t->d[1];
}

static uint8_t talk_replace(Talk* t) {                      /* the first two ToFR Garlands */
    hide_this(t, t->d[0]);
    show(t, t->d[3]);
    return t->d[1];
}

static uint8_t talk_coo_guy(Talk* t) {
    hide_this(t, t->d[0]);
    return t->d[1];
}

static uint8_t talk_fight(Talk* t) {                        /* final Garland, the Fiends */
    hide_this(t, t->d[0]);
    t->out->battle = t->d[3];
    return t->d[1];
}

static uint8_t talk_ifitem(Talk* t) { return t->s->items[t->d[0]] ? t->d[1] : t->d[2]; }

static uint8_t talk_invis(Talk* t) {                        /* the invisible lady */
    if (!is_visible(t, OBJID_PRINCESS_2) && !t->s->items[ITEM_LUTE]) return t->d[1];
    return t->d[2];
}

static uint8_t talk_ifbridge(Talk* t) { return t->s->bridge_vis ? t->d[1] : t->d[2]; }
static uint8_t talk_ifevent(Talk* t)  { return event_flag(t, t->d[0]) ? t->d[2] : t->d[1]; }

static uint8_t talk_gobridge(Talk* t) {
    if (is_visible(t, OBJID_PRINCESS_2) && !t->s->bridge_vis) return t->d[1];
    return t->d[2];
}

static uint8_t talk_blackorb(Talk* t) {
    if (!all_orbs_lit(t)) return t->d[2];
    hide_this(t, OBJID_BLACKORB);
    t->out->sfx += 2;                                        /* treasure jingle, not fanfare */
    return t->d[1];
}

static uint8_t talk_4orb(Talk* t)     { return all_orbs_lit(t) ? t->d[1] : t->d[2]; }
static uint8_t talk_ifcanoe(Talk* t)  { return t->s->has_canoe ? t->d[1] : t->d[2]; }
static uint8_t talk_ifcanal(Talk* t)  { return t->s->canal_vis ? t->d[2] : t->d[1]; }  /* nonzero = still blocked */

static uint8_t talk_ifkeytnt(Talk* t) {
    if (t->s->items[ITEM_MYSTICKEY] && !t->s->items[ITEM_TNT]) return t->d[1];
    return t->d[2];
}

static uint8_t talk_ifearthvamp(Talk* t) {
    if (!is_visible(t, OBJID_VAMPIRE) && !t->s->items[ORB_EARTH]) return t->d[1];
    return t->d[2];
}

static uint8_t talk_ifairship(Talk* t) { return t->s->airship_vis ? t->d[2] : t->d[1]; }

static uint8_t talk_ifearthfire(Talk* t) {
    if (t->s->items[ORB_EARTH] && !t->s->items[ORB_FIRE]) return t->d[1];
    return t->d[2];
}

static uint8_t talk_cubebot_bad(Talk* t) {                   /* unused duplicate, no fanfare */
    if (t->s->items[ITEM_CUBE]) return t->d[2];
    t->s->items[ITEM_CUBE]++;
    return t->d[1];
}

static uint8_t talk_chime(Talk* t) {
    if (!event_flag(t, OBJID_UNNE)) return t->d[3];         /* can't speak Lefeinish */
    if (t->s->items[ITEM_CHIME]) return t->d[2];
    t->s->items[ITEM_CHIME]++;
    t->out->sfx++;
    return t->d[1];
}

/* Talk_None / Talk_Unused just RTS (A holds leftover pointer data).  The game
 * never calls them: object 0 doesn't exist and object $1F is never placed. */
static uint8_t talk_rts(Talk* t) { (void)t; return 0; }
static uint8_t talk_norm(Talk* t) { return t->d[1]; }

static uint8_t (*const g_talk_fn[TALKFN_COUNT])(Talk*) = {
    [TALKFN_NONE] = talk_rts,              [TALKFN_KINGCONERIA] = talk_king_coneria,
    [TALKFN_GARLAND] = talk_garland,       [TALKFN_PRINCESS1] = talk_princess1,
    [TALKFN_BIKKE] = talk_bikke,           [TALKFN_ELFDOC] = talk_elf_doc,
    [TALKFN_ELFPRINCE] = talk_elf_prince,  [TALKFN_ASTOS] = talk_astos,
    [TALKFN_NERRICK] = talk_nerrick,       [TALKFN_SMITH] = talk_smith,
    [TALKFN_MATOYA] = talk_matoya,         [TALKFN_UNNE] = talk_unne,
    [TALKFN_VAMPIRE] = talk_vampire,       [TALKFN_SARDA] = talk_sarda,
    [TALKFN_BAHAMUT] = talk_bahamut,       [TALKFN_IFVIS] = talk_ifvis,
    [TALKFN_SUBENG] = talk_subeng,         [TALKFN_CUBEBOT] = talk_cubebot,
    [TALKFN_PRINCESS2] = talk_princess2,   [TALKFN_FAIRY] = talk_fairy,
    [TALKFN_TITAN] = talk_titan,           [TALKFN_CANOESAGE] = talk_canoe_sage,
    [TALKFN_NORM] = talk_norm,             [TALKFN_REPLACE] = talk_replace,
    [TALKFN_FIGHT] = talk_fight,           [TALKFN_UNUSED] = talk_rts,
    [TALKFN_IFITEM] = talk_ifitem,         [TALKFN_INVIS] = talk_invis,
    [TALKFN_IFBRIDGE] = talk_ifbridge,     [TALKFN_IFEVENT] = talk_ifevent,
    [TALKFN_GOBRIDGE] = talk_gobridge,     [TALKFN_4ORB] = talk_4orb,
    [TALKFN_IFCANOE] = talk_ifcanoe,       [TALKFN_IFCANAL] = talk_ifcanal,
    [TALKFN_IFKEYTNT] = talk_ifkeytnt,     [TALKFN_IFEARTHVAMP] = talk_ifearthvamp,
    [TALKFN_IFAIRSHIP] = talk_ifairship,   [TALKFN_IFEARTHFIRE] = talk_ifearthfire,
    [TALKFN_COOGUY] = talk_coo_guy,        [TALKFN_CUBEBOTBAD] = talk_cubebot_bad,
    [TALKFN_CHIME] = talk_chime,           [TALKFN_BLACKORB] = talk_blackorb,
};

void Talk_ToObject(SaveData* save, MapObjects* mo, int slot, TalkResult* out) {
    out->dialogue = 0;
    out->sfx = 0;
    out->battle = -1;
    out->normal_tele = -1;
    out->reentermap = 0;

    uint8_t id = mo->obj[slot].id;
    Talk t = { save, mo, slot, { 0, 0, 0, 0 }, out };
    for (int i = 0; i < 4; i++) t.d[i] = lut_MapObjTalkData[id * 4 + i];
    out->dialogue = g_talk_fn[lut_MapObjTalkFn[id]](&t);
}
