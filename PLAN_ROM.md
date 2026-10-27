# Plan: the game from the player's ROM

Goal: a prebuilt game (the LÖVE version first) that contains no data from
Final Fantasy. On first launch the player gives it their own ROM of Final
Fantasy (USA); the game checks it and reads everything it needs from it.

**Status:** done (R1–R6). Before this plan, the data came from
`tools/extract_data.py`, which read the disassembly's `bank_XX.dat`,
`bin/*.bin` and `bank_XX.asm` files and wrote `data/generated/*.h`, compiled
into the game (and committed). Now the game reads every table from the
player's ROM at startup (`src/rom.c`, `src/romdata.c`), and the release
(`tools/make_release.py`) holds no game data.

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
| R4 | ✅ The game reads its tables from the ROM (`src/romdata.c`): 156 of the 177 tables it uses are one run of the ROM, copied by the layout `tools/find_slices.py` writes (`src/rom_layout.h`, offsets only); the other 21 (text and its pointer tables, records, words, the overworld bank) are put together as the extractor does. Storage and the table list are generated without data (`src/romdata_storage.c`, `data/generated/tables.h`). Every program loads the ROM at start-up; building needs no Python and no disassembly | `ff1-dump-ref` vs `ff1-dump-rom`: identical for the reference and a real USA dump; 463 checks, 40 scenarios |
| R5 | ✅ Asking for the ROM: the LÖVE front end looks for its kept copy, `FF1_ROM`, a development ROM; else it asks for the file to be dropped on its window, checks it (the reason shown if refused) and keeps a copy in its save folder. The SDL build: `--rom`, `FF1_ROM`, or `FinalFantasy.nes` beside it, else a message box | `--ask-rom`, `--drop=FILE` |
| R6 | ✅ `python tools/make_release.py`: `release/FF1-x86/` (and a zip) -- LÖVE fused with the front end as `FF1-x86.exe`, LÖVE's runtime and licence, `ff1core.dll`, a README for players; no game data. The front end finds its DLL beside it and saves to its save folder | unzipped into a fresh folder: asks for the ROM, accepts the USA dump, plays |

## Note on history

The generated headers with the game's data stay in the development
repository's history. This public repository was started with a fresh
history and contains none of it.

## For development

```
cmake --build build            # needs nothing from the game
FF1_ROM=<rom> build/ff1-tests  # the tests read the ROM
```
When tables change (this needs the development tree with the
disassembly; it can't be done from this repository alone):
`python tools/build_rom.py` (the disassembly's listings), `python tools/extract_data.py` (regenerates the table list and
storage, and their data for the check), then build, run
`build/ff1-dump-ref ref.txt` and `python tools/find_slices.py ref.txt`
(the layout), and check `build/ff1-dump-rom <rom> rom.txt` equals ref.txt.
The unit tests use it when present (or `FF1_ROM=path`).
