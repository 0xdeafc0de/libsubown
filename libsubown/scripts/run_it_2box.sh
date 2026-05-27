#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

PROFILE="${1:-stable}"
SUB_ID="1001"
ITERS="200"
SLEEP_MS="20"
BOX0_MODE="once"
BOX1_MODE="off"
BOX0_CLAIM_EVERY="20"
BOX1_CLAIM_EVERY="20"

case "$PROFILE" in
  stable)
    BOX0_MODE="once"
    BOX1_MODE="off"
    ;;
  chaos)
    BOX0_MODE="periodic"
    BOX1_MODE="periodic"
    BOX0_CLAIM_EVERY="20"
    BOX1_CLAIM_EVERY="20"
    ;;
  split)
    BOX0_MODE="once"
    BOX1_MODE="once"
    ;;
  *)
    echo "Unknown profile: $PROFILE"
    echo "Usage: $0 [stable|chaos|split]"
    exit 2
    ;;
esac

# Use unique ports per run to avoid bind collisions from previous sessions.
BASE_PORT=$((7100 + (RANDOM % 400)))
PUB0="tcp://127.0.0.1:${BASE_PORT}"
PUB1="tcp://127.0.0.1:$((BASE_PORT + 1))"

cleanup() {
  if [[ -n "${PID0:-}" ]]; then kill "$PID0" 2>/dev/null || true; fi
  if [[ -n "${PID1:-}" ]]; then kill "$PID1" 2>/dev/null || true; fi
}
trap cleanup EXIT

make clean >/dev/null
make sim-box WITH_ZMQ=1 >/dev/null

echo "Starting box0 and box1 simulator processes (profile=$PROFILE, ports=${BASE_PORT}/${BASE_PORT}+1)..."

./sim/box_sim --box-id 0 --pub "$PUB0" --sub "$PUB1" \
  --subscriber-id "$SUB_ID" --iterations "$ITERS" --sleep-ms "$SLEEP_MS" \
  --claim-mode "$BOX0_MODE" --claim-every "$BOX0_CLAIM_EVERY" \
  > sim/box0.log 2>&1 &
PID0=$!

# Small stagger improves startup reliability for bind/connect ordering.
sleep 0.1

./sim/box_sim --box-id 1 --pub "$PUB1" --sub "$PUB0" \
  --subscriber-id "$SUB_ID" --iterations "$ITERS" --sleep-ms "$SLEEP_MS" \
  --claim-mode "$BOX1_MODE" --claim-every "$BOX1_CLAIM_EVERY" \
  > sim/box1.log 2>&1 &
PID1=$!

wait "$PID0"
wait "$PID1"

echo "Simulation done. Logs:"
echo "  $ROOT_DIR/sim/box0.log"
echo "  $ROOT_DIR/sim/box1.log"

echo "Final lines:"
tail -n 5 sim/box0.log
tail -n 5 sim/box1.log
