#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

./scripts/run_it_mobility.sh >/tmp/subown_mobility_run.out 2>&1 || {
  cat /tmp/subown_mobility_run.out
  exit 1
}

B1="sim/mobility_box1.log"
B2="sim/mobility_box2.log"

# Assert expected timeline events.
grep -q "traffic_seen: iter=20 .* owner=0" "$B1"
grep -q "traffic_seen: iter=100 .* owner=1" "$B2"
grep -q "traffic_seen: iter=170 .* owner=0" "$B1"

# Assert final convergence to box1(owner=0) with generation 3.
grep -q "final: owner=0 gen=3" "$B1"
grep -q "final: owner=0 gen=3" "$B2"

echo "PASS: mobility IT assertions"
