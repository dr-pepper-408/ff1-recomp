#!/usr/bin/env python3
"""
FF1 CHR Tile Graphics Extractor
================================
Converts all CHR (character ROM/tile graphics) files from the FF1 disassembly
into PNG sprite sheets with NES-accurate color palettes.

Usage:
    python extract_chr.py              # color output with default palettes
    python extract_chr.py --gray       # grayscale only (original mode)

Output goes to ../data/graphics/<source>_chr.png
"""

import os
import sys
import struct
from datetime import date

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT_DIR = os.path.dirname(BASE_DIR)
OUT_DIR = os.path.join(BASE_DIR, "data", "graphics")

os.makedirs(OUT_DIR, exist_ok=True)

# ---------------------------------------------------------------------------
# NES Master Palette (64 colors, RGB)
# ---------------------------------------------------------------------------
NES_PALETTE_RGB = [
    (0x75, 0x75, 0x75), (0x27, 0x1B, 0x8F), (0x00, 0x00, 0xAB), (0x47, 0x00, 0x9F),
    (0x8F, 0x00, 0x77), (0xAB, 0x00, 0x13), (0xA7, 0x00, 0x00), (0x7F, 0x0B, 0x00),
    (0x43, 0x2F, 0x00), (0x00, 0x47, 0x00), (0x00, 0x51, 0x00), (0x00, 0x3F, 0x17),
    (0x1B, 0x3F, 0x5F), (0x00, 0x2F, 0x6F), (0x00, 0x1F, 0x77), (0x13, 0x17, 0x7F),
    (0xAB, 0xAB, 0xAB), (0x53, 0x43, 0xBF), (0x0B, 0x0B, 0xDB), (0x77, 0x1B, 0xD3),
    (0xBF, 0x1F, 0xA7), (0xDB, 0x17, 0x2F), (0xDB, 0x1B, 0x00), (0xB3, 0x43, 0x00),
    (0x77, 0x5B, 0x00), (0x13, 0x73, 0x0B), (0x00, 0x7B, 0x0F), (0x00, 0x6F, 0x37),
    (0x2B, 0x6F, 0x83), (0x00, 0x5B, 0x97), (0x00, 0x43, 0x9F), (0x2B, 0x3B, 0xAB),
    (0xFF, 0xFF, 0xFF), (0x8B, 0x83, 0xFF), (0x47, 0x4B, 0xFF), (0xAF, 0x5F, 0xFF),
    (0xFF, 0x63, 0xEB), (0xFF, 0x67, 0x77), (0xFF, 0x6B, 0x37), (0xF3, 0x87, 0x0B),
    (0xBF, 0x9F, 0x0F), (0x5B, 0xBF, 0x43), (0x1B, 0xCB, 0x4F), (0x0B, 0xBB, 0x73),
    (0x6B, 0xB7, 0xCB), (0x47, 0x9F, 0xDF), (0x3B, 0x83, 0xEB), (0x73, 0x7F, 0xF7),
    (0xFF, 0xFF, 0xFF), (0xBF, 0xBF, 0xFF), (0x9B, 0x9B, 0xFF), (0xD7, 0xAB, 0xFF),
    (0xFF, 0xB3, 0xF7), (0xFF, 0xB3, 0xBB), (0xFF, 0xB7, 0x93), (0xF7, 0xC3, 0x6F),
    (0xDF, 0xCF, 0x73), (0xAB, 0xDB, 0x93), (0x87, 0xE3, 0x9B), (0x7F, 0xDF, 0xB3),
    (0xAB, 0xDB, 0xE3), (0x9B, 0xCB, 0xEF), (0x93, 0xBF, 0xF3), (0xAF, 0xBF, 0xFB),
]

# Grayscale palette
GRAY_PALETTE = [
    (0x00, 0x00, 0x00),   # 0: black / transparent
    (0x55, 0x55, 0x55),   # 1: dark gray
    (0xAA, 0xAA, 0xAA),   # 2: light gray
    (0xFF, 0xFF, 0xFF),   # 3: white
]

# Color palettes keyed by label prefix for context-appropriate colors
COLOR_PALETTES = {
    "map": [      # 0=transparent, 1=white, 2=blue, 3=red
        None,
        NES_PALETTE_RGB[0x20],
        NES_PALETTE_RGB[0x12],
        NES_PALETTE_RGB[0x06],
    ],
    "tileset": [  # 0=transparent, 1=gold, 2=brown, 3=dark orange
        None,
        NES_PALETTE_RGB[0x28],
        NES_PALETTE_RGB[0x18],
        NES_PALETTE_RGB[0x07],
    ],
    "battle": [   # 0=transparent, 1=white, 2=red, 3=dark blue
        None,
        NES_PALETTE_RGB[0x30],
        NES_PALETTE_RGB[0x16],
        NES_PALETTE_RGB[0x02],
    ],
    "puzzle": [
        None,
        NES_PALETTE_RGB[0x20],
        NES_PALETTE_RGB[0x20],
        NES_PALETTE_RGB[0x20],
    ],
    "endingbridge": [
        None,
        NES_PALETTE_RGB[0x20],
        NES_PALETTE_RGB[0x29],
        NES_PALETTE_RGB[0x12],
    ],
}

# Checkered transparency colors
CHECKER_A = (0x40, 0x40, 0x40)
CHECKER_B = (0x60, 0x60, 0x60)

# ---------------------------------------------------------------------------
# NES 2BPP tile decoding
# ---------------------------------------------------------------------------

def decode_2bpp_tile(data, tile_index):
    """Extract one 8x8 tile as a list of 8 rows, each row is 8 palette indices (0-3)."""
    base = tile_index * 16
    if base + 16 > len(data):
        return None
    rows = []
    for row in range(8):
        lo = data[base + row]
        hi = data[base + row + 8]
        row_pixels = []
        for col in range(8):
            bit = 7 - col
            lo_bit = (lo >> bit) & 1
            hi_bit = (hi >> bit) & 1
            row_pixels.append((hi_bit << 1) | lo_bit)
        rows.append(row_pixels)
    return rows


def decode_1bpp_tile(data, tile_index):
    """Extract one 8x8 tile as a list of 8 rows, each row is 8 palette indices (0 or 3)."""
    base = tile_index * 8
    if base + 8 > len(data):
        return None
    rows = []
    for row in range(8):
        byte_val = data[base + row]
        row_pixels = []
        for col in range(8):
            bit = 7 - col
            row_pixels.append(3 if (byte_val >> bit) & 1 else 0)
        rows.append(row_pixels)
    return rows


def get_palette_for_label(label):
    """Pick a color palette matching the label, or fall back to general."""
    for prefix in ["map", "tileset", "battle", "puzzle", "endingbridge"]:
        if label.startswith(prefix):
            return COLOR_PALETTES.get(prefix)
    return [None, NES_PALETTE_RGB[0x20], NES_PALETTE_RGB[0x12], NES_PALETTE_RGB[0x06]]


def tiles_to_image(tiles, num_tiles, palette, use_gray):
    """
    Convert a list of 8x8 tile arrays into flat pixel data.
    palette: list of 4 color tuples (index 0 may be None for transparency).
    """
    TILES_PER_ROW = 16
    rows_of_tiles = (num_tiles + TILES_PER_ROW - 1) // TILES_PER_ROW
    img_w = TILES_PER_ROW * 8
    img_h = rows_of_tiles * 8

    # Pick the rendering palette
    if use_gray:
        render = GRAY_PALETTE
    else:
        render = list(palette)

    pixels = []
    for tr in range(rows_of_tiles):
        for row_in_tile in range(8):
            for tc in range(TILES_PER_ROW):
                tile_idx = tr * TILES_PER_ROW + tc
                px = tc * 8
                py = tr * 8 + row_in_tile

                if tile_idx < num_tiles and tiles[tile_idx] is not None:
                    tile = tiles[tile_idx]
                    for col in range(8):
                        idx = tile[row_in_tile][col]
                        color = render[idx]
                        if color is None:
                            # Transparency: checkerboard
                            col_idx = tc * 8 + col
                            if ((col_idx % 2) + (py % 2)) % 2 == 0:
                                color = CHECKER_A
                            else:
                                color = CHECKER_B
                        pixels.append(color)
                else:
                    # Empty tile
                    for col in range(8):
                        col_idx = tc * 8 + col
                        if ((col_idx % 2) + (py % 2)) % 2 == 0:
                            pixels.append(CHECKER_A)
                        else:
                            pixels.append(CHECKER_B)
    return img_w, img_h, pixels


def save_ppm(filename, width, height, pixels):
    """Save as PPM (P3 ASCII format). Pillow-free fallback."""
    with open(filename, "w") as f:
        f.write(f"P3\n{width} {height}\n255\n")
        for i, (r, g, b) in enumerate(pixels):
            f.write(f"{r} {g} {b}")
            if (i + 1) % 16 == 0:
                f.write("\n")
            else:
                f.write("  ")
    return True


def save_png(filename, width, height, pixels):
    """Save as PNG using Pillow."""
    try:
        from PIL import Image
    except ImportError:
        return False
    img = Image.new("RGB", (width, height))
    img.putdata(pixels)
    img.save(filename, "PNG")
    return True


def save_image(filename, width, height, pixels):
    """Try PNG first, fall back to PPM."""
    if save_png(filename, width, height, pixels):
        return f"Saved PNG: {filename}"
    ppm_name = filename.replace(".png", ".ppm")
    save_ppm(ppm_name, width, height, pixels)
    return f"Saved PPM (no Pillow): {ppm_name}"


# ---------------------------------------------------------------------------
# CHR file processing
# ---------------------------------------------------------------------------

CHR_SOURCES = [
    ("bank_02.dat", "map_chr", "Map CHR tiles (bank $02, $8000)", "2bpp"),
    ("bank_03.dat", "tileset_chr", "Tileset CHR (bank $03, $8000)", "2bpp"),
    ("bank_07.dat", "battle_chr", "Battle CHR (bank $07, $8000)", "2bpp"),
    ("bin/0D_9E00_puzzle_1bpp.chr", "puzzle_chr", "Puzzle graphic 1BPP (bank $0D, $9E00)", "1bpp"),
    ("bin/minimap_decor.chr", "minimap_decor_chr", "Minimap decorations (bank $09)", "2bpp"),
    ("bin/minimap_title.chr", "minimap_title_chr", "Minimap title screen (bank $09)", "2bpp"),
    ("bin/minimap_sprite.chr", "minimap_sprite_chr", "Minimap sprites (bank $09)", "2bpp"),
    ("bin/0B_A800_endingbridge_chrnt.bin", "endingbridge_chr", "Ending/bridge CHR+NT (bank $0B, $A800)", "2bpp_chrnt"),
]


def process_chr_file(source_path, label, description, bpp_mode, use_gray):
    """Process one CHR source file and save PNG."""
    full_path = os.path.join(ROOT_DIR, source_path)
    if not os.path.exists(full_path):
        print(f"  SKIP {label}: missing {source_path}")
        return False

    with open(full_path, "rb") as f:
        data = f.read()
    print(f"  {label}: {len(data)} bytes from {source_path}")

    palette = get_palette_for_label(label)

    if bpp_mode in ("2bpp", "2bpp_chrnt"):
        if bpp_mode == "2bpp_chrnt":
            data = data[:4096] if len(data) >= 4096 else data
        tile_size = 16
        num_tiles = len(data) // tile_size
        tiles = [decode_2bpp_tile(data, i) for i in range(num_tiles)]
    elif bpp_mode == "1bpp":
        tile_size = 8
        num_tiles = len(data) // tile_size
        tiles = [decode_1bpp_tile(data, i) for i in range(num_tiles)]
    else:
        print(f"    Unknown bpp_mode: {bpp_mode}")
        return False

    if num_tiles == 0:
        print("    No tiles extracted")
        return False

    img_w, img_h, pixels = tiles_to_image(tiles, num_tiles, palette, use_gray)
    out_name = f"{label}.png"
    out_path = os.path.join(OUT_DIR, out_name)
    result = save_image(out_path, img_w, img_h, pixels)
    print(f"    {result}")
    print(f"    Image: {img_w}x{img_h}, {num_tiles} tiles")
    return True


def process_special_bank_0B_endingbridge(use_gray):
    """Handle endingbridge CHR+NT combo."""
    source_path = "bin/0B_A800_endingbridge_chrnt.bin"
    full_path = os.path.join(ROOT_DIR, source_path)
    if not os.path.exists(full_path):
        print(f"  SKIP endingbridge: missing {source_path}")
        return

    with open(full_path, "rb") as f:
        data = f.read()
    print(f"  endingbridge_chrnt: {len(data)} bytes total")

    chr_data = data[:4096]
    nt_data = data[4096:]

    tile_size = 16
    num_tiles = len(chr_data) // tile_size
    tiles = [decode_2bpp_tile(chr_data, i) for i in range(num_tiles)]

    palette = get_palette_for_label("endingbridge_chr")
    img_w, img_h, pixels = tiles_to_image(tiles, num_tiles, palette, use_gray)
    out_path = os.path.join(OUT_DIR, "endingbridge_chr.png")
    result = save_image(out_path, img_w, img_h, pixels)
    print(f"    {result}")
    print(f"    CHR: {img_w}x{img_h}, {num_tiles} tiles")

    if nt_data:
        print(f"    Nametable: {len(nt_data)} bytes (see endingbridge_nt.h)")


def process_minimap_nametable():
    """Process minimap_nt.nam."""
    source_path = "bin/minimap_nt.nam"
    full_path = os.path.join(ROOT_DIR, source_path)
    if not os.path.exists(full_path):
        print(f"  SKIP minimap_nt: missing {source_path}")
        return

    with open(full_path, "rb") as f:
        data = f.read()
    print(f"  minimap_nt.nam: {len(data)} bytes")

    if len(data) >= 960:
        tiles_portion = data[:960]
        w, h = 32, 30
        pixels = [(b, b, b) for b in tiles_portion]
        out_path = os.path.join(OUT_DIR, "minimap_nt.png")
        result = save_image(out_path, w, h, pixels)
        print(f"    {result} (tile index visualization)")


def main():
    use_gray = "--gray" in sys.argv

    print("=" * 60)
    print("FF1 CHR Tile Graphics Extractor")
    print("=" * 60)
    mode = "grayscale" if use_gray else "NES color"
    print(f"Mode: {mode}")
    print()

    try:
        import PIL
        print("Pillow available - will output PNG files.")
    except ImportError:
        print("Pillow NOT available - will output PPM files. Install Pillow: pip install Pillow")
    print()

    count = 0
    for source, label, desc, mode_bpp in CHR_SOURCES:
        print(f"[{label}] {desc}")
        if mode_bpp == "2bpp_chrnt":
            continue
        if process_chr_file(source, label, desc, mode_bpp, use_gray):
            count += 1
        print()

    print("[endingbridge_chrnt] Ending/bridge CHR+NT")
    process_special_bank_0B_endingbridge(use_gray)
    print()

    print("[minimap_nt] Minimap nametable")
    process_minimap_nametable()
    print()

    print("=" * 60)
    print(f"Done! Processed {count} CHR files.")
    print(f"Output: {OUT_DIR}")
    print("=" * 60)


if __name__ == "__main__":
    main()