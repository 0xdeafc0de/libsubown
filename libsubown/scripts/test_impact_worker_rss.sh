#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

RATE="${1:-100000}"

make bench-impact >/dev/null
./bench/bench_worker_rss_impact "$RATE"
