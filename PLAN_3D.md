# 3D presentation plan (LÖVE front end)

A 3D view of the same game, in the spirit of Gen1Recomp's voxel mode. The
C port stays the game: `ff1core.dll` runs it frame by frame, the LÖVE front
end in `love/` draws it. The 3D view only *reads* the game state; nothing in
the game logic changes for it, and the SDL build keeps working.

## Principles

1. **The game stays in C, unchanged.** The 3D view reads `GameContext`, the
   map data and what the PPU drew; it never feeds anything back.
2. **Generated from the ROM's graphics.** Shapes come from the original
   tiles' pixels and properties. The only hand-made data is presentation
   tuning (a height per tile class and colour index), kept in one table.
3. **Heavy work in C, presentation in Lua.** Meshing, height fields and
   atlases are built in C (`view3d_world.c`, `view3d_battle.c`); Lua handles camera, drawing, shaders.
4. **2D is always there.** F2 switches views; anything the 3D view can't
   show yet falls back to the 2D picture.
5. **Characters stay 2D.** The party, NPCs, vehicles and enemies keep their
   original sprites as flat cutouts facing the camera, in the style of
   Octopath Traveler (HD-2D) and Paper Mario, with a soft ground shadow.
   Only the world (terrain, buildings) is built in 3D.
6. **Checked by screenshots.** `lovec love --shot` captures the view; each
   step adds or updates shots, and the C tests keep passing.

## Steps

| # | Step | Notes |
|---|------|-------|
| 3D.0 | ✅ `ff1core.dll` + LÖVE front end: 2D picture, audio, input, save; first 3D overworld (raised tiles, sprite cutouts, camera, fog) | `ff1core.c`, `love/main.lua` |
| 3D.1 | ✅ **Voxel overworld**, built the way the Gen1Recomp voxel mod reads its art (a three-quarter view, not a height map): mountains and canopies stand up from their drawn silhouettes (grass = ground), mountains as cliffs rising in steps to the ridge, south faces fold the art upright one row per pixel, other sides take the material colour past the outline. Chunks of 16x16 tiles, faces merged, built ahead, wrapped | ~2.4M vertices, 60 fps; buildings by their own profile since 3D.4b |
| 3D.2 | ✅ Coastlines and river banks follow the drawn shore (grass at ground height, water below, a 4-pixel lip); the atlas is rebuilt every frame so palette changes reach it | the original doesn't animate its overworld water, so neither does this (a shimmer would belong to 3D.8) |
| 3D.3 | ✅ Sprites turn to face the camera about their metasprite's centre; the party's group is anchored to the party, vehicles at their drawn height (the ship sits low in the water, the airship flies above its shadow); what the original leaves out of the picture is logged for 3D: the party's lower half in a forest (the real canopy hides it) and the airship's shadow on its flicker-off frames | `Ppu_LogSprite` |
| 3D.B2 | ✅ Battle cameras: fixed shots (right shoulder, left shoulder, high, low side), one picked at random as a battle starts; bosses (no-run formations, fiends, Chaos) always get the boss shot (low, from the side); the overworld's Q/E no longer turn it. The party stands on a slight diagonal that fans out from the camera's side; enemies' black reaching their picture's edge is air (the fiends' and Chaos's black blocks); outlined cutouts on a dimmer stage | `BATTLE_CAMS`, `BOSS_CAM` in love/main.lua |
| 3D.4b | ✅ Overworld buildings by a per-tile profile: a BODY stands as tall as it is drawn (each column rises by its run of drawn rows, the front shows the whole drawing, the top wears its top row), a WALL raises only its outline, FLAT tiles (gates, paths) lie on the ground; each tile names which colours are floor (grass, courtyard grey). Coneria is profiled; other buildings use the default body rule (custom 3D models and 2D cards were tried and dropped: buildings keep their drawn-height voxels) | `building_profile` in view3d_world.c |
| 3D.B | ✅ **Battle diorama**: the backdrop stands as a painted wall over a floor continuing its bottom row; enemies (cut from the picture, their black as air) and the party stand on it as 2D cutouts, the screen's layout turned into the stage's (screen x = depth, y = across); weapons, spells and explosions stand with what they belong to; the camera sits behind the party's right shoulder; the menus and status column stay the 2D picture; the shake moves the camera, the magic flash lights the dark | `Battle_GetView`, `ff1_battle_scene`; `--battle=N --battlecam=N --auto --frames=N` |
| 3D.4 | ✅ Standard maps (towns, castles, dungeons) in 3D: the same mesher over the 64x64 map with its tileset; shapes worked out from the map itself (walkable tiles are floor, the colour most floor is drawn in is the ground blocked drawings stand on, mostly-blue blocked tiles are water, solid drawings (walls, towers, roofs: under a fifth ground colour) are tile VOLUMES -- 16x16 boxes as tall as the column's run of solid tiles (up to 2), the facade upright on the front and wrapped round the sides in 16-pixel bands, plain material tops, roofs' own art on the deeper tiles -- and objects -- trees, fences, lamps, furniture (non-solid blocked drawings that stand alone or in rows, not rock filling a cave), treasure chests and doors -- 2D cards like the characters, their floor colour cut away, on plain floor with a soft shadow; cave rock stays silhouettes); the door being opened is drawn in place; NPCs are 2D cutouts with shadows | rooms always shown open in 3D (their tiles -- blanked to one flat colour outdoors, on maps with doors -- always in the room palette, the rest in the outdoor one; NPCs on the other side of a door logged, not drawn); the 2D picture keeps the original's hidden rooms; per-tileset tuning later; `--enter=N` |
| 3D.5 | ✅ Dialogue boxes over the 3D view (talking keeps the world drawn; the box is the 2D picture's). The full-screen menus (main menu, shops, lineup) stay 2D: the original replaces the screen with them, and their black can't be told from the boxes' | `ff1_view3d_overlay`; `--dialogue=N` |
| 3D.6 | ✅ Screen effects in 3D, from what ScreenFx_Apply leaves in the PPU: palette cycling (the atlas is the current palette), the screen wipe (only its rows show), the battle transition (emphasis as a tint), the lava flash (monochrome), the altar's beam (its rectangle drawn again in grey), the shake (the camera) | `ff1_view3d_fx` |
| 3D.7 | ✅ Camera presets (C: classic, close, high, low third-person), easing between them; view distance (V: near 420, normal 560, far 760 -- far is ~3.7M vertices, still 60 fps here); zoom on top; the preset and distance kept in LOVE's save folder | `--camera=N` |
| 3D.8 | ✅ Extras, not in the original, each on its own key and remembered: F5 tilt-shift blur (HD-2D's miniature look; on), F6 water shimmer (on), F7 day / night through a six-minute day (off), F8 the battle camera gliding into its shot and drifting a little (on); conveniences: F9 random encounters on / off (the roll still runs; fixed battles stay; Game_SetRandomEncounters), F10 battle speed 1x / 2x / 4x (the last frame's sound kept) | `--daytime=F` |

## Status

Steps 3D.0–3D.8 are done: the overworld, towns, dungeons and battles all
have a 3D view. Still open:

- real lighting and shadows (now: soft shadows under the cutouts and the
  F7 day / night tint);
- a 3D battle stage beyond the diorama;
- profiles for the overworld buildings other than Coneria (3D.4b; they use
  the default body rule);
- per-tileset tuning of towns and dungeons (3D.4; every tileset uses the
  same rules).

## Running

```
cmake --build build
& "C:\Program Files\LOVE\lovec.exe" love          # PowerShell; add --new to skip the title
& "C:\Program Files\LOVE\lovec.exe" love --shot   # new game, 3D screenshot, quit
```
