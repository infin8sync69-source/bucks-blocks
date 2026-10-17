# Running a Bucks node on an Ubuntu server

Files in this directory, used by `bootstrap-ubuntu.sh`:

| File | Installed as | What it does |
|---|---|---|
| `bootstrap-ubuntu.sh` | (run once per server and per upgrade) | Builds a pinned release as an unprivileged user, installs it, writes the configuration, units, firewall rules and (console role) the nginx site |
| `bucks.service` | `/etc/systemd/system/bucks.service` | The node, as user `bucks`, sandboxed, logs to the journal |
| `roles/seed.env`, `roles/miner.env`, `roles/console.env` | `/etc/bucks/bucks.env` (one of them) | The node's flags for that role |
| `nginx-bucks-console.conf` | `/etc/nginx/sites-available/bucks-console.conf` | TLS, IP allowlist and basic auth in front of the operator console |
| `bucks-healthcheck.sh` + `.service` + `.timer` | `/usr/local/sbin/bucks-healthcheck`, every 5 minutes | One status line; the unit fails on CRITICAL (service down, API silent, wrong network, fork alert, stale tip, miner stopped) |
| `bucks-backup.sh` + `.service` + `.timer` | `/usr/local/sbin/bucks-backup`, daily | Encrypted copy of `wallets.json`, a consistent `chain.sqlite` snapshot and the configuration |

## Layout on the server

```
/opt/bucks/releases/<commit>/        cmake --install prefix: bin/bucks, bin/bucks-mine-genesis,
                                     share/bucks/web, share/bucks/wordlists, COMMIT, SHA256SUMS
/opt/bucks/current -> releases/<commit>
/var/lib/bucks/                      datadir (0700 bucks): <network>/chain.sqlite, <network>/wallets.json
/etc/bucks/bucks.env                 BUCKS_NETWORK, BUCKS_ARGS, BUCKS_PEERS (never overwritten)
/etc/bucks/api-token                 0600 bucks: the API token (the node refuses a file others can read)
/usr/local/src/bucks-blocks          the build tree (owned by the build user bucksbuild)
```

The unit passes `--web-root` and `--wordlist-dir` explicitly, so the node always uses the files of the
release it was installed with, never the build tree.

## Roles

| Role | Public ports | Holds keys | Notes |
|---|---|---|---|
| `seed` | P2P (8733 main, 18733 test) | no | Relays blocks and transactions. Its `host:8733` goes into the release's seed list or other operators' `--seed`. |
| `miner` | P2P | no | `--auto-mine --mine-address <address> --mine-threads <cores - 1>`. The payout address belongs to a wallet made on **your own computer**; its key never sits on the server. |
| `console` | P2P, 80 and 443 (nginx) | yes, every wallet created in the console | The operator console behind nginx with TLS, an IP allowlist and basic auth. See the security model below. |

The API port (8080 main, 18080 test) is never opened in the firewall. Every role binds it to 127.0.0.1;
the console reaches it only through nginx, with `--api-allowed-host <name>`, `--trust-proxy` and
`--api-secure-cookie`.

## Security model of the console

The web console is a **single-operator** tool. One API token (`/etc/bucks/api-token`) unlocks every
wallet on that node, and `wallets.json` stores their private keys unencrypted. It is not a wallet service
for other people: do not give anyone the token, and do not create wallets for users on your server.
Users who want BUCKS run their own node on their own computer (README.md) and keep their own phrase.

## Install

```bash
git clone https://github.com/infin8sync69-source/bucks-blocks.git && cd bucks-blocks
git checkout <release tag>            # the deploy files must come from the release you install
sudo BUCKS_ROLE=seed BUCKS_REF=<release tag> BUCKS_COMMIT=<its commit> deploy/bootstrap-ubuntu.sh
sudo systemctl start bucks && journalctl -fu bucks
```

Miner: add `BUCKS_MINE_ADDRESS=<your B... address>`. Console: add `BUCKS_DOMAIN=console.example.org
BUCKS_ADMIN_CIDR=<your IP>/32 ACME_EMAIL=<you>`; the basic-auth password is written to
`/root/bucks-console-credentials` and the login token is `sudo cat /etc/bucks/api-token`.
More peers: `BUCKS_SEEDS="seed1.example.org:8733 seed2.example.org:8733"`. A testnet node:
`BUCKS_NETWORK=test`. Without github.com access during the build: `BUCKS_RUN_TESTS=0` (no googletest
download) or `GTEST_SRC=<googletest v1.14.0 checkout>`.

The build needs about 1.2 GB of RAM per compile job (the script picks the job count from free memory)
and builds for the Ubuntu release it runs on; a binary built on 24.04 does not run on 22.04.

## Upgrade and rollback

Run the bootstrap again with the new `BUCKS_REF`/`BUCKS_COMMIT` and `BUCKS_START=1`. The previous
release stays in `/opt/bucks/releases/`; to roll back, point `/opt/bucks/current` at it
(`ln -sfn /opt/bucks/releases/<old commit> /opt/bucks/current`) and `systemctl restart bucks`.

## Backups

`bucks-backup.timer` is enabled but fails until you name a GnuPG recipient whose **private key is not on
this server**: import the public key into root's keyring and put `BUCKS_BACKUP_GPG_RECIPIENT=<key id>` in
`/etc/bucks/backup.env`. Copy `/var/backups/bucks` off the machine as well. Restore steps are at the top
of `bucks-backup.sh`.

## Exit codes and logs

`journalctl -u bucks` has every log line with a timestamp; the journal rotates it and rate-limits a
flood. Exit codes 2 (bad option), 3 (wrong network or genesis, also "mainnet genesis not configured"),
6 (database corrupt) and 7 (unsupported schema) are not restarted, because a person has to fix them;
4 (storage failure) and 5 (P2P port in use) are retried every 10 s, at most 5 times in 10 minutes.

## Checked

`systemd-analyze verify` (all units), `systemd-analyze security` (bucks.service: exposure 1.4, "OK"),
`bash -n` and `shellcheck` on the scripts, `nginx -t` on the rendered site, and an end-to-end run of
nginx in front of a regtest node with the console flags (401 without basic auth, login over HTTP/2 + TLS,
foreign Origin refused, HTTP redirected to HTTPS, wrong Host refused by the node). ctest's `deploy_kit`
test keeps these files consistent with the node's options and with the rules above.
