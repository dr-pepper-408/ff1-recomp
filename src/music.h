#pragma once
#include <stdint.h>

/*
 * music.h -- FF1's music driver (bank_0D MusicPlay): reads the score data
 * and writes the APU's square 1, square 2 and triangle registers once per
 * frame.  Square 2 is lent to sound effects while sq2_sfx counts down.
 */

/* Track numbers the game writes to music_track ($40 | song number). */
enum {
    MUSIC_PRELUDE = 0x41,       ///< the crystal theme (intro, title, minimap)
    MUSIC_BRIDGE = 0x42,        ///< bridge scene, minigame
    MUSIC_ENDING = 0x43,        ///< epilogue
    MUSIC_OVERWORLD = 0x44,
    MUSIC_SHIP = 0x45,
    MUSIC_AIRSHIP = 0x46,
    MUSIC_SHOP = 0x4F,
    MUSIC_BATTLE = 0x50,
    MUSIC_MENU = 0x51,
    MUSIC_GAME_OVER = 0x52,
    MUSIC_VICTORY = 0x53,
    MUSIC_FANFARE = 0x54,       ///< special item fanfare
    MUSIC_FANFARE_SAVED = 0x56, ///< "you saved your game" jingle
    MUSIC_LINEUP = 0x55,        ///< the lineup menu
    MUSIC_HP_JINGLE = 0x57,     ///< HP recovered
    MUSIC_TREASURE = 0x58,      ///< treasure chest jingle
};

/** music_track: write a track number ($41+) to start it; $00 while one
 *  plays; $80/$81 once it has ended and the channels are silenced. */
extern uint8_t g_music_track;

/** sq2_sfx: frames square 2 still belongs to a sound effect. */
extern uint8_t g_sq2_sfx;

/** btl_followupmusic: in battle, the track restarted whenever the current
 *  one has ended (0 outside battle). */
extern uint8_t g_music_followup;

/** MusicPlay: one frame of music (in battle, first the follow-up track if
 *  the song has ended: BattleUpdateAudio). */
void Music_Play(void);

/** The track ($41+) most recently started, for tests. */
uint8_t Music_CurrentSong(void);

/** PrepOverworld's @lut_VehicleMusic: the overworld's track for `vehicle`. */
uint8_t Music_VehicleTrack(int vehicle);

/** PrepStandardMap's @lut_TilesetMusicTrack: a standard map's track. */
uint8_t Music_TilesetTrack(int tileset);
