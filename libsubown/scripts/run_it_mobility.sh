#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

SUB_ID="1001"
SUB_IP="10.10.20.55"
ITERS="220"
SLEEP_MS="20"

# Mobility timeline:
# 1) subscriber synced to both boxes at startup
# 2) traffic seen at box1 (sim box-id=0) at iter 20
# 3) traffic moves to box2 (sim box-id=1) at iter 100
# 4) traffic moves back to box1 at iter 170
BOX0_CLAIMS="20,170"
BOX1_CLAIMS="100"

BASE_PORT=$((7500 + (RANDOM % 300)))
PUB0="tcp://127.0.0.1:${BASE_PORT}"
PUB1="tcp://127.0.0.1:$((BASE_PORT + 1))"

cleanup() {
  if [[ -n "${PID0:-}" ]]; then kill "$PID0" 2>/dev/null || true; fi
  if [[ -n "${PID1:-}" ]]; then kill "$PID1" 2>/dev/null || true; fi
}
trap cleanup EXIT

make clean >/dev/null
make sim-box WITH_ZMQ=1 >/dev/null

echo "Running mobility scenario (ports ${BASE_PORT}/${BASE_PORT}+1, sub=${SUB_ID}, ip=${SUB_IP})"

./sim/box_sim --box-id 0 --pub "$PUB0" --sub "$PUB1" \
  --subscriber-id "$SUB_ID" --ip "$SUB_IP" --iterations "$ITERS" --sleep-ms "$SLEEP_MS" \
  --claim-mode off --claim-at "$BOX0_CLAIMS" \
  > sim/mobility_box1.log 2>&1 &
PID0=$!

sleep 0.1

./sim/box_sim --box-id 1 --pub "$PUB1" --sub "$PUB0" \
  --subscriber-id "$SUB_ID" --ip "$SUB_IP" --iterations "$ITERS" --sleep-ms "$SLEEP_MS" \
  --claim-mode off --claim-at "$BOX1_CLAIMS" \
  > sim/mobility_box2.log 2>&1 &
PID1=$!

wait "$PID0"
wait "$PID1"

echo "Mobility simulation done. Logs:"
echo "  $ROOT_DIR/sim/mobility_box1.log"
echo "  $ROOT_DIR/sim/mobility_box2.log"

echo "Key ownership events:"
if command -v rg >/dev/null 2>&1; then
  rg -n "sync:|traffic_seen:|final:" sim/mobility_box1.log sim/mobility_box2.log
else
  grep -nE "sync:|traffic_seen:|final:" sim/mobility_box1.log sim/mobility_box2.log
fi
