# Bucks Blockchain - AI Coding Guidelines

## Project Overview
Bucks is a simplified educational blockchain implementation in C++ for the Bucks cryptocurrency. It includes core blockchain components (blocks, transactions, mining) with a web-based UI for interactive management.

## Architecture & Key Components
- **Core Classes**: `Block`, `Transaction`, `Blockchain`, `Wallet`, `UTXO`, `MerkleTree`, `Crypto`
- **Namespace**: All classes use `Bucks::` namespace
- **Data Flow**: Transactions → Mempool → Merkle Tree → Block → Mining → Blockchain validation
- **Crypto**: OpenSSL-based SHA-256 (double hashing), ECDSA signatures, RIPEMD-160 addresses

## Build & Run Commands
- **Build**: `./build.sh` (custom script) or `mkdir build && cd build && cmake .. && make`
- **Dependencies**: Requires OpenSSL dev headers (`brew install openssl@3` on macOS)
- **CLI Demo**: `./bucks`
- **Web Server**: `./bucks --server [--port 8080]`
- **Web UI**: Access at `http://localhost:8080` for dashboard, wallets, transactions, mining

# Bucks — AI Coding Guidelines (concise)

Purpose: Give AI agents the minimal, project-specific context to be productive in this C++ educational blockchain.

Quick start
- Build: run `./build.sh` (recommended) or `mkdir build && cd build && cmake .. && make`.
- macOS OpenSSL note: install via `brew install openssl@3` and add include/link flags if CMake fails.
- Run CLI demo: `./bucks`
- Run server/UI: `./bucks --server --port 8080` then open http://localhost:8080

Architecture (high level)
- Components: `Block`, `Transaction`, `Blockchain`, `Wallet`, `UTXO`, `MerkleTree`, `Crypto` (look in `src/` and `include/`).
- Dataflow: Transactions → Mempool → Merkle tree → Block → Mining → Chain validation. See [ARCHITECTURE.md](ARCHITECTURE.md).
- Web UI: frontend `web/app.js` calls REST endpoints implemented by `src/server.cpp`.

Conventions & gotchas
- Amounts use awraq: store as `uint64_t` (100000000 = 1 BUCKS). Convert for display only.
- Hashing: always double SHA-256 for block hashes (`SHA256(SHA256(...))`).
- Addresses: `RIPEMD160(SHA256(pubkey))` then Base58; code lives in `src/crypto.cpp`.
- Namespace: most classes use `Bucks::` patterns; follow existing naming and header structure in `include/`.

Common code paths to inspect (examples)
- Transaction creation: `Wallet::createTransaction()` (select UTXOs, compute fee, sign outputs).
- Mining loop: see `miner.cpp` and `pow.cpp` (nonce loop, target calculation).
- Coinbase: `Blockchain::createCoinbaseTransaction(...)` assembles reward + fees.
- REST API surface: endpoints in `src/server.cpp` → handlers called by `web/app.js`.

Developer workflows
- Use `./build.sh` first; fallback to CMake build in `build/` directory.
- VS Code auto tasks exist for quick compile of an active file (clang/clang++). For full build prefer CMake or `build.sh`.
- Run the executable with `--server` to exercise the web UI and inspect logs printed to stdout.

Where to look first
- [ARCHITECTURE.md](ARCHITECTURE.md) — overall design
- `src/` — `block.cpp`, `transaction.cpp`, `blockchain.cpp`, `wallet.cpp`, `miner.cpp`, `server.cpp`
- `include/` — public headers for types and interfaces
- `web/` — `app.js`, `index.html` — shows JSON API usage patterns

Testing & validation
- Basic runtime check: `./bucks` runs a demo and internal validation (`Blockchain::isValidChain()`).
- Unit tests: `tests/` contains quick benchmarks; run manually after build.

If you modify crypto or build flags
- Ensure OpenSSL linkage is preserved; verify `CMakeLists.txt` or `build.sh` picks up brew OpenSSL paths on macOS.

Questions for reviewer: Which areas need more detail (build flags, CMake quirks, or specific APIs)?
