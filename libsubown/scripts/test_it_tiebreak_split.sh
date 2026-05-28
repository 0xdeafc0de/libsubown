#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

./scripts/run_it_2box.sh split >/tmp/subown_split_run.out 2>&1 || {
  cat /tmp/subown_split_run.out
  exit 1
}

# Both claimed once at same generation; tie-break should favor lower box id (0).
grep -q "final: owner=0" sim/box0.log
grep -q "final: owner=0" sim/box1.log

echo "PASS: split tie-break IT assertions"
