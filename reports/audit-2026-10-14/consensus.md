# Consensus and chain state audit (chain v2)

Auditor: independent, read-only. Date: 2026-10-14. Branch `chain-v2-phase1-scaffold`
(working tree with the uncommitted testnet re-mine in `src/chainparams.cpp` and `tests/unit/test_genesis.cpp`).
Audited against `docs/design/consensus-validation.md` (and `storage-concurrency.md` where relevant).
Gold events are out of scope by instruction (stub `gold_rules.cpp`).

Method: full read of the files in scope, a private build (`cmake -S . -B <scratchpad>/build`, Release,
Apple clang 21, libc++, OpenSSL 4.0.3), all 188 unit tests and 70 non-server integration tests pass, plus my own
proof programs (listed at the end). No repository file was modified.

## Summary

| Severity | Count | IDs |
|---|---|---|
| Critical | 0 | |
| High | 1 | CON-1 |
| Medium | 1 | CON-2 |
| Low | 5 | CON-3, CON-4, CON-5, CON-6, CON-7 |
| Info | 4 | CON-8, CON-9, CON-10, CON-11 |

No money-creation, signature, sighash, merkle-duplicate (CVE-2012-2459), BIP30, maturity, retarget, chain-work or reorg-atomicity
defect was found. The consensus core (stages A to E, undo, coins view, U256/PoW, sighash, parsers) matches the spec and held up
under differential and fuzz testing. The real problems are at the edges: the miner gate that trusts unvalidated headers (CON-1),
the 64-byte-transaction rule that is classified as "invalid" instead of "mutated" (CON-2), and a few crash-safety / fail-stop gaps.

---

## CON-1 (High): one header with valid proof of work, and no body, stops mining on every node that sees it

- File: `src/chainstate.cpp:96-120` (`miningBlockedReason`), fed by `acceptHeader` (`:122-141`), `ProcessNewBlockHeaders` (`:143`);
  P2P entry `src/network.cpp:506-520`.
- What is wrong: spec section 8.5 makes the miner refuse to work while "a header chain with more work than the tip is known, not yet
  connected". The code implements that as: any non-failed index entry with `chainWork > tip.chainWork` (not under a fork alert) returns
  `"node is syncing"`. A header is accepted into the index after stages A and B only (PoW, difficulty, time). Any header that
  extends the tip has `chainWork = tip.chainWork + blockProof`, so it is always "more work". The body is never required, and an
  entry without a body can never be marked `FAILED_*`, so the gate never clears by itself.
- Scenario: attacker mines one header on top of the current tip (2^32 hashes at launch on main; 2^24, seconds on a laptop, on testnet),
  with any merkle root, and sends it in a `headers` message to every mining node (peers answer `getdata` for the body with nothing).
  `miningBlockedReason()` now returns "node is syncing" on all of them, so `BlockAssembler::createNewBlock` fails and the auto-miner
  and `/api/mining/mine` stop. The chain halts for as long as the attacker is connected to every miner; it only clears when a block
  from a miner that never saw the header arrives and the tip moves (equal work then no longer counts as "better").
  A variant needs no withheld body: a block whose body cannot be parsed (a transaction over 100,000 bytes, a non-minimal CompactSize,
  more than 15,384 transactions) is MALFORMED, is never marked failed (it cannot be: nothing binds the bytes to the header before
  parsing) and stays "best header" forever, being re-requested from peers and penalising them 20 points each time (`src/network.cpp:541-546`).
- Verified: yes. `poc/stall.cpp`: on regtest rules with `id = TEST`, one mined header with a junk merkle root, delivered through
  `ProcessNewBlockHeaders`, changes `miningBlockedReason()` from `''` to `'node is syncing'`; an honest block at the same height clears it.
- Fix: do not gate on index entries whose body is missing. Gate only on (a) `tip.chainWork < minimumChainWork`, or (b) a candidate that
  has data (`haveChainData`) and is not yet connected, or (c) a header chain that is actively being downloaded and is younger than a
  few minutes (give the gate a timeout and remember the time the header arrived). Optionally charge the sending peer when a header
  chain's body is not delivered within a deadline.

## CON-2 (Medium): the 64-byte transaction rule marks the header FAILED, so a forged body can poison a valid block

- File: `src/validation_rules.cpp:135` (`bad-txns-size-64` returned as `INVALID`), ordering in `CheckBlock` (`:206-240`),
  `src/chainstate.cpp:194-197` (`markFailed` on any stage C `INVALID`).
- What is wrong: the spec adds the 64-byte rule as the defence against CVE-2017-12842 (an interior merkle node, 64 bytes, replayed as a
  transaction). The defence only works if a block containing a 64-byte transaction is treated like a merkle mutation (header not
  marked invalid), which is what Bitcoin Core does. Here the merkle comparison passes first (the forged one-leaf tree has the same
  root), then the later rule returns `INVALID` and `markFailed` stores `FAILED_VALID` on the real header.
- Scenario: honest block with exactly two transactions [cb, t2]. If the 64 bytes `txid(cb) || txid(t2)` parse as a transaction,
  anyone can send the header plus a one-transaction body made of those 64 bytes. The merkle root matches, the block is rejected
  (`bad-cb-missing`) and the real block is `FAILED_VALID` on that node, together with every descendant (`FAILED_CHILD`). The node
  then refuses the canonical chain until `--reindex-chainstate`. Chances against honest blocks are low (one layout needs only
  `txid(cb)[4] == 0x01`, about 1/256 of two-transaction blocks, plus about 2^24 grinding of an attacker transaction's txid, which needs a
  signature per try), but the whole point of the rule is to remove this class.
- Verified: yes. `poc/tx64.cpp` builds a valid regtest block [cb, t2] (a coinbase grind of 19.1 M tries for the layout
  `00 01 .. 2d`), shows a control node connects it, then a victim that receives the forged 149-byte body first answers
  `bad-cb-missing`, flags the real block failed and answers `duplicate-invalid` to the honest block (tip stays at 100).
- Fix: in `CheckBlock`, before or together with the merkle comparison, return `MUTATED` (not `INVALID`) if any transaction serializes
  to exactly 64 bytes; keep `bad-txns-size-64` in `CheckTransaction` for the mempool.

## CON-3 (Low): crash between `putBlock` and the status write makes the node refuse to start

- File: `src/chaindb.cpp:591-615` (`putBlock`), `src/chainstate.cpp:208-213` (`acceptBlock`, status written by a second
  statement), `src/chaindb.cpp:554-570` (`selfCheck`). Spec deviation: `storage-concurrency.md` lines 357 and 492 say the body row and
  `status |= HAVE_DATA|DATA_CHECKED` are one transaction; the code uses two separate autocommit transactions.
- Scenario: power loss or `kill -9` in the window after `putBlock` commits and before `persistStatus`. Next start:
  `HAVE_DATA rows and block_data rows differ; restart with --reindex`, exit 6. No data is lost, but an unattended node does not come
  back without an operator running `--reindex`.
- Verified: yes. `poc/crashwin.cpp`: header row stored, `putBlock`, close without status, reopen returns `ok=0 exit=6` with that message.
- Fix: do the `status |= 6` update inside the `putBlock` transaction as the storage spec says (or let `selfCheck` tier 1 repair a body
  row that has no flag when the body hashes to its key).

## CON-4 (Low): fatal chain-state failures do not stop the process (spec 8.4 says FATAL)

- File: `src/chainstate.cpp:222` (`fatal-chainstate` returned as `INTERNAL`), `:261` (`storeFailed_` latch), `src/network.cpp:517` and
  `:551-565` (INTERNAL ignored, no penalty).
- What is wrong: commit failure, unreadable block or undo data, or "undo data does not match block" are specified as FATAL with a clean
  exit. They return `INTERNAL` and the process keeps running with a frozen tip; after any failed index write every later call returns
  `store-write-failed` forever. The in-memory state is still consistent (nothing is mutated before COMMIT), so this is not a
  money-safety issue, but the API and mempool keep serving a stale chain and nothing tells the operator.
- Verified: yes by code reading (no handler for these codes anywhere under `src/`); the in-memory tests intentionally assert the
  non-fatal behaviour (CS-6).
- Fix: have `Node`/`main` terminate (exit code 4 or 6) on `fatal-chainstate` and `store-write-failed`, or expose a persistent
  "node halted" flag that the API, miner and network check.

## CON-5 (Low): the `Miner` object is shared by the API mine call and the auto-miner without synchronisation

- File: `include/miner.h:85` (`timeLimit_`, `lastHash_`, `rollovers_`, `templates_`, `staleRestarts_` plain members),
  `src/node.cpp:348-351`, `src/miner.cpp:280`; callers `src/server.cpp:1550` (HTTP worker, limit 60 s) and `:1684` (auto-miner thread, limit 0).
- What is wrong: both threads call `Node::mine` on the single `miner_`. Only the hash loop is outside the lock; `setTimeLimitSeconds`,
  `lastHash_` and the counters are written without it. Results: a data race (undefined behaviour), the API can report the other thread's
  block hash (`lastBlockHash()` read after `FOUND`), and the auto-miner's `setTimeLimitSeconds(0)` can remove the 60 s limit of the HTTP
  request so that worker hashes without a time bound.
- Verified: yes with ThreadSanitizer (`poc/minerrace.cpp`, `Miner` wired with the chain lock exactly like `Node::open`): "data race ...
  Write of size 8 ... Miner::setTimeLimitSeconds". That the server really runs both paths concurrently is by code reading.
- Fix: make the miner state per call (pass the limit as an argument, return the hash through the result) or serialise `Node::mine`
  with a mining mutex; make counters atomic.

## CON-6 (Low): on testnet, header spam costs 65,536 times less than the spec assumes, and per-block work grows with the index

- File: `src/chainparams.cpp:52` (`powLimitBits = 0x1e00ffff`), `src/chainstate.cpp` (`pickCandidate`, `propagateHaveChainData`,
  `markFailed`, `missingBlocks`, `miningBlockedReason`: full `index_.forEach` scans), `src/chaindb.cpp` (`putIndexRow`, one autocommit
  fsync per header).
- What is wrong: spec section 8.6 prices header spam at 2^32 hashes per header. With the new testnet limit every valid header at
  minimum difficulty costs 2^24 (any fork from a recent block is at that difficulty). One million headers is about 1.7 x 10^13 hashes
  (hours on one GPU, minutes on an ASIC). Measured cost on the node: 192 bytes per index entry, about 288 MB resident per million
  entries, one fsync per header into SQLite, and every block arrival runs several O(N) scans under the chain lock (67 ms per scan at 1 M
  entries). Nothing evicts or limits stored headers that never get a body.
- Verified: cost numbers measured (`poc/mem.cpp`); the exploit itself is reasoning (needs hash power, not reproduced).
- Fix: keep per-peer header budgets (P2P spec), replace the full scans by a candidate set (as spec 8.2 describes), and for public testnet
  consider a higher floor than 2^24 or a cap on unconnected headers.

## CON-7 (Low): approved deep reorg can still be refused, approvals are unbounded

- File: `src/chainstate.cpp:359-364` and `:244-248` (`approveReorg`).
- What is wrong: the "overtake position" bound `stepLast > 2 * maxAutoReorgDepth + 32` is applied even when the operator approved the
  branch, so a branch that needs more than 232 blocks to out-work the tip (large difficulty difference) is parked again and the operator
  cannot force it. `approved_` accepts any 32-byte value from the API and only grows.
- Verified: no, reasoning only (needs a long low-work fork; not constructed).
- Fix: skip the position bound when `approved_` contains `best->hash`; ignore hashes that are not in the index.

## CON-8 (Info): the P2P handshake does not identify the chain; the testnet genesis was re-mined with the same magic

- File: `src/network.cpp:457` (`VERSION` carries only a height), `src/chainparams.cpp:44-60` (testnet magic `B7 C5 E3 7E` unchanged).
- Effect: nodes still on the previous testnet genesis (`00000000fe2553db...`) and nodes on the new one (`000000dd046f...`) pass the magic check,
  connect, exchange heights, and loop on `getheaders` / `MISSING_PARENT` (no penalty by design). Wasteful, not a split: checkpoint 0 and
  `prev-blk-not-found` keep the chains apart.
- Verified: by code reading only.
- Fix: change the testnet magic when the genesis changes, and/or put the genesis hash (first 4 bytes at least) into `VERSION` and drop mismatching peers.

## CON-9 (Info): documentation still says testnet uses `0x1d00ffff`

- `docs/design/consensus-validation.md` sections 1.4 (D6), 3.4, 3.6 and `tests/unit/test_genesis.cpp:85` still describe
  or use `0x1d00ffff` for testnet; the code now uses `0x1e00ffff`. Parameters themselves are consistent (checked below). Update the spec and
  decide whether the spec 3.6 reasoning (stall, spam, ASIC) is acceptable for testnet at 2^24.

## CON-10 (Info): template builder copies the whole mempool under the chain lock

- `src/miner.cpp:108` (`byFeeRateDescending()` returns a full copy, up to 100 MB of `MempoolEntry`) on every template build (each tip change and
  every 60 s per miner), then `stable_sort`. Latency spike, not a correctness issue. Iterate the index in place and copy only the selected entries.

## CON-11 (Info): `Hash256Hasher` is only safe because the standard libraries use prime bucket counts

- `include/chain.h:53`. It uses the first 8 bytes of the hash as the low bits of `size_t`; block hashes that satisfy PoW start with 3 to 4
  zero bytes, so the low 24 to 32 bits are always 0. With libc++ and libstdc++ (prime bucket counts, `h % p`) this works (measured: longest
  chain 6 to 8 at 80,000 entries for 0, 3 and 4 leading zero bytes). A power-of-two implementation (MSVC STL) would put every block in one bucket.
  The code already needs POSIX (`flock`, `geteuid`), so this is latent. Mix all 32 bytes (or use the last 8) to remove the dependency.

---

## Checked and found sound

Money and supply
- `ConnectBlock` (`src/connect_block.cpp`): input existence, same-block spends in order, `addMoney` on every sum (MAX_MONEY guarded, no wrap),
  `inSum >= outSum`, running fee sum, coinbase `<= subsidy + fees`, BIP30 on every P2PKH output, NULL_DATA never enters the coin set.
  Coinbase maturity is a strict `height - coin.height >= 100` in blocks, mempool and `spendableCoins`.
- `Subsidy` (no UB shift, 0 from 64 halvings), `UtxoStats` (count, total <= MAX_MONEY, additive digest), startup check
  `totalAmount <= sum of subsidies`.
- DB-level backstops in the SQLite schema (amount 1..2.1e15, 25-byte P2PKH script, unique main-chain height).

Signatures and transactions
- Sighash v1 (`src/sighash.cpp`): domain-separated tagged hash; commits to genesis hash (no cross-network replay), version, lock time,
  all prevouts, spent amounts, spent scripts, sequences, outputs and the input index; five hashes computed once per transaction (no
  quadratic hashing). Exact unlock template, strict DER by round-trip, r and s in range, low-S, compressed keys only, hash160 check,
  no input skipped. `verifyECDSA` fails closed; `ripemd160` throws instead of returning zeros.
- Transaction rules `CheckTransaction`: version, counts, size (including 64), output types and values, output sum, duplicate inputs
  (set), coinbase shape, null prevout, `isCoinbase` requires prevIndex `0xFFFFFFFF`. `IsFinal` is correct (BIP113 form).
- Malleability: no third-party malleation path found (signature, key, sighash byte, CompactSize, trailing bytes all canonical).
- Parsers: `Reader` bounds with subtraction form, minimal CompactSize with per-call caps, count vs remaining/min-size before `reserve`,
  trailing bytes rejected. 1.5 M mutation-fuzz iterations each for block, transaction and undo under ASan+UBSan: 0 crashes and
  `serialize(parse(x)) == x` for every accepted input.

Merkle and blocks
- Merkle mutation flag matches Bitcoin (pairs checked before the odd duplicate is appended); mutated and root mismatch give `MUTATED`
  and never mark the header; duplicate txid set; first-coinbase-only; B-SIZE re-serialised; sigop count shared with the template builder.
- Coinbase height prefix (BIP34 style) plus BIP30 makes every coinbase unique; template coinbase has an extra nonce.

Proof of work, difficulty, time
- `U256`: 182,153 random operations (add, sub, mul, shifts including >= 256, division with top-bit remainders, bit length, compact
  decode/encode, block proof) compared with Python big integers: 0 mismatches. `blockProof` formula, compact sign/overflow rules, 256-bit PoW compare.
- Retarget: signed 64-bit timespan, clamp to [T/4, 4T], powLimit clamp, overflow assertion `bits(powLimit) + bits(4T) <= 256` (testnet
  232 + 23). Verified on the real testnet parameters over a synthetic 2016-block chain: spacing 150/300/600/1200/6000 s give the expected bits
  (independent integer math). H-TW1 (600 s) and H-TW2, H-MTP computed on the block's own branch, H-FUTURE returned as `TIME_TOO_NEW` and not cached.
  Template builder computes the same `minTime`.

Chain state, reorg, undo
- Block index keyed by hash; `getAncestor` skip list; chain work recomputed at load and compared; invariants I1 to I5 at startup.
- `activateBestChain`: one scratch view per step, disconnect then connect, nothing changes in memory until `commitStep` succeeds;
  failed block marks only itself and descendants; reorg step bounded; first-seen on equal work; strictly-more-work rule; fork-alert
  parking. Reorg test (`poc/reorg.cpp`) with an intra-block spend chain, reorg to a heavier branch and back: UTXO stats equal a full
  recompute at every point.
- Undo record v1: version, block hash, count, checksum, flags, script template, trailing bytes checked; `DisconnectBlock` in reverse order restores
  height and coinbase flag, checks every restored/removed coin; `verifyDb` disconnects and reconnects the last N blocks with full signature checks
  and compares the result.
- `commitStep` is one `BEGIN IMMEDIATE` transaction (coins, stats, undo, statuses, best block), checks the expected old best block, deletes
  first then inserts, rolls back on any mismatch.

Mempool and policy
- Check order matches spec 9.2; confirmed inputs only; first-seen conflict; rolling fee floor and eviction logic; re-add after reorg
  re-runs signature checks and conflicts correctly (inputs already spent by the connected branch drop the transaction);
  `revalidate` compares spent digest, maturity, finality. Orphan pool bounded (100 total, 25 per peer, random eviction) and orphans pass
  standardness before parking. Fee arithmetic cannot overflow for reachable values.

Parameters
- Testnet: `powLimit` derived after the `0x1e00ffff` override (`finish()`), `checkChainParams` passes, genesis rebuild matches the hardcoded
  merkle root and hash, genesis hash meets its own PoW, checkpoints start with `{0, genesis}`, magic, ports and address version differ
  from main and regtest, genesis time is about now and below `now + 2 h`. Regtest: `0x207fffff`, no retargeting (overflow
  assertion correctly skipped), halving 150, genesis vector matches spec. Main refuses to start (genesis not configured), exit 3.
- No floating point, `rand`, or iteration-order dependence in consensus code; all integers fixed-width little-endian; time in `int64_t`.

## Proof programs (scratchpad, not in the repo)

`/private/tmp/claude-501/-Users-mikado-Desktop-Bucks-Core-Bucks-Blockchain/97a2bd2a-5080-472f-bbdf-131690eea71b/scratchpad/poc/`:
`stall.cpp` (CON-1), `tx64.cpp` (CON-2), `crashwin.cpp` (CON-3), `minerrace.cpp` (CON-5, TSAN), `mem.cpp` (CON-6), `hasher.cpp` (CON-11),
`u256gen.cpp` + `u256chk.py`, `fuzz.cpp`, `retarget.cpp`, `reorg.cpp` (coverage). Private build tree: `scratchpad/build`.
