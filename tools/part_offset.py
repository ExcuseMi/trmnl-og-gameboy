#!/usr/bin/env python3
"""Print the offset of a partition in partitions.csv: part_offset.py partitions.csv rom"""
import sys
for line in open(sys.argv[1]):
    f = [x.strip() for x in line.split(",")]
    if f[0] == sys.argv[2]:
        print(f[3])
        break
