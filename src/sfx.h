#pragma once
#include <stdint.h>
#include "input.h"

/*
 * sfx.h -- sound effects outside battle: ports of the routines that write
 * the APU directly (PlaySFX_MenuSel / _MenuMove / _Error, PlayDoorSFX,
 * DialogueBox_Sfx, MapPoisonDamage, MapTileDamage, the ship and airship
 * noise, Minimap_DrawSFX).  Effects on square 2 hold it for sq2_sfx frames
 * so the music leaves it alone.
 */

void Sfx_MenuSel(void);         ///< PlaySFX_MenuSel: a selection (A or B)
void Sfx_MenuMove(void);        ///< PlaySFX_MenuMove: the cursor moved

/** PlaySFX_Error: the buzz.  The original plays it in a 16-frame loop of
 *  its own, so the game (and the music) stop meanwhile: see Sfx_ErrorFrame. */
void Sfx_Error(void);

/** While the error buzz runs: one of its frames.  @return 1 if it ran (the
 *  game skips the frame). */
int Sfx_ErrorFrame(void);

/** The frame routines of shops, the equip menu, party generation and the
 *  lineup menu (ShopFrame, EquipMenuFrame, PtyGen_Joy, LineupMenu_UpdateJoy):
 *  A or B plays the selection, a newly pressed direction in `dirs` the move. */
void Sfx_JoyFrame(const InputState* in, uint8_t dirs);

void Sfx_Door(void);            ///< PlayDoorSFX
void Sfx_DialogueOpen(void);    ///< ShowDialogueBox: the upward sweep
void Sfx_DialogueClose(void);   ///< the downward sweep
void Sfx_PoisonStep(void);      ///< MapPoisonDamage, each frame of a step
void Sfx_DamageTile(void);      ///< MapTileDamage, each frame of a step on lava/frost

/** EnterOverworldLoop: the waves (ship) or the engine (airship), each frame. */
void Sfx_VehicleFrame(int vehicle, uint8_t framecounter);

/** AirshipTransitionFrame's engine noise. */
void Sfx_AirshipTransition(uint8_t framecounter);

void Sfx_NoiseOff(void);        ///< $400C = $00 (AnimateAirshipLanding)
void Sfx_NoiseSilence(void);    ///< $400C = $30 (DockShip)
void Sfx_MinimapDraw(void);     ///< Minimap_DrawSFX: a frame of map drawing

/** PlayBattleSFX: 0 the magic bleeps, 1 an enemy's hit ("boom bash"),
 *  2 a hit on an enemy ("cha").  Holds square 2 for the effect's length. */
void Sfx_BattlePlay(int id);

/** UpdateBattleSFX: a frame of the battle sound effect (BattleUpdateAudio). */
void Sfx_BattleUpdate(void);

/** ChaosDeath_FadeNoise: the rumble one step quieter ($9C - 1 to $400C). */
void Sfx_ChaosRumble(void);
void Sfx_ChaosRumbleStart(void); ///< ChaosDeath: $4015 = 8 and the rumble at full volume
void Sfx_ChaosRumbleStop(void);  ///< and at its end, $4015 = 0
