# Bucks

Bucks is a proof-of-work cryptocurrency node written in C++17: SHA256d proof of work on an 84-byte
block header, a SQLite chain store, a mempool, a CPU miner, encrypted peer-to-peer networking
(ECDH + AES-256-GCM), HD wallets restored from 13- or 25-word phrases on 2048-word Arabic and English
lists, a token-authenticated REST API and a web console that the node serves itself.

**Status**

- **Mainnet has not launched.** `bucks --network=main` refuses to start (exit code 3, "main genesis is
  not configured in this build") until a release carries the mainnet genesis. Use the test network
  (`--network=test`) or a private regtest chain until then. What is still open before launch is in
  [docs/MAINNET_INPUTS.md](docs/MAINNET_INPUTS.md); the launch procedure is [docs/LAUNCH.md](docs/LAUNCH.md).
- **No external audit yet.** The word lists are drafts until their review is finished; phrases made
  now may not restore with the final lists.
- **No gold backing.** BUCKS are not backed by or redeemable for gold. The gold-reserve module is a
  design proposal ([docs/design/GOLD_STANDARD.md](docs/design/GOLD_STANDARD.md)) and is not part of
  the first launch.

## Security model: run your own node

The web console is a **single-operator** tool. The node's one API token controls **every wallet on
that node**, and `wallets.json` holds their private keys unencrypted. So:

- Each user runs their own node on their own computer and keeps their own recovery phrase.
  Do not create wallets for other people on your node, and never give anyone your API token.
- The recovery phrase is shown once, when the wallet is created. Write it down offline; the node
  does not store it.
- The API listens on 127.0.0.1. To use the console of a remote node, use an SSH tunnel
  (`ssh -L 8080:127.0.0.1:8080 you@server`) or the TLS reverse proxy in [deploy/](deploy/README.md).
  Never bind the API to a public address without TLS in front of it.
- A mining server does not need a wallet: `--mine-address` pays the rewards to an address whose key
  stays on your own computer.

## Build on Linux

Ubuntu 22.04 or 24.04 (other distributions need the same packages):

```bash
sudo apt-get install -y build-essential cmake git libssl-dev ca-certificates python3
git clone https://github.com/infin8sync69-source/bucks-blocks.git
cd bucks-blocks
git checkout <release tag>
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

- Requirements: CMake 3.14 or newer, a C++17 compiler (GCC or Clang), OpenSSL 3.0 or newer.
  SQLite is compiled in from `src/sqlite3.c`; the `sqlite3` package is not needed to build.
  Python 3 runs the independent genesis check in `ctest`.
- There is no release tag before launch, and GitHub's default branch does not hold the code yet
  (an open item in [docs/MAINNET_INPUTS.md](docs/MAINNET_INPUTS.md)): check out the branch or tag the
  project names.
- Compiling `src/server.cpp` takes about 1.2 GB of memory. On a small server use `-j1`.
- Configuring downloads googletest v1.14.0 from github.com for the tests. Without that access, build
  only the node with `-DBUCKS_BUILD_TESTS=OFF`, or point
  `-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<googletest checkout>` at a local copy.
- Install: `sudo cmake --install build --prefix /opt/bucks` gives `/opt/bucks/bin/bucks`,
  `/opt/bucks/bin/bucks-mine-genesis` and `/opt/bucks/share/bucks/{web,wordlists}`. An installed
  binary finds the web UI and word lists there once its source tree is gone; `--web-root` and
  `--wordlist-dir` always take precedence.
- A binary is tied to the Ubuntu release it was built on (a 24.04 build does not run on 22.04).

macOS: `brew install cmake openssl@3`, then the same `cmake` commands (CI builds and tests macOS).
Windows builds with MinGW are not covered by CI; see the note on legacy scripts below.

## Run a node

| Network | Flag | P2P port | API / console | Addresses | Use |
|---|---|---|---|---|---|
| main | `--network=main` (the default) | 8733 | 8080 | `B...` | Refuses to start until the release with the mainnet genesis |
| test | `--network=test` | 18733 | 18080 | `T...` | Public test network: coins have no value and the chain may be reset |
| regtest | `--network=regtest` | 18744 | 18090 | `t...` | A private chain on your machine; blocks are found instantly |

Start a test node with the web console:

```bash
build/bucks --network=test --server --seed <host>:18733
```

Then open <http://127.0.0.1:18080> and sign in with the API token. At start the node prints where it
wrote the token: `<datadir>/.cookie` on main, `<datadir>/<network>/.cookie` on the test networks
(`cat ~/.bucks/test/.cookie`). A new token is written at every start unless you pass
`--api-token-file <file>` (mode 0600).

- No seed nodes are compiled in yet (an open launch item). Pass `--seed host:port` or
  `--connect host:port` for nodes you know. Other people reach your node on its P2P port: open
  TCP 8733 (main) or 18733 (test) in your firewall. Never open the API port.
- Data lives in `~/.bucks` unless you pass `--datadir`: `<datadir>/<network>/chain.sqlite` (the
  chain) and `<datadir>/<network>/wallets.json` (private keys: keep it private and backed up). Delete
  a chain only by removing `chain.sqlite*`, never the whole folder.
- Mining: the Mining page of the console, or start with
  `--auto-mine --mine-address <your address> --mine-threads <n>`. Mined coins can be spent after
  100 more blocks.
- After launch, a mainnet node is `build/bucks --server` (main is the default network).
- Stop the node with Ctrl-C or SIGTERM; it shuts down cleanly. To run it as a service on a server,
  with a firewall, TLS for the console, backups and health checks, use [deploy/](deploy/README.md).

### Options

`bucks --help` is the reference. The main options:

| Option | Meaning |
|---|---|
| `--network <main\|test\|regtest>` | Network: magic bytes, ports, genesis, address prefix |
| `--server` | Serve the REST API and the web console |
| `--port <n>`, `--p2p-port <n>` | API and P2P ports (defaults in the table above) |
| `--seed <host:port>`, `--connect <host:port>` | Peers to dial (repeatable); `--connect` keeps the connection |
| `--datadir <dir>` | Data directory (default `~/.bucks`) |
| `--web-root <dir>`, `--wordlist-dir <dir>` | Web console files and word lists, if not the bundled ones |
| `--api-bind <addr>` | API address (default 127.0.0.1) |
| `--api-token-file <file>` | Read the API token from a file (mode 0600); `$BUCKS_API_TOKEN` also works |
| `--api-allowed-host <h>`, `--trust-proxy`, `--api-secure-cookie` | Behind a TLS reverse proxy on the same host ([deploy/](deploy/README.md)) |
| `--auto-mine`, `--mine-address <addr>`, `--mine-threads <n>` | Mine from the start, pay to this address, use n threads |
| `--checkblocks <n>`, `--checkdb[=full]`, `--reindex-chainstate`, `--reindex` | Start-up checks and repairs |

Exit codes: 0 stopped cleanly, 1 API, miner or word lists could not start, 2 bad option, 3 wrong network
or genesis for this build or data directory, 4 storage failure, 5 P2P port in use, 6 database corrupt,
7 unsupported database version.

### REST API

Requests and responses are JSON. Read-only chain routes need no token: `GET /api/status`,
`/api/blockchain/info`, `/api/blocks/recent`, `/api/blocks/height/<n>`, `/api/blocks/hash/<hash>`,
`/api/mining/status`. Everything else needs the token in an `X-API-Token` (or
`Authorization: Bearer`) header, for example `GET /api/wallets`, `POST /api/wallets/create`,
`POST /api/wallets/restore`, `POST /api/transactions/send`, `POST /api/mining/start` and
`/api/mining/stop`, `POST /api/node/approve-reorg`. `src/server.cpp` is the reference.

## Tests

```bash
ctest --test-dir build --output-on-failure     # unit, integration, system, perf/security, word lists,
                                               # build hygiene, install layout, deployment kit
node --test web/test/*.mjs                     # web console
tools/ci.sh build|asan|tsan|fuzz|soak          # the CI jobs, runnable locally
```

`tools/soak.sh` runs three regtest nodes with mining, `kill -9` and restarts, and requires them to
converge (`BASE_API`/`BASE_P2P` choose the ports).

## Repository layout

| Path | Contents |
|---|---|
| `src/`, `include/` | The node (`src/main.cpp`), consensus, chain store, P2P, wallet, API |
| `web/` | The web console |
| `wordlists/` | Mnemonic word lists and their manifest (with SHA-256 pins) |
| `tests/` | gtest suites, fuzz targets, build and deployment checks |
| `tools/` | Genesis miner and verifier, word-list tools, CI and soak scripts |
| `deploy/` | systemd unit, server roles, nginx, backup and health check, bootstrap script |
| `docs/` | Launch inputs and runbook, audit scope, design specifications |

### Legacy scripts in the repository root

These are kept for reference and are not part of the supported build or deployment:

- `run_pilot.sh`: an old three-node local pilot. It kills **every** process whose command line
  contains `build/bucks`, so never run it on a server that runs a real node.
- `mining_node.ps1`, `setup_mining_node.ps1`, `run.ps1`, `run_tests.ps1`, `run_perf.ps1`,
  `run_system.ps1`: Windows only (PowerShell, MinGW in `tools/mingw64`); they start the default
  network and read a `wallets.json` from the repository root.
- `fix_app.js`, `concat_app.js`, `update_main.js`, `check-frontend.js`, `test-prop.js`,
  `test_ws.cpp`: one-off development scripts for the earlier `frontend/` UI; the node serves `web/`.
- `build.sh` builds without CMake; CMake is the supported build (ctest checks that the two agree).

## License

Copyright © 2026 Bucks Blockchain. All rights reserved. Which licence the released code carries is
an open decision ([docs/MAINNET_INPUTS.md](docs/MAINNET_INPUTS.md)). Third-party components and their
licences: [THIRD_PARTY.md](THIRD_PARTY.md).
