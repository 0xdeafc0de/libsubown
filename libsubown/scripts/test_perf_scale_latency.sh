#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

REQS="${1:-50000}"
MIN_RPS="4000"
MAX_P99_MS="100"

make bench >/dev/null
OUT="$(./bench/bench_subown "$REQS")"

echo "$OUT"

RPS="$(echo "$OUT" | awk -F= '/^throughput_rps=/{print $2}')"
P99="$(echo "$OUT" | awk -F= '/^latency_p99_ms=/{print $2}')"

awk -v rps="$RPS" -v min="$MIN_RPS" 'BEGIN{ if (rps+0 < min+0) exit 1; }'
awk -v p99="$P99" -v max="$MAX_P99_MS" 'BEGIN{ if (p99+0 >= max+0) exit 1; }'

echo "PASS: perf gate met (rps >= ${MIN_RPS}, p99 < ${MAX_P99_MS}ms)"
