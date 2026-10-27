# Plan: the game from the player's ROM

Goal: a prebuilt game (the LÖVE version first) that contains no data from
Final Fantasy. On first launch the player gives it their own ROM of Final
Fantasy (USA); the game checks it and reads everything it needs from it.

Today the data comes from `tools/extract_data.py`, which reads the
disassembly's `bank_XX.dat`, `bin/*.bin` and `bank_XX.asm` files and writes
`data/generated/*.h`, compiled into the game (and committed).

## Principles

1. **No game data in the shipped program or in git** (from R3 on). Offsets,
   sizes and table layouts are facts about the ROM and may stay.
2. **The ROM is checked**: the 256 KB after the (optional) iNES header must
   have SHA-1 `c9cfbf5455085e198dce039298b083cd6fc88bce` -- Final Fantasy
   (USA), as the disassembly reassembles it.
3. **Nothing changes in the game**: the data the game sees is byte for byte
   what it sees today; each step is checked against the current output.

## Steps

| # | Step | Check |
|---|------|-------|
| R1 | ✅ The reference ROM (`tools/build_rom.py`: reassembled from the disassembly, plus ca65 listings with every label's offset) and `src/rom.c`: load a ROM with or without its header, check its SHA-1 | unit tests (SHA-1 vectors, sizes, the reference ROM) |
| R2 | ✅ The Python extractor reads one ROM (`FF1_ROM` or `../FinalFantasy.nes`): the `.dat`/`.bin` slices are bank offsets (`BIN_LAYOUT`), the `.asm` tables are read at their labels' offsets from the ca65 listings (`build/labels/`), words become routine names through the same labels. Still read from the disassembly: `Constants.inc` (names) and the two character tables (glyph maps; the DTE pairs are also in the ROM) | all 42 generated headers byte-identical |
| R3 | ✅ Headers without data: the extractor moves every table's definition to `data/generated/data/<header>.c` (generated, git-ignored) and leaves `extern const` declarations, types and sizes in the committed headers (194 tables; `constants.h`, `types.h`, `palette_data.h` hold no game data). CMake builds the data files and stops with instructions when they're missing. The two unused nametable headers are gone | builds without warnings; 463 checks, 40 scenarios pass |
| R4 | The extractor in C (`src/romdata.c`): fills every table from the ROM at start-up, family by family; a test compares each with the Python output until all match, then the generated `.c` is dropped | tests compare all tables |
| R5 | Asking for the ROM: the LÖVE front end asks on first launch (drop the file on the window), checks it, keeps a copy in its save folder; the SDL build takes `--rom` | screenshots of the prompt; a wrong file refused |
| R6 | A release folder: the LÖVE game with `ff1core.dll`, no game data | runs on a clean copy with only a ROM |

## Note on history

The generated headers with the game's data are in this repository's past
commits. Before publishing it, the history needs rewriting (or a fresh
repository); that is left to the owner.

## For development

```
python tools/build_rom.py      # writes ../FinalFantasy.nes (never committed)
python tools/extract_data.py   # writes data/generated/data/*.c from it (never committed)
cmake --build build
```
The unit tests use it when present (or `FF1_ROM=path`).
