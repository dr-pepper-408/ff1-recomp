#!/usr/bin/env python3
"""
FF1 Data Table Extractor
========================
Extracts all data tables from the Final Fantasy 1 NES disassembly into C-compatible
header files in data/generated/.

Source files are .dat (raw ROM banks), .bin (extracted sections), and .tbl (character tables).
"""

import os
import re
import sys
import struct
import textwrap
from datetime import date

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT_DIR = os.path.dirname(BASE_DIR)  # FF1Disassembly root
# FF1_GEN_OUT overrides the output directory (e.g. to diff against the
# checked-in headers without overwriting them).
OUT_DIR = os.environ.get("FF1_GEN_OUT") or os.path.join(BASE_DIR, "data", "generated")

os.makedirs(OUT_DIR, exist_ok=True)

# ---------------------------------------------------------------------------
# The input: one ROM of Final Fantasy (USA)
#
# Everything is read from the ROM (FF1_ROM, or ../FinalFantasy.nes: see
# tools/build_rom.py).  Where tables are is known by address: the slices the
# disassembly keeps as bank_XX.dat / bin/*.bin by bank and offset
# (BIN_LAYOUT), the tables in its .asm sources by their labels' offsets in
# the ca65 listings build_rom.py writes (build/labels/bank_XX.lst).
# ---------------------------------------------------------------------------

import hashlib

ROM_SHA1 = "c9cfbf5455085e198dce039298b083cd6fc88bce"
BANK = 0x4000
_rom = None

def rom():
    """The ROM's 256 KB (no header), checked."""
    global _rom
    if _rom is None:
        path = os.environ.get("FF1_ROM") or os.path.join(ROOT_DIR, "FinalFantasy.nes")
        data = open(path, "rb").read()
        if len(data) == 16 * BANK + 16:
            data = data[16:]
        if len(data) != 16 * BANK or hashlib.sha1(data).hexdigest() != ROM_SHA1:
            raise SystemExit(f"{path}: not Final Fantasy (USA)")
        _rom = data
    return _rom

def rom_bytes(bank, offset, count):
    return rom()[bank * BANK + offset: bank * BANK + offset + count]

# The disassembly's .bin slices: name -> (bank, offset in the bank, size).
BIN_LAYOUT = {
    "0B_8000_battledomains.bin": (0x0B, 0x0000, 1024),
    "0B_8400_battleformations.bin": (0x0B, 0x0400, 2048),
    "0B_8C00_mapencounterrates.bin": (0x0B, 0x0C00, 64),
    "0B_8C40_battlemessages.bin": (0x0B, 0x0C40, 940),
    "0B_9094_levelupdata.bin": (0x0B, 0x1094, 588),
    "0B_92E0_fiendtsa.bin": (0x0B, 0x12E0, 320),
    "0B_9420_chaostsa.bin": (0x0B, 0x1420, 192),
    "0B_94E0_enemynames.bin": (0x0B, 0x14E0, 1136),
    "0B_A800_endingbridge_chrnt.bin": (0x0B, 0x2800, 6144),
    "0C_8000_weapondata.bin": (0x0C, 0x0000, 320),
    "0C_8140_armordata.bin": (0x0C, 0x0140, 160),
    "0C_81E0_magicdata.bin": (0x0C, 0x01E0, 736),
    "0C_8520_enemydata.bin": (0x0C, 0x0520, 2560),
    "0C_8F20_battlepalettes.bin": (0x0C, 0x0F20, 256),
    "0C_9020_aidata.bin": (0x0C, 0x1020, 704),
    "0D_8000_scoredata.bin": (0x0D, 0x0000, 7584),
    "0D_9E00_puzzle_1bpp.chr": (0x0D, 0x1E00, 512),
    "0D_A000_theenddrawdata.bin": (0x0D, 0x2000, 1024),
    "0D_A681_theendluts.bin": (0x0D, 0x2681, 248),
    "0D_A800_storytext.bin": (0x0D, 0x2800, 1536),
    "0D_B5C9_unknown.bin": (0x0D, 0x35C9, 567),
    "0D_BB00_credittext.bin": (0x0D, 0x3B00, 142),
    "0D_BB8E_unknown.bin": (0x0D, 0x3B8E, 626),
    "0D_BF20_introtext.bin": (0x0D, 0x3F20, 224),
    "0E_8000_shopstrings.bin": (0x0E, 0x0000, 768),
    "0E_8300_shopdata.bin": (0x0E, 0x0300, 384),
    "0E_8500_menutext.bin": (0x0E, 0x0500, 2160),
    "0E_95D5_objectdata.bin": (0x0E, 0x15D5, 832),
    "0F_F100_rngtable.bin": (0x0F, 0x3100, 256),
    "0F_FCF1_rngtable.bin": (0x0F, 0x3CF1, 256),
    "bank_01_data.bin": (0x01, 0x0000, 8192),
    "bank_09_data.bin": (0x09, 0x0000, 8192),
    "minimap_decor.chr": (0x09, 0x3400, 768),
    "minimap_nt.nam": (0x09, 0x3000, 1024),
    "minimap_sprite.chr": (0x09, 0x3F00, 32),
    "minimap_title.chr": (0x09, 0x3700, 640),
}

def rom_offset_to_file_pos(rom_addr, bank):
    """Convert a ROM address+within-bank to offset in the bank."""
    return rom_addr - 0x8000

def load_bank(bank_num):
    """A whole bank of the ROM."""
    return rom_bytes(bank_num, 0, BANK)

def load_bin(filename):
    """One of the disassembly's .bin slices, from the ROM."""
    if filename not in BIN_LAYOUT:
        return None
    return rom_bytes(*BIN_LAYOUT[filename])

# ---- labels: their offsets, from the ca65 listings ----

_labels = {}

def bank_labels(asm_name):
    """[(label, offset in the bank)] in source order, for bank_XX.asm."""
    import re
    if asm_name not in _labels:
        path = os.path.join(BASE_DIR, "build", "labels", asm_name.replace(".asm", ".lst"))
        if not os.path.exists(path):
            raise SystemExit(f"{path} missing: run tools/build_rom.py")
        out = []
        for line in open(path, encoding="utf-8", errors="replace"):
            m = re.match(r"^([0-9A-F]{6})r\s+1\s", line)
            if not m or len(line) < 25:
                continue
            src = line[24:].split(";")[0].strip()
            lm = re.match(r"^([@A-Za-z_]\w*):", src)
            if lm:
                out.append((lm.group(1), int(m.group(1), 16)))
        _labels[asm_name] = out
    return _labels[asm_name]

def label_offset(asm_name, label, after=None):
    """The offset in its bank of `label` (a local one: the first after the
    global `after`)."""
    labels = bank_labels(asm_name)
    start = 0
    if after:
        start = next(i for i, (n, _) in enumerate(labels) if n == after)
    for n, off in labels[start:]:
        if n == label:
            return off
    raise ValueError(f"label {label} not found in {asm_name}")

def asm_bank(asm_name):
    return int(asm_name[5:7], 16)

def asm_label_bytes(asm_name, label, count):
    """`count` bytes at `label:` (bank_XX.asm's), from the ROM."""
    return list(rom_bytes(asm_bank(asm_name), label_offset(asm_name, label), count))

# ---------------------------------------------------------------------------
# DTE Decoding (Dual Tile Encoding)
# ---------------------------------------------------------------------------

def load_tbl(filepath):
    """Parse a .tbl file returning dict of hex_byte -> char_string."""
    tbl = {}
    with open(filepath, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line.strip() or "=" not in line:
                continue
            key, val = line.split("=", 1)
            try:
                byte_val = int(key.strip(), 16)
            except ValueError:
                continue
            tbl[byte_val] = val
    return tbl

def decode_dte(data, offset, dte_tbl, max_len=256):
    """
    Decode a DTE-compressed string starting at `offset` in `data`.
    Returns (decoded_string, new_offset).
    """
    result = []
    pos = offset
    while pos < len(data):
        b = data[pos]
        pos += 1
        if b == 0x00:
            break
        if b == 0x05:
            result.append("\\n")
        elif b in dte_tbl:
            result.append(dte_tbl[b])
        else:
            # Unknown byte - output as escape
            result.append(f"\\x{b:02X}")
        if len(result) >= max_len:
            break
    return "".join(result), pos

# ---------------------------------------------------------------------------
# Header writer
# ---------------------------------------------------------------------------

def write_header(f, title, source_desc, extra=""):
    f.write("/*\n")
    f.write(f" * {title}\n")
    f.write(f" * Source: {source_desc}\n")
    f.write(f" * Generated: {date.today().isoformat()}\n")
    if extra:
        f.write(f" * {extra}\n")
    f.write(" */\n\n")
    f.write("#pragma once\n\n")
    f.write("#include <stdint.h>\n\n")

# ---------------------------------------------------------------------------
# 1. Constants
# ---------------------------------------------------------------------------

def extract_constants():
    """Parse Constants.inc and produce constants.h"""
    inc_path = os.path.join(ROOT_DIR, "Constants.inc")
    with open(inc_path, "r", encoding="utf-8") as f:
        lines = f.readlines()

    with open(os.path.join(OUT_DIR, "constants.h"), "w", encoding="utf-8") as out:
        out.write("/*\n")
        out.write(" * constants.h - FF1 game constants\n")
        out.write(f" * Source: Constants.inc\n")
        out.write(f" * Generated: {date.today().isoformat()}\n")
        out.write(" */\n\n")
        out.write("#pragma once\n\n")

        # Parse all = definitions
        defines = []
        enums = {}
        current_section = ""
        for line in lines:
            line = line.rstrip()
            if line.startswith(";;"):
                current_section = line.lstrip("; ").strip()
                continue
            if "=" not in line:
                continue
            # Remove comments
            if ";" in line:
                line = line.split(";")[0].strip()
            if not line or "=" not in line:
                continue
            parts = line.split("=", 1)
            name = parts[0].strip()
            value_str = parts[1].strip()

            # Handle values with arithmetic
            try:
                # Replace %XXXX with 0bXXXX
                val = value_str
                # Handle % binary
                import re
                val = re.sub(r"%([01]+)", lambda m: str(int(m.group(1), 2)), val)
                # Handle $ hex
                val = re.sub(r"\$([0-9A-Fa-f]+)", lambda m: str(int(m.group(1), 16)), val)
                # Evaluate any remaining arithmetic (may refer to earlier constants)
                num_val = eval(val, {"__builtins__": {}}, {n: v for n, v, _ in defines})
            except Exception:
                continue

            if name.startswith("CLS_") and name not in ["CLS_FT", "CLS_TH"]:
                pass  # Collect classes separately

            defines.append((name, num_val, current_section))

        # Collect classes into an enum
        class_defs = [(n, v) for n, v, _ in defines if n.startswith("CLS_")]
        spell_defs = [(n, v) for n, v, _ in defines if n.startswith("MG_") or n.startswith("SPELL_")]
        other_defs = [(n, v, s) for n, v, s in defines if not n.startswith("CLS_") and not (n.startswith("MG_") or n.startswith("SPELL_"))]

        last_section = ""
        for name, val, section in other_defs:
            # ROM addresses of tables get an _ADDR suffix so they can't clash
            # with the generated arrays that hold those tables.
            if name.startswith("lut_"):
                name += "_ADDR"
            if section != last_section:
                out.write(f"// {section}\n")
                last_section = section
            # Format value
            if isinstance(val, int):
                if val < 256:
                    out.write(f"#define {name} 0x{val:02X}\n")
                else:
                    out.write(f"#define {name} 0x{val:04X}\n")
            else:
                out.write(f"#define {name} {val}\n")

        # Classes enum
        out.write("\n// Character Classes\n")
        out.write("typedef enum {\n")
        for name, val in class_defs:
            label = name.replace("CLS_", "CLASS_")
            out.write(f"    {label} = 0x{val:02X},\n")
        out.write("} CharacterClass;\n\n")

        # Spells enum
        out.write("// Spell IDs\n")
        out.write("typedef enum {\n")
        for name, val in spell_defs:
            label = name.replace("MG_", "SPELL_")
            out.write(f"    {label} = 0x{val:02X},\n")
        out.write("} SpellID;\n\n")

    print(f"  constants.h: {len(defines)} definitions")

# ---------------------------------------------------------------------------
# 2. Weapon Data
# ---------------------------------------------------------------------------

def extract_weapon_data():
    """Extract weapon data from bin/0C_8000_weapondata.bin (8 bytes per weapon per bank_0C.asm)"""
    data = load_bin("0C_8000_weapondata.bin")
    if not data:
        print("  SKIP weapon_data.h: missing bin/0C_8000_weapondata.bin")
        return

    entry_size = 8  # Per bank_0C.asm: hit_rate, damage, crit_rate, spell, element, category, graphic, palette
    count = len(data) // entry_size

    with open(os.path.join(OUT_DIR, "weapon_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Weapon Data",
                     "bin/0C_8000_weapondata.bin (bank $0C, ROM $8000)",
                     "8 bytes per weapon. See bank_0C.asm for equip logic.")

        f.write(f"#define WEAPON_COUNT {count}\n\n")
        f.write("typedef struct {\n")
        f.write("    uint8_t hit_rate;      // +0: Hit rate bonus\n")
        f.write("    uint8_t damage;        // +1: Base damage\n")
        f.write("    uint8_t crit_rate;     // +2: Critical hit rate (BUGGED - not used)\n")
        f.write("    uint8_t spell_id;      // +3: Spell cast on hit\n")
        f.write("    uint8_t element;       // +4: Elemental property\n")
        f.write("    uint8_t category;      // +5: Category effectiveness (vs Giant/Undead/etc)\n")
        f.write("    uint8_t battle_gfx;    // +6: Graphic index in battle\n")
        f.write("    uint8_t palette;       // +7: Palette index\n")
        f.write("} WeaponData;\n\n")
        f.write("static const WeaponData weapon_table[WEAPON_COUNT] = {\n")

        for i in range(count):
            off = i * entry_size
            vals = list(data[off:off+entry_size])
            f.write("    { ")
            f.write(", ".join(f"0x{v:02X}" for v in vals))
            f.write(" }")
            if i < count - 1:
                f.write(",\n")
            else:
                f.write("\n")

        f.write("};\n")

    print(f"  weapon_data.h: {count} weapons ({len(data)} bytes)")

# ---------------------------------------------------------------------------
# 3. Armor Data
# ---------------------------------------------------------------------------

def extract_armor_data():
    """Extract armor data from bin/0C_8140_armordata.bin (4 bytes per armor per bank_0C.asm)"""
    data = load_bin("0C_8140_armordata.bin")
    if not data:
        print("  SKIP armor_data.h: missing bin/0C_8140_armordata.bin")
        return

    entry_size = 4  # Per bank_0C.asm: evade_penalty, absorb_boost, elemental_defense, spell
    count = len(data) // entry_size

    with open(os.path.join(OUT_DIR, "armor_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Armor Data",
                     "bin/0C_8140_armordata.bin (bank $0C, ROM $8140)",
                     "4 bytes per armor piece. See bank_0C.asm.")

        f.write(f"#define ARMOR_COUNT {count}\n\n")
        f.write("typedef struct {\n")
        f.write("    uint8_t evade_penalty;  // +0: Evade penalty\n")
        f.write("    uint8_t absorb_boost;   // +1: Absorb (defense) boost\n")
        f.write("    uint8_t elem_defense;   // +2: Elemental defense bits\n")
        f.write("    uint8_t spell_id;       // +3: Spell cast\n")
        f.write("} ArmorData;\n\n")
        f.write("static const ArmorData armor_table[ARMOR_COUNT] = {\n")

        for i in range(count):
            off = i * entry_size
            vals = list(data[off:off+entry_size])
            f.write("    { ")
            f.write(", ".join(f"0x{v:02X}" for v in vals))
            f.write(" }")
            if i < count - 1:
                f.write(",\n")
            else:
                f.write("\n")

        f.write("};\n")

    print(f"  armor_data.h: {count} armor pieces ({len(data)} bytes)")

# ---------------------------------------------------------------------------
# 4. Enemy Data
# ---------------------------------------------------------------------------

def extract_enemy_data():
    """Extract enemy data from bin/0C_8520_enemydata.bin ($14 bytes per enemy)"""
    data = load_bin("0C_8520_enemydata.bin")
    if not data:
        print("  SKIP enemy_data.h: missing bin/0C_8520_enemydata.bin")
        return

    entry_size = 20  # 0x14 bytes
    count = len(data) // entry_size

    with open(os.path.join(OUT_DIR, "enemy_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Enemy Data",
                     "bin/0C_8520_enemydata.bin (bank $0C, ROM $8520)",
                     "20 bytes per enemy. See Constants.inc ENROMSTAT_* for layout.")

        f.write(f"#define ENEMY_COUNT {count}\n\n")
        f.write("typedef struct {\n")
        f.write("    uint16_t exp_reward;     // +0x00: Experience reward\n")
        f.write("    uint16_t gp_reward;      // +0x02: GP reward\n")
        f.write("    uint16_t max_hp;         // +0x04: Max HP\n")
        f.write("    uint8_t  morale;         // +0x06: Morale\n")
        f.write("    uint8_t  ai_script;      // +0x07: AI script index\n")
        f.write("    uint8_t  evade;          // +0x08: Evade/Agility\n")
        f.write("    uint8_t  absorb;         // +0x09: Absorb (Defense)\n")
        f.write("    uint8_t  num_hits;       // +0x0A: Number of hits\n")
        f.write("    uint8_t  hit_rate;       // +0x0B: Hit rate\n")
        f.write("    uint8_t  damage;         // +0x0C: Damage (Strength)\n")
        f.write("    uint8_t  crit_rate;      // +0x0D: Critical hit rate\n")
        f.write("    uint8_t  unknown_E;      // +0x0E: Unknown\n")
        f.write("    uint8_t  attack_ailment; // +0x0F: Attack ailment inflicted\n")
        f.write("    uint8_t  category;       // +0x10: Category bitmask\n")
        f.write("    uint8_t  mag_def;        // +0x11: Magic defense\n")
        f.write("    uint8_t  elem_weakness;  // +0x12: Elemental weakness bitmask\n")
        f.write("    uint8_t  elem_resist;    // +0x13: Elemental resist bitmask\n")
        f.write("} EnemyData;\n\n")
        f.write("static const EnemyData enemy_table[ENEMY_COUNT] = {\n")

        for i in range(count):
            off = i * entry_size
            exp_r = struct.unpack_from("<H", data, off)[0]
            gp_r = struct.unpack_from("<H", data, off+2)[0]
            hp = struct.unpack_from("<H", data, off+4)[0]
            rest = list(data[off+6:off+20])
            f.write(f"    {{ 0x{exp_r:04X}, 0x{gp_r:04X}, 0x{hp:04X}, ")
            f.write(", ".join(f"0x{v:02X}" for v in rest))
            f.write(" }")
            if i < count - 1:
                f.write(",\n")
            else:
                f.write("\n")

        f.write("};\n")

    print(f"  enemy_data.h: {count} enemies ({len(data)} bytes)")

# ---------------------------------------------------------------------------
# 5. Magic Data
# ---------------------------------------------------------------------------

def extract_magic_data():
    """Extract magic data from bin/0C_81E0_magicdata.bin (8 bytes per entry)"""
    data = load_bin("0C_81E0_magicdata.bin")
    if not data:
        print("  SKIP magic_data.h: missing bin/0C_81E0_magicdata.bin")
        return

    entry_size = 8
    spell_count = 64       # $40 spells
    potion_count = 2       # $02 potions
    attack_count = 26      # $1A enemy attacks
    total_entries = spell_count + potion_count + attack_count  # 92

    count = min(len(data) // entry_size, total_entries)

    with open(os.path.join(OUT_DIR, "magic_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Magic Data",
                     "bin/0C_81E0_magicdata.bin (bank $0C, ROM $81E0)",
                     "8 bytes per entry: 64 spells + 2 potions + 26 enemy attacks.")

        f.write(f"#define MAGIC_SPELL_COUNT {spell_count}\n")
        f.write(f"#define MAGIC_POTION_COUNT {potion_count}\n")
        f.write(f"#define MAGIC_ATTACK_COUNT {attack_count}\n")
        f.write(f"#define MAGIC_TOTAL_COUNT {count}\n\n")
        f.write("typedef struct {\n")
        f.write("    uint8_t hit_rate;     // +0: Hit rate\n")
        f.write("    uint8_t effectivity;  // +1: Effectivity\n")
        f.write("    uint8_t element;      // +2: Element\n")
        f.write("    uint8_t target;       // +3: Target (1=all enemies, 2=one enemy, 4=self, 8=all party, 0x10=one party)\n")
        f.write("    uint8_t effect;       // +4: Effect type\n")
        f.write("    uint8_t graphic;      // +5: Graphic index\n")
        f.write("    uint8_t palette;      // +6: Palette index\n")
        f.write("    uint8_t unused;       // +7: Unused\n")
        f.write("} MagicData;\n\n")
        f.write("static const MagicData magic_table[MAGIC_TOTAL_COUNT] = {\n")

        labels = []
        for i in range(spell_count):
            labels.append(f"/* SPELL {i:02d} */")
        for i in range(potion_count):
            labels.append(f"/* POTION {i} */")
        for i in range(attack_count):
            labels.append(f"/* ATTACK {i:02d} */")

        for i in range(count):
            off = i * entry_size
            vals = list(data[off:off+entry_size])
            if i < len(labels):
                f.write(f"    {labels[i]} {{ ")
            else:
                f.write(f"    {{ ")
            f.write(", ".join(f"0x{v:02X}" for v in vals))
            f.write(" }")
            if i < count - 1:
                f.write(",\n")
            else:
                f.write("\n")

        f.write("};\n")

    print(f"  magic_data.h: {count} entries ({len(data)} bytes)")

# ---------------------------------------------------------------------------
# 6. Battle Domains
# ---------------------------------------------------------------------------

def extract_battle_domains():
    data = load_bin("0B_8000_battledomains.bin")
    if not data:
        print("  SKIP battle_domains.h: missing bin/0B_8000_battledomains.bin")
        return

    entry_size = 2
    count = len(data) // entry_size

    with open(os.path.join(OUT_DIR, "battle_domains.h"), "w", encoding="utf-8") as f:
        write_header(f, "Battle Domains",
                     "bin/0B_8000_battledomains.bin (bank $0B, ROM $8000)",
                     "256 entries × 2 bytes each, pointers to battle formations.")

        f.write(f"#define BATTLE_DOMAIN_COUNT {count}\n\n")
        f.write("static const uint16_t battle_domain_table[BATTLE_DOMAIN_COUNT] = {\n")

        for i in range(count):
            off = i * entry_size
            val = struct.unpack_from("<H", data, off)[0]
            f.write(f"    0x{val:04X}")
            if i < count - 1:
                f.write(",\n")
            else:
                f.write("\n")

        f.write("};\n")

    print(f"  battle_domains.h: {count} entries ({len(data)} bytes)")

# ---------------------------------------------------------------------------
# 7. Battle Formations
# ---------------------------------------------------------------------------

def extract_battle_formations():
    data = load_bin("0B_8400_battleformations.bin")
    if not data:
        print("  SKIP battle_formations.h: missing bin/0B_8400_battleformations.bin")
        return

    entry_size = 16
    count = len(data) // entry_size

    with open(os.path.join(OUT_DIR, "battle_formations.h"), "w", encoding="utf-8") as f:
        write_header(f, "Battle Formations",
                     "bin/0B_8400_battleformations.bin (bank $0B, ROM $8400)",
                     "16 bytes per formation. See some formats.txt.")

        f.write(f"#define BATTLE_FORMATION_COUNT {count}\n\n")
        f.write("typedef struct {\n")
        f.write("    uint8_t type_pattern;   // +0: High nibble=battle type, low nibble=pattern table\n")
        f.write("    uint8_t gfx_assign;     // +1: Graphics assignment (2 bits per enemy, bit-reversed)\n")
        f.write("    uint8_t enemy_ids[4];   // +2-+5: Enemy IDs\n")
        f.write("    uint8_t qty_a[4];       // +6-+9: Min/max counts, Formation A\n")
        f.write("    uint8_t palette_0;      // +10: Palette ID 0\n")
        f.write("    uint8_t palette_1;      // +11: Palette ID 1\n")
        f.write("    uint8_t surprise_rate;  // +12: Surprise rate\n")
        f.write("    uint8_t flags;          // +13: High nibble=palette assign, bit0=no-run\n")
        f.write("    uint8_t qty_b[2];       // +14-+15: Min/max counts, Formation B\n")
        f.write("} BattleFormation;\n\n")
        f.write("static const BattleFormation battle_formation_table[BATTLE_FORMATION_COUNT] = {\n")

        for i in range(count):
            off = i * entry_size
            tp = data[off]
            gfx = data[off+1]
            en_ids = list(data[off+2:off+6])
            qty_a = list(data[off+6:off+10])
            pal0 = data[off+10]
            pal1 = data[off+11]
            surp = data[off+12]
            flags = data[off+13]
            qty_b = list(data[off+14:off+16])
            f.write(f"    {{ 0x{tp:02X}, 0x{gfx:02X}, ")
            f.write("{" + ", ".join(f"0x{v:02X}" for v in en_ids) + "}, ")
            f.write("{" + ", ".join(f"0x{v:02X}" for v in qty_a) + "}, ")
            f.write(f"0x{pal0:02X}, 0x{pal1:02X}, 0x{surp:02X}, 0x{flags:02X}, ")
            f.write("{" + ", ".join(f"0x{v:02X}" for v in qty_b) + "}")
            f.write(" }")
            if i < count - 1:
                f.write(",\n")
            else:
                f.write("\n")

        f.write("};\n")

    print(f"  battle_formations.h: {count} formations ({len(data)} bytes)")

# ---------------------------------------------------------------------------
# 8. Battle Palettes
# ---------------------------------------------------------------------------

def extract_battle_palettes():
    data = load_bin("0C_8F20_battlepalettes.bin")
    if not data:
        print("  SKIP battle_palettes.h: missing bin/0C_8F20_battlepalettes.bin")
        return

    entry_size = 32  # $20 bytes = 16 colors × 2 bytes
    count = len(data) // entry_size

    with open(os.path.join(OUT_DIR, "battle_palettes.h"), "w", encoding="utf-8") as f:
        write_header(f, "Battle Palettes",
                     "bin/0C_8F20_battlepalettes.bin (bank $0C, ROM $8F20)",
                     "$20 bytes per palette (16 NES colors × 2 bytes each).")

        f.write(f"#define BATTLE_PALETTE_COUNT {count}\n")
        f.write(f"#define BATTLE_PALETTE_COLORS 16\n\n")
        f.write("static const uint8_t battle_palette_table[BATTLE_PALETTE_COUNT][BATTLE_PALETTE_COLORS * 2] = {\n")

        for i in range(count):
            off = i * entry_size
            colors = list(data[off:off+entry_size])
            f.write("    { ")
            f.write(", ".join(f"0x{v:02X}" for v in colors))
            f.write(" }")
            if i < count - 1:
                f.write(",\n")
            else:
                f.write("\n")

        f.write("};\n")

    print(f"  battle_palettes.h: {count} palettes ({len(data)} bytes)")

# ---------------------------------------------------------------------------
# 9. Enemy AI Data
# ---------------------------------------------------------------------------

def extract_enemy_ai():
    data = load_bin("0C_9020_aidata.bin")
    if not data:
        print("  SKIP enemy_ai.h: missing bin/0C_9020_aidata.bin")
        return

    # lut_EnemyAi (bank_0C, ROM $9020) is a FIXED 16-byte-per-entry table, indexed
    # by each enemy's ai_script byte.  Per bank_0C.asm:
    #   byte 0    = magic cast rate ($00-$80)
    #   byte 1    = special attack rate ($00-$80)
    #   bytes 2-9 = magic spell list (0-based spell ids, $FF = end)
    #   byte $A   = unused
    #   bytes $B-$E = special attack list (0-based attack ids, $FF = end)
    #   byte $F   = unused
    entry_size = 16
    count = len(data) // entry_size
    if count * entry_size != len(data):
        print(f"  WARN enemy_ai.h: {len(data)} bytes not a multiple of {entry_size}")

    with open(os.path.join(OUT_DIR, "enemy_ai.h"), "w", encoding="utf-8") as f:
        write_header(f, "Enemy AI Data",
                     "bin/0C_9020_aidata.bin (bank $0C, ROM $9020)",
                     "Fixed 16-byte entries (lut_EnemyAi), indexed by enemy_data.ai_script. See bank_0C.asm Enemy_DoAi.")

        f.write(f"#define ENEMY_AI_COUNT {count}\n")
        f.write(f"#define ENEMY_AI_NO_SCRIPT 0xFF\n\n")
        f.write("typedef struct {\n")
        f.write("    uint8_t magic_rate;   // +0:  chance to cast magic ($00-$80)\n")
        f.write("    uint8_t special_rate; // +1:  chance to use special attack ($00-$80)\n")
        f.write("    uint8_t spells[8];    // +2..9: magic spell ids (0-based, $FF = end)\n")
        f.write("    uint8_t _pad_A;       // +10:  unused\n")
        f.write("    uint8_t special[4];   // +11..14: special attack ids (0-based, $FF = end)\n")
        f.write("    uint8_t _pad_F;       // +15:  unused\n")
        f.write("} EnemyAi;\n\n")
        f.write(f"static const EnemyAi enemy_ai_table[ENEMY_AI_COUNT] = {{\n")

        for i in range(count):
            off = i * entry_size
            entry = data[off:off + entry_size]
            f.write("    { ")
            f.write(f"0x{entry[0]:02X}, 0x{entry[1]:02X}, ")
            f.write("{ " + ", ".join(f"0x{b:02X}" for b in entry[2:10]) + " }, ")
            f.write(f"0x{entry[10]:02X}, ")
            f.write("{ " + ", ".join(f"0x{b:02X}" for b in entry[11:15]) + " }, ")
            f.write(f"0x{entry[15]:02X} ")
            f.write("}")
            if i < count - 1:
                f.write(",\n")
            else:
                f.write("\n")

        f.write("};\n")

    print(f"  enemy_ai.h: {count} AI entries ({len(data)} bytes)")

# ---------------------------------------------------------------------------
# 10. Enemy Names (DTE-decoded)
# ---------------------------------------------------------------------------

def extract_enemy_names():
    data = load_bin("0B_94E0_enemynames.bin")
    if not data:
        print("  SKIP enemy_names.h: missing bin/0B_94E0_enemynames.bin")
        return

    dte_tbl = load_tbl(os.path.join(ROOT_DIR, "table_dte.tbl"))
    # Also load standard for any bytes not in DTE
    std_tbl = load_tbl(os.path.join(ROOT_DIR, "table_standard.tbl"))
    # Merge: standard table for bytes not in DTE
    for k, v in std_tbl.items():
        if k not in dte_tbl:
            dte_tbl[k] = v

    # Enemy names data: pointer table at start, then DTE strings
    # Pointers are ROM addresses within bank $0B, relative to $94E0
    BASE_ROM = 0x94E0
    ptrs = []
    i = 0
    while i + 2 <= len(data):
        p = struct.unpack_from("<H", data, i)[0]
        file_off = p - BASE_ROM
        if 0 <= file_off < len(data) and p >= 0x8000:
            ptrs.append(file_off)
            i += 2
        else:
            break

    # Decode each name using the pointer table
    names = []
    for file_off in ptrs:
        name, _ = decode_dte(data, file_off, dte_tbl)
        names.append(name)

    with open(os.path.join(OUT_DIR, "enemy_names.h"), "w", encoding="utf-8") as f:
        write_header(f, "Enemy Names",
                     "bin/0B_94E0_enemynames.bin (bank $0B, ROM $94E0)",
                     "DTE-decoded enemy name strings. Pointer table at start, then DTE strings.")

        f.write(f"#define ENEMY_NAME_COUNT {len(names)}\n\n")
        f.write("static const char *enemy_name_table[ENEMY_NAME_COUNT] = {\n")
        for i, name in enumerate(names):
            # Escape quotes and backslashes
            safe = name.replace("\\", "\\\\").replace('"', '\\"')
            f.write(f'    "{safe}"')
            if i < len(names) - 1:
                f.write(",\n")
            else:
                f.write("\n")
        f.write("};\n")

    print(f"  enemy_names.h: {len(names)} names decoded")
    if names:
        print(f"    First few: {names[:5]}")

# ---------------------------------------------------------------------------
# 11. Level-Up Data
# ---------------------------------------------------------------------------

def extract_level_up_data():
    data = load_bin("0B_9094_levelupdata.bin")
    if not data:
        print("  SKIP level_up_data.h: missing bin/0B_9094_levelupdata.bin")
        return

    # Per bank_0B.asm: 49 levels × 6 classes × 2 bytes = 588 bytes
    entry_size = 2
    levels_per_class = 49
    num_classes = 6
    total_entries = levels_per_class * num_classes
    count = min(len(data) // entry_size, total_entries)

    with open(os.path.join(OUT_DIR, "level_up_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Level-Up Data",
                     "bin/0B_9094_levelupdata.bin (bank $0B, ROM $9094)",
                     "2 bytes per level, 49 levels per class, 6 classes. See bank_0B.asm for bit layout.")

        f.write(f"#define LEVEL_UP_CLASSES {num_classes}\n")
        f.write(f"#define LEVEL_UP_LEVELS {levels_per_class}\n\n")
        f.write("// Per-class, per-level stat gains.\n")
        f.write("// Byte 0: bit5=strong HP, bit4=guaranteed Str, bit3=Agil, bit2=Int, bit1=Vit, bit0=Luck\n")
        f.write("// Byte 1: MP up - each bit corresponds to a level of spell charge gained\n\n")
        f.write(f"static const uint8_t level_up_data[LEVEL_UP_CLASSES][LEVEL_UP_LEVELS][2] = {{\n")

        class_names = ["Fighter", "Thief", "BlackBelt", "RedMage", "WhiteMage", "BlackMage"]
        for c in range(num_classes):
            f.write(f"    /* {class_names[c]} */ {{\n")
            for lv in range(levels_per_class):
                off = (c * levels_per_class + lv) * entry_size
                vals = list(data[off:off+entry_size]) if off + entry_size <= len(data) else [0, 0]
                f.write(f"        {{ 0x{vals[0]:02X}, 0x{vals[1]:02X} }}")
                if lv < levels_per_class - 1:
                    f.write(",\n")
                else:
                    f.write("\n")
            f.write("    }")
            if c < num_classes - 1:
                f.write(",\n")
            else:
                f.write("\n")
        f.write("};\n")

        # bank_0B lut_ExpToAdvance: total EXP needed to leave each level
        # (49 3-byte .FARADDR values; index = 0-based level).
        raw = asm_label_bytes("bank_0B.asm", "lut_ExpToAdvance", levels_per_class * 3)
        exp = [raw[k] | raw[k + 1] << 8 | raw[k + 2] << 16 for k in range(0, len(raw), 3)]
        assert len(exp) == levels_per_class, len(exp)
        f.write("\n/* bank_0B lut_ExpToAdvance: total EXP to reach the next level, by 0-based level. */\n")
        f.write(f"static const uint32_t lut_ExpToAdvance[{len(exp)}] = {{\n")
        for k in range(0, len(exp), 8):
            f.write("    " + ", ".join(str(v) for v in exp[k:k + 8]) + ",\n")
        f.write("};\n")

        # Per-class (12) hit rate and magic defense gains per level.
        for label in ("lut_LvlUpHitRateBonus", "lut_LvlUpMagDefBonus"):
            vals = asm_label_bytes("bank_0B.asm", label, 12)
            f.write(f"\n/* bank_0B {label}, by class. */\n")
            f.write(f"static const uint8_t {label}[12] = {{ " + ", ".join(str(v) for v in vals) + " };\n")

    print(f"  level_up_data.h: {count} entries ({len(data)} bytes)")

# ---------------------------------------------------------------------------
# 12. Shop Data
# ---------------------------------------------------------------------------

def extract_shop_data():
    """Shops (bank_0E / bank_0F / bank_00): inventories, shop types, screen
    layout tables, magic permissions, palettes and the stock shop text."""
    data = load_bin("0E_8300_shopdata.bin")
    strings = load_bin("0E_8000_shopstrings.bin")
    bank00 = load_bank(0x00)
    if not data or not strings or not bank00:
        print("  SKIP shop_data.h: missing shop bins or bank_00.dat")
        return

    # lut_ShopData: a pointer per shop id, up to the first inventory; each
    # inventory is read 5 bytes at a time (LoadShopInventory).  Inns and
    # clinics keep their price in the first 2 bytes.
    nshops = (struct.unpack_from("<H", data, 0)[0] - 0x8300) // 2
    ptrs = struct.unpack_from(f"<{nshops}H", data, 0)
    inv = [list(data[ptr - 0x8300: ptr - 0x8300 + 5]) for ptr in ptrs]

    # lut_ShopStrings: pointer table, then the strings.
    nstr = (struct.unpack_from("<H", strings, 0)[0] - 0x8000) // 2
    sptrs = list(struct.unpack_from(f"<{nstr}H", strings, 0))
    blob, offs = _collect_strings(strings, 0x8000, sptrs)

    def label_bytes(asm, label, n):
        """`n` bytes at `label:` (from the ROM)."""
        return asm_label_bytes(asm, label, n)

    tables = [
        ("lut_ShopTypes", "bank_0F.asm", nshops, "shop type per shop id (0 weapon .. 7 caravan)"),
        ("lut_ShopkeepAdditive", "bank_0E.asm", 8, "shopkeeper tile offset per shop type"),
        ("lut_ShopAttributes", "bank_0E.asm", 64, "the shop screen's attribute table"),
        ("lut_ShopkeepImage", "bank_0E.asm", 100, "10x10 shopkeeper image (0 = blank)"),
        ("lut_ShopBox_X", "bank_0E.asm", 5, "boxes: 0 dialogue, 1 title, 2 inventory, 3 command, 4 gold"),
        ("lut_ShopBox_Y", "bank_0E.asm", 5, ""),
        ("lut_ShopBox_Wd", "bank_0E.asm", 5, ""),
        ("lut_ShopBox_Ht", "bank_0E.asm", 5, ""),
        ("lut_ShopCurs_Cmd", "bank_0E.asm", 8, "cursor x,y in the command box"),
        ("lut_ShopCurs_List", "bank_0E.asm", 10, "cursor x,y in the inventory box"),
        ("lut_ShopPalettes", "bank_0F.asm", 8, "title and gold box palettes"),
    ]
    with open(os.path.join(OUT_DIR, "shop_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Shop Data",
                     "bin/0E_8300_shopdata.bin, bin/0E_8000_shopstrings.bin, bank_0E/0F/00 tables",
                     "Shop inventories (5 bytes, $00-terminated; inn/clinic: 2-byte price),\n"
                     " * shop types, screen layout, magic permissions, palettes, stock text.")
        f.write(f"#define SHOP_COUNT {nshops}\n\n")
        f.write("/* lut_ShopData: 5 inventory bytes per shop id (LoadShopInventory). */\n")
        f.write(f"static const uint8_t shop_inventory[SHOP_COUNT][5] = {{\n")
        for k, row in enumerate(inv):
            f.write(f"    /* {k:2d} */ {{ " + ", ".join(f"0x{b:02X}" for b in row) + " },\n")
        f.write("};\n\n")
        for label, asm, n, comment in tables:
            vals = label_bytes(asm, label, n)
            if comment:
                f.write(f"/* {asm} {label}: {comment} */\n")
            f.write(f"static const uint8_t {label}[{n}] = {{ " + ", ".join(f"0x{v:02X}" for v in vals) + " };\n\n")
        # lut_MagicPermisPtr's tables, one per class (bit set = can't learn).
        f.write("/* bank_0E magic permissions, per class and spell level: bit 7 = the\n"
                " * level's first spell; a set bit means the class can't learn it. */\n")
        f.write("static const uint8_t lut_MagicPermis[12][8] = {\n")
        for cls in ("FT", "TH", "BB", "RM", "WM", "BM", "KN", "NJ", "MA", "RW", "WW", "BW"):
            vals = label_bytes("bank_0E.asm", "@" + cls, 8)
            f.write(f"    {{ " + ", ".join(f"0x{v:02X}" for v in vals) + f" }},   /* {cls} */\n")
        f.write("};\n\n")
        # lut_BackdropPal entries $10-$17: the shopkeeper palette per shop type.
        off = 0x3200 + 0x40
        f.write("/* bank_00 lut_BackdropPal $40+: the shopkeeper's palette per shop type. */\n")
        f.write("static const uint8_t shopkeep_palettes[8][4] = {\n")
        for t in range(8):
            f.write("    { " + ", ".join(f"0x{b:02X}" for b in bank00[off + t * 4: off + t * 4 + 4]) + " },\n")
        f.write("};\n\n")
        _write_string_table(f, "shop_text", "SHOP_TEXT_COUNT", blob, offs,
                            "lut_ShopStrings (bank_0E $8000): titles 0-7, \" G\", dialogue")
    print(f"  shop_data.h: {nshops} shops, {nstr} strings")

def extract_treasure_data():
    """Extract treasure data from bank_00.dat at lut_Treasure = $B100 → offset $3100"""
    bank00 = load_bank(0x00)
    if not bank00:
        print("  SKIP treasure_data.h: missing bank_00.dat")
        return

    offset = rom_offset_to_file_pos(0xB100, 0x00)
    # Treasure data format: needs analysis. For now extract 256 bytes.
    data = bank00[offset:offset+256]

    with open(os.path.join(OUT_DIR, "treasure_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Treasure Data",
                     "bank_00.dat (bank $00, ROM $B100)",
                     "Treasure chest contents. See bank_0E.asm for exact format.")
        f.write(f"#define TREASURE_DATA_SIZE {len(data)}\n")
        f.write("static const uint8_t treasure_data[] = {\n")
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            f.write("    " + ", ".join(f"0x{b:02X}" for b in chunk))
            if i + 16 < len(data):
                f.write(",\n")
            else:
                f.write("\n")
        f.write("};\n")

    print(f"  treasure_data.h: {len(data)} bytes")

# ---------------------------------------------------------------------------
# 14. Item Names (DTE-decoded from bank_0A.dat)
# ---------------------------------------------------------------------------

def load_item_prices():
    """lut_ItemPrices (bank_0D $BC00): 256 little-endian 16-bit prices, one per
    item-name id.  Bank $0D only exists as source, so read the .INCBIN that
    covers $BB8E-$BDFF."""
    data = load_bin("0D_BB8E_unknown.bin")
    if not data:
        print("  WARNING: missing bin/0D_BB8E_unknown.bin, prices will be 0")
        return [0] * 256
    off = 0xBC00 - 0xBB8E
    return list(struct.unpack_from("<256H", data, off))

def extract_item_data():
    bank0A = load_bank(0x0A)
    if not bank0A:
        print("  SKIP item_data.h: missing bank_0A.dat")
        return

    dte_tbl = load_tbl(os.path.join(ROOT_DIR, "table_dte.tbl"))
    std_tbl = load_tbl(os.path.join(ROOT_DIR, "table_standard.tbl"))
    for k, v in std_tbl.items():
        if k not in dte_tbl:
            dte_tbl[k] = v

    # Item name pointer table at $B700 → offset $3700
    ptr_table_offset = rom_offset_to_file_pos(0xB700, 0x0A)
    # The pointer table has 256 entries.  Entries 0..111 are items (0..27 =
    # key/consumable items, 28..67 = weapons, 68..107 = armor, 108..111 = the
    # "10/20/25/30" gold-amount strings).  Spell names live at $B0-$EF and
    # class names at $F0-$FB; those are extracted separately.
    num_items = 112

    # The pointer table: 2-byte pointers (little-endian, relative to bank start)
    item_names = []
    for i in range(num_items):
        ptr_off = ptr_table_offset + i * 2
        if ptr_off + 2 <= len(bank0A):
            ptr = struct.unpack_from("<H", bank0A, ptr_off)[0]
            # Pointer is relative to bank start ($8000)
            str_off = ptr - 0x8000
            if 0 <= str_off < len(bank0A):
                name, _ = decode_dte(bank0A, str_off, dte_tbl, 64)
                item_names.append(name)
            else:
                item_names.append("")
        else:
            item_names.append("")

    item_prices = load_item_prices()[:num_items]

    with open(os.path.join(OUT_DIR, "item_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Item Data",
                     "bank_0A.dat (bank $0A, ROM $B700 for names) / bank_0D for prices",
                     "DTE-decoded item names and prices.")

        f.write(f"#define ITEM_COUNT {len(item_names)}\n\n")
        f.write("typedef struct {\n")
        f.write("    const char *name;\n")
        f.write("    uint16_t price;\n")
        f.write("} ItemData;\n\n")
        f.write("static const ItemData item_table[ITEM_COUNT] = {\n")
        for i, (name, price) in enumerate(zip(item_names, item_prices)):
            safe = name.replace("\\", "\\\\").replace('"', '\\"')
            f.write(f'    {{ "{safe}", 0x{price:04X} }}')
            if i < len(item_names) - 1:
                f.write(",\n")
            else:
                f.write("\n")
        f.write("};\n")

    print(f"  item_data.h: {len(item_names)} items")
    if item_names:
        print(f"    First few: {item_names[:5]}")

# ---------------------------------------------------------------------------
# 14b. Spell Names (bank_0A, item-name table IDs $B0-$EF)
# ---------------------------------------------------------------------------

def extract_spell_names():
    bank0A = load_bank(0x0A)
    if not bank0A:
        print("  SKIP spell_names.h: missing bank_0A.dat")
        return

    dte_tbl = load_tbl(os.path.join(ROOT_DIR, "table_dte.tbl"))
    std_tbl = load_tbl(os.path.join(ROOT_DIR, "table_standard.tbl"))
    for k, v in std_tbl.items():
        if k not in dte_tbl:
            dte_tbl[k] = v

    # Spell names live in the same 256-entry item-name pointer table as items,
    # at ids $B0-$EF.  A spell's id here equals its magic_table index (0..63),
    # so spell_names[i] is the name of spell i.
    ptr_table_offset = rom_offset_to_file_pos(0xB700, 0x0A)
    spell_names = []
    for i in range(64):
        ptr_off = ptr_table_offset + (0xB0 + i) * 2
        ptr = struct.unpack_from("<H", bank0A, ptr_off)[0]
        str_off = ptr - 0x8000
        if 0 <= str_off < len(bank0A):
            name, _ = decode_dte(bank0A, str_off, dte_tbl, 64)
            spell_names.append(name)
        else:
            spell_names.append("")

    with open(os.path.join(OUT_DIR, "spell_names.h"), "w", encoding="utf-8") as f:
        write_header(f, "Spell Names",
                     "bank_0A.dat (bank $0A, ROM $B700 item-name table IDs $B0-$EF)",
                     "DTE-decoded spell names, indexed by magic_table spell id 0..63.")
        f.write(f"#define SPELL_NAME_COUNT {len(spell_names)}\n\n")
        f.write("static const char *const spell_names[SPELL_NAME_COUNT] = {\n")
        for i, name in enumerate(spell_names):
            safe = name.replace("\\", "\\\\").replace('"', '\\"')
            f.write(f'    "{safe}"')
            if i < len(spell_names) - 1:
                f.write(",\n")
            else:
                f.write("\n")
        f.write("};\n")

    print(f"  spell_names.h: {len(spell_names)} spell names")

# ---------------------------------------------------------------------------
# 15. Dialogue Data (DTE-decoded from bank_0A.dat)
# ---------------------------------------------------------------------------

def extract_dialogue():
    bank0A = load_bank(0x0A)
    if not bank0A:
        print("  SKIP dialogue.h: missing bank_0A.dat")
        return

    dte_tbl = load_tbl(os.path.join(ROOT_DIR, "table_dte.tbl"))
    std_tbl = load_tbl(os.path.join(ROOT_DIR, "table_standard.tbl"))
    for k, v in std_tbl.items():
        if k not in dte_tbl:
            dte_tbl[k] = v

    # Dialogue pointer table at $8000 → offset $0000
    # Read pointers until we hit a non-sensical value or end of data
    ptr_table_offset = 0
    ptrs = []
    for i in range(0, 0x2000, 2):  # Max 4096 dialogue entries
        if ptr_table_offset + i + 2 > len(bank0A):
            break
        ptr = struct.unpack_from("<H", bank0A, ptr_table_offset + i)[0]
        if ptr == 0 or ptr < 0x8000 or ptr >= 0xC000:
            break
        ptrs.append(ptr)

    # Decode each dialogue string
    dialogues = []
    for ptr in ptrs:
        str_off = ptr - 0x8000
        if 0 <= str_off < len(bank0A):
            text, _ = decode_dte(bank0A, str_off, dte_tbl, 512)
            dialogues.append(text)
        else:
            dialogues.append("")

    with open(os.path.join(OUT_DIR, "dialogue.h"), "w", encoding="utf-8") as f:
        write_header(f, "Dialogue Data",
                     "bank_0A.dat (bank $0A, ROM $8000)",
                     "DTE-decoded dialogue strings.")

        f.write(f"#define DIALOGUE_COUNT {len(dialogues)}\n\n")
        f.write("static const char *dialogue_table[DIALOGUE_COUNT] = {\n")
        for i, dlg in enumerate(dialogues):
            safe = dlg.replace("\\", "\\\\").replace('"', '\\"')
            # Wrap long lines
            if len(safe) > 120:
                f.write(f'    /* {i:03d} */\n    "{safe}"')
            else:
                f.write(f'    "{safe}"')
            if i < len(dialogues) - 1:
                f.write(",\n")
            else:
                f.write("\n")
        f.write("};\n")

    print(f"  dialogue.h: {len(dialogues)} dialogue strings")

# ---------------------------------------------------------------------------
# 15b. Raw text tables (exact ROM bytes; the C text engine decodes them)
# ---------------------------------------------------------------------------

def _collect_strings(data, base, ptrs):
    """Copy each pointed-to string (up to and including its $00 terminator)
    into one blob.  Returns (blob, offsets).  Shared pointers share bytes."""
    blob = bytearray()
    seen = {}
    offsets = []
    for ptr in ptrs:
        if ptr in seen:
            offsets.append(seen[ptr])
            continue
        start = ptr - base
        if not (0 <= start < len(data)):
            raise ValueError(f"string pointer ${ptr:04X} out of range")
        end = data.index(0, start)
        seen[ptr] = len(blob)
        offsets.append(len(blob))
        blob += data[start:end + 1]
    return bytes(blob), offsets

def _write_string_table(f, name, count_macro, blob, offsets, comment):
    f.write(f"/* {comment} */\n")
    f.write(f"#define {count_macro} {len(offsets)}\n\n")
    f.write(f"static const uint16_t {name}_offsets[{count_macro}] = {{\n")
    for i in range(0, len(offsets), 12):
        f.write("    " + ", ".join(f"{o:5d}" for o in offsets[i:i + 12]) + ",\n")
    f.write("};\n\n")
    f.write(f"static const uint8_t {name}_bytes[{len(blob)}] = {{\n")
    for i in range(0, len(blob), 16):
        f.write("    " + ",".join(f"0x{b:02X}" for b in blob[i:i + 16]) + ",\n")
    f.write("};\n\n")

def extract_text_data():
    bank0A = load_bank(0x0A)
    menu = load_bin("0E_8500_menutext.bin")
    if not bank0A or not menu:
        print("  SKIP text_data.h: missing bank_0A.dat or bin/0E_8500_menutext.bin")
        return

    # lut_DialoguePtrTbl (bank_0A $8000): one pointer per dialogue id $00-$FF.
    dlg_ptrs = list(struct.unpack_from("<256H", bank0A, 0))
    dlg_blob, dlg_offs = _collect_strings(bank0A, 0x8000, dlg_ptrs)

    # lut_ItemNamePtrTbl (bank_0A $B700): items, weapons, armor, gold amounts,
    # spells ($B0), class names ($F0) and ailment blurbs ($FC).
    item_ptrs = list(struct.unpack_from("<256H", bank0A, 0xB700 - 0x8000))
    item_blob, item_offs = _collect_strings(bank0A, 0x8000, item_ptrs)

    # lut_EnemyAttack (bank_0A $B600): names of the enemy attacks ($42-$5B).
    atk_ptrs = list(struct.unpack_from("<26H", bank0A, 0xB600 - 0x8000))
    atk_blob, atk_offs = _collect_strings(bank0A, 0x8000, atk_ptrs)

    # Battle messages (bank_0B $8C40): text, then the pointer table at +$310
    # (1-based ids).  The last pointer (message $4E) points just past
    # the .bin to bytes bank_0B.asm has as .BYTEs, so append those.
    bmsg = bytearray(load_bin("0B_8C40_battlemessages.bin")) + nothing_happens_bytes()
    nmsg = (0x3AC - 0x310) // 2
    msg_ptrs = list(struct.unpack_from(f"<{nmsg}H", bmsg, 0x310))
    msg_blob, msg_offs = _collect_strings(bytes(bmsg), 0x8C40, msg_ptrs)

    # Menu text (bank_0E $8500): the pointer table runs up to the first string.
    first = struct.unpack_from("<H", menu, 0)[0]
    count = (first - 0x8500) // 2
    menu_ptrs = list(struct.unpack_from(f"<{count}H", menu, 0))
    # Kept as the ROM has them, not string by string: DrawCharMenuString
    # copies a fixed length past the terminator, and a name code ($10 $00)
    # has a $00 argument that isn't the end of the string.
    menu_blob, menu_offs = bytes(menu), [ptr - 0x8500 for ptr in menu_ptrs]

    with open(os.path.join(OUT_DIR, "text_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Raw Text Tables",
                     "bank_0A.dat ($8000 dialogue, $B700 item names) / bin/0E_8500_menutext.bin",
                     "Exact ROM string bytes (DTE + control codes), $00-terminated.\n"
                     " * String i starts at <table>_bytes + <table>_offsets[i].\n"
                     " * Decoded at runtime by text.c (port of DrawComplexString).")
        _write_string_table(f, "dialogue", "DIALOGUE_STRING_COUNT", dlg_blob, dlg_offs,
                            "lut_DialoguePtrTbl (bank_0A $8000)")
        _write_string_table(f, "item_name", "ITEM_NAME_COUNT", item_blob, item_offs,
                            "lut_ItemNamePtrTbl (bank_0A $B700)")
        _write_string_table(f, "menu_text", "MENU_STRING_COUNT", menu_blob, menu_offs,
                            "Menu text (bank_0E $8500)")
        _write_string_table(f, "enemy_attack", "ENEMY_ATTACK_NAME_COUNT", atk_blob, atk_offs,
                            "lut_EnemyAttack (bank_0A $B600), attack id - $42")
        _write_string_table(f, "battle_msg", "BATTLE_MSG_COUNT", msg_blob, msg_offs,
                            "data_BattleMessages (bank_0B $8C40 + $310), BTLMSG id - 1")

        # lut_DTE1/lut_DTE2 (bank_0F).  DrawComplexString treats every byte
        # below $7A as DTE (the intended limit was $6A), so bytes $6A-$79 read
        # 16 entries past each 80-entry table.  lut_DTE2 is followed by
        # lut_DTE1 in ROM, and lut_DTE1 by 16 stray bytes; both are included
        # so the C port reproduces that quirk exactly.
        dte1 = asm_label_bytes("bank_0F.asm", "lut_DTE1", 96)
        dte2 = asm_label_bytes("bank_0F.asm", "lut_DTE2", 80) + dte1[:16]
        for name, tbl in (("lut_DTE1", dte1), ("lut_DTE2", dte2)):
            f.write(f"/* {name} (bank_0F), indexed by byte - $1A for bytes $1A-$79. */\n")
            f.write(f"static const uint8_t {name}[96] = {{\n")
            for i in range(0, 96, 16):
                f.write("    " + ",".join(f"0x{b:02X}" for b in tbl[i:i + 16]) + ",\n")
            f.write("};\n\n")

        prices = load_item_prices()
        f.write("/* lut_ItemPrices (bank_0D $BC00), indexed like item names.  For the\n")
        f.write(" * gold-chest ids ($6C-$AF) the \"price\" is the gold amount. */\n")
        f.write("static const uint16_t item_prices[ITEM_NAME_COUNT] = {\n")
        for i in range(0, 256, 12):
            f.write("    " + ", ".join(f"{v:5d}" for v in prices[i:i + 12]) + ",\n")
        f.write("};\n")

    print(f"  text_data.h: {len(dlg_offs)} dialogue ({len(dlg_blob)} B), "
          f"{len(item_offs)} item names ({len(item_blob)} B), "
          f"{len(menu_offs)} menu strings ({len(menu_blob)} B)")

# ---------------------------------------------------------------------------
# 16. Story / Intro / Credits Text
# ---------------------------------------------------------------------------

def extract_text_bin(bin_name, out_name, title, desc):
    data = load_bin(bin_name)
    if not data:
        print(f"  SKIP {out_name}: missing {bin_name}")
        return

    dte_tbl = load_tbl(os.path.join(ROOT_DIR, "table_dte.tbl"))
    std_tbl = load_tbl(os.path.join(ROOT_DIR, "table_standard.tbl"))
    for k, v in std_tbl.items():
        if k not in dte_tbl:
            dte_tbl[k] = v

    text, _ = decode_dte(data, 0, dte_tbl, len(data) * 4)

    with open(os.path.join(OUT_DIR, out_name), "w", encoding="utf-8") as f:
        write_header(f, title, f"bin/{bin_name}", desc)
        # Store as raw C string with \n for newlines
        safe = text.replace("\\", "\\\\").replace('"', '\\"')
        f.write(f'static const char {out_name.replace(".h", "")}_text[] = "{safe}";\n')

    print(f"  {out_name}: {len(data)} bytes decoded to {len(text)} chars")

# ---------------------------------------------------------------------------
# 17. Menu Text
# ---------------------------------------------------------------------------

def extract_menu_text():
    data = load_bin("0E_8500_menutext.bin")
    if not data:
        print("  SKIP menu_text.h: missing bin/0E_8500_menutext.bin")
        return

    dte_tbl = load_tbl(os.path.join(ROOT_DIR, "table_dte.tbl"))
    std_tbl = load_tbl(os.path.join(ROOT_DIR, "table_standard.tbl"))
    for k, v in std_tbl.items():
        if k not in dte_tbl:
            dte_tbl[k] = v

    # Decode individual strings (null-terminated)
    strings = []
    pos = 0
    while pos < len(data):
        s, pos = decode_dte(data, pos, dte_tbl, 128)
        if s:
            strings.append(s)

    with open(os.path.join(OUT_DIR, "menu_text.h"), "w", encoding="utf-8") as f:
        write_header(f, "Menu Text",
                     "bin/0E_8500_menutext.bin (bank $0E, ROM $8500)",
                     "DTE-decoded menu label strings.")

        f.write(f"#define MENU_TEXT_COUNT {len(strings)}\n\n")
        f.write("static const char *menu_text_table[MENU_TEXT_COUNT] = {\n")
        for i, s in enumerate(strings):
            safe = s.replace("\\", "\\\\").replace('"', '\\"')
            f.write(f'    "{safe}"')
            if i < len(strings) - 1:
                f.write(",\n")
            else:
                f.write("\n")
        f.write("};\n")

    print(f"  menu_text.h: {len(strings)} strings decoded")

# ---------------------------------------------------------------------------
# 18. Class Starting Stats
# ---------------------------------------------------------------------------

def extract_class_starting_stats():
    """lut_ClassStartingStats (bank_00 $B040): $10 bytes per base class (only
    the 6 starting classes have entries), $B used.  See NewGame_LoadStartingStats."""
    bank00 = load_bank(0x00)
    if not bank00:
        print("  SKIP class_starting_stats.h: missing bank_00.dat")
        return
    off = 0xB040 - 0x8000
    names = ["Fighter", "Thief", "Black Belt", "Red Mage", "White Mage", "Black Mage"]
    with open(os.path.join(OUT_DIR, "class_starting_stats.h"), "w", encoding="utf-8") as f:
        write_header(f, "Class Starting Stats",
                     "bank_00.dat (lut_ClassStartingStats, ROM $B040)",
                     "New-game stats of the 6 starting classes (NewGame_LoadStartingStats).")
        f.write("#define CLASS_STARTING_STATS_COUNT 6\n\n")
        f.write("typedef struct {\n")
        f.write("    uint8_t class_id;   // +0: unused copy of the class id\n")
        f.write("    uint8_t hp;         // +1: starting (and max) HP\n")
        f.write("    uint8_t str, agil, intel, vit, luck;   // +2..+6\n")
        f.write("    uint8_t damage;     // +7: ch_dmg\n")
        f.write("    uint8_t hit_rate;   // +8: ch_hitrate\n")
        f.write("    uint8_t evade;      // +9: ch_evade\n")
        f.write("    uint8_t mag_def;    // +A: ch_magdef\n")
        f.write("} ClassStartingStats;\n\n")
        f.write("static const ClassStartingStats class_starting_stats[CLASS_STARTING_STATS_COUNT] = {\n")
        for c in range(6):
            v = bank00[off + c * 16: off + c * 16 + 11]
            f.write("    { " + ", ".join(str(x) for x in v) + f" }},   /* {names[c]} */\n")
        f.write("};\n")
    print("  class_starting_stats.h: 6 classes")

def extract_game_flags_init():
    bank00 = load_bank(0x00)
    if not bank00:
        print("  SKIP game_flags_init.h: missing bank_00.dat")
        return

    flags_offset = rom_offset_to_file_pos(0xAF00, 0x00)
    unsram_offset = rom_offset_to_file_pos(0xB000, 0x00)

    flags_data = bank00[flags_offset:flags_offset+256]
    unsram_data = bank00[unsram_offset:unsram_offset+256]

    with open(os.path.join(OUT_DIR, "game_flags_init.h"), "w", encoding="utf-8") as f:
        write_header(f, "Game Flags Initial State",
                     "bank_00.dat (bank $00, ROM $AF00 / $B000)",
                     "Initial values for game flags and first page of unsram (save RAM).")

        f.write(f"#define GAME_FLAGS_SIZE {len(flags_data)}\n")
        f.write("static const uint8_t init_game_flags[] = {\n")
        f.write("    " + ", ".join(f"0x{b:02X}" for b in flags_data) + "\n")
        f.write("};\n\n")
        f.write(f"#define UNSRAM_FIRST_PAGE_SIZE {len(unsram_data)}\n")
        f.write("static const uint8_t init_unsram_first_page[] = {\n")
        f.write("    " + ", ".join(f"0x{b:02X}" for b in unsram_data) + "\n")
        f.write("};\n")

    print(f"  game_flags_init.h: {len(flags_data)} + {len(unsram_data)} bytes")

# ---------------------------------------------------------------------------
# 20. RNG Table
# ---------------------------------------------------------------------------

def extract_rng_table():
    data = load_bin("0F_F100_rngtable.bin")
    battle = load_bin("0F_FCF1_rngtable.bin")
    if not data or not battle:
        print("  SKIP rng_table.h: missing bin/0F_F100_rngtable.bin or bin/0F_FCF1_rngtable.bin")
        return
    weight = asm_label_bytes("bank_0F.asm", "lut_FormationWeight", 64)

    def table(f, name, values, per_line=16):
        f.write(f"static const uint8_t {name}[{len(values)}] = {{\n")
        for i in range(0, len(values), per_line):
            f.write("    " + ", ".join(f"0x{b:02X}" for b in values[i:i + per_line]) + ",\n")
        f.write("};\n\n")

    with open(os.path.join(OUT_DIR, "rng_table.h"), "w", encoding="utf-8") as f:
        write_header(f, "RNG Tables",
                     "bin/0F_F100_rngtable.bin, bin/0F_FCF1_rngtable.bin, bank_0F.asm",
                     "Scramble tables used by FF1's random number generators.")
        f.write("#define RNG_TABLE_SIZE 256\n\n")
        f.write("/* lut_RNG ($F100): BattleStepRNG and GetBattleFormation. */\n")
        table(f, "rng_table", list(data[:256]))
        f.write("/* BattleRNG's own scramble table ($FCF1): all in-battle randomness. */\n")
        table(f, "battle_rng_table", list(battle[:256]))
        f.write("/* lut_FormationWeight ($C58C): (random & $3F) -> formation slot 0-7\n")
        f.write(" * within a domain (12,12,12,12,6,6,3,1 out of 64). */\n")
        table(f, "lut_FormationWeight", weight)

    print("  rng_table.h: lut_RNG, BattleRNG table, lut_FormationWeight")

# ---------------------------------------------------------------------------
# 21. Map Data (partial - flags what needs analysis)
# ---------------------------------------------------------------------------

def extract_map_data():
    """Extract partial map-related data. Many formats need deeper analysis."""
    bank00 = load_bank(0x00)
    bank01 = load_bank(0x01)  # .asm file, not .dat - may not load
    bank04 = load_bank(0x04)

    with open(os.path.join(OUT_DIR, "map_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Map Data",
                     "bank_00.dat, bank_01.asm, bank_04.dat",
                     "Overworld and standard map tile data. NEEDS FURTHER ANALYSIS for full format.")

        # OW tileset from bank_00: lut_OWTileset = $8000
        if bank00:
            ow_data = bank00[0:0x400]
            f.write("// Overworld tileset properties (lut_OWTileset = $8000)\n")
            f.write(f"#define OW_TILESET_SIZE {len(ow_data)}\n")
            f.write("static const uint8_t ow_tileset[] = {\n")
            for i in range(0, len(ow_data), 16):
                chunk = ow_data[i:i+16]
                f.write("    " + ", ".join(f"0x{b:02X}" for b in chunk))
                if i + 16 < len(ow_data):
                    f.write(",\n")
                else:
                    f.write("\n")
            f.write("};\n\n")

        # SM tileset attribs: lut_SMTilesetAttr = $8400
        if bank00:
            sm_attr = bank00[0x400:0x800]
            f.write("// Standard map tileset attributes (lut_SMTilesetAttr = $8400)\n")
            f.write(f"#define SM_TILESET_ATTR_SIZE {len(sm_attr)}\n")
            f.write("static const uint8_t sm_tileset_attr[] = {\n")
            for i in range(0, len(sm_attr), 16):
                chunk = sm_attr[i:i+16]
                f.write("    " + ", ".join(f"0x{b:02X}" for b in chunk))
                if i + 16 < len(sm_attr):
                    f.write(",\n")
                else:
                    f.write("\n")
            f.write("};\n\n")

        # SM tileset props: lut_SMTilesetProp = $8800
        if bank00:
            sm_prop = bank00[0x800:0x1000]
            f.write("// Standard map tileset properties (lut_SMTilesetProp = $8800)\n")
            f.write(f"#define SM_TILESET_PROP_SIZE {len(sm_prop)}\n")
            f.write("static const uint8_t sm_tileset_prop[] = {\n")
            for i in range(0, len(sm_prop), 16):
                chunk = sm_prop[i:i+16]
                f.write("    " + ", ".join(f"0x{b:02X}" for b in chunk))
                if i + 16 < len(sm_prop):
                    f.write(",\n")
                else:
                    f.write("\n")
            f.write("};\n\n")

        # SM tileset TSA: lut_SMTilesetTSA = $9000
        if bank00:
            sm_tsa = bank00[0x1000:0x2000]
            f.write("// Standard map tileset TSA (lut_SMTilesetTSA = $9000)\n")
            f.write(f"#define SM_TILESET_TSA_SIZE {len(sm_tsa)}\n")
            f.write("static const uint8_t sm_tileset_tsa[] = {\n")
            for i in range(0, len(sm_tsa), 16):
                chunk = sm_tsa[i:i+16]
                f.write("    " + ", ".join(f"0x{b:02X}" for b in chunk))
                if i + 16 < len(sm_tsa):
                    f.write(",\n")
                else:
                    f.write("\n")
            f.write("};\n\n")

        f.write("// NOTE: Full map decoding (overworld grid, standard maps) needs further analysis\n")
        f.write("// of the assembly routines in bank_01.asm and bank_04.\n")

    print("  map_data.h: partial data extracted (needs further analysis)")

# ---------------------------------------------------------------------------
# 22. Teleport Data
# ---------------------------------------------------------------------------

def extract_teleport_data():
    bank00 = load_bank(0x00)
    if not bank00:
        print("  SKIP teleport_data.h: missing bank_00.dat")
        return

    with open(os.path.join(OUT_DIR, "teleport_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Teleport Data",
                     "bank_00.dat (bank $00, ROM $AC00-$AD80)",
                     "Exit/entrance/normal teleport coordinates. See Constants.inc for addresses.")

        # Exit teleports: $AC60 (X) and $AC70 (Y) - 16 bytes each
        # Entrance teleports: $AC00 (X), $AC20 (Y), $AC40 (Map)
        # Normal teleports: $AD00 (X), $AD40 (Y), $AD80 (Map)
        # Tileset assignments: $ACC0

        def dump_table(rom_addr, size, label):
            off = rom_offset_to_file_pos(rom_addr, 0x00)
            vals = list(bank00[off:off+size])
            f.write(f"// {label}\n")
            f.write(f"static const uint8_t {label.lower().replace(' ','_')}[] = {{ ")
            f.write(", ".join(f"0x{v:02X}" for v in vals))
            f.write(" };\n\n")

        dump_table(0xAC00, 32, "EntrTele_X")      # 32 bytes for 32 entrance teleports? 
        dump_table(0xAC20, 32, "EntrTele_Y")
        dump_table(0xAC40, 32, "EntrTele_Map")
        dump_table(0xAC60, 16, "ExitTele_X")
        dump_table(0xAC70, 16, "ExitTele_Y")
        dump_table(0xAD00, 64, "NormTele_X")
        dump_table(0xAD40, 64, "NormTele_Y")
        dump_table(0xAD80, 64, "NormTele_Map")
        dump_table(0xACC0, 64, "Tilesets")

    print("  teleport_data.h: extracted")

# ---------------------------------------------------------------------------
# 23. Score Data (Mini-game)
# ---------------------------------------------------------------------------

def extract_score_data():
    data = load_bin("0D_8000_scoredata.bin")
    if not data:
        print("  SKIP score_data.h (optional): missing bin/0D_8000_scoredata.bin")
        return

    with open(os.path.join(OUT_DIR, "score_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Score Data (Mini-game)",
                     "bin/0D_8000_scoredata.bin (bank $0D, ROM $8000)",
                     "Mini-game scoring data.")

        f.write(f"#define SCORE_DATA_SIZE {len(data)}\n")
        f.write("static const uint8_t score_data[] = {\n")
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            f.write("    " + ", ".join(f"0x{b:02X}" for b in chunk))
            if i + 16 < len(data):
                f.write(",\n")
            else:
                f.write("\n")
        f.write("};\n")

    print(f"  score_data.h: {len(data)} bytes")

# ---------------------------------------------------------------------------
# 24. Types / Save Structure
# ---------------------------------------------------------------------------

TYPES_H_BODY = """\
#include <stddef.h>

/*
 * The structs below mirror the NES save RAM byte for byte (unsram $6000-$63FF,
 * copied to battery SRAM $6400-$67FF on save), so a SaveData can be read from
 * or written to an emulator .sav file directly.  Offsets are checked at
 * compile time against variables.inc.  Multi-byte fields are little-endian,
 * as on the NES (the host must be little-endian too: x86, ARM).
 */

#define CH_STATS_SIZE 0x40
#define MAX_PARTY_SIZE 4

// ch_stats (unsram + $100): $40 bytes per character.
typedef struct {
    uint8_t  class;          // +$00 ch_class
    uint8_t  ailments;       // +$01 ch_ailments (out-of-battle ailment id)
    uint8_t  name[4];        // +$02 ch_name: 4 font tiles (not NUL-terminated)
    uint8_t  _pad_06;
    uint8_t  exp[3];         // +$07 ch_exp
    uint16_t cur_hp;         // +$0A ch_curhp
    uint16_t max_hp;         // +$0C ch_maxhp
    uint8_t  _pad_0E[2];
    uint8_t  str;            // +$10 ch_str
    uint8_t  agil;           // +$11 ch_agil
    uint8_t  intel;          // +$12 ch_int
    uint8_t  vit;            // +$13 ch_vit
    uint8_t  luck;           // +$14 ch_luck
    uint8_t  _pad_15;
    uint16_t exp_to_next;    // +$16 ch_exptonext (display only)
    uint8_t  weapons[4];     // +$18 ch_weapons (0 = none, bit 7 = equipped)
    uint8_t  armor[4];       // +$1C ch_armor   (0 = none, bit 7 = equipped)
    uint8_t  damage;         // +$20 ch_dmg
    uint8_t  hit_rate;       // +$21 ch_hitrate
    uint8_t  absorb;         // +$22 ch_absorb
    uint8_t  evade;          // +$23 ch_evade
    uint8_t  elem_resist;    // +$24 ch_resist
    uint8_t  mag_def;        // +$25 ch_magdef
    uint8_t  level;          // +$26 ch_level (0-based out of battle)
    uint8_t  _pad_27[0x19];
} CharacterStats;

// ch_magicdata (unsram + $300): $40 bytes per character.
#define SPELL_SLOTS 0x20
#define MP_LEVELS 8

typedef struct {
    uint8_t spells[SPELL_SLOTS];  // +$00 ch_spells: 8 levels x 4 slots (4th unused)
    uint8_t cur_mp[MP_LEVELS];    // +$20 ch_curmp
    uint8_t max_mp[MP_LEVELS];    // +$28 ch_maxmp
    uint8_t _pad_30[0x10];
} CharacterMagic;

#define ITEMS_SIZE 0x30          // items[] region (unsram + $20)
#define GAME_FLAGS_SIZE 0x100

// The full $400-byte save block.
typedef struct {
    // ---- page 0: overworld state, gold, items ----
    uint8_t  ship_vis;           // +$00
    uint8_t  ship_x;             // +$01
    uint8_t  ship_y;             // +$02
    uint8_t  _pad_03;
    uint8_t  airship_vis;        // +$04
    uint8_t  airship_x;          // +$05
    uint8_t  airship_y;          // +$06
    uint8_t  _pad_07;
    uint8_t  bridge_vis;         // +$08
    uint8_t  bridge_x;           // +$09
    uint8_t  bridge_y;           // +$0A
    uint8_t  _pad_0B;
    uint8_t  canal_vis;          // +$0C
    uint8_t  canal_x;            // +$0D
    uint8_t  canal_y;            // +$0E
    uint8_t  _pad_0F;
    uint8_t  ow_scroll_x;        // +$10 unsram_ow_scroll_x
    uint8_t  ow_scroll_y;        // +$11 unsram_ow_scroll_y
    uint8_t  has_canoe;          // +$12
    uint8_t  _pad_13;
    uint8_t  vehicle;            // +$14 unsram_vehicle
    uint8_t  _pad_15;
    uint8_t  bridgescene;        // +$16
    uint8_t  _pad_17[5];
    uint8_t  gold[3];            // +$1C (little-endian)
    uint8_t  _pad_1F;
    uint8_t  items[ITEMS_SIZE];  // +$20 key items, orbs, consumable counts
    uint8_t  _pad_50[0xAD];
    uint8_t  checksum;           // +$FD sram_checksum
    uint8_t  assert_55;          // +$FE sram_assert_55
    uint8_t  assert_AA;          // +$FF sram_assert_AA
    // ---- pages 1-3 ----
    CharacterStats characters[MAX_PARTY_SIZE];       // +$100 ch_stats
    uint8_t  game_flags[GAME_FLAGS_SIZE];            // +$200 game_flags
    CharacterMagic character_magic[MAX_PARTY_SIZE];  // +$300 ch_magicdata
} SaveData;

#define TYPES_CHECK(t, f, off) _Static_assert(offsetof(t, f) == (off), #t "." #f " offset")
TYPES_CHECK(CharacterStats, exp,         0x07);
TYPES_CHECK(CharacterStats, cur_hp,      0x0A);
TYPES_CHECK(CharacterStats, max_hp,      0x0C);
TYPES_CHECK(CharacterStats, str,         0x10);
TYPES_CHECK(CharacterStats, exp_to_next, 0x16);
TYPES_CHECK(CharacterStats, weapons,     0x18);
TYPES_CHECK(CharacterStats, armor,       0x1C);
TYPES_CHECK(CharacterStats, damage,      0x20);
TYPES_CHECK(CharacterStats, level,       0x26);
TYPES_CHECK(CharacterMagic, cur_mp,      0x20);
TYPES_CHECK(CharacterMagic, max_mp,      0x28);
TYPES_CHECK(SaveData, gold,              0x1C);
TYPES_CHECK(SaveData, items,             0x20);
TYPES_CHECK(SaveData, checksum,          0xFD);
TYPES_CHECK(SaveData, characters,        0x100);
TYPES_CHECK(SaveData, game_flags,        0x200);
TYPES_CHECK(SaveData, character_magic,   0x300);
_Static_assert(sizeof(CharacterStats) == 0x40, "CharacterStats size");
_Static_assert(sizeof(CharacterMagic) == 0x40, "CharacterMagic size");
_Static_assert(sizeof(SaveData) == 0x400, "SaveData size");
#undef TYPES_CHECK
"""

def extract_types():
    """Generate types.h: the NES save RAM layout from variables.inc."""
    with open(os.path.join(OUT_DIR, "types.h"), "w", encoding="utf-8") as f:
        write_header(f, "Types and Save Structure",
                     "variables.inc",
                     "C structs matching the FF1 save RAM layout byte for byte.")
        f.write(TYPES_H_BODY)

    print("  types.h: generated from variables.inc")

# ---------------------------------------------------------------------------
# 25. Battle Messages (additional)
# ---------------------------------------------------------------------------

def extract_battle_messages():
    """Extract battle message strings from bin/0B_8C40_battlemessages.bin"""
    data = load_bin("0B_8C40_battlemessages.bin")
    if not data:
        print("  SKIP battle_messages.h: missing bin/0B_8C40_battlemessages.bin")
        return

    # Per bank_0B.asm: text at offset 0, pointer table at offset $310
    dte_tbl = load_tbl(os.path.join(ROOT_DIR, "table_dte.tbl"))
    std_tbl = load_tbl(os.path.join(ROOT_DIR, "table_standard.tbl"))
    for k, v in std_tbl.items():
        if k not in dte_tbl:
            dte_tbl[k] = v

    # data_BattleMessages is a pointer table at +$310; message IDs (BTLMSG_xxx)
    # are 1-based, so ID n is entry n-1.  The last entry points past the text
    # (into the table itself) and isn't a message.
    nptrs = (len(data) - 0x310) // 2
    strings = []
    for i in range(nptrs):
        ptr = struct.unpack_from("<H", data, 0x310 + 2 * i)[0] - 0x8C40
        if ptr >= 0x310:
            break
        s, _ = decode_dte(data, ptr, dte_tbl, 128)
        strings.append(s)
    # That last entry points at $8FEC, just past the .bin, where bank_0B has
    # message $4E (BTLMSG_NOTHINGHAPPENS) as .BYTEs.
    s, _ = decode_dte(nothing_happens_bytes(), 0, dte_tbl, 128)
    strings.append(s)

    with open(os.path.join(OUT_DIR, "battle_messages.h"), "w", encoding="utf-8") as f:
        write_header(f, "Battle Messages",
                     "bin/0B_8C40_battlemessages.bin (bank $0B, ROM $8C40)",
                     "DTE-decoded battle message strings.")

        f.write("/* Indexed by BTLMSG id - 1. */\n")
        f.write(f"#define BATTLE_MESSAGE_COUNT {len(strings)}\n\n")
        f.write("static const char *const battle_message_table[BATTLE_MESSAGE_COUNT] = {\n")
        for i, s in enumerate(strings):
            safe = s.replace("\\", "\\\\").replace('"', '\\"')
            f.write(f'    "{safe}"')
            if i < len(strings) - 1:
                f.write(",\n")
            else:
                f.write("\n")
        f.write("};\n\n")

        # Message ids (Constants.inc BTLMSG_xxx).
        with open(os.path.join(ROOT_DIR, "Constants.inc"), encoding="utf-8", errors="replace") as cf:
            for line in cf:
                m = re.match(r"\s*(BTLMSG_\w+)\s*=\s*\$([0-9A-Fa-f]+)", line)
                if m:
                    f.write(f"#define {m.group(1)} 0x{int(m.group(2), 16):02X}\n")

        # bank_0C lut_MagicBattleMessages: the message a connected spell prints
        # (BtlMag_PrintMagicMessage), per attack id; 0 = none.
        lut = asm_label_bytes("bank_0C.asm", "lut_MagicBattleMessages", 64 + 2 + 26)
        f.write("\n/* bank_0C lut_MagicBattleMessages: BTLMSG id per attack id (0 = none). */\n")
        f.write(f"static const uint8_t lut_MagicBattleMessages[{len(lut)}] = {{\n")
        for i in range(0, len(lut), 16):
            f.write("    " + ", ".join(f"0x{b:02X}" for b in lut[i:i + 16]) + ",\n")
        f.write("};\n")

    print(f"  battle_messages.h: {len(strings)} messages decoded")

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def extract_fiend_tsa():
    """Fiend and Chaos battle graphics layouts (bank_0B data_FiendTSA / data_ChaosTSA)."""
    fiend = load_bin("0B_92E0_fiendtsa.bin")
    chaos = load_bin("0B_9420_chaostsa.bin")
    if not fiend or not chaos:
        print("  SKIP fiend_tsa.h: missing bin/0B_92E0_fiendtsa.bin or bin/0B_9420_chaostsa.bin")
        return
    with open(os.path.join(OUT_DIR, "fiend_tsa.h"), "w", encoding="utf-8") as f:
        write_header(f, "Fiend / Chaos TSA",
                     "bin/0B_92E0_fiendtsa.bin, bin/0B_9420_chaostsa.bin (bank $0B)",
                     "Tile maps (tile ids in the enemy CHR page, 0 = blank) and the $10 attribute bytes\n"
                     " * that PrepareEnemyFormation_Fiend/_Chaos copy to attribute rows 1-4, columns 0-3.")
        f.write("/* 4 fiends (by formation graphic assignment): 8x8 tiles + 16 attribute bytes. */\n")
        f.write("static const uint8_t fiend_tsa[4][0x50] = {\n")
        for k in range(4):
            blk = fiend[k * 0x50:(k + 1) * 0x50]
            f.write("    {\n")
            for i in range(0, 0x50, 16):
                f.write("        " + ", ".join(f"0x{b:02X}" for b in blk[i:i + 16]) + ",\n")
            f.write("    },\n")
        f.write("};\n\n")
        f.write("/* Chaos: 14x12 tiles, then 16 attribute bytes (and 8 bytes of padding). */\n")
        f.write(f"static const uint8_t chaos_tsa[{len(chaos)}] = {{\n")
        for i in range(0, len(chaos), 14):
            f.write("    " + ", ".join(f"0x{b:02X}" for b in chaos[i:i + 14]) + ",\n")
        f.write("};\n")
    print("  fiend_tsa.h: 4 fiends + Chaos")

def extract_battle_backdrops():
    """Extract battle backdrop palettes + selection table (bank_00)."""
    data = load_bank(0x00)
    if not data:
        print("  SKIP battle_backdrops.h: missing bank_00.dat")
        return

    # lut_BackdropPal (ROM $B200): 16 backdrops x 4 NES color indices.
    pal_off = 0x3200
    # lut_BtlBackdrops (ROM $B300): overworld tile id (0..127) -> backdrop id.
    tbl_off = 0x3300

    palettes = [list(data[pal_off + i * 4: pal_off + i * 4 + 4]) for i in range(16)]
    backdrops = list(data[tbl_off: tbl_off + 128])

    with open(os.path.join(OUT_DIR, "battle_backdrops.h"), "w", encoding="utf-8") as f:
        write_header(f, "Battle Backdrops",
                     "bank_00.dat (ROM $B200 lut_BackdropPal / $B300 lut_BtlBackdrops)",
                     "16 backdrops; each is a 4x4 tile pattern in battle_chr.png/"
                     "battle_chr2.png (16 tiles at backdrop_id*128), drawn as a tiled"
                     " background.  backdrop_table maps an overworld tile id (0..127)"
                     " to its backdrop id (0xFF = no battle).")

        f.write("#define BATTLE_BACKDROP_COUNT 16\n\n")

        f.write("/* Overworld tile id (0..127) -> backdrop id (0..15, 0xFF = none). */\n")
        f.write("static const uint8_t backdrop_table[128] = {\n    ")
        for i, b in enumerate(backdrops):
            f.write(f"0x{b:02X}")
            if i < len(backdrops) - 1:
                f.write(", " if i % 16 != 15 else ",\n    ")
        f.write("\n};\n\n")

        f.write("/* 4-color palette per backdrop (NES color indices; index 0 = base). */\n")
        f.write("static const uint8_t backdrop_palettes[BATTLE_BACKDROP_COUNT][4] = {\n")
        for i, p in enumerate(palettes):
            f.write("    { " + ", ".join(f"0x{v:02X}" for v in p) + " }")
            f.write("," if i < 15 else "")
            f.write(f"  /* backdrop {i:2d} */\n")
        f.write("};\n")

    print(f"  battle_backdrops.h: {len(backdrops)} backdrop map entries, {len(palettes)} palettes")


# ---------------------------------------------------------------------------
# ROM banks used directly by the map code (no runtime file reads)
# ---------------------------------------------------------------------------

def overworld_rle_from_asm():
    """The overworld row data: bank_01 $A000 up to MinimapDecompress."""
    end = label_offset("bank_01.asm", "MinimapDecompress")
    return rom_bytes(0x01, 0x2000, end - 0x2000)

def nothing_happens_bytes():
    """Battle message $4E at bank_0B $8FEC, just past the
    battle messages' .bin: up to and including its $00."""
    data = rom_bytes(0x0B, 0x0FEC, 64)
    return data[:data.index(0) + 1]

def _write_byte_array(f, name, data):
    f.write(f"static const uint8_t {name}[{len(data)}] = {{\n")
    for i in range(0, len(data), 32):
        f.write("    " + ",".join(str(b) for b in data[i:i + 32]) + ",\n")
    f.write("};\n\n")

def extract_rom_banks():
    banks = {n: load_bank(n) for n in (0x00, 0x04, 0x05, 0x06)}
    ow_lo = load_bin("bank_01_data.bin")
    if any(v is None for v in banks.values()) or ow_lo is None:
        print("  SKIP rom_banks.h: missing bank_00/04/05/06.dat or bin/bank_01_data.bin")
        return
    rle = overworld_rle_from_asm()
    ow = bytearray(0x4000)
    ow[:len(ow_lo)] = ow_lo
    ow[0x2000:0x2000 + len(rle)] = rle

    with open(os.path.join(OUT_DIR, "rom_banks.h"), "w", encoding="utf-8") as f:
        write_header(f, "ROM Banks",
                     "bank_00.dat, bank_04-06.dat, bin/bank_01_data.bin + bank_01.asm",
                     "Raw ROM bytes for the map code, indexed by ROM address - $8000.")
        f.write("/* bank_00: standard-map tilesets, palettes, teleport tables. */\n")
        _write_byte_array(f, "rom_bank_00", banks[0x00])
        f.write("/* banks 04-06 back to back: standard-map data (lut_SMPtrTbl at $8000). */\n")
        _write_byte_array(f, "rom_sm_maps", banks[0x04] + banks[0x05] + banks[0x06])
        f.write("/* bank_01 $8000-$BFFF: overworld row pointers + RLE rows. */\n")
        _write_byte_array(f, "rom_ow_bank", bytes(ow))
    print(f"  rom_banks.h: bank_00, banks 04-06, overworld bank ({len(rle)} RLE bytes)")

# ---------------------------------------------------------------------------
# CHR (tile graphics) banks, raw NES 2bpp: 16 bytes per tile
# ---------------------------------------------------------------------------

def bank09_bytes():
    """bank_09 $8000-$AFFF: bin/bank_09_data.bin ($8000-$9FFF) followed by the
    one-byte .BYTE lines in bank_09.asm ($A000-$AFFF).  Holds the menu font
    ($8800), battle character sprites ($9000, lut_BatSprCHR) and battle
    object sprites ($A800, lut_BatObjCHR)."""
    return rom_bytes(0x09, 0, label_offset("bank_09.asm", "lut_MinimapNT"))

def orb_chr_bytes():
    """lut_OrbCHR: 32 tiles at bank_0D $B600, inside bin/0D_B5C9_unknown.bin."""
    data = load_bin("0D_B5C9_unknown.bin")
    if data is None:
        return None
    return bytes(data[0xB600 - 0xB5C9: 0xB600 - 0xB5C9 + 0x200])

def bridge_chr_bytes():
    """data_BridgeCHR: bank_0B $B400 ($800 bytes), $C00 into the ending/bridge bin."""
    data = load_bin("0B_A800_endingbridge_chrnt.bin")
    return None if data is None else bytes(data[0xC00:0x1400])

def extract_chr_data():
    sources = [
        ("chr_bank_02", load_bank(0x02), "BANK_MAPCHR: overworld tiles, mapman ($9000), map objects ($A200)"),
        ("chr_bank_03", load_bank(0x03), "BANK_TILESETCHR: 8 standard-map tilesets x 128 tiles"),
        ("chr_bank_07", load_bank(0x07), "BANK_BATTLECHR: battle backdrops + enemy pages 0-7 (128 tiles each)"),
        ("chr_bank_08", load_bank(0x08), "battle backdrops + enemy pages 8-15"),
        ("chr_bank_09", bank09_bytes(), "BANK_MENUCHR/BANK_BTLCHR $8000-$AFFF: font $8800, battle sprites $9000"),
        ("chr_orbs", orb_chr_bytes(), "lut_OrbCHR (BANK_ORBCHR $B600, 2 rows): menu tiles $60-$7F"),
        ("chr_bridge", bridge_chr_bytes(), "data_BridgeCHR (bank_0B $B400, 8 rows): bridge scene tiles $00-$7F"),
    ]
    if any(d is None for _, d, _ in sources):
        print("  SKIP chr_data.h: missing a CHR bank")
        return
    with open(os.path.join(OUT_DIR, "chr_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "CHR Data",
                     "bank_02/03/07/08.dat, bin/bank_09_data.bin + bank_09.asm",
                     "Raw NES 2bpp tile data: tile n is bytes [n*16, n*16+16)\n"
                     " * (8 bytes bit plane 0, then 8 bytes bit plane 1).")
        for name, data, desc in sources:
            f.write(f"/* {desc} */\n")
            _write_byte_array(f, name, data)
    print("  chr_data.h: " + ", ".join(f"{n} ({len(d)} B)" for n, d, _ in sources))

# ---------------------------------------------------------------------------
# Field menu (bank_0E / bank_0F)
# ---------------------------------------------------------------------------

def asm_data(asm_name, label, count, after=None, word=False):
    """`count` bytes (or little-endian words) at `label:` in bank_XX.asm's
    bank, from the ROM.  Local labels (@x): the first after global `after`."""
    off = label_offset(asm_name, label, after)
    raw = rom_bytes(asm_bank(asm_name), off, count * (2 if word else 1))
    if word:
        return [raw[k] | raw[k + 1] << 8 for k in range(0, len(raw), 2)]
    return list(raw)


def extract_ow_objects():
    """lut_OWObjectSprTbl (bank_0F): the overworld objects' 2x2 sprites."""
    vals = asm_data("bank_0F.asm", "lut_OWObjectSprTbl", 24)
    with open(os.path.join(OUT_DIR, "ow_objects.h"), "w", encoding="utf-8") as f:
        write_header(f, "Overworld Objects", "bank_0F.asm",
                     "Overworld object sprites (DrawOWObj_*).")
        f.write("/* bank_0F.asm lut_OWObjectSprTbl: tile, attribute for UL, DL, UR, DR;\n"
                " * 0 airship shadow, 1 bridge, 2 canal.  Tiles are drawn + $10 (sprite\n"
                " * pattern table $1100, LoadOWObjectCHR: bank_02 $9C00). */\n")
        f.write("static const uint8_t lut_OWObjectSprTbl[3][8] = {\n")
        for k in range(3):
            f.write("    { " + ", ".join(f"0x{v:02X}" for v in vals[k * 8:k * 8 + 8]) + " },\n")
        f.write("};\n\n")
        for label, n, comment in (
                ("lut_PlayerMapmanSprTbl", 64, "the party on foot: 8 tables (R0 R1 L0 L1 U0 U1 D0 D1) "
                 "of tile, attribute for UL, DL, UR, DR; tiles from the lead class's mapman CHR"),
                ("lut_VehicleSprTbl", 64, "the same for the vehicles; tiles + $50 canoe, $20 ship, $38 airship"),
                ("lut_VehicleSprY", 9, "sprite y by vehicle (1 foot, 2 canoe, 4 ship, 8 airship)"),
                ("lut_VehicleFacingSprTblOffset", 16, "sprite table offset by facing bits")):
            vals = asm_data("bank_0F.asm", label, n)
            f.write(f"/* bank_0F.asm {label}: {comment} */\n")
            f.write(f"static const uint8_t {label}[{n}] = {{ " + ", ".join(f"0x{v:02X}" for v in vals) + " };\n\n")
        mm = rom_bytes(0x00, 0x03A0, 24)
        f.write("/* bank_00 lut_MapmanPalettes ($83A0): the mapman's two body colours by class\n"
                " * (LoadMapPalettes: sprite palettes 0 and 1, colour 2). */\n")
        f.write("static const uint8_t lut_MapmanPalettes[24] = { " + ", ".join(f"0x{v:02X}" for v in mm) + " };\n\n")
    print("  ow_objects.h: lut_OWObjectSprTbl, mapman and vehicle sprite tables")


def extract_story_data():
    """The story screens (bridge scene; the ending uses the same code):
    bridge nametable, story and credits text, palette, attributes."""
    gfx = load_bin("0B_A800_endingbridge_chrnt.bin")
    story = load_bin("0D_A800_storytext.bin")
    credits = load_bin("0D_BB00_credittext.bin")
    if gfx is None or story is None or credits is None:
        print("  SKIP story_data.h: missing bins")
        return
    with open(os.path.join(OUT_DIR, "story_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Story Screens", "bin/0B_A800_endingbridge_chrnt.bin, bin/0D_A800_storytext.bin, "
                     "bin/0D_BB00_credittext.bin, bank_0D.asm",
                     "Bridge scene (and ending) data: nametable, text, palette.")
        f.write("/* bank_0B data_BridgeNT ($BC00): the bridge picture, a full nametable\n"
                " * (960 tiles + 64 attribute bytes).  Its CHR is chr_bridge (chr_data.h). */\n")
        _write_byte_array(f, "data_BridgeNT", bytes(gfx[0x1400:0x1800]))
        for label, n, comment in (
                ("lut_BridgeBGPal", 16, "the bridge scene's BG palettes"),
                ("lut_StorySecondaryAttrib", 32, "top half of the $2400 nametable's attributes "
                 "(the closed shutters: the box in palette 0)"),
                ("lut_Bridge_LastPage", 1, "bridge scene story pages: 0 to this - 1"),
                ("lut_Ending_LastPage", 1, "ending scene story pages: lut_Bridge_LastPage to this - 1")):
            vals = asm_data("bank_0D.asm", label, n)
            f.write(f"/* bank_0D.asm {label}: {comment} */\n")
            f.write(f"static const uint8_t {label}[{n}] = {{ " + ", ".join(f"0x{v:02X}" for v in vals) + " };\n\n")
        # lut_StoryText ($A800): pointer table, then complex strings (ROM bytes).
        nstory = (struct.unpack_from("<H", story, 0)[0] - 0xA800) // 2
        sptrs = struct.unpack_from(f"<{nstory}H", story, 0)
        f.write("/* bank_0D lut_StoryText ($A800): one complex string per page, as ROM bytes */\n")
        _write_byte_array(f, "story_text_bytes", bytes(story))
        f.write(f"static const uint16_t story_text_offsets[{nstory}] = {{ " +
                ", ".join(str(p - 0xA800) for p in sptrs) + " };\n\n")
        # lut_CreditsText ($BB00): 4 pointers, then pages of simple strings:
        # a PPU address (low, high), tiles, then $01 (another string) or $00.
        cptrs = struct.unpack_from("<4H", credits, 0)
        f.write("/* bank_0D lut_CreditsText ($BB00): 4 pages of (PPU address, tiles, $01 or $00) */\n")
        _write_byte_array(f, "credits_text_bytes", bytes(credits))
        f.write("static const uint16_t credits_text_offsets[4] = { " +
                ", ".join(str(p - 0xBB00) for p in cptrs) + " };\n\n")
        # The sliding-puzzle minigame (bank_0D EnterMiniGame).
        puzzle = load_bin("0D_9E00_puzzle_1bpp.chr")
        f.write("/* bank_0D lut_MinigameCHR: 16 puzzle pieces of 2x2 tiles, 1bpp (8 bytes a tile) */\n")
        _write_byte_array(f, "lut_MinigameCHR", bytes(puzzle[:0x200]))
        for label, n, comment in (
                ("lut_PuzzleStart", 16, "the pieces before the shuffle"),
                ("lut_MGDirection_Y", 4, "slide y per frame: right, left, down, up"),
                ("lut_MGDirection_X", 4, "slide x per frame")):
            vals = asm_data("bank_0D.asm", label, n)
            f.write(f"/* bank_0D.asm {label}: {comment} */\n")
            f.write(f"static const int8_t {label}[{n}] = {{ " + ", ".join(str(v if v < 128 else v - 256) for v in vals) + " };\n\n")
        # The ending: the epilogue picture, its palette, and "The End".
        f.write("/* bank_0B data_EpilogueCHR ($A800, 8 rows) and data_EpilogueNT ($B000) */\n")
        _write_byte_array(f, "data_EpilogueCHR", bytes(gfx[0x000:0x800]))
        _write_byte_array(f, "data_EpilogueNT", bytes(gfx[0x800:0xC00]))
        vals = asm_data("bank_0D.asm", "lut_EndingBGPal", 16)
        f.write("/* bank_0D.asm lut_EndingBGPal: the ending's BG palettes */\n")
        f.write("static const uint8_t lut_EndingBGPal[16] = { " + ", ".join(f"0x{v:02X}" for v in vals) + " };\n\n")
        draw = load_bin("0D_A000_theenddrawdata.bin")
        luts = load_bin("0D_A681_theendluts.bin")
        f.write("/* bank_0D data_TheEndDrawData: the outline's moves, a byte a pixel (bit 0 right,\n"
                " * 1 down, 2 left, 3 up), $00-terminated */\n")
        _write_byte_array(f, "data_TheEndDrawData", bytes(draw))
        f.write("/* bank_0D lut_TheEndPixelPosition_Ylo/_Yhi/_X ($50 each) and lut_TheEndPixelMasks:\n"
                " * the CHR address of pixel (x, y) is X[x] + Ylo[y] + Yhi[y] * 256 */\n")
        _write_byte_array(f, "lut_TheEndPixelPosition", bytes(luts))
        for label, n, after, comment in (
                ("lut_TheEnd_AttrTable", 19, None, "attribute bytes written at $23CA"),
                ("@lut_FillGradient", 80, "DrawFancyTheEndGraphic", "rows the fill may fill")):
            vals = asm_data("bank_0D.asm", label, n, after)
            name = label.lstrip("@")
            f.write(f"/* bank_0D.asm {label}: {comment} */\n")
            f.write(f"static const uint8_t {name}[{n}] = {{ " + ", ".join(f"0x{v:02X}" for v in vals) + " };\n\n")
    print(f"  story_data.h: bridge NT, {nstory} story pages, 4 credits pages")


def extract_minimap_data():
    """The overworld minimap (bank_09 EnterMinimap)."""
    bins = {n: load_bin(f) for n, f in (("nt", "minimap_nt.nam"), ("decor", "minimap_decor.chr"),
                                        ("title", "minimap_title.chr"), ("spr", "minimap_sprite.chr"))}
    if any(v is None for v in bins.values()):
        print("  SKIP minimap_data.h: missing bins")
        return
    with open(os.path.join(OUT_DIR, "minimap_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Minimap", "bank_09.asm, bin/minimap_*",
                     "The overworld minimap: nametable, decoration/title/sprite CHR, tables.")
        f.write("/* bank_09 lut_MinimapNT: the full nametable (960 tiles + attributes) */\n")
        _write_byte_array(f, "lut_MinimapNT", bytes(bins["nt"]))
        f.write("/* bank_09 chr_MinimapDecor: $30 tiles of the dragon decorations */\n")
        _write_byte_array(f, "chr_MinimapDecor", bytes(bins["decor"]))
        f.write("/* bank_09 chr_MinimapTitle: $28 tiles of the FINAL FANTASY title */\n")
        _write_byte_array(f, "chr_MinimapTitle", bytes(bins["title"]))
        f.write("/* bank_09 lut_MinimapSprCHR: sprite tiles $80 (you are here) and $81 (towns) */\n")
        _write_byte_array(f, "lut_MinimapSprCHR", bytes(bins["spr"]))
        for label, n, comment in (
                ("lut_MinimapBGPal", 16, "BG palettes"),
                ("lut_MinimapTileset", 128, "per overworld tile: color (bits 0-1), town marker (bit 2)"),
                ("lut_MinimapBitplane", 16, "the byte of each tile written on each of 16 frames"),
                ("lut_MinimapDecorCHRDest_lo", 48, "decoration tiles' pattern table addresses"),
                ("lut_MinimapDecorCHRDest_hi", 48, ""),
                ("lut_MinimapTitleCHRDest_lo", 40, "title tiles' pattern table addresses"),
                ("lut_MinimapTitleCHRDest_hi", 40, "")):
            vals = asm_data("bank_09.asm", label, n)
            if comment:
                f.write(f"/* bank_09.asm {label}: {comment} */\n")
            f.write(f"static const uint8_t {label}[{n}] = {{ " + ", ".join(f"0x{v:02X}" for v in vals) + " };\n\n")
    print("  minimap_data.h: nametable, CHR, tables")


def extract_music_data():
    """The music driver's data (bank_0D MusicPlay): the score data and the
    driver's tables, kept in ROM order."""
    score = load_bin("0D_8000_scoredata.bin")
    if score is None:
        print("  SKIP music_data.h: missing bin/0D_8000_scoredata.bin")
        return
    freqs = asm_data("bank_0D.asm", "lut_NoteFreqs", 48, word=True)
    lengths = asm_data("bank_0D.asm", "lut_NoteLengths", 96)
    speeds = asm_data("bank_0D.asm", "lut_EnvSpeeds", 16)
    patterns = asm_data("bank_0D.asm", "lut_EnvPatterns", 512)
    octave = asm_data("bank_0D.asm", "lut_OctaveOffset", 6)
    tables = bytearray()
    for w in freqs:
        tables += bytes((w & 0xFF, w >> 8))
    tables += bytes(lengths) + bytes(speeds) + bytes(patterns)
    with open(os.path.join(OUT_DIR, "music_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Music", "bin/0D_8000_scoredata.bin, bank_0D.asm",
                     "Score data and the music driver's tables.")
        f.write("/* bank_0D lut_ScoreData ($8000): per song 4 channel pointers (sq1, sq2, tri,\n"
                " * unused), then the scores; pointers and loop targets are bank addresses. */\n")
        f.write("#define MUSIC_SCORE_BASE 0x8000\n")
        _write_byte_array(f, "lut_ScoreData", bytes(score))
        f.write("/* bank_0D $B2F9-$B5C8, as the ROM has them one after another (the driver\n"
                " * reads them through pointers, and the unused octaves $DC-$DF run past\n"
                " * lut_NoteFreqs into lut_NoteLengths). */\n")
        f.write("#define MUSIC_TBL_NOTEFREQS   0x000   /* 4 octaves x 12 notes, 2 bytes each */\n")
        f.write("#define MUSIC_TBL_NOTELENGTHS 0x060   /* 6 tempos x 16 lengths */\n")
        f.write("#define MUSIC_TBL_ENVSPEEDS   0x0C0   /* 16 envelope speeds */\n")
        f.write("#define MUSIC_TBL_ENVPATTERNS 0x0D0   /* 16 patterns x 32 volumes */\n")
        _write_byte_array(f, "music_tables", bytes(tables))
        vm = asm_data("bank_0F.asm", "@lut_VehicleMusic", 9, "PrepOverworld")
        tm = asm_data("bank_0F.asm", "@lut_TilesetMusicTrack", 8, "PrepStandardMap")
        f.write("/* bank_0F PrepOverworld @lut_VehicleMusic: the overworld's track by vehicle */\n")
        f.write("static const uint8_t lut_VehicleMusic[9] = { " + ", ".join(f"0x{v:02X}" for v in vm) + " };\n\n")
        f.write("/* bank_0F PrepStandardMap @lut_TilesetMusicTrack: a standard map's track by tileset */\n")
        f.write("static const uint8_t lut_TilesetMusicTrack[8] = { " + ", ".join(f"0x{v:02X}" for v in tm) + " };\n\n")
        f.write("/* bank_0D lut_OctaveOffset: byte offset of each octave in lut_NoteFreqs */\n")
        f.write("static const uint8_t lut_OctaveOffset[6] = { " + ", ".join(str(v) for v in octave) + " };\n")
    print(f"  music_data.h: {len(score)} bytes of scores, {len(tables)} bytes of tables")

    # bank_0C data_BattleSoundEffects: a 3-byte header (frames, square
    # blocks, noise blocks), then max(blocks) 8-byte blocks.
    sfx = []
    for name in ("@sfx_Magic", "@sfx_EnemyAttack", "@sfx_PlayerAttack"):
        head = asm_data("bank_0C.asm", name, 3, "data_BattleSoundEffects")
        sfx.append(asm_data("bank_0C.asm", name, 3 + 8 * max(head[1], head[2]), "data_BattleSoundEffects"))
    with open(os.path.join(OUT_DIR, "battle_sfx_data.h"), "w", encoding="utf-8") as f:
        f.write("/* Generated by tools/extract_data.py -- do not edit. */\n#pragma once\n#include <stdint.h>\n\n")
        f.write("/* bank_0C data_BattleSoundEffects: 0 magic, 1 enemy attack, 2 player attack.\n")
        f.write(" * Header: frames, square blocks, noise blocks; then 8-byte blocks:\n")
        f.write(" * $4004 $4005 $4006 $4007 frames | $400C $400E frames. */\n")
        offs = []
        flat = []
        for blob in sfx:
            offs.append(len(flat))
            flat += blob
        _write_byte_array(f, "data_BattleSoundEffects", bytes(flat))
        f.write("static const uint16_t lut_BattleSfxOffset[3] = { " + ", ".join(str(o) for o in offs) + " };\n")
    print(f"  battle_sfx_data.h: {len(flat)} bytes")


def extract_menu_data():
    """The field menu's layout, cursor, palette and equipment tables."""
    tables = [
        # (C name, asm, label, count, after, word, comment)
        ("lut_MainItemBoxes", "bank_0E.asm", "lut_MainItemBoxes", 40, None, False,
         "x, y, width, height: 0 orbs, 1-4 characters, 5 gold, 6 options, 7 title, 8 description, 9 list"),
        ("lutMenuPalettes", "bank_0E.asm", "lutMenuPalettes", 12, None, False,
         "BG palettes 0-2 (0: lit orbs)"),
        ("lut_MainMenuSubCursor", "bank_0E.asm", "lut_MainMenuSubCursor", 8, None, False,
         "x, y per character box"),
        ("lut_MainMenuCursor_Y", "bank_0E.asm", "lut_MainMenuCursor_Y", 5, None, False,
         "option cursor y (x is $10)"),
        ("lut_ItemMenuCursor", "bank_0E.asm", "lut_ItemMenuCursor", 48, None, False,
         "x, y per item slot"),
        ("lutItemBoxStrPos", "bank_0F.asm", "lutItemBoxStrPos", 48, None, False,
         "tile x, y per item slot"),
        ("lut_ItemTargetCursorX", "bank_0E.asm", "@lut", 4, "DrawItemTargetCursor", False,
         "DrawItemTargetCursor: x per character (y is $68)"),
        ("lutStatusBoxes", "bank_0E.asm", "@lutStatusBoxes", 24, "EnterStatusMenu", False,
         "x, y, width, height of the 6 status boxes"),
        ("lut_EquipBoxes", "bank_0E.asm", "@lut_EquipBoxes", 40, "DrawEquipMenu", False,
         "x, y, width, height: 0 title, 1 modes, then name/equipment box per character"),
        ("lut_EquipMenuCurs", "bank_0E.asm", "lut_EquipMenuCurs", 32, None, False,
         "x, y per equipment slot"),
        ("lut_EquipModeCursorX", "bank_0E.asm", "@lut_CursorX", 3, "DrawEquipMenuModeCurs", False,
         "DrawEquipMenuModeCurs: x per mode (y is $14)"),
        ("lut_EquipModeAttr", "bank_0E.asm", "@lut_Attr", 12, "UpdateEquipMenuModeAttrib", False,
         "UpdateEquipMenuModeAttrib: attribute bytes $23C3-$23C6 per mode"),
        ("lut_EquipStringPositions", "bank_0F.asm", "lut_EquipStringPositions", 32, None, False,
         "tile x, y per equipment slot"),
        ("lut_ArmorTypes", "bank_0E.asm", "lut_ArmorTypes", 40, None, False,
         "0 body, 1 shield, 2 helmet, 3 gloves (by 0-based armor id)"),
        ("lutClassBatSprPalette", "bank_0F.asm", "lutClassBatSprPalette", 12, None, False,
         "sprite palette per class"),
        ("lut_TitleText_Continue", "bank_0E.asm", "lut_TitleText_Continue", 9, None, False,
         "the title screen's strings"),
        ("lut_TitleText_NewGame", "bank_0E.asm", "lut_TitleText_NewGame", 9, None, False, ""),
        ("lut_TitleText_RespondRate", "bank_0E.asm", "lut_TitleText_RespondRate", 13, None, False, ""),
        ("lut_TitleCursor_Y", "bank_0E.asm", "lut_TitleCursor_Y", 2, None, False,
         "title cursor y: Continue, New Game (x is $48)"),
        ("lut_PtyGenBuf", "bank_0E.asm", "lut_PtyGenBuf", 64, None, False,
         "the ptygen buffer per character: class, ?, name[4], name x/y, class x/y, "
         "sprite x/y, box x/y, cursor x/y"),
        ("lut_NameInputRowStart", "bank_0E.asm", "lut_NameInputRowStart", 7, None, False,
         "name input: first character of each row"),
        ("lut_NameInput", "bank_0E.asm", "lut_NameInput", 7 * 20 + 1 + 16, None, False,
         "name input: the characters (every other byte), drawn as one string"),
        ("lut_LineupSlots", "bank_0E.asm", "lut_LineupSlots", 32, None, False,
         "lineup menu slots: dead, char index, x, y, target x, target y, -, -"),
        ("str_TargetMaxHP", "bank_0E.asm", "@str_charmaxhp", 22, "DrawItemTargetMenu", False,
         "DrawItemTargetMenu's strings"),
        ("str_TargetCurHP", "bank_0E.asm", "@str_charcurhp", 22, "DrawItemTargetMenu", False, ""),
        ("str_TargetAilment", "bank_0E.asm", "@str_ailmentblurb", 21, "DrawItemTargetMenu", False, ""),
        ("str_TargetName", "bank_0E.asm", "@str_name", 18, "DrawItemTargetMenu", False, ""),
    ]
    words = [
        ("lut_ClassEquipBit", "lut_ClassEquipBit", 12, "a set bit in a permission word: the class can't equip it"),
        ("lut_WeaponPermissions", "lut_WeaponPermissions", 40, "by 0-based weapon id"),
        ("lut_ArmorPermissions", "lut_ArmorPermissions", 40, "by 0-based armor id"),
    ]
    with open(os.path.join(OUT_DIR, "menu_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Menu Data", "bank_0E.asm, bank_0F.asm",
                     "Field menu layout, cursors, palettes, equipment permissions.")
        for name, asm, label, n, after, word, comment in tables:
            vals = asm_data(asm, label, n, after, word)
            if comment:
                f.write(f"/* {asm} {label}: {comment} */\n")
            f.write(f"static const uint8_t {name}[{n}] = {{ " + ", ".join(f"0x{v:02X}" for v in vals) + " };\n\n")
        for name, label, n, comment in words:
            vals = asm_data("bank_0E.asm", label, n, None, True)
            f.write(f"/* bank_0E.asm {label}: {comment} */\n")
            f.write(f"static const uint16_t {name}[{n}] = {{ " + ", ".join(f"0x{v:04X}" for v in vals) + " };\n\n")
        # TitleScreen_Copyright @lut_Copyright: (.WORD PPU address, .BYTE string) x 2.
        off = label_offset("bank_0E.asm", "@lut_Copyright", "TitleScreen_Copyright")
        entries = []
        for _ in range(2):                                     # 2 lines: a word, then a string
            raw = rom_bytes(0x0E, off, 40)
            addr = raw[0] | raw[1] << 8
            end = raw.index(0, 2) + 1
            entries.append([addr, list(raw[2:end])])
            off += end
        f.write("/* bank_0E.asm TitleScreen_Copyright @lut_Copyright: tile x, y, $00-terminated string */\n")
        f.write(f"static const uint8_t lut_Copyright[{len(entries)}][18] = {{\n")
        for addr, data in entries:
            row = [(addr - 0x2000) % 32, (addr - 0x2000) // 32] + data
            row += [0] * (18 - len(row))
            f.write("    { " + ", ".join(f"0x{v:02X}" for v in row[:18]) + " },\n")
        f.write("};\n\n")
        # lut_IntroStoryText (bank_0D $BF20): one complex string, as ROM bytes.
        intro = load_bin("0D_BF20_introtext.bin")
        if intro is not None:
            f.write("/* bank_0D lut_IntroStoryText ($BF20): the intro story, one complex string */\n")
            _write_byte_array(f, "lut_IntroStoryText", bytes(intro))
    print(f"  menu_data.h: {len(tables) + len(words) + 2} tables")


# ---------------------------------------------------------------------------
# Map objects (townspeople etc.)
# ---------------------------------------------------------------------------

def asm_label_words(asm_name, label, count):
    """`count` .WORDs at `label:`, from the ROM: as the name of the routine
    at that address (banks 0E / 0F) where there is one, else as "$XXXX"."""
    names = {}
    for asm, base in (("bank_0F.asm", 0xC000), (asm_name, 0x8000)):
        for n, off in bank_labels(asm):
            if not n.startswith("@"):
                names.setdefault(base + off, n)
    out = []
    for v in asm_data(asm_name, label, count, word=True):
        out.append(names.get(v, f"${v:04X}"))
    return out

def extract_battle_ui():
    """Battle screen layout tables: cursor positions, enemy target order,
    combat box geometry, respond delays, the command box strings, the
    backdrop column layout and the status column positions."""
    def cmd_strings():
        """@txt0-4 of lua_BattleCommandBoxInfo, up to and including their $00."""
        out = []
        for k in range(5):
            off = label_offset("bank_0F.asm", f"@txt{k}", "lua_BattleCommandBoxInfo")
            data = rom_bytes(0x0F, off, 8)
            out.append(list(data[:data.index(0) + 1]))
        return out

    tables = [
        ("lut_MainCombatBoxCursorPos", "bank_0C.asm", 16, "x,y per 2x4 command cell (col-major: FIGHT..ITEM, RUN x4)"),
        ("lut_PlayerTargetCursorPos", "bank_0C.asm", 16, "x,y per character (mirrored)"),
        ("lut_Target9SmallCursorPos", "bank_0C.asm", 18, "x,y per cursor index"),
        ("lut_Target4LargeCursorPos", "bank_0C.asm", 8, "x,y per cursor index"),
        ("lut_TargetMixCursorPos", "bank_0C.asm", 16, "x,y per cursor index"),
        ("lut_MagicCursorPos", "bank_0C.asm", 24, "x,y, column-major (3 columns x 4 rows)"),
        ("lut_ItemCursorPos", "bank_0C.asm", 16, "x,y, weapons then armor"),
        ("lut_DrinkCursorPos", "bank_0C.asm", 16, "x,y, HEAL/PURE (mirrored)"),
        ("lut_EnemyIndex9Small", "bank_0C.asm", 9, "cursor index -> enemy slot"),
        ("lut_EnemyIndex4Large", "bank_0C.asm", 4, "cursor index -> enemy slot"),
        ("lut_EnemyIndexMix", "bank_0C.asm", 8, "cursor index -> enemy slot"),
        ("lut_CombatBoxes", "bank_0F.asm", 40, "per box: hdr,x,y,w,h (box) then hdr,x,y (text); y is relative to row 18"),
        ("lut_EnemyRosterBox", "bank_0F.asm", 5, "hdr,x,y,w,h"),
        ("lut_CombatItemMagicBox", "bank_0F.asm", 5, "hdr,x,y,w,h"),
        ("lut_CombatDrinkBox", "bank_0F.asm", 5, "hdr,x,y,w,h"),
        ("lua_BattleCommandBoxInfo", "bank_0F.asm", 5, "the box: hdr,x,y,w,h"),
        ("lut_RespondDelay", "bank_0B.asm", 8, "frames per respond rate"),
        ("@lut_BackdropLayout", "bank_0F.asm", 14, "backdrop tile per column"),
        ("lut_ExplosionCoords_9Small", "bank_0C.asm", 18, "explosion area top-left x,y per slot (+$18)"),
        ("@lut_ExplosionCoords_4Large", "bank_0C.asm", 8, "explosion area top-left x,y per slot (+$28)"),
        ("@lut_ExplosionCoords_6Small", "bank_0C.asm", 12, "mix formation small slots 2-7 (+$18)"),
        ("lut_CharacterPoseTSA", "bank_0C.asm", 32, "per pose ($04 apart): the 3 row tiles + pad"),
        ("lut_WeaponSwingTSA", "bank_0C.asm", 16, "per frame: 4 tile offsets, 4 attributes"),
        ("lut_InBattleCharPaletteAssign", "bank_0C.asm", 12, "sprite palette per class"),
        ("@BattleSpritePalettes", "bank_0F.asm", 16, "LoadBattleSpritePalettes: the 4 sprite palettes"),
    ]
    with open(os.path.join(OUT_DIR, "battle_ui.h"), "w", encoding="utf-8") as f:
        write_header(f, "Battle UI Tables", "bank_0B / bank_0C / bank_0F .BYTE tables",
                     "Battle screen layout data, copied from the disassembly.")
        for label, asm, n, comment in tables:
            vals = asm_label_bytes(asm, label, n)
            name = label.lstrip("@")
            f.write(f"/* {asm} {label}: {comment} */\n")
            f.write(f"static const uint8_t {name}[{n}] = {{ " + ", ".join(f"0x{v:02X}" for v in vals) + " };\n\n")
        strs = cmd_strings()
        f.write("/* bank_0F lua_BattleCommandBoxInfo @txt0-4: FIGHT, MAGIC, DRINK, ITEM, RUN ($00-terminated). */\n")
        f.write("static const uint8_t lut_BattleCommandText[5][5] = {\n")
        for t in strs:
            f.write("    { " + ", ".join(f"0x{v:02X}" for v in (t + [0] * 5)[:5]) + " },\n")
        f.write("};\n\n")
        box = label_offset("bank_0F.asm", "lua_BattleCommandBoxInfo")
        recs = rom_bytes(0x0F, box + 5, 25)                  # 5 text records after the box's
        pos = []
        for k in range(5):
            pos += [recs[k * 5 + 1], recs[k * 5 + 2]]
        assert len(pos) == 10, pos
        f.write("/* bank_0F lua_BattleCommandBoxInfo text blocks: x,y of each command (y relative to row 18). */\n")
        f.write("static const uint8_t lut_BattleCommandTextPos[10] = { " + ", ".join(f"0x{v:02X}" for v in pos) + " };\n\n")
        words = asm_label_words("bank_0C.asm", "lut_CharStatusPPUAddr", 4)
        f.write("/* bank_0C lut_CharStatusPPUAddr: nametable address of each character's status. */\n")
        f.write("static const uint16_t lut_CharStatusPPUAddr[4] = { " + ", ".join("0x" + w.lstrip("$") for w in words) + " };\n")
    print("  battle_ui.h: layout tables")

def extract_mapobj_data():
    bank00 = load_bank(0x00)
    talk = load_bin("0E_95D5_objectdata.bin")
    if not bank00 or not talk:
        print("  SKIP mapobj_data.h: missing bank_00.dat or bin/0E_95D5_objectdata.bin")
        return
    NUM_OBJ = 0xD0
    handlers = asm_label_words("bank_0E.asm", "lut_MapObjTalkJumpTbl", NUM_OBJ)
    names = []
    for h in handlers:
        if h not in names:
            names.append(h)

    objects = bank00[0xB400 - 0x8000:0xC000 - 0x8000]   # lut_MapObjects: $30 bytes per map
    gfx = bank00[0xAE00 - 0x8000:0xAE00 - 0x8000 + NUM_OBJ]

    with open(os.path.join(OUT_DIR, "mapobj_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Map Object Data",
                     "bank_00.dat ($B400 lut_MapObjects, $AE00 lut_MapObjGfx), "
                     "bin/0E_95D5_objectdata.bin, bank_0E.asm lut_MapObjTalkJumpTbl",
                     "Townspeople and other standard-map objects.")
        f.write(f"#define MAPOBJ_ID_COUNT 0x{NUM_OBJ:02X}\n")
        f.write("#define MAPOBJ_PER_MAP 15\n\n")
        f.write("/* lut_MapObjects: per map, 15 objects x 3 bytes (id, x | flags, y) + 3 pad.\n")
        f.write(" * Flags in the x byte: $80 = in a room, $40 = stationary. */\n")
        f.write(f"#define MAPOBJ_MAP_COUNT {len(objects) // 0x30}\n")
        _write_byte_array(f, "lut_MapObjects", objects)
        f.write("/* lut_MapObjGfx: graphic id per object (CHR at bank_02 $A200 + gfx*$100). */\n")
        _write_byte_array(f, "lut_MapObjGfx", gfx)
        f.write("/* lut_MapObjTalkData: 4 bytes per object, used by its talk routine. */\n")
        _write_byte_array(f, "lut_MapObjTalkData", talk[:NUM_OBJ * 4])
        f.write("/* Talk routines (bank_0E TalkRoutines), in lut_MapObjTalkJumpTbl order. */\n")
        f.write("typedef enum {\n")
        for n in names:
            f.write(f"    TALKFN_{n[5:].upper() if n.startswith('Talk_') else n.upper()},\n")
        f.write("    TALKFN_COUNT\n} TalkFn;\n\n")
        f.write("/* lut_MapObjTalkJumpTbl: the talk routine for each object id. */\n")
        f.write(f"static const uint8_t lut_MapObjTalkFn[MAPOBJ_ID_COUNT] = {{\n")
        for i in range(0, NUM_OBJ, 8):
            f.write("    " + ", ".join(
                f"TALKFN_{h[5:].upper()}" for h in handlers[i:i + 8]) + ",\n")
        f.write("};\n\n")
        tsa = asm_label_bytes("bank_0F.asm", "lut_2x2MapObj_Right", 64)   # (Right, Left, Up, Down follow)
        f.write("/* bank_0F lut_2x2MapObj_Right/Left/Up/Down: [facing][frame] = UL, DL, UR, DR\n")
        f.write(" * (tile, attribute) pairs; attribute bits 0-1 = sprite palette, $40 = flip. */\n")
        f.write("static const uint8_t lut_2x2MapObj[4][2][8] = {\n")
        for k in range(8):
            f.write("    " + ("{ " if k % 2 == 0 else "") + "{ " + ", ".join(f"0x{v:02X}" for v in tsa[k * 8:k * 8 + 8])
                    + (" }," if k % 2 == 0 else " } },") + "\n")
        f.write("};\n")
    print(f"  mapobj_data.h: {len(objects) // 0x30} maps, {len(names)} talk routines")

def extract_encounter_data():
    rates = load_bin("0B_8C00_mapencounterrates.bin")
    if not rates:
        print("  SKIP encounter_data.h: missing bin/0B_8C00_mapencounterrates.bin")
        return
    with open(os.path.join(OUT_DIR, "encounter_data.h"), "w", encoding="utf-8") as f:
        write_header(f, "Encounter Data",
                     "bin/0B_8C00_mapencounterrates.bin (bank_0B $8C00, lut_BattleRates)",
                     "Random encounter rates (X/256 per step).")
        f.write("/* lut_BattleRates: entry 0 is the overworld (unused: its rate is\n")
        f.write(" * hard-coded), entry map+1 is standard map `map`. */\n")
        _write_byte_array(f, "lut_BattleRates", rates)
    print(f"  encounter_data.h: {len(rates)} rates")

# ---------------------------------------------------------------------------
# Headers without data
#
# The game's data never goes into git: each generated header keeps its
# types, sizes and declarations (`extern const ...`), and the definitions
# go to data/generated/data/<header>.c, which is generated from the ROM and
# not committed.
# ---------------------------------------------------------------------------

NOT_ROM_DATA = {"constants.h", "types.h", "palette_data.h"}   # (no game data in them)

def _statements(text):
    """Top-level `static const ...;` definitions: (start, end, eq) offsets,
    `eq` the '=' starting the initializer (or -1)."""
    out = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("static const", i) and (i == 0 or text[i - 1] == "\n"):
            j, depth, eq = i, 0, -1
            while j < n:
                c = text[j]
                if text.startswith("/*", j):
                    j = text.index("*/", j) + 2
                    continue
                if text.startswith("//", j):
                    j = text.index("\n", j)
                    continue
                if c in "\"'":
                    j += 1
                    while text[j] != c:
                        j += 2 if text[j] == "\\" else 1
                elif c in "{(":
                    depth += 1
                elif c in "})":
                    depth -= 1
                elif c == "=" and depth == 0 and eq < 0:
                    eq = j
                elif c == ";" and depth == 0:
                    break
                j += 1
            out.append((i, j + 1, eq))
            i = j + 1
        else:
            nl = text.find("\n", i)
            i = n if nl < 0 else nl + 1
    return out

def split_data(name):
    """Move header `name`'s definitions to data/<name>.c."""
    path = os.path.join(OUT_DIR, name)
    text = open(path, encoding="utf-8").read()
    stmts = _statements(text)
    if not stmts:
        return 0
    header, defs, last = [], [], 0
    for start, end, eq in stmts:
        decl = text[start + len("static "):eq if eq >= 0 else end - 1].strip()
        header.append(text[last:start])
        header.append(f"extern {decl};")
        defs.append(text[start + len("static "):end])
        last = end
    header.append(text[last:])
    open(path, "w", encoding="utf-8", newline="\n").write("".join(header))
    data_dir = os.path.join(OUT_DIR, "data")
    os.makedirs(data_dir, exist_ok=True)
    with open(os.path.join(data_dir, name[:-2] + ".c"), "w", encoding="utf-8", newline="\n") as f:
        f.write(f"/* Generated from the ROM by tools/extract_data.py: game data,\n"
                f" * never committed.  Declared in {name}. */\n\n")
        f.write(f'#include "../{name}"\n\n')
        f.write("\n\n".join(defs) + "\n")
    return len(stmts)

def split_all():
    import glob
    for old in glob.glob(os.path.join(OUT_DIR, "data", "*.c")):   # (none left over)
        os.remove(old)
    total = 0
    for name in sorted(os.listdir(OUT_DIR)):
        if name.endswith(".h") and name not in NOT_ROM_DATA:
            total += split_data(name)
    print(f"  {total} tables moved to data/generated/data/")


def main():
    print("=" * 60)
    print("FF1 Data Extraction Tool")
    print("=" * 60)

    os.makedirs(OUT_DIR, exist_ok=True)

    extractors = [
        ("Constants", extract_constants),
        ("Weapon Data", extract_weapon_data),
        ("Armor Data", extract_armor_data),
        ("Enemy Data", extract_enemy_data),
        ("Magic Data", extract_magic_data),
        ("Battle Domains", extract_battle_domains),
        ("Battle Formations", extract_battle_formations),
        ("Battle Palettes", extract_battle_palettes),
        ("Battle Backdrops", extract_battle_backdrops),
        ("Fiend TSA", extract_fiend_tsa),
        ("Battle UI", extract_battle_ui),
        ("Battle Messages", extract_battle_messages),
        ("Enemy AI", extract_enemy_ai),
        ("Enemy Names", extract_enemy_names),
        ("Level-Up Data", extract_level_up_data),
        ("Shop Data", extract_shop_data),
        ("Treasure Data", extract_treasure_data),
        ("Item Data", extract_item_data),
        ("Spell Names", extract_spell_names),
        ("Dialogue", extract_dialogue),
        ("Raw Text Tables", extract_text_data),
        ("Story Text", lambda: extract_text_bin("0D_A800_storytext.bin", "story_text.h", "Story Text", "DTE-decoded story text")),
        ("Intro Text", lambda: extract_text_bin("0D_BF20_introtext.bin", "intro_text.h", "Intro Text", "DTE-decoded intro text")),
        ("Credit Text", lambda: extract_text_bin("0D_BB00_credittext.bin", "credit_text.h", "Credit Text", "DTE-decoded credit text")),
        ("Menu Text", extract_menu_text),
        ("Class Starting Stats", extract_class_starting_stats),
        ("Game Flags Init", extract_game_flags_init),
        ("RNG Table", extract_rng_table),
        ("Map Data", extract_map_data),
        ("Teleport Data", extract_teleport_data),
        ("Score Data", extract_score_data),
        ("Types (Save Structure)", extract_types),
        ("ROM Banks", extract_rom_banks),
        ("CHR Data", extract_chr_data),
        ("Map Objects", extract_mapobj_data),
        ("Encounter Data", extract_encounter_data),
        ("Menu Data", extract_menu_data),
        ("Overworld Objects", extract_ow_objects),
        ("Story Screens", extract_story_data),
        ("Minimap", extract_minimap_data),
        ("Music", extract_music_data),
    ]

    for name, func in extractors:
        print(f"\n[{name}]")
        try:
            func()
        except Exception as e:
            print(f"  ERROR: {e}")
            import traceback
            traceback.print_exc()

    print("\n[Headers without data]")
    split_all()

    print(f"\n{'=' * 60}")
    print(f"Done! Output files written to: {OUT_DIR}")
    print(f"{'=' * 60}")

if __name__ == "__main__":
    main()