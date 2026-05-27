#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

BASE_PORT=$((8200 + (RANDOM % 200)))
PUB0="tcp://127.0.0.1:${BASE_PORT}"
PUB1="tcp://127.0.0.1:$((BASE_PORT + 1))"

cleanup() {
  if [[ -n "${PID0:-}" ]]; then kill "$PID0" 2>/dev/null || true; fi
  if [[ -n "${PID1:-}" ]]; then kill "$PID1" 2>/dev/null || true; fi
}
trap cleanup EXIT

make sim-box WITH_ZMQ=1 >/dev/null

# Mobility-like timeline with inbound drop fault on box2.
./sim/box_sim --box-id 0 --pub "$PUB0" --sub "$PUB1" \
  --subscriber-id 3002 --ip 10.1.1.20 --iterations 220 --sleep-ms 20 \
  --claim-mode off --claim-at 20,170 --emit-dup 6 \
  > sim/fault_drop_box1.log 2>&1 &
PID0=$!

sleep 0.1

./sim/box_sim --box-id 1 --pub "$PUB1" --sub "$PUB0" \
  --subscriber-id 3002 --ip 10.1.1.20 --iterations 220 --sleep-ms 20 \
  --claim-mode off --claim-at 100 --emit-dup 6 --drop-incoming-every 3 \
  > sim/fault_drop_box2.log 2>&1 &
PID1=$!

wait "$PID0"
wait "$PID1"

# Assertions:
# - Drop fault must be observed on box2.
grep -q "recv_drop_fault:" sim/fault_drop_box2.log
# - Despite drops, both should converge to final owner box1(owner=0) after reclaim at iter 170.
grep -q "final: owner=0" sim/fault_drop_box1.log
grep -q "final: owner=0" sim/fault_drop_box2.log

echo "PASS: drop fault IT assertions"
