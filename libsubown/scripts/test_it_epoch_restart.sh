#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

./scripts/run_it_epoch_restart.sh >/tmp/subown_epoch_run.out 2>&1 || {
  cat /tmp/subown_epoch_run.out
  exit 1
}

B1="sim/epoch_box1.log"
B2="sim/epoch_box2.log"

# Assert initial box1 claim then box2 claim at higher epoch.
grep -q "traffic_seen: iter=20 .* owner=0" "$B1"
grep -q "traffic_seen: iter=100 .* owner=1" "$B2"

# Validate epoch dominance signals:
# - box2 (epoch2) claims
# - box1 sees remote owner=1 at least once
# - both nodes end at epoch=2
grep -q "recv: in_owner=1 in_gen=1" "$B1"
grep -q "final: .* epoch=2" "$B1"
grep -q "final: .* epoch=2" "$B2"

echo "PASS: epoch-restart IT assertions"
