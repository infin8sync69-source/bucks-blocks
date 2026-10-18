#!/usr/bin/env bash
# bucks-backup.sh: online backup of the wallet file and the chain database of a running node.
#
# Run as root (bucks-backup.timer). Everything that touches the datadir runs as the service user, so
# SQLite never leaves root-owned chain.sqlite-wal/-shm files behind (the node, running as 'bucks',
# could then no longer open its own database).
#
#   wallets.json  the node replaces it with rename(), so a copy is always one whole version. It holds
#                 PRIVATE KEYS IN PLAINTEXT: the archive is encrypted to BUCKS_BACKUP_GPG_RECIPIENT and
#                 the script refuses to write it unencrypted unless BUCKS_BACKUP_ALLOW_PLAINTEXT=1.
#   chain.sqlite  VACUUM INTO: one read transaction, a consistent snapshot while the node keeps writing
#                 (WAL mode). Checked with PRAGMA integrity_check before it is kept.
#
# Restore (node stopped; never copy chain.sqlite next to an old -wal/-shm pair):
#   systemctl stop bucks
#   gpg -d bucks-main-<stamp>.tar.gz.gpg | tar -xz -C /root/restore
#   rm -f /var/lib/bucks/main/chain.sqlite-wal /var/lib/bucks/main/chain.sqlite-shm
#   install -o bucks -g bucks -m 0600 /root/restore/chain.sqlite /var/lib/bucks/main/
#   install -o bucks -g bucks -m 0600 /root/restore/wallets.json /var/lib/bucks/main/   # if the node has wallets
#   systemctl start bucks      # add --checkdb once to BUCKS_ARGS for a deeper check
#
# Environment: BUCKS_NETWORK (default: from /etc/bucks/bucks.env), BUCKS_DATADIR, BUCKS_USER,
# BUCKS_BACKUP_DIR, BUCKS_BACKUP_STAGE, BUCKS_BACKUP_KEEP_DAYS, BUCKS_BACKUP_GPG_RECIPIENT,
# BUCKS_BACKUP_ALLOW_PLAINTEXT, BUCKS_BACKUP_SKIP_CHAIN, BUCKS_ENV_FILE.
set -euo pipefail

ENV_FILE=${BUCKS_ENV_FILE:-/etc/bucks/bucks.env}
if [ -z "${BUCKS_NETWORK:-}" ] && [ -r "$ENV_FILE" ]; then
    BUCKS_NETWORK=$(sed -n 's/^BUCKS_NETWORK=//p' "$ENV_FILE" | tail -n 1 | tr -d '"'"'"' ')
fi
NETWORK=${BUCKS_NETWORK:-main}
DATADIR=${BUCKS_DATADIR:-/var/lib/bucks}
RUN_AS=${BUCKS_USER:-bucks}
DEST=${BUCKS_BACKUP_DIR:-/var/backups/bucks}
STAGE=${BUCKS_BACKUP_STAGE:-/var/lib/bucks-backup-stage}
KEEP_DAYS=${BUCKS_BACKUP_KEEP_DAYS:-14}
RECIPIENT=${BUCKS_BACKUP_GPG_RECIPIENT:-}
ALLOW_PLAINTEXT=${BUCKS_BACKUP_ALLOW_PLAINTEXT:-0}
SKIP_CHAIN=${BUCKS_BACKUP_SKIP_CHAIN:-0}

log() { printf '[bucks-backup] %s\n' "$*"; }
die() { printf '[bucks-backup] ERROR: %s\n' "$*" >&2; exit 1; }
as_owner() { runuser -u "$RUN_AS" -- "$@"; }

case "$NETWORK" in main|test|regtest) ;; *) die "unknown network '$NETWORK'" ;; esac
[ "$(id -u)" -eq 0 ] || die "run as root"
command -v sqlite3 >/dev/null || die "sqlite3 CLI missing (apt-get install sqlite3)"
NETDIR="$DATADIR/$NETWORK"
[ -d "$NETDIR" ] || die "no $NETDIR"
if [ -z "$RECIPIENT" ] && [ "$ALLOW_PLAINTEXT" != 1 ]; then
    die "BUCKS_BACKUP_GPG_RECIPIENT is not set; refusing to write private keys unencrypted"
fi

umask 077
stamp=$(date -u +%Y%m%dT%H%M%SZ)
install -d -m 0700 "$DEST"
install -d -m 0700 -o "$RUN_AS" "$STAGE"
work="$STAGE/$NETWORK-$stamp"
as_owner mkdir -m 0700 "$work"
# Root's own copies go to a directory the service user cannot touch: cp would follow a symlink planted in $work.
rootwork=$(mktemp -d "$DEST/.stage.XXXXXX")
out=""
cleanup() {
    rm -rf -- "$work" "$rootwork"
    # A failed tar | gpg leaves the half-written archive behind.
    if [ -n "$out" ]; then rm -f -- "$out.part"; fi
}
trap cleanup EXIT

# 1. wallet file(s)
nwallets=0
for f in wallets.json wallets.json.bak; do
    if [ -f "$NETDIR/$f" ]; then
        as_owner cp -p "$NETDIR/$f" "$work/$f"
        python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$work/$f" || die "$f is not valid JSON"
        nwallets=$((nwallets + 1))
    fi
done
[ "$nwallets" -gt 0 ] || log "no wallets.json in $NETDIR (seed node?)"

# 2. chain database
if [ "$SKIP_CHAIN" != 1 ] && [ -f "$NETDIR/chain.sqlite" ]; then
    as_owner sqlite3 -bail "$NETDIR/chain.sqlite" "VACUUM INTO '$work/chain.sqlite'"
    check=$(as_owner sqlite3 -bail "$work/chain.sqlite" "PRAGMA integrity_check" | head -n 1)
    [ "$check" = ok ] || die "integrity_check of the copy: $check"
    height=$(as_owner sqlite3 "$work/chain.sqlite" "SELECT COUNT(*) - 1 FROM block_index" 2>/dev/null || echo '?')
    log "chain copy ok (block_index rows - 1 = $height)"
fi

# 3. configuration (the API token is a secret too; it only travels inside the encrypted archive)
for f in "$ENV_FILE" /etc/bucks/api-token; do
    [ -r "$f" ] && cp -p "$f" "$rootwork/$(basename "$f")"
done

# 4. pack, encrypt, checksum
base="$DEST/bucks-$NETWORK-$stamp.tar.gz"
if [ -n "$RECIPIENT" ]; then
    out="$base.gpg"
    tar -czf - -C "$work" . -C "$rootwork" . | gpg --batch --yes --quiet --trust-model always \
        --recipient "$RECIPIENT" --output "$out.part" --encrypt
else
    out="$base"
    log "WARNING: writing an UNENCRYPTED archive with private keys (BUCKS_BACKUP_ALLOW_PLAINTEXT=1)"
    tar -czf "$out.part" -C "$work" . -C "$rootwork" .
fi
mv -f "$out.part" "$out"
(cd "$DEST" && sha256sum "$(basename "$out")" > "$(basename "$out").sha256")
log "wrote $out ($(du -h "$out" | cut -f1))"

# 5. retention (copy $DEST off the machine as well: a backup on the same disk is not a backup)
find "$DEST" -maxdepth 1 -type f -name "bucks-$NETWORK-*.tar.gz*" -mtime +"$KEEP_DAYS" -print -delete
