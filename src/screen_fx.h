#pragma once
#include <stdint.h>

/*
 * screen_fx.h — FF1's screen transitions, stepped one frame at a time
 * (no SDL dependency).
 *
 *   SCREENFX_WIPE_OPEN / _CLOSE    ScreenWipe_Open / _Close (bank_0F): map
 *                                   changes.  A band of visible lines grows
 *                                   from / shrinks to the screen center.
 *   SCREENFX_PALCYC_OUT / _IN      CyclePalettes (bank_0F): into the menu and
 *                                   back.  Background colors step through the
 *                                   hues to black, or back from black.
 *   SCREENFX_BATTLE                BattleTransition (bank_0F): cycles the
 *                                   color-emphasis bits before a battle.
 *   SCREENFX_ALTAR                 DoAltarEffect (bank_0F): a diagonal beam of
 *                                   monochrome scanlines when an orb is lit.
 *
 * The originals block until done; here the caller draws the scene each frame,
 * calls ScreenFx_Apply() on top, and ScreenFx_Step() once per frame.  Sprites
 * are hidden during the transitions (the originals clear OAM or disable sprite
 * rendering), but not during the altar effect: see ScreenFx_ShowsSprites().
 */

typedef enum {
    SCREENFX_NONE,
    SCREENFX_WIPE_OPEN,
    SCREENFX_WIPE_CLOSE,
    SCREENFX_PALCYC_OUT,
    SCREENFX_PALCYC_IN,
    SCREENFX_BATTLE,
    SCREENFX_ALTAR,
} ScreenFxKind;

typedef struct {
    ScreenFxKind kind;
    int counter;          ///< Frame counter (meaning depends on the effect).
    int wipe_top;         ///< ScreenWipe tmp+4 (first visible line + 11).
    int wipe_rows;        ///< ScreenWipe tmp+5 (number of visible lines).
    uint8_t pal[16];      ///< CyclePalettes tmp_pal: the cycling BG palette.
    uint8_t target[16];   ///< CyclePalettes target (cycling in: the map palette).
    int altar_delay;      ///< DoAltarEffect tmp:   lines before the beam.
    int altar_lines;      ///< DoAltarEffect tmp+1: lines in the beam.
    int altar_retract;    ///< DoAltarEffect tmp+2: 0 expanding, 1 retracting.
} ScreenFx;

/** Start an effect.  `bg_pal` is the scene's 16 background colors (needed by
 *  the palette cycles; may be NULL otherwise). */
void ScreenFx_Start(ScreenFx* fx, ScreenFxKind kind, const uint8_t bg_pal[16]);

/** Advance one frame.  @return 1 when the effect has finished (nothing more
 *  to show), else 0. */
int ScreenFx_Step(ScreenFx* fx);

/** Apply the current frame of the effect to the PPU (call after drawing the
 *  scene). */
void ScreenFx_Apply(const ScreenFx* fx);

/** 1 if sprites stay visible during this effect. */
int ScreenFx_ShowsSprites(const ScreenFx* fx);
