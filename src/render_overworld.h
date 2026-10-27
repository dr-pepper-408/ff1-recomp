/**
 * @file render_overworld.h
 * @brief Overworld viewport and player-sprite renderer (draws into ppu.h).
 */

#pragma once
#include <stdint.h>

/** Number of visible tile columns in the NES viewport. */
#define VIEWPORT_COLS   16

/** Number of visible tile rows in the NES viewport. */
#define VIEWPORT_ROWS   15

/** Draw the overworld viewport (BG palettes 0-3 from each metatile's
 *  attribute bits).
 *  @param scroll_x   Viewport left tile coordinate (wraps at 256).
 *  @param scroll_y   Viewport top  tile coordinate (wraps at 256).
 *  @param sub_x      Sub-tile X pixel offset for smooth scrolling.
 *  @param sub_y      Sub-tile Y pixel offset for smooth scrolling. */
void OWRender_Draw(int scroll_x, int scroll_y, int sub_x, int sub_y);

/** Draw the party at its fixed screen spot (DrawPlayerMapmanSprite).
 *  @param vehicle      1 foot, 2 canoe, 4 ship, 8 airship.
 *  @param facing       BTN_RIGHT/LEFT/DOWN/UP.
 *  @param mapman_class Lead party member's class (the on-foot graphics).
 *  @param move_ctr     Pixels into the current step (bit 3 picks the frame).
 *  @param inforest     On foot in a forest: the bottom half is hidden.
 *  @param framecounter Animates the airship. */
void OWRender_DrawPlayer(int vehicle, uint8_t facing, uint8_t mapman_class, int move_ctr,
                         int inforest, uint8_t framecounter);

/** Overworld objects (lut_OWObjectSprTbl rows). */
enum { OWOBJ_AIRSHIP_SHADOW, OWOBJ_BRIDGE, OWOBJ_CANAL };

/** Draw overworld object `object` at map tile (obj_x, obj_y) if it is on
 *  screen (ConvertOWToSprite), with the viewport given as to OWRender_Draw. */
void OWRender_DrawObject(int object, int obj_x, int obj_y, int scroll_x, int scroll_y,
                         int sub_x, int sub_y);

/** AirshipTransitionFrame's airship at sprite y `y` (taking off or landing),
 *  with its shadow. */
void OWRender_DrawAirshipAt(int y, uint8_t framecounter);

/** Draw the parked ship (4) or landed airship (8) at map tile (obj_x, obj_y). */
void OWRender_DrawVehicle(int vehicle, int obj_x, int obj_y, int scroll_x, int scroll_y,
                          int sub_x, int sub_y);
