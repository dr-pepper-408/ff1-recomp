"""
make_release.py -- a ready-to-play folder of the LÖVE version, for Windows.

    python tools/make_release.py [--love "C:/Program Files/LOVE"]

Builds release/FF1-x86/ (and release/FF1-x86-windows.zip):

    FF1-x86.exe      LÖVE with the game fused in (love/ as game.love)
    *.dll            LÖVE's runtime, and ff1core.dll (the game, from build/)
    LOVE-license.txt LÖVE's licence (zlib), which allows this
    README.txt       for players: what they need, how to start

No game data is included: on first launch the game asks for the player's
ROM of Final Fantasy (USA).  Build first (cmake --build build).
"""

import argparse
import os
import shutil
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)

PLAYER_README = """FF1-x86 -- Final Fantasy (NES), ported to the PC, with an optional 3D view
=====================================================================

This is an unofficial fan project. It contains no data from the game:
you need your own copy of Final Fantasy (USA) for the NES, as a ROM file.

Starting
--------
Run FF1-x86.exe. The first time, it asks for the ROM: drag the file onto
the window. It must be Final Fantasy (USA), the original 1990 NES release,
unmodified (other versions, translations and hacks don't work). It is
checked and kept in the game's save folder, so you're asked only once.

Keys
----
Arrows          move, move the cursor
Z / Space       A
X / Backspace   B
Tab             Select
Enter           Start
F2              2D / 3D
C / V           camera preset / view distance
Q / E           turn the camera;  PgUp / PgDn  zoom
F5 F6 F7 F8     tilt-shift, water shimmer, day and night, battle camera motion
F9              random encounters on / off
F10             battle speed 1x / 2x / 4x
Esc             quit

Final Fantasy is (c) Square Enix. Not affiliated with or endorsed by
Square Enix. Made with LOVE (love2d.org, zlib licence).
"""


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

    out = os.path.join(PORT, "release", "FF1-x86")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)

    # game.love: the front end, zipped; fused after love.exe
    game = os.path.join(PORT, "release", "game.love")
    with zipfile.ZipFile(game, "w", zipfile.ZIP_DEFLATED) as z:
        for name in os.listdir(os.path.join(PORT, "love")):
            if name.endswith(".lua"):
                z.write(os.path.join(PORT, "love", name), name)
    with open(os.path.join(out, "FF1-x86.exe"), "wb") as f:
        f.write(open(love_exe, "rb").read())
        f.write(open(game, "rb").read())
    os.remove(game)

    for name in os.listdir(args.love):
        if name.lower().endswith(".dll"):
            shutil.copy(os.path.join(args.love, name), out)
    shutil.copy(os.path.join(args.love, "license.txt"), os.path.join(out, "LOVE-license.txt"))
    shutil.copy(dll, out)
    open(os.path.join(out, "README.txt"), "w", encoding="utf-8", newline="\r\n").write(PLAYER_README)

    archive = os.path.join(PORT, "release", "FF1-x86-windows")
    shutil.make_archive(archive, "zip", os.path.join(PORT, "release"), "FF1-x86")
    print(f"wrote {out} and {archive}.zip:")
    for name in sorted(os.listdir(out)):
        print(f"  {name}  {os.path.getsize(os.path.join(out, name))} bytes")


if __name__ == "__main__":
    main()
