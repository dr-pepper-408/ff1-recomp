"""
build_rom.py -- reassemble the reference ROM from the disassembly.

For development: the disassembly in the parent folder assembles byte for
byte into Final Fantasy (USA).  This builds it (ca65 / ld65, in a temporary
folder, the disassembly untouched) and writes:

    FinalFantasy.nes   the ROM (iNES header + 256 KB), next to the disassembly
    ff1-x86/build/labels/bank_XX.lst   ca65 listings (every label's address)

The ROM is game data: it is never committed.  The game reads its data from
a ROM the player supplies (src/rom.c checks it against ROM_SHA1).

    python tools/build_rom.py
"""

import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
DISASM = os.path.dirname(PORT)
ASM_BANKS = ["01", "09", "0B", "0C", "0D", "0E", "0F"]
ORDER = ["00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "0A", "0B", "0C", "0D", "0E", "0F"]
EXPECTED_SHA1 = "c9cfbf5455085e198dce039298b083cd6fc88bce"   # the 256 KB after the header


def main():
    tmp = tempfile.mkdtemp(prefix="ff1rom_")
    try:
        for name in os.listdir(DISASM):
            if name.endswith((".asm", ".inc", ".dat", ".cfg", ".exe")) or name == "nesheader.bin":
                shutil.copy(os.path.join(DISASM, name), tmp)
        shutil.copytree(os.path.join(DISASM, "bin"), os.path.join(tmp, "bin"))
        ca65 = os.path.join(tmp, "ca65.exe" if os.name == "nt" else "ca65")
        ld65 = os.path.join(tmp, "ld65.exe" if os.name == "nt" else "ld65")
        for b in ASM_BANKS:
            subprocess.run([ca65, "-l", f"bank_{b}.asm"], cwd=tmp, check=True)
        subprocess.run([ld65, "-C", "nes.cfg"] + [f"bank_{b}.o" for b in ASM_BANKS], cwd=tmp, check=True)

        rom = open(os.path.join(tmp, "nesheader.bin"), "rb").read()
        for b in ORDER:
            ext = "bin" if b in ASM_BANKS else "dat"
            rom += open(os.path.join(tmp, f"bank_{b}.{ext}"), "rb").read()
        sha1 = hashlib.sha1(rom[16:]).hexdigest()
        if sha1 != EXPECTED_SHA1:
            print(f"warning: SHA-1 {sha1}, expected {EXPECTED_SHA1}")
        out = os.path.join(DISASM, "FinalFantasy.nes")
        open(out, "wb").write(rom)

        labels = os.path.join(PORT, "build", "labels")
        os.makedirs(labels, exist_ok=True)
        for b in ASM_BANKS:
            shutil.copy(os.path.join(tmp, f"bank_{b}.lst"), labels)
        print(f"wrote {out} ({len(rom)} bytes, SHA-1 of the banks {sha1})")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
