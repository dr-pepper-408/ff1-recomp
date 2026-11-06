"""
make_release.py -- a ready-to-play folder of the LÖVE version, for Windows.

    python tools/make_release.py [--love "C:/Program Files/LOVE"]

Builds release/FF1-recomp/ (and release/FF1-recomp-windows.zip):

    FF1-recomp.exe   LÖVE with the game fused in (love/ as game.love)
    *.dll            LÖVE's runtime, and ff1core.dll (the game, from build/)
    LOVE-license.txt LÖVE's licence (zlib), which allows this
    README.txt       for players: what they need, how to start

No game data is included: on first launch the game asks for the player's
ROM of Final Fantasy (USA).  Build first (cmake --build build).

If you play from release/FF1-recomp/, the game keeps its save, settings and
ROM copy there (ff1.sav, settings.txt, rom.nes): they are set aside while
the folder is rebuilt and put back after the zip is made, never in it.
"""

import argparse
import os
import shutil
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)

PLAYER_README = """FF1-recomp -- Final Fantasy (NES), ported to the PC, with an optional 3D view
=============================================================================

This is an unofficial fan project. It contains no data from the game:
you need your own copy of Final Fantasy (USA) for the NES, as a ROM file.

Starting
--------
Run FF1-recomp.exe. The first time, it asks for the ROM: drag the file onto
the window. It must be Final Fantasy (USA), the original 1990 NES release,
unmodified (other versions, translations and hacks don't work). It is
checked and kept in %APPDATA%\\ff1-recomp as rom.nes, so you're asked only
once.

Your game is saved there too (ff1.sav; the save before it as ff1.sav.bak),
with your settings (settings.txt). A new version, unzipped anywhere, finds
them: nothing to copy when you update.

Portable: put an empty file named portable.txt beside FF1-recomp.exe and
the ROM copy, the save and the settings are kept in this folder instead
(for a USB stick, say). Don't share the folder with rom.nes in it.

Keys and controllers
-------------------
Arrows          move, move the cursor      (controller: D-pad or left stick)
Z / Space       A                          (controller: bottom button)
X / Backspace   B                          (controller: right button)
Tab             Select                     (controller: Back)
Enter           Start                      (controller: Start)
Q / E           turn the camera;  PgUp / PgDn  zoom   (controller: right stick)
Esc             the options menu           (controller: Select + Start)

The options menu sets the view (2D / 3D), the camera, the view distance,
the extras (tilt-shift, water shimmer, day and night, battle camera
motion, random encounters, battle speed, EXP and gold from battles) and
the controls: every button can be remapped, on the keyboard and on the
controller. Everything is remembered. Quit the game from the menu.

Final Fantasy is (c) Square Enix. Not affiliated with or endorsed by
Square Enix. Made with LOVE (love2d.org, zlib licence).
"""


PLAYER_FILES = ("ff1.sav", "settings.txt", "rom.nes")   # (what the game keeps beside FF1-recomp.exe)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--love", default=r"C:/Program Files/LOVE", help="the LÖVE 11.5 folder")
    args = ap.parse_args()

    dll = os.path.join(PORT, "build", "ff1core.dll")
    if not os.path.exists(dll):
        raise SystemExit("build/ff1core.dll missing: build first (cmake --build build)")
    love_exe = os.path.join(args.love, "love.exe")
    if not os.path.exists(love_exe):
        raise SystemExit(f"{love_exe} missing: install LÖVE 11.5 or pass --love")

    out = os.path.join(PORT, "release", "FF1-recomp")
    # The player's files from playing this folder (the release is portable):
    # set aside while it's rebuilt, put back after the zip (never in it).
    kept = os.path.join(PORT, "release", "player-data-kept")
    moved = []
    for name in PLAYER_FILES:
        src = os.path.join(out, name)
        if not os.path.exists(src):
            continue
        if os.path.exists(os.path.join(kept, name)):
            raise SystemExit(f"{kept} already holds {name} (from a run that failed?): "
                             f"put it back in {out} or remove it, then run again")
        os.makedirs(kept, exist_ok=True)
        shutil.move(src, os.path.join(kept, name))
        moved.append(name)
    try:
        build(args, dll, love_exe, out)
    finally:
        os.makedirs(out, exist_ok=True)
        for name in moved:
            shutil.move(os.path.join(kept, name), os.path.join(out, name))
        if moved and not os.listdir(kept):
            os.rmdir(kept)
    if moved:
        print(f"kept your {', '.join(moved)} in {out} (not in the zip)")


def build(args, dll, love_exe, out):
    try:
        if os.path.exists(out):
            shutil.rmtree(out)
    except OSError as e:
        raise SystemExit(f"can't replace {out} ({e.strerror}): close FF1-recomp.exe if it's running")
    os.makedirs(out)

    # game.love: the front end, zipped; fused after love.exe
    game = os.path.join(PORT, "release", "game.love")
    with zipfile.ZipFile(game, "w", zipfile.ZIP_DEFLATED) as z:
        for name in os.listdir(os.path.join(PORT, "love")):
            if name.endswith(".lua"):
                z.write(os.path.join(PORT, "love", name), name)
    with open(os.path.join(out, "FF1-recomp.exe"), "wb") as f:
        f.write(open(love_exe, "rb").read())
        f.write(open(game, "rb").read())
    os.remove(game)

    for name in os.listdir(args.love):
        if name.lower().endswith(".dll"):
            shutil.copy(os.path.join(args.love, name), out)
    shutil.copy(os.path.join(args.love, "license.txt"), os.path.join(out, "LOVE-license.txt"))
    shutil.copy(dll, out)
    open(os.path.join(out, "README.txt"), "w", encoding="utf-8", newline="\r\n").write(PLAYER_README)

    # (the game keeps the player's ROM and save beside the program: never ship them)
    for name in os.listdir(out):
        if name.lower().endswith((".nes", ".sav", ".new")) or name == "settings.txt":
            raise SystemExit(f"{name} in {out}: player data, not for the release")
    archive = os.path.join(PORT, "release", "FF1-recomp-windows")
    shutil.make_archive(archive, "zip", os.path.join(PORT, "release"), "FF1-recomp")
    print(f"wrote {out} and {archive}.zip:")
    for name in sorted(os.listdir(out)):
        print(f"  {name}  {os.path.getsize(os.path.join(out, name))} bytes")


if __name__ == "__main__":
    main()
