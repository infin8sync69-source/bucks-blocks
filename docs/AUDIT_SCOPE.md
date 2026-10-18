# External audit scope (Bucks node, chain v2)

For an outside security firm. Internal review results (AI-assisted, read-only, 2026-10-14) are in
`reports/audit-2026-10-14/`; they do not replace an independent audit.

## What the software is
A C++17 full node for a SHA256d proof-of-work coin: consensus and validation, SQLite chain store, mempool, miner,
encrypted P2P, HD wallet with a 2048-word (13/25-word) recovery phrase in Arabic or English, a token-authenticated
REST API, and a dependency-free web console served by the node. Mainnet is disabled until launch; testnet and
regtest run today.

## In scope, by priority
1. **Consensus and chain state**: `src/{validation_rules,connect_block,chainstate,coins,undo,chain,pow,sighash,script,transaction,block,merkle,serialize,genesis,chainparams}.cpp`. Specification: `docs/design/consensus-validation.md`. Look for consensus splits, money creation, reorg or undo errors, retarget/timewarp problems, parser and DoS issues.
2. **Network and concurrency**: `src/{network,protocol_enc,node,miner,mempool,mempool_file,main}.cpp` (+ `server.cpp` threading). Specification: `docs/design/storage-concurrency.md`. Look for remote crashes and hangs, resource exhaustion, transport-encryption weaknesses, eclipse and Sybil exposure, races.
3. **Wallet, mnemonic, API, storage, web console**: `src/{wallet,hd_8192,bip_8192,crypto,server,chaindb,fsutil}.cpp`, `wordlists/`, `web/`. Look for key or seed leakage, restoring the wrong wallet from a phrase (multi-language lists), normalizer confusables, auth/CSRF/DNS-rebinding, fund-loss bugs, XSS.

## Out of scope for now
- The gold-reserve module (stub; later phase) and governance (removed).
- The old `frontend/` and `external/` applications, and the legacy files `blockchain.db`, `utxodb.sqlite`, `wallets.json` (compromised, never used).
- Third-party code: SQLite 3.54.0 (`THIRD_PARTY.md`), OpenSSL, `httplib.h`, `nlohmann/json`.

## How to build and test
`cmake -S . -B build && cmake --build build -j && ctest --test-dir build` (6 suites). Sanitizers and fuzzing: `tools/ci.sh asan|tsan|fuzz`. Three-node soak: `tools/soak.sh build/bucks 90`. Run a node: `build/bucks --network=regtest --server --datadir <dir>`.

## Known gaps the auditors should expect
Threading rework and lock-ranking layer, crash-injection harness and read-pool/backups from the storage spec are not
done; the wallet file is not encrypted at rest; word lists are frozen (version 1) without a scholar or translator review; release checkpoints and
`minimumChainWork` are empty. See `docs/MAINNET_INPUTS.md` and the findings list from the last run of `/code-review`.
