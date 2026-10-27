# FF1-recomp

A native port of *Final Fantasy* (NES, 1990) to C99, written routine by
routine from a disassembly of the original game rather than by emulating
the 6502. The game logic follows the original's, including its bugs (marked
`BUGGED` in the source).

It has two front ends:

- **SDL3** (`ff1-recomp`): the game as the NES showed it, pixel for pixel.
- **LÖVE** (`love/`): the same game, with an optional 3D view in the style
  of HD-2D games: a voxel overworld, towns and dungeons in 3D, characters as
  2D cutouts, a battle diorama, camera presets and a few extras (tilt-shift,
  day and night). F2 switches between 2D and 3D.

> **No game data is included.** This repository contains code only. To
> build and play it you need your own copy of *Final Fantasy* (USA) for the
> NES, as a ROM file. Nothing from the game is committed: the data is
> read from your ROM when the game starts.

## Status

The whole game is playable: title screen, party creation, the overworld
and its vehicles, towns, shops, dungeons, battles, menus, saving, music and
sound, the minigame, and the ending. See `PLAN.md` (the port),
`PLAN_3D.md` (the 3D view, with its open points) and `PLAN_ROM.md`
(reading the data from your ROM).

## Requirements

- CMake 3.10+ and a C99 compiler (tested with MinGW-w64 GCC on Windows)
- SDL3 is included under `src/SDL3/` (zlib licence)
- [LÖVE 11.5](https://love2d.org) for the 3D front end
- To play: a ROM of **Final Fantasy (USA)** for the NES, the original 1990
  North American release, unmodified (tested with a dump named
  `Final Fantasy (USA).nes`). The file's 16-byte header may be iNES or
  NES 2.0, or missing; what is checked is the 256 KB of game data after it,
  whose SHA-1 must be `c9cfbf5455085e198dce039298b083cd6fc88bce`.
  Other versions don't work: the Japanese release, the Famicom
  *Final Fantasy I・II*, the remakes, and translated or hacked ROMs.

The game reads everything it needs from the ROM when it starts
(`src/rom.c`, `src/romdata.c`); nothing from the game is built in.

## Build

```sh
cmake -S . -B build
cmake --build build
```

Tests (they need the ROM: set `FF1_ROM` to its path):

```sh
build/ff1-tests                    # unit tests of the game logic
build/ff1-screenshots <dir>        # scripted scenes, PASS/FAIL, PPM screenshots
```

## Release

```sh
python tools/make_release.py       # release/FF1-recomp/ and release/FF1-recomp-windows.zip
```

A ready-to-play Windows folder of the LÖVE version (LÖVE fused in, its
runtime, `ff1core.dll`), with no game data: players run `FF1-recomp.exe` and
drop their ROM on the window the first time. The release is portable: it
keeps the ROM's copy, the save and the settings beside `FF1-recomp.exe`
(or in `%APPDATA%\ff1-recomp` if that folder can't be written to).

## Run

- LÖVE: `love love` from this folder (`lovec` on Windows shows a console).
  The first time, it asks for the ROM: drag the file onto the window. It is
  checked and kept in LÖVE's save folder, so it's asked for only once.
  `--new` skips the title screen.
- SDL3: `build/ff1-recomp --rom <file>`, or put the ROM beside the program as
  `FinalFantasy.nes` (`FF1_ROM` works too). `--new` skips the title screen.

| Key | Action |
|-----|--------|
| Arrows | Move, move the cursor |
| `Z` / `Space` | A |
| `X` / `Backspace` | B |
| `Tab` / `Right Shift` | Select |
| `Enter` | Start |
| `Esc` | Quit |

LÖVE front end only:

| Key | Action |
|-----|--------|
| `F2` | 2D / 3D |
| `C` / `V` | Camera preset / view distance |
| `Q` / `E`, `PgUp` / `PgDn` | Turn / zoom the camera |
| `F5` `F6` `F7` `F8` | Tilt-shift, water shimmer, day and night, battle camera motion |
| `F9` | Random encounters on / off |
| `F10` | Battle speed 1x / 2x / 4x |

## Layout

```
src/            the game (C99); main.c, input.c, audio.c are the only SDL files
src/ff1core.c   the game as a library for other front ends (the LÖVE one)
love/           the LÖVE front end and 3D view
src/romdata.c   the game's tables, read from the player's ROM
tools/          development tools (Python 3): the table list, the layout, checks
data/generated/ declarations of the game's tables (no data)
tests/          unit tests and screenshot scenarios
```

## Legal

*Final Fantasy* is © Square Enix. This is an unofficial, non-commercial fan
project, not affiliated with or endorsed by Square Enix. It contains no
game data; you need your own copy of the game.
