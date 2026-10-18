#!/usr/bin/env bash
# bucks-healthcheck.sh: one status line; exit 0 OK, 1 WARNING, 2 CRITICAL (Nagios/Icinga style).
#
# Checks: service active; API answering on loopback (if the role runs --server); network name; peers;
# fork alert; tip age; miner running (if --auto-mine); free disk on the datadir. Without an API
# (P2P-only seed) it probes the P2P port instead.
#
# Environment (defaults from /etc/bucks/bucks.env where possible):
#   BUCKS_HEALTH_API            http://127.0.0.1:<port>   (from --port in BUCKS_ARGS, else 8080/18080)
#   BUCKS_HEALTH_P2P_PORT       from --p2p-port, else 8733/18733
#   BUCKS_HEALTH_MAX_TIP_AGE    7200 s warning (target block time is 600 s); critical at 3x
#   BUCKS_HEALTH_MIN_PEERS      1
#   BUCKS_HEALTH_MIN_FREE_MB    2048
#   BUCKS_HEALTH_UNIT           bucks.service ("" skips the systemd check)
set -u

ENV_FILE=${BUCKS_ENV_FILE:-/etc/bucks/bucks.env}
NET=${BUCKS_NETWORK:-}
ARGS=${BUCKS_ARGS:-}
if [ -r "$ENV_FILE" ]; then
    [ -n "$NET" ] || NET=$(sed -n 's/^BUCKS_NETWORK=//p' "$ENV_FILE" | tail -n 1 | tr -d '"'"'"' ')
    [ -n "$ARGS" ] || ARGS=$(sed -n 's/^BUCKS_ARGS=//p' "$ENV_FILE" | tail -n 1 | tr -d '"'"'"'')
fi
NET=${NET:-main}
case "$NET" in main) dp2p=8733; dapi=8080 ;; test) dp2p=18733; dapi=18080 ;; *) dp2p=18744; dapi=18090 ;; esac

flag_value() {   # flag_value --port  -> value following the flag in ARGS
    local want=$1 prev="" a
    for a in $ARGS; do
        if [ "$prev" = "$want" ]; then printf '%s' "$a"; return 0; fi
        prev=$a
    done
    return 1
}
has_flag() { case " $ARGS " in *" $1 "*) return 0 ;; esac; return 1; }

api_port=$(flag_value --port || echo "$dapi")
API=${BUCKS_HEALTH_API:-http://127.0.0.1:$api_port}
P2P_PORT=${BUCKS_HEALTH_P2P_PORT:-$(flag_value --p2p-port || echo "$dp2p")}
MAX_TIP_AGE=${BUCKS_HEALTH_MAX_TIP_AGE:-7200}
MIN_PEERS=${BUCKS_HEALTH_MIN_PEERS:-1}
MIN_FREE_MB=${BUCKS_HEALTH_MIN_FREE_MB:-2048}
UNIT=${BUCKS_HEALTH_UNIT-bucks.service}
DATADIR=${BUCKS_DATADIR:-/var/lib/bucks}

state=0
msgs=()
warn() { msgs+=("WARN: $*"); [ "$state" -lt 1 ] && state=1; }
crit() { msgs+=("CRIT: $*"); state=2; }
finish() {
    local label=OK
    [ "$state" -eq 1 ] && label=WARNING
    [ "$state" -eq 2 ] && label=CRITICAL
    printf 'BUCKS %s - %s\n' "$label" "${msgs[*]:-all checks passed}"
    exit "$state"
}

# 1. service
if [ -n "$UNIT" ] && command -v systemctl >/dev/null; then
    systemctl is-active --quiet "$UNIT" || { crit "$UNIT is not active"; finish; }
fi

# 2. disk
if [ -d "$DATADIR" ]; then
    free_mb=$(df -Pm "$DATADIR" | awk 'NR==2 {print $4}')
    [ -n "$free_mb" ] && [ "$free_mb" -lt "$MIN_FREE_MB" ] && warn "only ${free_mb} MB free on $DATADIR"
fi

# 3. API or P2P
if has_flag --server || has_flag --auto-mine; then
    json=$(python3 - "$API" "$NET" "$MAX_TIP_AGE" "$MIN_PEERS" "$(has_flag --auto-mine && echo 1 || echo 0)" <<'PY'
import json, sys, time, urllib.request
api, net, max_age, min_peers, expect_mining = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5] == "1"
out = {"crit": [], "warn": [], "info": []}
def get(path):
    with urllib.request.urlopen(api + path, timeout=5) as r:
        return json.load(r)
try:
    st = get("/api/status")
    info = get("/api/blockchain/info")
except Exception as e:
    out["crit"].append("API %s not answering (%s)" % (api, e))
    print(json.dumps(out)); sys.exit(0)
if info.get("network") != net:
    out["crit"].append("node runs network %r, expected %r" % (info.get("network"), net))
h = int(info.get("height", 0))
out["info"].append("height=%d tip=%s peers=%s" % (h, str(info.get("tipHash", ""))[:16], st.get("peerCount")))
if int(st.get("peerCount", 0)) < min_peers:
    out["warn"].append("%s peers (< %d)" % (st.get("peerCount"), min_peers))
if info.get("forkAlert"):
    out["crit"].append("fork alert at height %s (other tip %s): operator decision needed (POST /api/node/approve-reorg)"
                       % (info.get("forkHeight"), str(info.get("forkOtherTip", ""))[:16]))
if not info.get("isValid", True):
    out["crit"].append("chain reports isValid=false")
try:
    blk = get("/api/blocks/height/%d" % h)
    ts = int(blk.get("block", blk).get("timestamp", 0))
    age = int(time.time()) - ts
    if h > 0 and age > 3 * max_age:
        out["crit"].append("tip is %d s old" % age)
    elif h > 0 and age > max_age:
        out["warn"].append("tip is %d s old" % age)
except Exception as e:
    out["warn"].append("cannot read the tip block (%s)" % e)
if expect_mining:
    try:
        m = get("/api/mining/status")
        if not m.get("mining"):
            out["crit"].append("--auto-mine is set but the miner is not running (journalctl -u bucks)")
        else:
            out["info"].append("hashrate=%s H/s found=%s" % (m.get("hashRate"), m.get("blocksFound")))
            if m.get("blockedReason"):
                out["warn"].append("mining is held back: %s" % m.get("blockedReason"))
    except Exception as e:
        out["warn"].append("cannot read mining status (%s)" % e)
print(json.dumps(out))
PY
    )
    while IFS=$'\t' read -r level text; do
        case "$level" in
            crit) crit "$text" ;;
            warn) warn "$text" ;;
            info) msgs+=("$text") ;;
        esac
    done < <(printf '%s' "$json" | python3 -c '
import json, sys
d = json.load(sys.stdin)
for level in ("crit", "warn", "info"):
    for text in d.get(level, []):
        print(level + "\t" + text)
')
else
    if timeout 3 bash -c "exec 3<>/dev/tcp/127.0.0.1/$P2P_PORT" 2>/dev/null; then
        msgs+=("P2P port $P2P_PORT accepting")
    else
        crit "P2P port $P2P_PORT not accepting connections"
    fi
fi
finish
