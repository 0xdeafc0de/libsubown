#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

SUB_ID="2002"
SUB_IP="10.99.1.44"
ITERS="260"
SLEEP_MS="20"

# Epoch scenario:
# box1(epoch1) claims first, box2(epoch2, simulates restart/new epoch) claims later -> epoch2 should dominate.
BOX0_CLAIMS="20"
BOX1_CLAIMS="100"

BASE_PORT=$((7800 + (RANDOM % 200)))
PUB0="tcp://127.0.0.1:${BASE_PORT}"
PUB1="tcp://127.0.0.1:$((BASE_PORT + 1))"

cleanup() {
  if [[ -n "${PID0:-}" ]]; then kill "$PID0" 2>/dev/null || true; fi
  if [[ -n "${PID1:-}" ]]; then kill "$PID1" 2>/dev/null || true; fi
}
trap cleanup EXIT

make clean >/dev/null
make sim-box WITH_ZMQ=1 >/dev/null

echo "Running epoch-restart scenario (sub=${SUB_ID}, ip=${SUB_IP})"

./sim/box_sim --box-id 0 --pub "$PUB0" --sub "$PUB1" \
  --subscriber-id "$SUB_ID" --ip "$SUB_IP" --owner-epoch 1 \
  --iterations "$ITERS" --sleep-ms "$SLEEP_MS" --claim-mode off --claim-at "$BOX0_CLAIMS" --emit-dup 4 \
  > sim/epoch_box1.log 2>&1 &
PID0=$!

sleep 0.1

./sim/box_sim --box-id 1 --pub "$PUB1" --sub "$PUB0" \
  --subscriber-id "$SUB_ID" --ip "$SUB_IP" --owner-epoch 2 \
  --iterations "$ITERS" --sleep-ms "$SLEEP_MS" --claim-mode off --claim-at "$BOX1_CLAIMS" --emit-dup 24 \
  > sim/epoch_box2.log 2>&1 &
PID1=$!

wait "$PID0"
wait "$PID1"

echo "Epoch scenario done. Key lines:"
grep -nE "traffic_seen:|final:" sim/epoch_box1.log sim/epoch_box2.log
