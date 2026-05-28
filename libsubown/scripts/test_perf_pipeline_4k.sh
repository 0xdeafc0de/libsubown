#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

make bench-pipeline >/dev/null
OUT="$(./bench/bench_pipeline_4k)"

echo "$OUT"

# Gate: all cases must sustain >= 4000 rps and p99 < 100ms
# Parse each throughput/p99 line.
FAIL=0
while IFS= read -r line; do
  if [[ "$line" == throughput_rps=* ]]; then
    RPS="$(echo "$line" | awk '{for(i=1;i<=NF;i++) if($i ~ /^throughput_rps=/){split($i,a,"="); print a[2]}}')"
    P99="$(echo "$line" | awk '{for(i=1;i<=NF;i++) if($i ~ /^p99_ms=/){split($i,a,"="); print a[2]}}')"
    awk -v rps="$RPS" 'BEGIN{ if (rps+0 < 4000) exit 1; }' || FAIL=1
    awk -v p99="$P99" 'BEGIN{ if (p99+0 >= 100.0) exit 1; }' || FAIL=1
  fi
done <<< "$OUT"

if [[ "$FAIL" -ne 0 ]]; then
  echo "FAIL: one or more pipeline cases missed perf targets"
  exit 1
fi

echo "PASS: all pipeline cases meet >=4k rps and p99 < 100ms"
