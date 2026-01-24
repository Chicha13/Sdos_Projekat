"""
memory_dump_to_compressed.py

Usage:
    python memory_dump_to_compressed.py in_memory_dump.txt out_compressed.txt
    
Converts the DSP hex dump (0x000000XX) to compressed ASCII text
"""
import sys

if len(sys.argv) != 3:
    print("Usage: python memory_dump_to_compressed.py in_memory_dump.txt out_compressed.txt")
    sys.exit(1)

input_file = sys.argv[1]
output_file = sys.argv[2]

with open(input_file, "r") as f:
    lines = f.readlines()

out_bytes = bytearray()

for line in lines:
    line = line.strip()
    if not line:
        continue

    # remove "0x" and convert to int
    value = int(line, 16)

    # take only LSB (last 8 bits)
    ch = value & 0xFF
    out_bytes.append(ch)

with open(output_file, "wb") as f:
    f.write(out_bytes)

print(f"Wrote {len(out_bytes)} characters to '{output_file}'")
