#!/usr/bin/env python3
"""Scan an Aidyn Chronicles ROM to find string resource addresses.

The debug build has these at known offsets, but the retail ROM differs.
This script searches for LZ01-compressed string data by looking for
non-zero regions near the expected locations.
"""
import sys, struct

if len(sys.argv) < 2:
    print(f"Usage: {sys.argv[0]} <rom.z64>")
    sys.exit(1)

with open(sys.argv[1], "rb") as f:
    rom = f.read()

print(f"ROM size: {len(rom)} bytes (0x{len(rom):X})")
print(f"ROM header: {rom[0x20:0x34].decode('ascii', errors='replace').strip()}")
print()

# Debug build addresses (ROM file offsets, after stripping PI_ROM_BASE 0x10000000)
debug_addrs = {
    "common_string_array":   0x1FF6700,
    "combat_romstrings":     0x1FFC4D0,
    "romstring_controller":  0x1FFC0C0,
    "romstring_credits":     0x1FFD330,
    "romstring_items":       0x1FDD7F0,
    "romstring_skills":      0x1FDFD50,
    "romstring_spells":      0x1FDE9B0,
    "romstring_potiondetails": 0x1FE04E0,
    "romstring_stats":       0x1FDE6C0,
    "cinematic_titles":      0x1FFB140,
    "dialouge_entity":       0x1FE3CE0,
    "gameStatemod_dat":      0x1FE4060,
    "itemDB":                0x1FDB5E0,
    "journal_ROM":           0x1FF90B0,
    "RomstringPotion":       0x1FFDA20,
    "audiokey_rom":          0x1FF5300,
    "borg_files":            0x0F5DA0,
    "borg_listings":         0x1F98790,
}

# Check each address
print("=== Debug address check ===")
for name, offset in sorted(debug_addrs.items(), key=lambda x: x[1]):
    if offset + 32 <= len(rom):
        data = rom[offset:offset+32]
        nonzero = sum(1 for b in data if b != 0)
        first8 = ' '.join(f'{b:02x}' for b in data[:8])
        status = "OK" if nonzero > 4 else "ZEROS" if nonzero == 0 else "SPARSE"
        print(f"  {name:30s} @ 0x{offset:07X}: [{status:6s}] {first8}")
    else:
        print(f"  {name:30s} @ 0x{offset:07X}: OUT OF RANGE")

# For addresses that are zeros, try to find the actual data
# by scanning nearby regions
print()
print("=== Searching for relocated string data ===")
for name, debug_off in sorted(debug_addrs.items(), key=lambda x: x[1]):
    if debug_off + 32 > len(rom):
        continue
    data = rom[debug_off:debug_off+8]
    if all(b == 0 for b in data):
        # Search backwards and forwards for non-zero data
        found = None
        for delta in range(-0x100000, 0x100000, 0x10):
            test_off = debug_off + delta
            if 0 <= test_off < len(rom) - 32:
                test_data = rom[test_off:test_off+32]
                nonzero = sum(1 for b in test_data if b != 0)
                # Look for LZ01 signature: first byte typically 0x11-0xFF
                # and the data should look like compressed content
                if nonzero > 20 and test_data[0] >= 0x10:
                    found = test_off
                    break
        if found:
            data = rom[found:found+32]
            first16 = ' '.join(f'{b:02x}' for b in data[:16])
            pi_addr = found + 0x10000000
            print(f"  {name:30s}: debug=0x{debug_off:07X} → found data at 0x{found:07X} (PI=0x{pi_addr:08X})")
            print(f"    first 16 bytes: {first16}")
        else:
            print(f"  {name:30s}: debug=0x{debug_off:07X} → NO DATA FOUND nearby")

# Also dump the region around borg_listings to verify ROM is valid
print()
borg_off = debug_addrs["borg_listings"]
if borg_off + 32 <= len(rom):
    data = rom[borg_off:borg_off+32]
    first16 = ' '.join(f'{b:02x}' for b in data[:16])
    count = struct.unpack(">I", data[:4])[0]
    print(f"=== Borg listings sanity check @ 0x{borg_off:07X} ===")
    print(f"  first 16 bytes: {first16}")
    print(f"  borg count (BE u32): {count}")
