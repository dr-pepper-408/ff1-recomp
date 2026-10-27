#pragma once
#include <stdint.h>
#include "input.h"
#include "../data/generated/types.h"

/*
 * story.h -- the story screens (bank_0D): the bridge scene, shown the first
 * time the party steps on the Coneria bridge.  Pages of story, then of
 * credits, each revealed and hidden by the "shutter" effect over the
 * bridge picture.
 */

/** LoadBridgeSceneGFX + EnterBridgeScene. */
void Story_OpenBridge(void);

/** LoadEpilogueSceneGFX + EnterEndingScene: after Chaos.  It never ends
 *  (the original waits forever on "The End"). */
void Story_OpenEnding(void);

/** 1 once the ending's "The End" is fully drawn. */
int Story_Finished(void);

/** One frame.  @return 1 once the bridge scene is over (bridgescene set to
 *  $80); the ending never returns 1. */
int Story_Update(SaveData* save, const InputState* in);

/** Draw the scene. */
void Story_Draw(void);

/** The scene's 16 BG colors (lut_BridgeBGPal), for the palette cycle out. */
const uint8_t* Story_Palette(void);
