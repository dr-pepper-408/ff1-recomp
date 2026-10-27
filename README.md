# FF1-x86

A native port of *Final Fantasy* (NES, 1990) to C99, written routine by
routine from a disassembly of the original game rather than by emulating
the 6502. The game logic follows the original's, including its bugs (marked
`BUGGED` in the source).

It has two front ends:

- **SDL3** (`ff1-x86`): the game as the NES showed it, pixel for pixel.
- **LÖVE** (`love/`): the same game, with an optional 3D view in the style
  of HD-2D games: a voxel overworld, towns and dungeons in 3D, characters as
  2D cutouts, a battle diorama, camera presets and a few extras (tilt-shift,
  day and night). F2 switches between 2D and 3D.

> **No game data is included.** This repository contains code only. To
> build and play it you need your own copy of *Final Fantasy* (USA) for the
> NES, as a ROM file. Nothing from the game is committed: the data is
> generated from your ROM and git-ignored.

## Status

The whole game is playable: title screen, party creation, the overworld
and its vehicles, towns, shops, dungeons, battles, menus, saving, music and
sound, the minigame, and the ending. See `PLAN.md` (the port),
`PLAN_3D.md` (the 3D view) and `PLAN_ROM.md` (building from your ROM).

## Requirements

- CMake 3.10+ and a C99 compiler (tested with MinGW-w64 GCC on Windows)
- Python 3 (to generate the game data from your ROM)
- SDL3 is included under `src/SDL3/` (zlib licence)
- [LÖVE 11.5](https://love2d.org) for the 3D front end
- A ROM of **Final Fantasy (USA)** for the NES, the original 1990 North
  American release, unmodified (tested with a dump named
  `Final Fantasy (USA).nes`). The file's 16-byte header may be iNES or
  NES 2.0, or missing; what is checked is the 256 KB of game data after it,
  whose SHA-1 must be `c9cfbf5455085e198dce039298b083cd6fc88bce`.
  Other versions don't work: the Japanese release, the Famicom
  *Final Fantasy I・II*, the remakes, and translated or hacked ROMs.

For now, generating the data also needs the FF1 disassembly this port was
written against, in the folder above this one: the extractor takes the
tables' addresses from its assembler listings. Removing that dependency, so
that a ROM alone is enough, is the next step of `PLAN_ROM.md`.

## Build

```sh
python tools/build_rom.py         # assembler listings (and a reference ROM) from the disassembly
FF1_ROM=path/to/your.nes python tools/extract_data.py   # the game data, from your ROM
cmake -S . -B build
cmake --build build
```

Without `FF1_ROM`, the extractor uses the reference ROM `build_rom.py`
writes. The generated data goes to `data/generated/data/` (git-ignored).

Tests:

```sh
build/ff1-tests                    # unit tests of the game logic
build/ff1-screenshots <dir>        # scripted scenes, PASS/FAIL, PPM screenshots
```

## Run

- SDL3: `build/ff1-x86` (`--new` skips the title screen).
- LÖVE: `love love` from this folder (`lovec` on Windows shows a console).
  `--new` skips the title screen.

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
tools/          the data extractor and helpers (Python 3)
data/generated/ declarations of the game's tables (their data: generated, git-ignored)
tests/          unit tests and screenshot scenarios
```

## Legal

*Final Fantasy* is © Square Enix. This is an unofficial, non-commercial fan
project, not affiliated with or endorsed by Square Enix. It contains no
game data; you need your own copy of the game.
