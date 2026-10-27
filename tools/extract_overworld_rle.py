#!/usr/bin/env python3
"""Extract the RLE-compressed overworld data from bank_01.asm.

The .BYTE directives between .INCBIN and MinimapDecompress:
contain the per-row RLE data for the 256×256 overworld map.
"""
import re
import sys

asm_path = sys.argv[1] if len(sys.argv) > 1 else "bank_01.asm"
out_path = sys.argv[2] if len(sys.argv) > 2 else "ff1-x86/data/overworld_rle.bin"

with open(asm_path, "r") as f:
    data = f.read()

# Find all .BYTE lines after .INCBIN, up to the end of the file (MinimapDecompress follows)
# Match lines starting with .BYTE that contain hex values like $XX
pattern = r'\.BYTE\s+((?:\$[0-9A-Fa-f]{2}[,\s]*)+)'
matches = list(re.finditer(pattern, data))

vals = []
in_incbin = False
for m in matches:
    # We want .BYTE lines that appear AFTER .INCBIN
    # Find the .INCBIN first
    if not in_incbin:
        # Look backwards for .INCBIN
        before = data[:m.start()]
        incbin_pos = before.rfind(".INCBIN")
        if incbin_pos >= 0:
            in_incbin = True
    
    if in_incbin:
        hex_vals = re.findall(r'\$([0-9A-Fa-f]{2})', m.group(0))
        for h in hex_vals:
            vals.append(int(h, 16))

print(f"Extracted {len(vals)} bytes")
with open(out_path, "wb") as f:
    f.write(bytes(vals))
print(f"Written to {out_path}")