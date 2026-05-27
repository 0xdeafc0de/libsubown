#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

BASE_PORT=$((8000 + (RANDOM % 200)))
PUB0="tcp://127.0.0.1:${BASE_PORT}"
PUB1="tcp://127.0.0.1:$((BASE_PORT + 1))"

cleanup() {
  if [[ -n "${PID0:-}" ]]; then kill "$PID0" 2>/dev/null || true; fi
  if [[ -n "${PID1:-}" ]]; then kill "$PID1" 2>/dev/null || true; fi
}
trap cleanup EXIT

make sim-box WITH_ZMQ=1 >/dev/null

# Box0 claims once and emits duplicates; Box1 only receives.
./sim/box_sim --box-id 0 --pub "$PUB0" --sub "$PUB1" \
  --subscriber-id 3001 --ip 10.1.1.10 --iterations 180 --sleep-ms 20 \
  --claim-mode off --claim-at 20 --emit-dup 8 \
  > sim/fault_dup_box0.log 2>&1 &
PID0=$!

sleep 0.1

./sim/box_sim --box-id 1 --pub "$PUB1" --sub "$PUB0" \
  --subscriber-id 3001 --ip 10.1.1.10 --iterations 180 --sleep-ms 20 \
  --claim-mode off \
  > sim/fault_dup_box1.log 2>&1 &
PID1=$!

wait "$PID0"
wait "$PID1"

# Assertions:
# - Box1 should show at least one APPLY_REMOTE and at least one duplicate NOOP.
grep -q "decision=0" sim/fault_dup_box1.log
grep -q "decision=2 reason=3" sim/fault_dup_box1.log
# - Final convergence owner should be box0 (owner=0).
grep -q "final: owner=0" sim/fault_dup_box0.log
grep -q "final: owner=0" sim/fault_dup_box1.log

echo "PASS: duplicate fault IT assertions"
