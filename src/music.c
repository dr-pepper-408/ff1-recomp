/**
 * @file music.c
 * @brief The music driver: a port of bank_0D's MusicPlay, Music_NewSong,
 *        Music_DoScore and the Music_* score handlers.
 *
 * Each channel (square 1, square 2, triangle) walks its score: notes
 * ($00-$BF: pitch in the high nibble, length in the low), rests ($Cx),
 * loops ($D0 forever, $D1-$D7 that many times, then a 2-byte address),
 * octave ($D8-$DF), envelope pattern ($E0-$EF), envelope speed ($F8 xx),
 * tempo ($F9-$FE) and the end of the song ($FF).  Each frame the channels'
 * frequency and volume (stepping through the envelope pattern) are written
 * to the APU.  The driver reads its tables through pointers; here they're
 * offsets into music_tables, which keeps the ROM's layout (so the bugged
 * octaves $DC-$DF read past the frequency table as the original does).
 */

#include "music.h"
#include "apu.h"
#include "../data/generated/music_data.h"

enum { CHAN_SQ1, CHAN_SQ2, CHAN_TRI, CHAN_STOP };

typedef struct {
    uint16_t scoreptr;          /* ch_scoreptr (bank address) */
    uint16_t envptr;            /* ch_envptr:    offset in music_tables */
    uint16_t lentblptr;         /* ch_lentblptr */
    uint16_t frqtblptr;         /* ch_frqtblptr */
    uint8_t envrate, envpos, envrem;
    uint8_t lenctr, vol, loopctr;
    uint8_t freq_lo, freq_hi;   /* ch_freq: high bit of the high byte = no change */
} Channel;

uint8_t g_music_track;
uint8_t g_sq2_sfx;
uint8_t g_music_followup;
static uint8_t mu_chanprimer;
static uint8_t current_song;
static Channel ch[4];

static uint8_t score(uint16_t addr) {
    unsigned i = (unsigned)(addr - MUSIC_SCORE_BASE);
    return i < sizeof(lut_ScoreData) ? lut_ScoreData[i] : 0xFF;
}

static uint8_t table(unsigned off) {
    return off < sizeof(music_tables) ? music_tables[off] : 0;
}

/* lut_OctaveOffset is 6 entries; octaves 6 and 7 read the 2 bytes after
 * it, the start of lut_NoteFreqs. */
static uint8_t octave_offset(int i) {
    return i < 6 ? lut_OctaveOffset[i] : music_tables[MUSIC_TBL_NOTEFREQS + i - 6];
}

static void do_score(int c);

/* Music_LoopCode ($D0-$D7). */
static void loop_code(int c, uint8_t code) {
    Channel* k = &ch[c];
    if (k->loopctr == 0) {
        k->loopctr = code & 0x0F;               /* @StartLoop */
    } else if (--k->loopctr == 0) {
        k->scoreptr += 3;                       /* done: past the loop */
        return;
    }
    uint16_t p = k->scoreptr;                   /* @ResumeLoop */
    k->scoreptr = (uint16_t)(score(p + 1) | (score(p + 2) << 8));
}

/* Music_ApplyLength(_Rest): note lengths from the tempo's table. */
static void apply_length(int c, uint8_t code, int restart_env) {
    Channel* k = &ch[c];
    k->lenctr = table(k->lentblptr + (code & 0x0F));
    if (restart_env) k->envpos = k->envrem = 0;
}

/* Music_DoScore: process score bytes until a note or rest (or the end). */
static void do_score(int c) {
    Channel* k = &ch[c];
    for (;;) {
        uint8_t a = score(k->scoreptr);
        if (a < 0xC0) {                         /* Music_Note */
            int idx = (a >> 3) & 0x1E;          /* Music_ApplyTone */
            k->freq_lo = table(k->frqtblptr + idx);
            k->freq_hi = table(k->frqtblptr + idx + 1);
            apply_length(c, a, 1);
            k->scoreptr++;
            return;
        }
        if (a < 0xD0) {                         /* Music_Rest */
            apply_length(c, a, 0);
            if (c == CHAN_TRI) k->freq_hi = 0xFF;   /* only the triangle rests */
            k->scoreptr++;
            return;
        }
        if (a < 0xD8) { loop_code(c, a); continue; }
        if (a < 0xE0) {                         /* Music_SetOctave */
            k->frqtblptr = (uint16_t)(MUSIC_TBL_NOTEFREQS + octave_offset(a & 7));
            k->scoreptr++;
            continue;
        }
        if (a < 0xF0) {                         /* Music_SetEnvPattern */
            k->envptr = (uint16_t)(MUSIC_TBL_ENVPATTERNS + (a & 0x0F) * 32);
            k->scoreptr++;
            continue;
        }
        if (a == 0xFF) {                        /* the end: silence next frame */
            g_music_track = 0x80;
            return;
        }
        if (a == 0xF8) {                        /* envelope speed */
            k->envrate = table(MUSIC_TBL_ENVSPEEDS + (score(k->scoreptr + 1) & 0x0F));
            k->scoreptr += 2;
            continue;
        }
        if (a >= 0xF9)                          /* Music_SetTempo */
            k->lentblptr = (uint16_t)(MUSIC_TBL_NOTELENGTHS + ((a - 9) & 0x0F) * 16);
        k->scoreptr++;                          /* ($F0-$F7 do nothing) */
    }
}

/* Music_NewSong: halt the channels, then prime one channel a frame. */
static void new_song(void) {
    static const int primer_to_chan[4] = { CHAN_SQ1, CHAN_SQ2, CHAN_TRI, CHAN_STOP };
    if (g_music_track >= 0x40) {
        current_song = g_music_track;
        g_music_track &= 0x3F;
        mu_chanprimer = 0;
        g_sq2_sfx = 0;
        static const uint16_t zero[] = { 0x4002, 0x4003, 0x4006, 0x4007, 0x400A, 0x400B, 0x400E };
        for (unsigned i = 0; i < sizeof(zero) / sizeof(zero[0]); i++) Apu_Write(zero[i], 0);
        Apu_Write(0x4000, 0x30);
        Apu_Write(0x4004, 0x30);
        Apu_Write(0x4008, 0x30);
        Apu_Write(0x400C, 0x30);
    }
    int song = (g_music_track - 1) & 0x3F;
    int c = primer_to_chan[mu_chanprimer >> 1];
    unsigned i = (unsigned)(song * 8 + mu_chanprimer);
    ch[c].scoreptr = (uint16_t)(lut_ScoreData[i] | (lut_ScoreData[i + 1] << 8));
    do_score(c);
    mu_chanprimer += 2;
    if (mu_chanprimer >= 6) {                   /* all three primed: play */
        g_music_track = 0;
        ch[CHAN_SQ1].loopctr = ch[CHAN_SQ2].loopctr = ch[CHAN_TRI].loopctr = ch[CHAN_STOP].loopctr = 0;
        Apu_Write(0x4015, 0x0F);
        g_sq2_sfx = 0;
        static const uint16_t zero[] = { 0x4002, 0x4003, 0x4006, 0x4007, 0x400A, 0x400B };
        for (unsigned j = 0; j < sizeof(zero) / sizeof(zero[0]); j++) Apu_Write(zero[j], 0);
    }
}

uint8_t Music_VehicleTrack(int vehicle) {
    int v = vehicle & 0x0F;
    return lut_VehicleMusic[v < 9 ? v : 1];
}
uint8_t Music_CurrentSong(void) { return current_song; }
uint8_t Music_TilesetTrack(int tileset) { return lut_TilesetMusicTrack[tileset & 7]; }

void Music_Play(void) {
    if (g_music_followup && (g_music_track & 0x80)) g_music_track = g_music_followup;
    if (g_music_track & 0x80) {                 /* the song is over */
        if (g_music_track == 0x80) {
            Apu_Write(0x4000, 0x70);            /* silence the channels, once */
            Apu_Write(0x4004, 0x70);
            Apu_Write(0x4008, 0x70);
            g_music_track++;
        }
        return;
    }
    if (g_music_track) { new_song(); return; }

    /* Square 1: 25% duty, no sweep; the frequency only when it changed
     * (writing $4003 restarts the duty cycle). */
    Channel* k = &ch[CHAN_SQ1];
    Apu_Write(0x4000, (uint8_t)(k->vol | 0x70));
    Apu_Write(0x4001, 0x7F);
    if (!(k->freq_hi & 0x80)) {
        Apu_Write(0x4002, k->freq_lo);
        Apu_Write(0x4003, k->freq_hi);
        k->freq_hi = 0x80;
    }

    /* Square 2, unless a sound effect has it. */
    k = &ch[CHAN_SQ2];
    int sq2_force = 0, sq2 = 1;
    if (g_sq2_sfx) {
        if (--g_sq2_sfx) sq2 = 0;
        else sq2_force = 1;                     /* the effect is over: resume at once */
    }
    if (sq2) {
        Apu_Write(0x4004, (uint8_t)(k->vol | 0x30));
        Apu_Write(0x4005, 0x7F);
        if (sq2_force || !(k->freq_hi & 0x80)) {
            Apu_Write(0x4006, k->freq_lo);
            Apu_Write(0x4007, k->freq_hi & 0x0F);
            k->freq_hi = (uint8_t)((k->freq_hi & 0x0F) | 0x80);
        }
    }

    /* Triangle: a high byte of $FF is a rest. */
    k = &ch[CHAN_TRI];
    if (!(k->freq_hi & 0x80)) {
        Apu_Write(0x4008, 0x8F);
        Apu_Write(0x400A, k->freq_lo);
        Apu_Write(0x400B, k->freq_hi);
        k->freq_hi = 0x80;
    } else if (k->freq_hi == 0xFF) {
        Apu_Write(0x4008, 0x80);
    }

    /* Then the score: a new note when the length runs out, else the next
     * step of the envelope (speed in eighths of a step a frame). */
    for (int c = CHAN_SQ1; c < CHAN_STOP; c++) {
        k = &ch[c];
        int pos;
        if (--k->lenctr == 0) {
            do_score(c);
            pos = k->envpos;
        } else {
            unsigned sum = (unsigned)k->envrate + k->envrem;
            k->envrem = (uint8_t)sum;
            if (k->envrem < 8) continue;
            k->envrem &= 7;
            pos = k->envpos + ((sum & 0xFF) >> 3);
        }
        if (pos >= 0x20) pos = 0x1F;
        k->envpos = (uint8_t)pos;
        k->vol = table(k->envptr + pos);
    }
}
