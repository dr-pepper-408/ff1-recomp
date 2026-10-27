# FF1-recomp — Full Port Plan

Goal: a complete, playable C99 port of *Final Fantasy* (NES), from title screen
to "The End", reimplemented from a disassembly of the original.
SDL3 stays the platform layer for now; abstracting it is a later task (see
[SDL containment](#sdl-containment-rule) for the one low-cost rule we follow
in the meantime).

**Status:** done. Phases 0–7 are complete and milestones M1–M5 are reached:
the whole game can be played from the title screen to "The End". The game's
data is read from the player's ROM (see `PLAN_ROM.md`). What remains open:

- abstracting the platform layer (SDL3), out of scope here;
- optional fixes for the original's bugs: none exist yet (the bugs are
  reproduced and marked `BUGGED`);
- reference traces from an emulator and `.sav` compatibility checks
  (verification 2 and 3 below) are not part of this repository.

The 3D view has its own open points, listed in `PLAN_3D.md`.

---

## Guiding principles

1. **Port routines, not features.** Every gameplay function names the
   disassembly routine it ports (`/* port of DoPhysicalAttack (bank_0C) */`)
   and follows its logic, including integer widths, clamps and RNG calls.
   Invented formulas are bugs.
2. **Faithful by default, fixes behind flags.** Known ROM bugs (the biased
   turn shuffle, spells whose effects don't work, the INT stat doing nothing)
   are reproduced. Optional fixes would go behind switches, off by default
   (none exist yet).
3. **No hand-typed game data.** The tables were first generated from the
   disassembly by `tools/extract_*.py`; they are now read from the player's
   ROM at startup (`src/romdata.c`, see `PLAN_ROM.md`), and the headers in
   `data/generated/` hold declarations only.
4. **Each milestone ends playable.** Milestones are story checkpoints, so
   every phase can be verified by actually playing up to that point.
5. **Update and draw are separate.** Game logic must run without a renderer
   (needed for tests, and for the later SDL abstraction).

### SDL containment rule

Until the platform layer is abstracted, only `main.c`, `input.c` and
`audio.c` include `<SDL3/SDL.h>`. Everything else, game logic and
rendering alike, takes plain C types and draws into the NES-style frame
buffer (`ppu.c`); `ff1core.c` builds the same game as a library without
SDL for the LÖVE front end. This keeps the future abstraction to a handful
of files.

---

## Phase 0 — Foundations

Fix what's there and build the shared infrastructure every later phase needs.

**Status:** done.  Along the way: `types.h` now matches the NES save layout
byte for byte (it was wrong), the generator's text decoding no longer cuts
strings at spaces, prices are extracted, the dialogue box uses FF1's real
position, treasure chests award gold and respect a full inventory, and the
NES master palette was corrected.  Rendering moved to an NES-style indexed
frame buffer (`ppu.c`), so SDL is now confined to `main.c` and `input.c`.

| # | Task | Ports / notes |
|---|------|---------------|
| 0.1 | ✅ Fix the build (`CMD_NAMES` missing in `battle.c`) | — |
| 0.2 | ✅ **Fixed timestep** at 60.0988 Hz (accumulator loop in `main.c`); game logic advances in whole NES frames | replaces `SDL_Delay(16)` |
| 0.3 | ✅ **`GameContext`** struct: `SaveData`, player/vehicle position, map mode, game state, RNG state, pending transitions. Passed to all update functions; removes file-level globals from `game.c` | needed for save/load and tests |
| 0.4 | ✅ Battle state into a `BattleState` struct (the ~15 `g_enemy_*` arrays) | `battle.c` |
| 0.5 | ✅ **Shared UI module `ui.c`**: box, cursor, number formatting, string drawing. Remove duplicates from `menu.c`/`battle.c` | `DrawBox`, `DrawCursor`, `PrintNumber_*`, `FormatNumber_*` (bank_0E/0F) |
| 0.6 | ✅ **Full complex-string renderer**: all DTE control codes (`$02`–`$19`: character names, stats, item names, gold, prices, line breaks) | `DrawComplexString`, `DrawDialogueString`, `DrawBattleString` + `_Code0C`/`_Code11` (bank_0F) |
| 0.7 | ✅ Move remaining runtime `.dat` reads into generated headers (SM tilesets, palettes, teleports, map data, dialogue, menu text, overworld RLE) | `extract_data.py`; then delete the path-probing loaders |
| 0.8 | ✅ RNG module: `BattleRNG` / `lut_RNG` stepping, plus the separate battle-step RNG, with a seedable state for tests | `BattleRNG`, `BattleStepRNG`, `RandAX` |
| 0.9 | ✅ **Test target**: `ctest` executable linking the logic modules without SDL. First tests: RLE decode, string decode, number formatting | see [Verification](#verification-strategy) |
| 0.10 | ✅ Screen transitions: fades, screen wipe, battle transition | `ScreenWipe_*`, `BattleTransition`, `FadeOutOneShade` |

**Done when:** the build is clean with `-Wall -Wextra`, the game runs at a
locked 60.1 Hz, `ctest` passes, and nothing reads `bank_*.dat` at runtime.

---

## Phase 1 — Standard maps and NPCs (the world becomes interactive)

The biggest gap: almost every story event goes through NPCs and their flags.

| # | Task | Ports |
|---|------|-------|
| 1.1 | ✅ SM → SM teleports (normal) and the warp/return stack (warp tiles; the EXIT/WARP spells come later) | `TP_TELE_NORM`/`TP_TELE_WARP`, `EnterStandardMap`, `ReenterStandardMap` |
| 1.2 | ✅ Map edge exits for towns (the original has no edge check: towns are ringed with `TP_TELE_EXIT` tiles) | `StandardMapMovement` |
| 1.3 | ✅ **Map objects**: load per-map object lists, sprite CHR, walking AI, collision with the party | `LoadMapObjects`, `LoadSingleMapObject`, `MapObjectMove`, `CanMapObjMove`, `AnimateAndDrawMapObject`, `DrawSMSprites`, `IsObjectInPath` |
| 1.4 | ✅ **Event flags + object visibility** | `CheckGameEventFlag`, `SetGameEventFlag`, `ClearGameEventFlag`, `IsObjectVisible`, `HideMapObject`, `ShowMapObject` |
| 1.5 | ✅ **Talk dispatcher + all `Talk_*` handlers** (~50: King, Garland, Princess, Bikke, Astos, Matoya, the `Talk_if*` family, `Talk_fight`, ...) | `TalkToObject`, `CanTalkToMapObject`, bank_0E `Talk_*` |
| 1.6 | ✅ Special tiles: doors + rooms, locked doors (Mystic Key), close-room, battle tiles, damage tiles, treasure blocking, crown/cube/4-orb gates, rod/lute plates, the four altars with the altar effect (door sound comes with audio) | `SMMove_*` (bank_0F), `RedrawDoor`, `DoAltarEffect` |
| 1.7 | ✅ Dungeon random encounters (per-map rates and domains), fixed tile battles, `Talk_fight` battles; overworld encounters fixed to OWCanMove (fighting tiles only, land/river/sea domains, 3/256 at sea) | `SMMove_Battle`, `GetBattleFormation`, `OWCanMove` |
| 1.8 | ✅ Gold chests (extract item price table from bank_0D) | `OpenTreasureChest`, `AddGPToParty` |
| 1.9 | ✅ Map tile and poison damage while walking (with the monochrome flash) | `MapTileDamage`, `MapPoisonDamage` |
| 1.10 | ✅ (Dropped: FF1 has no palette cycling in the orb rooms; lighting an orb uses the altar effect, done in 1.6) | — |

**Status:** done.

**Done when:** you can walk through Coneria town and castle, talk to everyone
with correct text, enter the Temple of Fiends and trigger Garland.

---

## Phase 2 — Battle fidelity and progression

Replace every approximated formula with the real routine.

**Status:** done.

| # | Task | Ports |
|---|------|-------|
| 2.1 | ✅ Physical attacks: hit chance, multi-hit, criticals, damage roll, category/weakness bonuses | `DoPhysicalAttack`, `PlayerAttackEnemy_Physical`, `EnemyAttackPlayer_Physical` |
| 2.2 | ✅ All magic effects; MAGIC/DRINK/ITEM targeting, MP and potions, mute | `BtlMag_Effect_*`, `BtlMag_ApplyDamage`, `BtlMag_ApplyAilments`, `BtlMag_HandleAilmentChanges` |
| 2.3 | ✅ Ailments gate turns: sleep, stun, confuse, mute, dark; wake-up/unstun rolls | `Battle_PlayerTryWakeup`, `Battle_PlayerTryUnstun`, `ApplyEnemyAilmentMask` |
| 2.4 | ✅ End-of-round poison and regen | `ApplyEndOfRoundEffects`, `ApplyPoisonToPlayer`, `ApplyRegenToAllEnemies` |
| 2.5 | ✅ Run formula | `Battle_PlayerTryRun` |
| 2.6 | ✅ Surprise and preemptive rounds; weighted formation selection | `GetBattleFormation`, `PrepareEnemyFormation` |
| 2.7 | ✅ Formation types: 9-small, 4-large, mixed, fiend, Chaos (layout, targeting, palettes; exact cursor spots with 2.11) | `PrepareEnemyFormation_*`, `DrawFormation_*`, `EnemyTargetMenu_*` |
| 2.8 | ✅ **Level-up** and EXP split | `LvlUp_*`, `DivideRewardBySurvivors`, `GiveRewardToParty` |
| 2.9 | ✅ End of battle and game over (back to the title screen) | `EndOfBattleWrapUp`, `BattleOver_ProcessResult`, `GameOver` |
| 2.10 | ✅ Party re-sort by ailment, in-battle/out-of-battle stat conversion | `ReSortPartyByAilment`, `ConvertOBStatsToIB`/`ConvertIBStatsToOB` |
| 2.11 | ✅ Battle presentation: combat boxes, message buffer, enemy attack names | `DrawCombatBox*`, `DrawBattleMessage`, `ShowAltBattleMessage` |
| 2.12 | ✅ Animations: walk-forward-and-strike, weapon sprites, magic flash, enemy death/explosions, screen shake, fanfare | `WalkForwardAndStrike`, `DrawWeaponGraphicRow`, `DoMagicFlash`, `EraseSmallEnemy`, `DrawExplosions`, `BattleScreenShake`, `PlayFanfareAndCheer` |
| 2.13 | ✅ Chaos death sequence | `ChaosDeath`, `ChaosDeath_FadeNoise` |

**Done when:** given the same RNG seed and inputs, the port and the original
produce the same damage, hits and outcomes (see verification), and the party
levels up correctly.

---

## Phase 3 — Towns: shops, inns, clinics, saving

**Status:** done.

| # | Task | Ports |
|---|------|-------|
| 3.1 | ✅ Shop framework: draw, buy/sell/exit loop, yes/no, gold box, party sprites | `EnterShop`, `ShopFrame`, `DrawShop*`, `CommonShopLoop_*`, `ShopLoop_*` |
| 3.2 | ✅ Weapon/armor shops (buy for a character, sell box) | `EnterShop_Equip`, `EquipShop_GiveItemToChar`, `EquipMenu_BuildSellBox` |
| 3.3 | ✅ Item shop and caravan | `EnterShop_Item`, `EnterShop_Caravan` |
| 3.4 | ✅ White/black magic shops with class and slot rules | `EnterShop_Magic`, `ShopSelectBuyMagic`, `MagicShop_AssertLearn` |
| 3.5 | ✅ Inn (heal, save) and clinic (revive) | `EnterShop_Inn`, `EnterShop_Clinic`, `Clinic_SelectTarget` |
| 3.6 | ✅ **Save/load**: write `SaveData` as the NES 8 KB SRAM layout with its checksum, so emulator `.sav` files can be imported and exported | `SaveGame`, `VerifyChecksum` |
| 3.7a | ✅ Field menu ported from bank_0E (replacing the earlier approximation): main menu with orbs, item, magic, target, status and equip screens, original strings and layouts | `EnterMainMenu`, `DrawMainMenu`, `EnterItemMenu`, `EnterMagicMenu`, `ItemTargetMenuLoop`, `EnterStatusMenu`, `EnterEquipMenu`, `IsEquipLegal` |
| 3.7 | ✅ TENT/CABIN/HOUSE save prompt; WARP/EXIT spells; all remaining `UseItem_*` (key items) | `MenuSaveConfirm`, `UseMagic_WARP`, `UseMagic_EXIT`, `UseItem_*` |

**Done when:** you can buy equipment and spells, rest at the inn, save, quit,
and continue from the save.

---

## Phase 4 — Front end

**Status:** done; milestone M1 reached.

| # | Task | Ports |
|---|------|-------|
| 4.1 | ✅ Title screen (Continue / New Game / respond rate) | `EnterTitleScreen`, `IntroTitlePrepare`, `TitleScreen_DrawRespondRate` |
| 4.2 | ✅ Intro story (fade-in, A/B/Start behavior) | `EnterIntroStory`, `IntroStory_*` |
| 4.3 | ✅ Party generation and name input | `NewGamePartyGeneration`, `PtyGen_*`, `DoNameInput`, `CharName_*` |
| 4.4 | ✅ Starting stats from the real tables for the chosen classes (the table was already fixed; New Game now uses it, level 1, no debug loadout) | `NewGame_LoadStartingStats` |
| 4.5 | ✅ Lineup (party order) menu (Select; Select+B on the overworld is the minimap, 5.8) | `EnterLineupMenu`, `LineupMenu_*` |
| 4.6 | ✅ Respond rate affects battle message speed | `RespondDelay` |
| 4.7 | ✅ Coneria bridge on the overworld: drawn once the King builds it (`bridge_vis`), walkable on foot (no battle on it), sets `bridgescene` the first time | `DrawOWObj_BridgeCanal`, `ConvertOWToSprite`, `IsOnBridge` (bridge half), `lut_OWObjectSprTbl` |
| 4.8 | ✅ Bridge scene: when `bridgescene` is set the overworld cycles out, the story pages then the credits pages are shown over the bridge picture with the shutter effect (A/B skips a page's wait), `bridgescene` becomes $80, and the overworld cycles back in. Data in `story_data.h` (the old `endingbridge_nt.h` from `extract_chr.py` holds CHR, not a nametable, and is unused) | `DoOWTransitions` (@bridgescene), `LoadBridgeSceneGFX`, `EnterBridgeScene`, `Story_DoPage`, `Story_OpenShutters` / `_CloseShutters` (bank_0D) |

`FF1_DEBUG_STATE` stays as a shortcut that skips the front end: `--new` starts
the default party with the debug loadout; New Game on the title screen goes
through party generation and starts a real level-1 party.

**Milestone M1 — "The Princess":** a new game started from the title screen
can reach Garland, rescue the princess, receive the Lute, see the bridge
scene (`EnterBridgeScene`) and cross the bridge.

---

## Phase 5 — Overworld completeness and vehicles

**Status:** done; milestones M2 and M3 reached.

| # | Task | Ports |
|---|------|-------|
| 5.1 | ✅ Overworld objects: ship, airship (with the Floater's flashing), canal sprites, and the full `DrawOWSprites` order per vehicle (in the ship the bridge and canal are drawn over the mapman; the bridge itself is done in 4.7) | `DrawOWSprites`, `DrawOWObj_*` |
| 5.2 | ✅ Ship: board, sail, dock, sea encounters | `BoardShip`, `DockShip`, `UnboardBoat` |
| 5.3 | ✅ Canoe: rivers, river encounters; the canal blocking the ship until it's opened | `BoardCanoe`, `IsOnCanal` |
| 5.4 | ✅ Airship: Floater raise, takeoff/landing animation, shadow, fast movement, landing rules | `FlyAirship`, `LandAirship`, `AnimateAirshipTakeoff`, `AnimateAirshipLanding`, `DrawAirshipShadow` |
| 5.5 | ✅ Vehicle mapman graphics (and the party's bottom half hidden in forests, `inforest`) | `DrawMMV_Ship`, `DrawMMV_Canoe`, `Draw2x2Vehicle` |
| 5.6 | ✅ Encounter domains per vehicle | `OWCanMove`, `GetBattleFormation` |
| 5.7 | ✅ Caravan and other special overworld tiles (chime, etc.) | terrain `SS` bits |
| 5.8 | ✅ Overworld minimap (Select with B held) | bank_09 `EnterMinimap`, `Minimap_*` |
| 5.9 | ✅ Class change (Bahamut; the class change itself was in 1.5, now the map is re-entered after it, `dlgflg_reentermap`) | `DoClassChange` |

**Milestone M2 — "Ship and Crown":** Pravoka/Bikke, Elfland, Marsh Cave,
Astos, Matoya, the Earth Cave, Vampire, Sarda's Rod, Lich.

**Milestone M3 — "Canoe and Airship":** Crescent Lake, Gurgu Volcano,
Ice Cave, the Floater, the airship, Bahamut's class change, Kary.

---

## Phase 6 — Endgame

**Status:** done; milestones M4 and M5 reached.

| # | Task | Ports |
|---|------|-------|
| 6.1 | ✅ Remaining key-item flows (all `Talk_*` handlers, `UseItem_*`, gate tiles already ported in 1.4-1.6 and 3.7; tests now cover the submarine): Oxyale, Cube, Slab/Unne, Chime, Lefein, submarine, Mirage Tower ↔ Flying Fortress | `UseItem_*`, `Talk_*`, `SMMove_*` |
| 6.2 | ✅ Chaos Shrine time loop (tested: the Black Orb goes once the four orbs are lit, its tile leads to the past) (four-orb teleport) | `Talk_4Orb`, `Talk_BlackOrb` |
| 6.3 | ✅ Sliding-puzzle minigame and reward | bank_0D `EnterMiniGame`, `MiniGame_*`, `MinigameReward` |
| 6.4 | ✅ Ending scene, credits, "The End" graphic | `EnterEndingScene`, `Story_*`, `DrawFancyTheEndGraphic` |

**Milestone M4 — "Four Orbs"** and **M5 — "The End":** the whole game can be
finished from a new save.

---

## Phase 7 — Audio (can run in parallel from Phase 1 onward)

**Status:** done.

| # | Task | Ports |
|---|------|-------|
| 7.1 | ✅ 2A03 APU synthesis (2 pulse, triangle, noise) to an `SDL_AudioStream` in `audio.c`, driven by register writes | — |
| 7.2 | ✅ Music driver: song data, score interpreter, tempo/octave/envelopes | bank_0D `MusicPlay`, `Music_*` |
| 7.3 | ✅ Menu, map and door SFX (also the wipe, battle transition, altar, poison, lava, ship/airship and minimap sounds) | `PlaySFX_MenuSel`, `PlaySFX_MenuMove`, `PlayDoorSFX`, `DialogueBox_Sfx`, `PlaySFX_Error` |
| 7.4 | ✅ Battle SFX (and the Chaos death rumble) | `PlayBattleSFX`, `UpdateBattleSFX*` |
| 7.5 | ✅ Hook song changes into map/battle/shop/vehicle transitions | `CallMusicPlay` |

Porting the music driver (rather than playing recorded tracks) keeps the
data pipeline consistent and needs no extra assets.

---

## Verification strategy

There is no checksum to compare against, so we build our own reference.
In this repository: 1 (`build/ff1-tests`, 463 checks) and the screenshot
scenarios (`build/ff1-screenshots`, 40 scripted scenes with PASS/FAIL
checks). The emulator traces and save files of 2–4 are not part of it.

1. **Unit tests (`ctest`)** for pure logic: decoders, number formatting,
   damage/hit formulas, level-up, shop prices, equip legality, save checksum.
   Expected values are worked out by hand from the assembly and cited in the
   test.
2. **Reference traces from the original.** The disassembly builds the
   ROM (`build.bat`, ca65/ld65). Run it in a Lua-scriptable emulator
   (e.g. Mesen), seed RNG state, script inputs, and dump RAM snapshots
   (party stats, enemy HP, flags) at key points. A port test replays the same
   inputs from the same state and compares the snapshots. Start with battles
   (Phase 2): it's the area where drift is most likely and hardest to spot.
3. **Save compatibility check.** Load a `.sav` made in an emulator; the port
   must show identical party, inventory and flags (Phase 3).
4. **Milestone playthroughs.** At each milestone, play from a new game to the
   checkpoint and keep a save at the checkpoint for regression testing.

---

## Data extraction backlog

✅ Done: every table was extracted by `tools/extract_data.py` into
`data/generated/`; since `PLAN_ROM.md` they are read from the player's ROM
at startup instead, and the headers hold declarations only.

- ✅ Map object lists, NPC sprites and talk handlers (`mapobj_data.h`, `dialogue.h`)
- ✅ Item/equipment prices (`item_data.h`) and shop inventories (`shop_data.h`)
- ✅ Enemy special-attack names (`text_data.h`, `Text_EnemyAttackName`)
- ✅ Standard-map encounter domains and fixed battles (`encounter_data.h`)
- ✅ Class starting stats (`class_starting_stats.h`, all six classes)
- ✅ Music and SFX data (`music_data.h`, `battle_sfx_data.h`; field SFX are code)
- ✅ Minimap, puzzle, intro/ending graphics and layouts (`minimap_data.h`,
  `story_data.h`, `menu_data.h`)
- ✅ Runtime `.dat` reads (Phase 0.7)

---

## Suggested order and size

| Phase | Size | Depends on |
|-------|------|------------|
| 0 Foundations | M | — |
| 1 Standard maps & NPCs | L | 0 |
| 2 Battle fidelity | L | 0 |
| 3 Towns & saving | M | 0, 1 |
| 4 Front end → **M1** | M | 0 |
| 5 Overworld & vehicles → **M2, M3** | L | 1, 2 |
| 6 Endgame → **M4, M5** | M | 5 |
| 7 Audio | M | 0 (parallel) |
| Platform abstraction (out of scope here) | S–M | SDL containment rule |

Phases 1 and 2 can be worked on in parallel. Phase 4 is small and can be
slotted in whenever a break from the larger phases is useful.
