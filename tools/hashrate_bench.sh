#!/usr/bin/env bash
# How fast can this machine hash, and how long would a block take at each candidate launch limit?
#
#   tools/hashrate_bench.sh [path/to/bucks-mine-genesis] [threads] [seconds]
#
# Defaults: build/bucks-mine-genesis, every core, 40 seconds. It runs the genesis miner (the same SHA256d
# search with a midstate that the node's miner uses) for a fixed time, reads its last progress line, and prints
# the average seconds per block for a few powLimitBits values. Run it on the machine that will mine, with
# nothing else busy. It writes nothing and needs no running node.
#
# The limit (powLimitBits) is frozen by the genesis and is the easiest the chain can ever be (docs/MAINNET_INPUTS.md).

set -euo pipefail

TOOL="${1:-build/bucks-mine-genesis}"
THREADS="${2:-$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1) )}"
SECONDS_TO_RUN="${3:-40}"

if [ ! -x "$TOOL" ]; then
    echo "error: $TOOL is not an executable (build first, or pass the path)" >&2
    exit 2
fi

LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

echo "Hashing with $THREADS thread(s) for ${SECONDS_TO_RUN}s (nothing is mined or saved)..."
# timeout ends the search; its exit status (124) is expected.
timeout "$SECONDS_TO_RUN" "$TOOL" --network=main --time="$(date +%s)" \
    --inscription="hashrate benchmark" --threads="$THREADS" 2> "$LOG" >/dev/null || true

RATE="$(grep -o '[0-9.]* MH/s' "$LOG" | tail -n 1 | cut -d' ' -f1 || true)"
if [ -z "$RATE" ]; then
    echo "error: no rate was printed; run longer (third argument) or check: $TOOL --help" >&2
    exit 1
fi

echo "Measured: $RATE MH/s in total ($THREADS thread(s))"
echo
# hashes per block is about 2^256 / target; for 0xNN00ffff the target is about 2^(8 * (NN - 3) + 16).
printf '%-13s %-18s %s\n' "powLimitBits" "hashes per block" "seconds per block at this rate"
for bits in 1c00ffff 1d00ffff 1e00ffff; do
    exponent=$((256 - 8 * (0x${bits:0:2} - 3) - 16))
    awk -v bits="$bits" -v exponent="$exponent" -v rate="$RATE" 'BEGIN {
        printf "0x%-11s 2^%-16d %.0f\n", bits, exponent, (2 ^ exponent) / (rate * 1e6)
    }'
done
echo
echo "10-minute blocks (600 s) need as many MH/s as the 'hashes per block' column divided by 600e6."
echo "With ASERT the difficulty rises by itself when blocks come faster than 600 s, but never falls below the limit."
