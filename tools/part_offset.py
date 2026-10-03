#!/usr/bin/env python3
"""partitions.csv lookups.
part_offset.py partitions.csv rom      print the offset of a partition
part_offset.py partitions.csv --json   offsets for tools/web (offsets.json): the merged image at 0, rom, save
"""
import json
import sys

parts = {}
for line in open(sys.argv[1]):
    f = [x.strip() for x in line.split(",")]
    if len(f) >= 5 and not f[0].startswith("#"):
        parts[f[0]] = {"offset": int(f[3], 0), "size": int(f[4], 0)}
if sys.argv[2] == "--json":
    out = {"firmware": {"offset": 0, "file": "gameboy-merged.bin"}, "rom": parts["rom"], "save": parts["save"]}
    print(json.dumps(out, indent=1))
else:
    print(hex(parts[sys.argv[2]]["offset"]))
