#!/bin/bash
# run_pilot.sh — Launch a 3-node local Bucks pilot network
#
# Each node gets its own data directory so their SQLite databases
# don't collide.  Node 1 mines; nodes 2 & 3 sync via P2P.
#
# Usage:
#   ./build.sh          # compile first
#   ./run_pilot.sh      # then run the pilot

set -e

BINARY="./build/bucks"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: Binary not found at $BINARY"
    echo "       Run ./build.sh first."
    exit 1
fi

echo "=== Starting Bucks 3-node pilot network ==="

# Kill any leftover nodes from a previous run so ports aren't stuck in TIME_WAIT
pkill -9 -f "build/bucks" 2>/dev/null || true
sleep 3

PILOT_DIR="/tmp/bucks_pilot"
mkdir -p "${PILOT_DIR}/node1"
mkdir -p "${PILOT_DIR}/node2"
mkdir -p "${PILOT_DIR}/node3"

# ---- Node 1: seed + miner ----
# p2p-port 8333, REST port 8080
# Connects to nodes 2 and 3 as peers
(
    cd "${PILOT_DIR}/node1"
    echo "[node1] Starting (miner, p2p=8333, rest=8080)..."
    "${OLDPWD}/${BINARY}" \
        --network=regtest \
        --datadir "${PILOT_DIR}/node1/data" \
        --server \
        --port 8080 \
        --p2p-port 8333 \
        --connect 127.0.0.1:8334 \
        --connect 127.0.0.1:8335 \
        --auto-mine
) &
PID1=$!
echo "Node 1 started (PID $PID1) — mining genesis block..."
sleep 4   # give node 1 time to mine genesis before peers connect

# ---- Node 2 ----
# p2p-port 8334, REST port 8081
(
    cd "${PILOT_DIR}/node2"
    echo "[node2] Starting (p2p=8334, rest=8081)..."
    "${OLDPWD}/${BINARY}" \
        --network=regtest \
        --datadir "${PILOT_DIR}/node2/data" \
        --server \
        --port 8081 \
        --p2p-port 8334 \
        --connect 127.0.0.1:8333
) &
PID2=$!
echo "Node 2 started (PID $PID2)"

# ---- Node 3 ----
# p2p-port 8335, REST port 8082
(
    cd "${PILOT_DIR}/node3"
    echo "[node3] Starting (p2p=8335, rest=8082)..."
    "${OLDPWD}/${BINARY}" \
        --network=regtest \
        --datadir "${PILOT_DIR}/node3/data" \
        --server \
        --port 8082 \
        --p2p-port 8335 \
        --connect 127.0.0.1:8333
) &
PID3=$!
echo "Node 3 started (PID $PID3)"

echo ""
echo "=== Pilot network running ==="
echo "  Node 1 (miner) — P2P :8333 | REST http://127.0.0.1:8080"
echo "  Node 2          — P2P :8334 | REST http://127.0.0.1:8081"
echo "  Node 3          — P2P :8335 | REST http://127.0.0.1:8082"
echo ""
echo "  Data dirs: ${PILOT_DIR}/node{1,2,3}"
echo ""
echo "  Press Ctrl+C to stop all nodes"

trap "echo 'Stopping pilot...'; kill -9 $PID1 $PID2 $PID3 2>/dev/null; echo 'Pilot stopped.'" EXIT INT TERM
wait
