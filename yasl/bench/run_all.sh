#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
filter=""
out="$root/bench-results"
jobs="$(nproc)"

while getopts "f:o:j:" opt; do
  case "$opt" in
    f) filter="$OPTARG" ;;
    o) out="$OPTARG" ;;
    j) jobs="$OPTARG" ;;
    *) echo "usage: $0 [-f filter] [-o out_dir] [-j jobs]" >&2; exit 2 ;;
  esac
done

mkdir -p "$out"

variants=(libc)
tc_lib="$root/third_party/tcmalloc_build/lib/libtcmalloc_google.a"
if [ -f "$tc_lib" ]; then
  variants+=(tcmalloc)
else
  echo "skip tcmalloc: $tc_lib not found (run third_party/tcmalloc_build/build.sh)" >&2
fi

failed=0
for v in "${variants[@]}"; do
  bdir="$root/build-bench-$v"
  echo "== build $v"
  cmake -S "$root" -B "$bdir" -DCMAKE_BUILD_TYPE=Release -DYASL_BUILD_BENCH=ON -DYASL_MALLOC="$v" > /dev/null
  cmake --build "$bdir" --target yasl_bench -j "$jobs" > /dev/null
done

for v in "${variants[@]}"; do
  bin="$root/build-bench-$v/yasl_bench"
  echo "== run $v"
  if [ -n "$filter" ]; then
    args=(--csv "$filter")
  else
    args=(--csv)
  fi
  if ! "$bin" "${args[@]}" > "$out/$v.csv"; then
    echo "run failed: $v" >&2
    failed=1
  fi
done

python3 -I - "$out" "${variants[@]}" <<'PY' || failed=1
import math, pathlib, re, sys

out = pathlib.Path(sys.argv[1])
variants = sys.argv[2:]
header = '"group","name","ns_per_op"'
pat = re.compile(r'^"(.*)","(.*)",([0-9]+(?:\.[0-9]+)?)$')
errors = []
data = {}

for v in variants:
    path = out / f"{v}.csv"
    if not path.exists():
        errors.append(f"{v}: no output file")
        continue
    lines = path.read_text().splitlines()
    if not lines or lines[0] != header:
        errors.append(f"{v}: bad header")
    rows = {}
    for n, line in enumerate(lines[1:], 2):
        m = pat.match(line)
        if not m:
            errors.append(f"{v}:{n}: unparsable line: {line!r}")
            continue
        key = (m.group(1), m.group(2))
        ns = float(m.group(3))
        if not math.isfinite(ns) or ns <= 0:
            errors.append(f"{v}:{n}: bad value {ns}")
        if key in rows:
            errors.append(f"{v}:{n}: duplicate {key}")
        rows[key] = ns
    if not rows:
        errors.append(f"{v}: no results")
    data[v] = rows

present = list(data)
if len(present) > 1 and any(set(data[v]) != set(data[present[0]]) for v in present):
    errors.append("variants do not report the same benchmarks")

order = []
for rows in data.values():
    for key in rows:
        if key not in order:
            order.append(key)

lines = [
    "| Группа | Вариант | " + " | ".join(f"{v}, ns/op" for v in present) + " |",
    "|---|---|" + "---:|" * len(present),
]
last = None
for grp, name in order:
    cells = [f"{data[v][(grp, name)]:.1f}" if (grp, name) in data[v] else "—" for v in present]
    lines.append(f"| {grp if grp != last else ''} | {name} | " + " | ".join(cells) + " |")
    last = grp
table = "\n".join(lines) + f"\n\nВсего бенчмарков: {len(order)}\n"

(out / "summary.md").write_text(table)
print(table)

if errors:
    print("CHECK FAILED:", file=sys.stderr)
    for e in errors:
        print(f"  {e}", file=sys.stderr)
    sys.exit(1)
print(f"CHECK OK: {len(present)} variant(s), {len(order)} benchmarks, results in {out}")
PY

exit "$failed"
