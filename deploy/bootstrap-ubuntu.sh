#!/usr/bin/env bash
# bootstrap-ubuntu.sh: build Bucks from a pinned release and run it as a systemd service.
# Idempotent: re-run it with a new BUCKS_REF to upgrade; configuration it wrote before is kept.
#
# Builds as an unprivileged user, installs with `cmake --install` to /opt/bucks/releases/<commit>
# (/opt/bucks/current points at it, the previous release stays for rollback), creates the 'bucks'
# service user, /etc/bucks, the systemd units, the firewall rules and, for the console role, the
# nginx TLS site. Ubuntu 22.04 or 24.04, run as root:
#
#   sudo BUCKS_ROLE=seed    BUCKS_REF=<tag> BUCKS_COMMIT=<40-hex> ./bootstrap-ubuntu.sh
#   sudo BUCKS_ROLE=miner   BUCKS_REF=<tag> BUCKS_COMMIT=<40-hex> BUCKS_MINE_ADDRESS=<B...> ./bootstrap-ubuntu.sh
#   sudo BUCKS_ROLE=console BUCKS_REF=<tag> BUCKS_COMMIT=<40-hex> BUCKS_DOMAIN=console.example.org \
#        BUCKS_ADMIN_CIDR=203.0.113.4/32 ACME_EMAIL=ops@example.org ./bootstrap-ubuntu.sh
#
# Optional: BUCKS_NETWORK=main|test (default main), BUCKS_SEEDS="host:port ..." (other seeds/peers),
# BUCKS_REPO, BUCKS_JOBS (default: cores, at most one per 1.5 GB of RAM), BUCKS_RUN_TESTS=0 (skip the
# test suites, which also skips the googletest download), GTEST_SRC (a local googletest v1.14.0
# checkout when github.com is unreachable), SSH_PORT (default 22), BUCKS_START=1 (start or restart now).
#
# The P2P port is the only Bucks port opened in the firewall. The API listens on 127.0.0.1; the console
# role reaches it through nginx with TLS, an IP allowlist and basic auth.
# Mainnet stays disabled until the release has the real genesis: `bucks --network=main` then exits 3,
# and systemd does not restart it (RestartPreventExitStatus in bucks.service).
set -euo pipefail

BUCKS_REPO=${BUCKS_REPO:-https://github.com/infin8sync69-source/bucks-blocks.git}
BUCKS_REF=${BUCKS_REF:-}
BUCKS_COMMIT=${BUCKS_COMMIT:-}
BUCKS_ROLE=${BUCKS_ROLE:-seed}
BUCKS_NETWORK=${BUCKS_NETWORK:-main}
BUCKS_SEEDS=${BUCKS_SEEDS:-}
BUCKS_MINE_ADDRESS=${BUCKS_MINE_ADDRESS:-}
BUCKS_DOMAIN=${BUCKS_DOMAIN:-}
BUCKS_ADMIN_CIDR=${BUCKS_ADMIN_CIDR:-}
ACME_EMAIL=${ACME_EMAIL:-}
BUCKS_JOBS=${BUCKS_JOBS:-}
BUCKS_RUN_TESTS=${BUCKS_RUN_TESTS:-1}
BUCKS_START=${BUCKS_START:-0}
GTEST_SRC=${GTEST_SRC:-}
SSH_PORT=${SSH_PORT:-22}

SRC=/usr/local/src/bucks-blocks
BUILD=$SRC/build-release
PREFIX=/opt/bucks
DATADIR=/var/lib/bucks
ETC=/etc/bucks
BUILD_USER=bucksbuild
DEPLOY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

log() { printf '[bootstrap] %s\n' "$*"; }
die() { printf '[bootstrap] ERROR: %s\n' "$*" >&2; exit 1; }
as_builder() { runuser -u "$BUILD_USER" -- "$@"; }

# ---------------------------------------------------------------- 0. checks
[ "$(id -u)" -eq 0 ] || die "run as root"
# shellcheck source=/dev/null
. /etc/os-release
case "${VERSION_ID:-}" in 22.04|24.04) ;; *) die "only Ubuntu 22.04/24.04 are covered (found ${PRETTY_NAME:-unknown})" ;; esac
case "$BUCKS_ROLE" in seed|miner|console) ;; *) die "BUCKS_ROLE must be seed, miner or console" ;; esac
case "$BUCKS_NETWORK" in
    main) P2P_PORT=8733; API_PORT=8080 ;;
    test) P2P_PORT=18733; API_PORT=18080 ;;
    *) die "BUCKS_NETWORK must be main or test" ;;
esac
# GitHub's default branch does not hold the code; build only a named release (docs/LAUNCH.md).
[ -n "$BUCKS_REF" ] || die "set BUCKS_REF to the release tag (and BUCKS_COMMIT to its commit)"
if [ "$BUCKS_ROLE" = console ]; then
    [ -n "$BUCKS_DOMAIN" ] || die "the console role needs BUCKS_DOMAIN"
    [ -n "$BUCKS_ADMIN_CIDR" ] || die "the console role needs BUCKS_ADMIN_CIDR (operator addresses allowed to reach it)"
fi
if [ "$BUCKS_ROLE" = miner ] && [ ! -f "$ETC/bucks.env" ] && [ -z "$BUCKS_MINE_ADDRESS" ]; then
    die "the miner role needs BUCKS_MINE_ADDRESS: an address whose key is not on this server"
fi
for f in bucks.service roles/"$BUCKS_ROLE".env bucks-backup.sh bucks-backup.service bucks-backup.timer \
         bucks-healthcheck.sh bucks-healthcheck.service bucks-healthcheck.timer nginx-bucks-console.conf; do
    [ -f "$DEPLOY_DIR/$f" ] || die "missing $DEPLOY_DIR/$f"
done
cores=$(nproc)
if [ -z "$BUCKS_JOBS" ]; then
    # Compiling src/server.cpp alone takes about 1.2 GB.
    mem_mb=$(awk '/^MemAvailable:/ {print int($2 / 1024)}' /proc/meminfo)
    BUCKS_JOBS=$(( mem_mb / 1500 ))
    [ "$BUCKS_JOBS" -ge 1 ] || BUCKS_JOBS=1
    [ "$BUCKS_JOBS" -le "$cores" ] || BUCKS_JOBS=$cores
fi

# ---------------------------------------------------------------- 1. packages
export DEBIAN_FRONTEND=noninteractive
pkgs=(build-essential cmake git libssl-dev ca-certificates curl openssl python3 sqlite3 gnupg ufw)
[ "$BUCKS_ROLE" = console ] && pkgs+=(nginx certbot)
log "installing packages: ${pkgs[*]}"
apt-get update -q
apt-get install -y -q --no-install-recommends "${pkgs[@]}"

# Block timestamps are checked against the local clock (no more than 2 h ahead).
if [ "$(timedatectl show -p NTPSynchronized --value 2>/dev/null || echo no)" != yes ]; then
    log "WARNING: the clock is not NTP-synchronised. Enable systemd-timesyncd or chrony before mining."
fi

# ---------------------------------------------------------------- 2. users and directories
if ! id bucks >/dev/null 2>&1; then
    log "creating system user bucks"
    useradd --system --user-group --home-dir "$DATADIR" --no-create-home --shell /usr/sbin/nologin bucks
fi
if ! id "$BUILD_USER" >/dev/null 2>&1; then
    log "creating build user $BUILD_USER"
    useradd --system --user-group --home-dir /var/lib/"$BUILD_USER" --create-home --shell /usr/sbin/nologin "$BUILD_USER"
fi
install -d -o bucks -g bucks -m 0700 "$DATADIR"
install -d -o root -g bucks -m 0750 "$ETC"
install -d -o root -g root -m 0755 "$PREFIX" "$PREFIX/releases"
install -d -o "$BUILD_USER" -g "$BUILD_USER" -m 0755 "$SRC"

# ---------------------------------------------------------------- 3. source at the pinned release
if [ -d "$SRC/.git" ]; then
    log "fetching $BUCKS_REPO"
    as_builder git -C "$SRC" fetch --tags --prune --force origin '+refs/heads/*:refs/remotes/origin/*'
else
    log "cloning $BUCKS_REPO"
    as_builder git clone --no-checkout "$BUCKS_REPO" "$SRC"
fi
# A branch resolves through origin/ (the clone's own local branch may be stale), a tag or commit as is.
target=$(as_builder git -C "$SRC" rev-parse --verify --quiet "origin/$BUCKS_REF^{commit}" \
         || as_builder git -C "$SRC" rev-parse --verify --quiet "$BUCKS_REF^{commit}") \
    || die "ref $BUCKS_REF not found in $BUCKS_REPO"
if [ -n "$BUCKS_COMMIT" ] && [ "$target" != "$BUCKS_COMMIT" ]; then
    die "ref $BUCKS_REF is $target, expected BUCKS_COMMIT=$BUCKS_COMMIT"
fi
[ -n "$BUCKS_COMMIT" ] || log "WARNING: BUCKS_COMMIT not set; building whatever $BUCKS_REF points to ($target)"
as_builder git -C "$SRC" -c advice.detachedHead=false checkout --force --detach "$target"
as_builder git -C "$SRC" clean -ffdxq
COMMIT=$target
log "source at $COMMIT"

# ---------------------------------------------------------------- 4. build, test and install
REL="$PREFIX/releases/$COMMIT"
if [ -x "$REL/bin/bucks" ]; then
    log "release $COMMIT already installed; skipping the build"
else
    tests=OFF
    [ "$BUCKS_RUN_TESTS" = 1 ] && tests=ON
    cmake_args=(-S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUCKS_BUILD_TESTS="$tests")
    [ -n "$GTEST_SRC" ] && cmake_args+=("-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$GTEST_SRC")
    log "configuring (tests $tests)"
    as_builder cmake "${cmake_args[@]}"
    log "building with -j$BUCKS_JOBS"
    as_builder cmake --build "$BUILD" -j "$BUCKS_JOBS"
    if [ "$tests" = ON ]; then
        log "running the test suites"
        as_builder ctest --test-dir "$BUILD" --output-on-failure --timeout 900
    fi
    tmp=$(mktemp -d "$PREFIX/releases/.tmp.XXXXXX")
    cmake --install "$BUILD" --prefix "$tmp"
    "$BUILD/bucks-check-wordlists" "$tmp/share/bucks/wordlists" >/dev/null || die "installed word lists do not verify"
    printf '%s\n' "$COMMIT" > "$tmp/COMMIT"
    (cd "$tmp" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum) > "$tmp/SHA256SUMS"
    chown -R root:root "$tmp"
    chmod -R go-w "$tmp"
    chmod 0755 "$tmp"
    mv -T "$tmp" "$REL"
    log "installed $REL"
fi
previous=""
[ -L "$PREFIX/current" ] && previous=$(readlink -f "$PREFIX/current")
ln -sfn "$REL" "$PREFIX/current.new"
mv -T "$PREFIX/current.new" "$PREFIX/current"
if [ -n "$previous" ] && [ "$previous" != "$REL" ]; then
    log "previous release kept for rollback: $previous (ln -sfn it to $PREFIX/current, then restart)"
fi

# ---------------------------------------------------------------- 5. configuration (never overwritten)
# The node refuses a token file that anyone but its owner can read.
if [ ! -s "$ETC/api-token" ]; then
    log "generating $ETC/api-token"
    (umask 077; openssl rand -hex 32 > "$ETC/api-token")
fi
chown bucks:bucks "$ETC/api-token"
chmod 0600 "$ETC/api-token"
if [ ! -f "$ETC/bucks.env" ]; then
    log "writing $ETC/bucks.env for role $BUCKS_ROLE on $BUCKS_NETWORK"
    peers=""
    for s in $BUCKS_SEEDS; do peers="$peers --seed $s"; done
    threads=$(( cores > 1 ? cores - 1 : 1 ))
    sed -e "s/^BUCKS_NETWORK=.*/BUCKS_NETWORK=$BUCKS_NETWORK/" \
        -e "s/--p2p-port 8733/--p2p-port $P2P_PORT/" \
        -e "s/--port 8080/--port $API_PORT/" \
        -e "s/console\.example\.org/${BUCKS_DOMAIN:-console.example.org}/g" \
        -e "s/REPLACE_WITH_PAYOUT_ADDRESS/${BUCKS_MINE_ADDRESS:-REPLACE_WITH_PAYOUT_ADDRESS}/" \
        -e "s/--mine-threads 1/--mine-threads $threads/" \
        -e "s/^BUCKS_PEERS=.*/BUCKS_PEERS=\"${peers# }\"/" \
        "$DEPLOY_DIR/roles/$BUCKS_ROLE.env" > "$ETC/bucks.env"
    [ -n "$BUCKS_SEEDS" ] || log "no BUCKS_SEEDS given: add peers to BUCKS_PEERS in $ETC/bucks.env unless the release has seed nodes"
else
    log "$ETC/bucks.env exists; left unchanged"
fi
chown root:bucks "$ETC/bucks.env"
chmod 0640 "$ETC/bucks.env"

# ---------------------------------------------------------------- 6. systemd units and helper scripts
install -m 0644 "$DEPLOY_DIR/bucks.service" /etc/systemd/system/bucks.service
install -m 0755 "$DEPLOY_DIR/bucks-backup.sh" /usr/local/sbin/bucks-backup
install -m 0755 "$DEPLOY_DIR/bucks-healthcheck.sh" /usr/local/sbin/bucks-healthcheck
install -m 0644 "$DEPLOY_DIR/bucks-backup.service" "$DEPLOY_DIR/bucks-backup.timer" \
    "$DEPLOY_DIR/bucks-healthcheck.service" "$DEPLOY_DIR/bucks-healthcheck.timer" /etc/systemd/system/
install -d -m 0700 /var/backups/bucks
systemctl daemon-reload
systemctl enable bucks.service bucks-backup.timer bucks-healthcheck.timer >/dev/null
systemd-analyze verify /etc/systemd/system/bucks.service && log "bucks.service verifies"

# ---------------------------------------------------------------- 7. firewall
# Public: SSH (rate limited), P2P, and 80/443 on the console host. The API port is never opened: it
# speaks plain HTTP, and its token unlocks every wallet on the node. (ufw skips rules it already has.)
log "configuring ufw"
ufw default deny incoming >/dev/null
ufw default allow outgoing >/dev/null
ufw limit "$SSH_PORT/tcp" comment 'ssh' >/dev/null
ufw allow "$P2P_PORT/tcp" comment 'bucks p2p' >/dev/null
if [ "$BUCKS_ROLE" = console ]; then
    ufw allow 80/tcp comment 'acme + redirect' >/dev/null
    ufw allow 443/tcp comment 'bucks console (nginx)' >/dev/null
fi
ufw --force enable >/dev/null
if ufw status | grep -qE "^$API_PORT(/tcp)?[[:space:]]+ALLOW"; then
    log "WARNING: ufw allows the API port $API_PORT from outside (an earlier rule); remove it with: ufw delete allow $API_PORT/tcp"
fi
ufw status verbose

# ---------------------------------------------------------------- 8. nginx + TLS (console role)
if [ "$BUCKS_ROLE" = console ]; then
    site=/etc/nginx/sites-available/bucks-console.conf
    htpasswd=/etc/nginx/bucks-console.htpasswd
    cert="/etc/letsencrypt/live/$BUCKS_DOMAIN/fullchain.pem"
    install -d -m 0755 /var/www/letsencrypt
    rm -f /etc/nginx/sites-enabled/default

    if [ ! -s "$htpasswd" ]; then
        pass=$(openssl rand -base64 18)
        printf 'operator:%s\n' "$(openssl passwd -apr1 "$pass")" > "$htpasswd"
        chown root:www-data "$htpasswd"
        chmod 0640 "$htpasswd"
        (umask 077; printf 'console basic-auth user: operator\npassword: %s\n' "$pass" > /root/bucks-console-credentials)
        log "basic-auth password written to /root/bucks-console-credentials (0600)"
    fi

    if [ ! -s "$cert" ]; then
        [ -n "$ACME_EMAIL" ] || die "no certificate at $cert; set ACME_EMAIL to obtain one with certbot"
        log "obtaining a certificate for $BUCKS_DOMAIN (HTTP-01 via webroot)"
        cat > /etc/nginx/sites-available/bucks-acme.conf <<EOF
server {
    listen 80;
    listen [::]:80;
    server_name $BUCKS_DOMAIN;
    location ^~ /.well-known/acme-challenge/ { root /var/www/letsencrypt; default_type text/plain; }
    location / { return 404; }
}
EOF
        ln -sfn /etc/nginx/sites-available/bucks-acme.conf /etc/nginx/sites-enabled/bucks-acme.conf
        nginx -t && systemctl reload-or-restart nginx
        certbot certonly --webroot -w /var/www/letsencrypt -d "$BUCKS_DOMAIN" \
            --non-interactive --agree-tos -m "$ACME_EMAIL" \
            --deploy-hook 'systemctl reload nginx'
        rm -f /etc/nginx/sites-enabled/bucks-acme.conf
    fi

    # Regenerated on every run from the template; local edits belong in the template.
    sed -e "s/console\.example\.org/$BUCKS_DOMAIN/g" \
        -e "s#203\.0\.113\.4/32#$BUCKS_ADMIN_CIDR#" \
        -e "s/127\.0\.0\.1:8080/127.0.0.1:$API_PORT/" \
        "$DEPLOY_DIR/nginx-bucks-console.conf" > "$site.new"
    mv -f "$site.new" "$site"
    ln -sfn "$site" /etc/nginx/sites-enabled/bucks-console.conf
    nginx -t
    systemctl reload-or-restart nginx
fi

# ---------------------------------------------------------------- 9. start
if [ "$BUCKS_START" = 1 ]; then
    log "restarting bucks.service"
    systemctl restart bucks.service
    sleep 3
    systemctl --no-pager --lines=15 status bucks.service || true
else
    log "not started (BUCKS_START=1 starts it). Next: review $ETC/bucks.env, systemctl start bucks, journalctl -fu bucks"
fi
log "done: release $COMMIT, role $BUCKS_ROLE, network $BUCKS_NETWORK"
