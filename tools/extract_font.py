#!/usr/bin/env python3
"""
Extract the FF1 menu/dialogue font from bin/bank_09_data.bin.

The font is 128 2BPP tiles (NES tile indices $80-$FF) stored at ROM $8800
(offset 0x800 in bank_09_data.bin).  It is copied to CHR-RAM tiles $80-$FF
by LoadMenuCHR (bank_0F).  Output is a grayscale sprite sheet matching the
other CHR PNGs (levels 0/85/170/255).
"""
import os

try:
    from PIL import Image
except ImportError:
    print("Pillow required: pip install Pillow")
    raise SystemExit(1)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "bin", "bank_09_data.bin")
OUT = os.path.join(ROOT, "ff1-x86", "data", "graphics", "font_chr.png")

FONT_OFFSET = 0x800   # $8800 - $8000
FONT_TILES  = 128

GRAY = [0, 85, 170, 255]


def decode_tile(data, base):
    """Decode one 2BPP tile (16 bytes) -> list of 64 grayscale values."""
    px = []
    for row in range(8):
        lo = data[base + row]
        hi = data[base + row + 8]
        for bit in range(7, -1, -1):
            val = ((hi >> bit) & 1) << 1 | ((lo >> bit) & 1)
            px.append(GRAY[val])
    return px


def main():
    with open(SRC, "rb") as f:
        data = f.read()

    if len(data) < FONT_OFFSET + FONT_TILES * 16:
        print(f"ERROR: {SRC} too small ({len(data)} bytes)")
        return 1

    tiles_per_row = 16
    rows = FONT_TILES // tiles_per_row
    W, H = tiles_per_row * 8, rows * 8
    img = Image.new("RGB", (W, H))
    px = img.load()

    for t in range(FONT_TILES):
        tile = decode_tile(data, FONT_OFFSET + t * 16)
        tx = (t % tiles_per_row) * 8
        ty = (t // tiles_per_row) * 8
        for y in range(8):
            for x in range(8):
                v = tile[y * 8 + x]
                px[tx + x, ty + y] = (v, v, v)

    img.save(OUT)
    print(f"Saved {OUT} ({W}x{H}, {FONT_TILES} tiles)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
