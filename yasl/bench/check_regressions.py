#!/usr/bin/env python3
"""Compare benchmark CSV files and fail on regressions over a budget."""
import csv
import math
import sys

if len(sys.argv) not in (3, 4):
    raise SystemExit("usage: check_regressions.py BASELINE.csv CURRENT.csv [max_slowdown]")

limit = float(sys.argv[3]) if len(sys.argv) == 4 else 1.20

def read(path):
    with open(path, newline="") as f:
        rows = {(r["group"], r["name"]): float(r["ns_per_op"])
                for r in csv.DictReader(f)}
    return rows

base, current = read(sys.argv[1]), read(sys.argv[2])
missing = sorted(set(base) - set(current))
if missing:
    raise SystemExit("missing benchmarks: " + ", ".join(map(str, missing)))

bad = []
for key, before in base.items():
    after = current[key]
    if not (math.isfinite(before) and math.isfinite(after) and before > 0 and after > 0):
        raise SystemExit(f"invalid measurement for {key}")
    ratio = after / before
    if ratio > limit:
        bad.append((ratio, key, before, after))

for ratio, key, before, after in sorted(bad, reverse=True):
    print(f"REGRESSION {key[0]} / {key[1]}: {before:.2f} -> {after:.2f} ns/op ({ratio:.2f}x)", file=sys.stderr)
if bad:
    raise SystemExit(f"{len(bad)} benchmark(s) exceed {limit:.2f}x slowdown budget")
print(f"OK: {len(base)} benchmarks within {limit:.2f}x slowdown budget")
