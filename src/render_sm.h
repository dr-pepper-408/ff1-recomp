#pragma once

/** Draw the 16x15 standard-map viewport at (scroll_x, scroll_y) with a
 *  sub-tile pixel offset, using the loaded map and tileset (sm_map.h) and BG
 *  palettes 0-3 (from the attribute bits of each metatile). */
void SMRender_Draw(int scroll_x, int scroll_y, int sub_x, int sub_y);

/** Draw metatile `tile` at map tile (x, y) instead of the map's own tile, as
 *  RedrawDoor does when a door opens or closes; tile < 0 removes it.  Reset
 *  when a map is loaded. */
void SMRender_SetDoorTile(int x, int y, int tile);

/** The tile drawn in place of the map's at (x, y), or -1 (the door). */
int SMRender_DoorTileAt(int x, int y);
