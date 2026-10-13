#!/usr/bin/env bash
# Multi-node regtest soak: node 0 mines (~100 blocks/s on regtest) while nodes 1 and 2 follow. Node 2 is
# killed with SIGKILL and later restarted cleanly, node 0 is restarted at the end, then everything must
# agree on one tip. One miner only: two miners at this speed fork deeper than the 100-block automatic
# reorg limit because propagation (~70 blocks/s) is slower than mining; fork handling is covered by
# the P2P tests and the chainstate tests.
#
#   tools/soak.sh [path/to/bucks] [seconds]        default: build/bucks, 90 s
#
# Exit 0 only if every node ends on the same tip at a height of at least MIN_HEIGHT, no node
# crashed on its own, and no node logged a consensus failure.
set -u
BIN="${1:-build/bucks}"
SECS="${2:-90}"
MIN_HEIGHT="${MIN_HEIGHT:-200}"
[ -x "$BIN" ] || { echo "no binary at $BIN (build first)"; exit 2; }

DIR="$(mktemp -d "${TMPDIR:-/tmp}/bucks-soak.XXXXXX")"
BASE_API=18290
BASE_P2P=18390
PIDS=("" "" "")
fail=0

start_node() {   # index, extra args...
    local i=$1; shift
    local tokfile="$DIR/tok$i"
    [ -f "$tokfile" ] || printf 'soak-token-%s-0123456789abcdef0123456789abcdef' "$i" > "$tokfile"
    "$BIN" --network=regtest --server --datadir "$DIR/n$i" --web-root "$DIR" \
        --port $((BASE_API + i)) --p2p-port $((BASE_P2P + i)) --api-token-file "$tokfile" "$@" \
        >> "$DIR/n$i.log" 2>&1 &
    PIDS[$i]=$!
}
info() { curl -s --max-time 3 "http://127.0.0.1:$((BASE_API + $1))/api/blockchain/info"; }
field() { python3 -c "import sys,json; print(json.load(sys.stdin)['$1'])" 2>/dev/null; }
cleanup() { for p in "${PIDS[@]}"; do [ -n "$p" ] && kill "$p" 2>/dev/null; done; wait 2>/dev/null; }
trap cleanup EXIT

echo "soak dir: $DIR"
start_node 0 --auto-mine
start_node 1 --connect "127.0.0.1:$BASE_P2P"
start_node 2 --connect "127.0.0.1:$BASE_P2P"
sleep 2
# a full mesh, so a block reaches every node by two routes
echo "running ${SECS}s ..."

third=$((SECS / 3))
sleep "$third"
echo "kill -9 node 2"
kill -9 "${PIDS[2]}"; wait "${PIDS[2]}" 2>/dev/null
sleep 3
start_node 2 --connect "127.0.0.1:$BASE_P2P"
sleep "$third"
echo "clean restart of node 2"
kill -TERM "${PIDS[2]}"; wait "${PIDS[2]}" 2>/dev/null
sleep 2
start_node 2 --connect "127.0.0.1:$BASE_P2P"
sleep "$third"

echo "stopping miners, waiting for convergence"
kill -TERM "${PIDS[0]}"; wait "${PIDS[0]}" 2>/dev/null
start_node 0 --connect "127.0.0.1:$((BASE_P2P + 1))" --connect "127.0.0.1:$((BASE_P2P + 2))"
converged=0
for _ in $(seq 1 240); do   # regtest mines faster than peers sync (~70 blocks/s), so allow time
    t0=$(info 0 | field tipHash); t1=$(info 1 | field tipHash); t2=$(info 2 | field tipHash)
    if [ -n "$t0" ] && [ "$t0" = "$t1" ] && [ "$t1" = "$t2" ]; then converged=1; break; fi
    sleep 1
done

for i in 0 1 2; do
    echo "node $i: height $(info $i | field height) tip $(info $i | field tipHash | cut -c1-16)"
done
h=$(info 0 | field height)
[ "$converged" = 1 ] || { echo "FAIL: nodes did not converge on one tip"; fail=1; }
[ "${h:-0}" -ge "$MIN_HEIGHT" ] || { echo "FAIL: height ${h:-?} < $MIN_HEIGHT"; fail=1; }
for i in 0 1 2; do
    kill -0 "${PIDS[$i]}" 2>/dev/null || { echo "FAIL: node $i is not running at the end"; fail=1; }
    if grep -qiE "fatal|abort|sanitizer|segmentation|consensus failure|corrupt" "$DIR/n$i.log"; then
        echo "FAIL: node $i logged a fatal condition:"; grep -iE "fatal|abort|sanitizer|segmentation|consensus failure|corrupt" "$DIR/n$i.log" | head -3; fail=1
    fi
done
[ "$fail" = 0 ] && { echo "SOAK OK"; rm -rf "$DIR"; exit 0; }
echo "logs kept in $DIR"; exit 1
