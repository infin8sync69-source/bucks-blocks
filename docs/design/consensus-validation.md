# Bucks Consensus and Validation Specification (Phase 2a)

| | |
|---|---|
| Status | Draft for owner review. Every decision below is final unless it is listed in §16 (Open questions). |
| Date | 2026-10-08 |
| Applies to | The relaunched chain ("chain v2", new genesis). No backward compatibility with the old chain, old database files or old wire format. |
| Related specs | Storage, crash safety and concurrency (Phase 2b, "storage spec"); Gold reserve module (Phase 3, "gold spec"); Phase 1 crypto (`src/crypto.cpp`, already merged). |
| Audit input | `audit.json` of 2026-10-07. Finding IDs (CON-x, NET-x, DB-x, CRY-x, API-x, OPS-x) are mapped to sections in §13. |

All `file:line` references point to the current tree (2026-10-08). The consensus, storage, network and gold code is still the original code that the audit describes.

---

## 0. Conventions

- **MUST / MUST NOT**: a hard requirement. In a "rule" it is a consensus rule: a block or transaction that breaks it is *invalid*.
- **Integers** are little-endian (LE) on the wire and on disk unless a table says otherwise.
- **Hash256** is 32 raw bytes. It is printed as hex of the bytes in stored order (byte 0 first). When a hash is used as a *number* (proof of work, chain work), it is read **big-endian**: byte 0 is the most significant byte. This keeps the current Bucks convention (`src/validation.cpp:110-119` treats leading bytes as the high bytes). Bitcoin reads hashes little-endian; we do not need Bitcoin compatibility.
- `SHA256d(x) = SHA256(SHA256(x))`.
- `TaggedHash(tag, x) = SHA256(SHA256(tag) ‖ SHA256(tag) ‖ x)` (BIP340 style; `tag` is ASCII).
- `‖` means byte concatenation. `LE32(v)`, `LE64(v)` are 4/8-byte little-endian encodings.
- Amounts are in **awraq**: 1 BUCKS = 100,000,000 awraq, stored as `uint64`.
- Every rule has an ID (H = header, B = block, T = transaction, C = connect, P = policy) and a **reject code** (a short ASCII string used in logs, the API and tests).
- `now` is the local system clock in UTC seconds. Bucks does not use peer-adjusted time (recent Bitcoin Core versions also use the local clock only); the operator keeps the clock correct with NTP.

---

## 1. Scope and non-goals

### 1.1 In scope

1. Chain parameters for main, test and regtest networks, and the hardcoded genesis block.
2. Exact byte layouts: transaction v2, input, output, block header v2, block, block undo record.
3. Signature hash v1 and spend verification.
4. Proof of work: 256-bit arithmetic, compact `bits` encoding, chain work, difficulty retarget, time rules (median time past, future limit, timewarp).
5. One block-validation path used for every block from every source, with rule order and result categories ("invalid" versus "cannot validate yet").
6. Block index keyed by hash, chain selection by most work, atomic reorganisation, startup verification.
7. Mempool policy, orphan transactions, block template builder and miner rules.
8. Parser hardening for every deserializer.
9. Removal of dead consensus code.
10. A phased implementation plan and a test plan.

### 1.2 Out of scope (owned elsewhere)

| Topic | Owner |
|---|---|
| SQLite schema, atomic commit implementation, crash recovery, lock model and threads, datadir layout, file permissions | Storage spec. This spec states only what it needs from storage (§8.8). |
| Gold, vault and governance event rules (payload format, signatures, reserve state) | Gold spec. This spec defines the hook and its guarantees (§7.8). |
| P2P handshake, peer management, ban list, encryption, inventory relay, download scheduling | Future P2P spec. This spec defines the validation interface, the penalty category for each result, and two interim message formats (§8.9). |
| REST API, web UI, wallet key storage | API/wallet specs. Integration points are listed where they matter. |

### 1.3 Explicit non-goals

- **No general script engine.** Only two output types exist: P2PKH and NULL_DATA (§4.2). Adding a new type later is a hard fork with a new transaction version.
- **No SegWit-style witness.** Witness parsing is removed (CON-13).
- **No skipping of validation.** No "assumevalid", no "skip signatures below checkpoint". Every signature of every block is checked.
- **No replace-by-fee, no child-pays-for-parent, no package relay** in v1 (first-seen mempool, §9). Deferred.
- **No relative lock-time (BIP68) and no CLTV/CSV opcodes** (there are no opcodes). `nLockTime` finality is enforced (§7.4).

### 1.4 Main decisions (summary)

| # | Decision | Section |
|---|---|---|
| D1 | Only P2PKH and NULL_DATA outputs are valid. NULL_DATA outputs carry 0 value and never enter the UTXO set. The script interpreter is deleted. | §4.2, §12 |
| D2 | Signature and public key travel inside `scriptSig` (exact P2PKH template), so they are serialized and committed by the txid. | §4.2, §4.3 |
| D3 | Signature hash v1: BIP341-style, commits to all prevouts, all spent amounts, all spent scripts, all sequences, all outputs, version, lock time, input index and the network's genesis hash. Only SIGHASH_ALL. | §5 |
| D4 | Strict DER and low-S are consensus (Bitcoin has low-S only as relay policy). | §5 |
| D5 | Header stays 84 bytes (keeps `height`); version 2. The 84-byte layout cannot be hashed by stock Bitcoin ASIC chips. | §4.4, §3.6 |
| D6 | Launch difficulty = minimum difficulty = `0x1d00ffff` (about 2^32 hashes per block). | §3.6 |
| D7 | Chain work is exact 256-bit `floor(2^256/(target+1))`; most cumulative work wins; ties keep the first-seen block. | §6.4, §8.3 |
| D8 | Bitcoin retarget formula (2016 blocks, 4x clamp, powLimit clamp) in signed 64-bit time, plus two timewarp rules (BIP94 600 s rule, BIP54 non-negative period rule). | §6.5, §6.6 |
| D9 | BIP34-style height (fixed 4-byte LE) in every coinbase; BIP30 check on every output; merkle mutation detection (CVE-2012-2459); 64-byte transactions invalid. | §4.3, §4.6, §7 |
| D10 | One validation path. Peers, the miner, startup checks and reorgs all call the same functions. | §7.2 |
| D11 | Block index keyed by hash; a reorg is one atomic storage commit; on any failure nothing changes. | §8 |
| D12 | Automatic reorgs deeper than 100 blocks are refused and raise a fork alert (operator decides). | §8.5 |
| D13 | Mempool: full consensus validation plus policy; confirmed inputs only; first-seen; fee-rate eviction; revalidated on every tip change. | §9 |
| D14 | Template builder uses the same size and sigop counting as validation and runs a full validity test before mining. | §10 |

---

## 2. Root causes in the current code (short)

1. **Two validation paths.** `Validation::checkBlock` (`src/validation.cpp:267-434`) has the header, PoW, merkle and subsidy rules, but the reorg path calls `Validation::connectBlock` (`src/validation.cpp:440-528`), which has none of them (CON-1, DB-2, NET-1, API-V2).
2. **Blocks stored by height, not hash.** `INSERT OR REPLACE` keyed by the sender's `header.height` (`src/blockdb.cpp:33-34, 81`); unvalidated side blocks are saved (`src/blockchain.cpp:308-312`) and reloaded as the main chain (`src/blockchain.cpp:563-597`) (CON-6, DB-1, NET-2).
3. **Chain work is always zero** for real targets (`src/blockchain.cpp:21-42`), so honest reorgs never happen (CON-2, NET-V1).
4. **Spends are not self-contained.** The public key is a side field never serialized (`include/transaction.h:25`, `src/transaction.cpp:164-207`); ownership is checked only for exact P2PKH (`src/validation.cpp:214-232`) (CRY-4, CRY-6, CON-3, CON-4, DB-3, DB-V1).
5. **Identical coinbases** (`src/server.cpp:1663-1681`) overwrite each other through `INSERT OR REPLACE` (`src/utxodb.cpp:53-55`) (CON-8, API-6, DB-6).
6. **Miner and validator disagree** on size, sigops and reward (`src/server.cpp:1499, 1503-1505, 1675, 1683-1685` versus `src/validation.cpp:324-353, 411-426`) (CON-16, API-17, DB-9).
7. **Parsers trust lengths and counts** (`src/block.cpp:237`, `src/network.cpp:148`, `src/transaction.cpp:56-98`) (OPS-7, OPS-8, CON-14, NET-16).

---

## 3. Chain parameters

### 3.1 Consensus constants (same on every network)

All of these live in `include/consensus.h` and nowhere else. The duplicates in `include/transaction.h:14-16` and the unused ones (`MAX_SCRIPT_SIZE`, `include/consensus.h:108`) are deleted.

| Name | Value | Notes (old value / place) |
|---|---|---|
| `COIN` | 100,000,000 | unchanged (`consensus.h:46`) |
| `MAX_MONEY` | 21,000,000 × COIN | unchanged (`consensus.h:62`) |
| `INITIAL_SUBSIDY` | 50 × COIN | was `INITIAL_BLOCK_REWARD` |
| `COINBASE_MATURITY` | 100 | unchanged |
| `BLOCK_VERSION` | 2 (exact) | was 1, only `!= 0` was checked (`validation.cpp:151`) |
| `TX_VERSION` | 2 (exact) | was never checked |
| `BLOCK_HEADER_SIZE` | 84 bytes | |
| `MAX_BLOCK_SIZE` | 1,000,000 bytes (whole serialized block) | old code summed `serialize(true)` of txs only (`validation.cpp:330`) |
| `MAX_BLOCK_SIGOPS` | 20,000 | counting rule changed (§7.4 B-SIGOPS) |
| `MAX_BLOCK_TXS_PARSE` | 15,384 (= 1,000,000 / 65) | parser cap only |
| `MAX_TX_SIZE` | 100,000 bytes | was in two files |
| `MAX_TX_INPUTS` | 2,000 | was 10,000 (`consensus.h:90`) and 1,000 (`transaction.h:14`) |
| `MAX_TX_OUTPUTS` | 3,000 | was 10,000 and 1,000 |
| `COINBASE_SCRIPTSIG_MIN` / `_MAX` | 4 / 100 bytes | |
| `MAX_NULLDATA_PAYLOAD` | 1,024 bytes | gold events must fit (coordinate with gold spec) |
| `SIGHASH_ALL` | 0x01 (the only allowed value) | |
| `TARGET_SPACING` | 600 s | |
| `RETARGET_INTERVAL` | 2016 blocks | |
| `TARGET_TIMESPAN` (T) | 1,209,600 s | |
| `MAX_FUTURE_BLOCK_TIME` | 7,200 s | was hardcoded 7200 (`validation.cpp:167`) |
| `MTP_WINDOW` | 11 blocks | |
| `TIMEWARP_GRACE` | 600 s | new |
| `LOCKTIME_THRESHOLD` | 500,000,000 | below = height, at/above = UNIX time |
| `SEQUENCE_FINAL` | 0xFFFFFFFF | was unused (`consensus.h:122`) |
| `REQUIRE_STRICT_DER`, `REQUIRE_LOW_S` | true | already used by `src/crypto.cpp:552, 571` |

### 3.2 Policy constants (local node rules, not consensus)

| Name | Value |
|---|---|
| `MAX_STANDARD_TX_SIZE` | 100,000 bytes |
| `MIN_RELAY_FEERATE` | 1,000 awraq per 1,000 bytes |
| `INCREMENTAL_RELAY_FEERATE` | 1,000 awraq per 1,000 bytes |
| `DUST_LIMIT` | 546 awraq (every P2PKH output) |
| `MAX_STANDARD_NULLDATA_PAYLOAD` | 80 bytes (gold events: limit set by gold spec, at most `MAX_NULLDATA_PAYLOAD`) |
| `MAX_STANDARD_NULLDATA_OUTPUTS` | 1 per transaction |
| `MAX_STANDARD_TX_SIGOPS` | 4,000 |
| `STANDARD_SEQUENCES` | {0xFFFFFFFE, 0xFFFFFFFF} |
| `MAX_MEMPOOL_BYTES` | 100,000,000 (sum of serialized sizes) |
| `MEMPOOL_EXPIRY` | 1,209,600 s (14 days) |
| `ROLLING_FEE_HALFLIFE` | 43,200 s (12 h) |
| `MAX_ORPHAN_TXS` / per peer | 100 / 25 |
| `ORPHAN_TX_EXPIRY` | 1,200 s |
| `COINBASE_RESERVED_SIZE` | 1,000 bytes (template space kept for header + coinbase) |
| `MAX_AUTO_REORG_DEPTH` | 100 blocks (per network param, §3.4) |
| `VERIFY_BLOCKS_AT_STARTUP` | 24 (flag `--checkblocks=N`, 0 = all) |
| `MAX_BLOCKS_PER_STEP` | 32 |
| `UNREQUESTED_BLOCK_WINDOW` | 288 blocks |
| `MAX_HEADERS_PER_MSG` / `MAX_LOCATOR_HASHES` | 2,000 / 101 |
| `MAX_TIP_AGE` (initial-download test) | 86,400 s |

### 3.3 `ChainParams` structure

New files `include/chainparams.h`, `src/chainparams.cpp`. There is no mutable global: `ChainState`, `Mempool`, `BlockAssembler`, the wallet and the network code receive `const ChainParams&`.

```cpp
namespace Bucks {
enum class NetworkId : uint8_t { MAIN = 0, TEST = 1, REGTEST = 2 };
struct Checkpoint { uint32_t height; Hash256 hash; };

struct ChainParams {
    NetworkId   id;
    const char* name;                    // "main", "test", "regtest"
    std::array<uint8_t, 4> netMagic;     // P2P frame start, in wire order
    uint16_t    defaultP2PPort;
    uint16_t    defaultApiPort;
    uint8_t     p2pkhAddressVersion;     // Base58Check version byte
    // Genesis (§3.5). The node rebuilds genesis from these fields at startup.
    uint32_t    genesisTime;
    uint32_t    genesisNonce;
    std::string genesisInscription;      // ASCII, at most 96 bytes
    Hash256     genesisMerkleRoot;       // expected
    Hash256     genesisHash;             // expected; all-zero = "not configured"
    // Proof of work (§6)
    uint32_t    powLimitBits;            // minimum difficulty; also the genesis bits
    U256        powLimit;                // decodeCompact(powLimitBits)
    bool        powNoRetargeting;        // true only on regtest
    // Money
    uint32_t    halvingInterval;
    // Chain selection safety (§8.5)
    std::vector<Checkpoint> checkpoints; // ascending height; at least {0, genesisHash}
    U256        minimumChainWork;
    uint32_t    maxAutoReorgDepth;
    // Gold module parameters (vault keys, ...) are added by the gold spec.
};
const ChainParams& chainParams(NetworkId id);
}
```

### 3.4 Network values

| Field | main | test | regtest |
|---|---|---|---|
| `netMagic` (wire bytes) | `B7 C5 E3 A9` | `B7 C5 E3 7E` | `B7 C5 E3 5D` |
| `defaultP2PPort` | 8733 | 18733 | 18744 |
| `defaultApiPort` | 8080 | 18080 | 18090 |
| `p2pkhAddressVersion` | 0x1A (addresses start with `B`) | 0x41 (`T`) | 0x7F (`t`) |
| `powLimitBits` = genesis bits | `0x1d00ffff` | `0x1d00ffff` | `0x207fffff` |
| `powNoRetargeting` | false | false | true |
| `halvingInterval` | 210,000 | 210,000 | 150 |
| `maxAutoReorgDepth` | 100 | 100 | 100 (tests may override) |
| `minimumChainWork` | 0 at launch, raised every release | 0, raised every release | 0 |
| `checkpoints` | {0, genesis} at launch | {0, genesis} | {0, genesis} |
| genesis inscription | chosen by owner at launch (§16 Q5) | `Bucks testnet genesis` | `Bucks regtest genesis` |
| genesis time | launch time | fixed at implementation | 1767225600 (2026-01-01 00:00:00 UTC) |

Notes:
- All three magics differ from Bitcoin's and from the old Bucks magic `F5 B3 B4 D9` (`include/network.h:27`), so old nodes and other networks cannot talk to v2 nodes.
- P2P ports no longer reuse Bitcoin's 8333 (`src/main.cpp:48`, NET-18).
- Address version bytes no longer equal Bitcoin's 0x00 (CRY-17). The wallet (`src/wallet.cpp:204`) MUST use `params.p2pkhAddressVersion` for encoding and MUST reject any other version when decoding.
- Regtest exists for tests: blocks need about 2 hashes, retargeting is off, halving every 150 blocks.

### 3.5 Genesis block

**Construction (same on every network):**

| Part | Value |
|---|---|
| Header | version 2, height 0, prevBlockHash = 32 zero bytes, merkleRoot = txid(coinbase), time = `genesisTime`, bits = `powLimitBits`, nonce = `genesisNonce` |
| Coinbase | version 2; one input: prevTxid = 32 zero bytes, prevIndex = 0xFFFFFFFF, scriptSig = `LE32(0) ‖ inscription bytes`, sequence 0xFFFFFFFF; one output: amount 0, scriptPubKey = `6a` (NULL_DATA with empty payload); lockTime 0 |

The genesis block creates **no coins**: there is no premine and no placeholder payout key (the current genesis pays 50 BUCKS to an all-zero hash160, `src/genesis.cpp:98-111`, OPS-13). The 50 BUCKS subsidy of height 0 is never created (Bitcoin's genesis output is also unspendable).

**Hardcoding and checks (in this order at startup):**
1. If `genesisHash` is all zero (mainnet before launch), the node refuses to start that network: `FATAL: <network> genesis is not configured in this build`, exit code 3.
2. `ChainParams` holds the genesis fields and the expected merkle root and hash. `Genesis::build(params)` rebuilds the block. The node checks: computed merkle root == `genesisMerkleRoot`, computed hash == `genesisHash`, `CheckBlockHeader` (PoW) passes, `CheckBlock` passes. On any mismatch it prints `FATAL: genesis mismatch on <network>: expected <hash>, computed <hash>` and exits with code 3.
3. The node never mines genesis at runtime. Delete the call to `Genesis::findGenesisNonce` from `src/blockchain.cpp:151` and the runtime miner `src/genesis.cpp:126-155` (CON-20).
4. When the node opens an existing datadir, the stored network name and genesis hash (storage spec, `chain_state` table) MUST equal the params. Otherwise: `FATAL: datadir belongs to network <x> genesis <y>`, exit code 3. An empty datadir is initialised with the genesis index entry (status `HEADER_VALID | HAVE_DATA | DATA_CHECKED | CONNECTED_VALID | IN_MAIN_CHAIN`, chain work = `blockProof(bits)`), an empty UTXO set, the initial gold state (gold spec) and best block = genesis. Genesis has no undo record and is never disconnected; `ConnectBlock` is never run on it.

**Launch procedure (mainnet; testnet at implementation time):**
1. The owner fixes the inscription (ASCII, at most 96 bytes) and the genesis time (the announced launch time).
2. Run `bucks-mine-genesis --network=main --time=<t> --inscription="<text>"` (new tool `tools/mine_genesis.cpp`, separate CMake target, not linked into the node). It prints nonce, merkle root and hash. At 2^32 expected hashes this takes minutes on one CPU core. If the 32-bit nonce space is exhausted, the tool increments `time` by 1 and continues, and prints the final time.
3. Paste the values into `src/chainparams.cpp`; add checkpoint `{0, hash}`.
4. Publish the hash and inscription in the launch announcement before the launch time (fair launch).

**Regtest genesis vector** (computed by an independent Python implementation):

| Item | Value |
|---|---|
| Coinbase bytes (86) | `02000000` `01` `00…00` (32) `ffffffff` `19` `00000000` `4275636b7320726567746573742067656e65736973` `ffffffff` `01` `0000000000000000` `01` `6a` `00000000` |
| txid = merkle root | `e3cc1903035c668a5d3b09ad4873b414adaaaa5894a93dbd1b13a759b33f36d5` |
| Header bytes (84) | `02000000` `00000000` `00…00` (32) `e3cc…36d5` `00b95569` `ffff7f20` `00000000` |
| nonce | 0 |
| Genesis hash | `3491115920c29000d16f7d776bbab4a3399e5065abeeaf73fa23a273406bf520` |
| Genesis chain work | 2 |

### 3.6 Launch difficulty (CON-17)

**Decision:** `powLimitBits` = genesis bits = `0x1d00ffff` on main and test. Target = `0xffff × 2^208`; expected work per block = 2^32 ≈ 4.3 × 10^9 hashes. This is Bitcoin's own minimum difficulty.

**Why:**
1. *No overflow.* The retarget multiplies target × timespan before dividing (§6.5). With today's `0x1f00ffff` (`include/consensus.h:81`) the product needs 263 bits and overflows 256-bit arithmetic. With `0x1d00ffff` it needs 247 bits. The node asserts this at startup (§3.7).
2. *No stall.* One ordinary CPU core (about 10^7 SHA256d per second) finds a block in about 7 minutes, so the chain keeps moving even if only one small miner is online.
3. *No instant supply grab.* It is 65,536 times harder than today's 2^16 hashes per block, which let one GPU make thousands of blocks per second (CON-17).
4. *Header spam costs real work.* Each fake header costs at least 2^32 hashes (§8.6).

**What it does not solve, and the mitigations:**
- *Early supply capture.* If launch hashrate is much higher than 2^32 hashes per 10 minutes, the first difficulty periods run fast until the 4x-per-period increase catches up. Mitigation: announce launch time and genesis inscription in advance so everyone can mine from block 1. The owner may set a higher launch difficulty if the launch hashrate is known (§16 Q1); never set it higher than what the expected launch miners produce in 10 minutes, because a 2016-block period at too-high difficulty can stall the chain for months.
- *51% attacks while hashrate is small.* Mitigations: checkpoints and `minimumChainWork` in every release; the automatic reorg limit (§8.5); recommendation to exchanges: wait at least 120 confirmations in the first year (above the 100-block reorg limit, so a node that saw the deposit confirmed will not reorg it away automatically); merge mining is an owner decision (§16 Q3).
- *Rented Bitcoin ASICs.* Stock Bitcoin ASIC chips hash exactly 80-byte headers (fixed padding in the second SHA-256 block). The 84-byte Bucks header (§4.4) cannot be computed by them, so the Bitcoin ASIC fleet cannot simply be pointed at Bucks. This is a deliberate deviation from Bitcoin's 80-byte header. Custom hardware could still be built later.

### 3.7 Startup assertions on parameters

The node MUST abort (exit code 3) if any of these fail:
1. `powLimit == decodeCompact(powLimitBits)` and the decode is valid (not negative, no overflow, not zero).
2. If `!powNoRetargeting`: `powLimit × 4 × T < 2^256` (checked with `U256` bit lengths: `bits(powLimit) + bits(4T) ≤ 256`).
3. Genesis checks of §3.5.
4. `checkpoints` is sorted, starts with `{0, genesisHash}`.
5. All networks have distinct `netMagic`, ports and address versions (unit test, not runtime).

---

## 4. Serialization (byte layouts)

### 4.1 Primitives

| Type | Encoding |
|---|---|
| `u8`, `u16`, `u32`, `u64` | little-endian, fixed width |
| `Hash256` | 32 raw bytes |
| `CompactSize` | see table below; always minimal |
| `VarBytes(max)` | `CompactSize len` (len ≤ max and ≤ remaining bytes) then `len` raw bytes |

CompactSize:

| Value | Encoding | Bytes |
|---|---|---|
| 0 … 252 | the value as one byte | 1 |
| 253 … 65,535 | `FD` + u16 | 3 |
| 65,536 … 4,294,967,295 | `FE` + u32 | 5 |
| ≥ 2^32 | `FF` + u64 | 9 |

Rules:
- The writer MUST emit the shortest form. (Old `writeVarInt`, `src/transaction.cpp:56-64`, wrote 2 bytes for any value ≥ 253 and truncated values ≥ 65,536: CON-14.)
- The reader MUST reject a non-minimal form (for example `FD FC 00`), MUST accept the `FE` and `FF` forms (old reader threw on them, `src/transaction.cpp:97`), and MUST reject any value above the caller's maximum. Every call site passes a maximum; the global hard maximum is 33,554,432.
- Vectors: 0 → `00`; 252 → `fc`; 253 → `fdfd00`; 65,535 → `fdffff`; 65,536 → `fe00000100`; 100,000 → `fea0860100`; 4,294,967,295 → `feffffffff`. Reject: `fdfc00`, `fe00010000`, `feffff0000`.

### 4.2 Script templates (the only scripts that exist)

**P2PKH output** (`scriptPubKey`, exactly 25 bytes):

`76 a9 14 <20-byte HASH160(pubkey)> 88 ac`

**NULL_DATA output** (`scriptPubKey`, canonical "OP_RETURN + one push"):

| Payload length L | Bytes | Total length |
|---|---|---|
| 0 | `6a` | 1 |
| 1 … 75 | `6a`, `L`, payload | L + 2 |
| 76 … 255 | `6a`, `4c`, `L`, payload | L + 3 |
| 256 … 1,024 | `6a`, `4d`, `L` as u16, payload | L + 4 |

Exactly one form is valid for each L (minimal push), the script length MUST equal the total length in the table, and L ≤ `MAX_NULLDATA_PAYLOAD`. Any other `scriptPubKey` (any other opcode, any other length, P2PK, multisig, P2SH, `51`/OP_TRUE, empty script) makes the transaction invalid (`bad-txns-output-type`).

**P2PKH unlock** (`scriptSig` of every non-coinbase input), exact layout:

| Offset | Size | Content | Rule |
|---|---|---|---|
| 0 | 1 | `L` = DER length + 1 | 9 ≤ L ≤ 73 |
| 1 | L − 1 | DER signature | strict DER, r and s in [1, n−1], s ≤ n/2 (checked by `verifyECDSA`, `src/crypto.cpp:527-580`) |
| L | 1 | sighash type | MUST be `01` |
| L + 1 | 1 | `21` (push of 33 bytes) | exact |
| L + 2 | 33 | compressed public key | `02`/`03` prefix; `isValidPublicKey` (`include/crypto.h:73`) |

The `scriptSig` length MUST be exactly L + 35 (44 … 108 bytes). With low-S signatures the real maximum is 107 bytes (DER ≤ 71). Anything else: `bad-scriptsig-format`.

Why templates instead of a script engine: the node never executes code from the chain; every spend is the well-studied P2PKH pattern; there are no unknown or "anyone-can-spend" outputs (CRY-4, CON-3, DB-3). The dead `ScriptInterpreter` (`src/script.cpp:10-133`) is deleted (§12).

### 4.3 Transaction v2

| Field | Type | Rule (checked in §7.4) |
|---|---|---|
| `version` | u32 | MUST be 2 |
| input count | CompactSize | 1 … 2,000 |
| inputs | see below | |
| output count | CompactSize | 1 … 3,000 |
| outputs | see below | |
| `lockTime` | u32 | finality rule B-FINAL (§7.4) |

Input:

| Field | Type |
|---|---|
| `prevTxid` | Hash256 |
| `prevIndex` | u32 |
| `scriptSig` | VarBytes(MAX_TX_SIZE): coinbase data (4 … 100 bytes) or P2PKH unlock |
| `sequence` | u32 |

Output:

| Field | Type |
|---|---|
| `amount` | u64 (P2PKH: 1 … MAX_MONEY; NULL_DATA: exactly 0) |
| `scriptPubKey` | VarBytes(MAX_TX_SIZE): P2PKH or NULL_DATA |

- There is **no** witness, no SegWit marker/flag, no separate `pubKey` field, no `timestamp`, no `fee_amount` (remove `include/transaction.h:25, 45-48, 59-61, 72, 85-86`). Everything validation needs is inside the serialized bytes.
- `txid = SHA256d(serialization)`. The signature and public key are inside `scriptSig`, so the txid commits to them (fixes CRY-6, CON-4, DB-V1, DB-10).
- Third parties cannot change a valid transaction's txid: strict DER, low-S, the fixed sighash byte, exact templates, minimal CompactSize and "no trailing bytes" remove every known malleation (CRY-5, CON-12). The signer itself can re-sign (new txid); this is acceptable because the v1 mempool never accepts spends of unconfirmed outputs (§9).
- Transaction size = length of the serialization. It MUST be ≤ 100,000 and MUST NOT be exactly 64 bytes (BIP54; protects the merkle tree against the 64-byte-transaction ambiguity, CVE-2017-12842). Under the other rules every valid transaction is at least 65 bytes, so this rule is a safety net.
- `isCoinbase(tx)` := exactly one input AND `prevTxid` = 32 zero bytes AND `prevIndex` = 0xFFFFFFFF. (Old `isCoinbase`, `src/transaction.cpp:289-294`, ignored `prevIndex`.)
- Typical sizes: one P2PKH input is 148 bytes at most, one P2PKH output 34 bytes, fixed overhead 10 bytes; a 1-input 2-output payment is at most 226 bytes.

### 4.4 Block header v2 (84 bytes)

| Offset | Size | Field | Rule |
|---|---|---|---|
| 0 | 4 | `version` u32 | MUST be 2 |
| 4 | 4 | `height` u32 | MUST equal parent height + 1 (genesis: 0) |
| 8 | 32 | `prevBlockHash` | hash of the parent header |
| 40 | 32 | `merkleRoot` | §4.6 |
| 72 | 4 | `time` u32 | §6.6 |
| 76 | 4 | `bits` u32 | MUST equal `GetNextWorkRequired(parent)` |
| 80 | 4 | `nonce` u32 | free |

`blockHash = SHA256d(84 header bytes)`. The layout is the current one (`src/block.cpp:21-62`). The `height` field is kept: it is now checked against the parent on every path, and it keeps the header at 84 bytes (§3.6). Time arithmetic is done in `int64_t`.

### 4.5 Block

| Field | Type | Rule |
|---|---|---|
| header | 84 bytes | §4.4 |
| tx count | CompactSize | ≥ 1; parser cap 15,384 |
| transactions | tx v2 serializations, back to back | first is the coinbase |

- No magic bytes (old code wrote Bitcoin's `F9 BE B4 D9`, `src/block.cpp:157-161`), no per-transaction length prefix (`src/block.cpp:179-185`), no trailing bytes.
- Block size = total length of this serialization. It MUST be ≤ 1,000,000 bytes.
- The same bytes are used on the wire and on disk. Remove the per-transaction debug output (`src/block.cpp:165, 170, 181, 190`, NET-14).

### 4.6 Merkle root with mutation detection

```
MerkleRoot(leaves) -> (root, mutated)        // leaves = txids in block order, at least 1
    mutated = false
    level = leaves
    while level.size() > 1:
        for i = 0, 2, 4, ... while i + 1 < level.size():
            if level[i] == level[i+1]: mutated = true
        if level.size() is odd: append a copy of the last element
        level = [ SHA256d(level[i] ‖ level[i+1]) for i = 0, 2, 4, ... ]
    root = level[0]                          // one leaf: root = that txid
```

The root equals the current one (`src/merkle.cpp:12-29`); the new `mutated` flag is Bitcoin's defence against CVE-2012-2459 (`[a,b,c]` and `[a,b,c,c]` have the same root). A block with `mutated = true` or with a root that does not match the header is a **MUTATED** result: this copy is rejected but the header is **not** marked invalid (§7.1), because the honest block with the same header may be valid (CON-13).

Vectors (leaves a = SHA256d("a"), b = SHA256d("b"), c = SHA256d("c")):

| Leaves | Root | mutated |
|---|---|---|
| [a] | `bf5d3affb73efd2ec6c36ad3112dd933efed63c4e1cbffcfa88e2759c144f2d8` | no |
| [a, b] | `b767a3a12f5f8bb1949d163c51f9a42e6bda8dcd02d50353717f73d4338b1bf0` | no |
| [a, b, c] | `74449b8328cb6e97d305adb2fca5e90993fdf9c667fa40cb625f40508da40cbf` | no |
| [a, b, c, c] | `74449b8328cb6e97d305adb2fca5e90993fdf9c667fa40cb625f40508da40cbf` | **yes** |
| [a, a] | `dd2b2a71c44ea394239c2ee2cd6541048fe397d6bc68554241dd195be71e3af2` | **yes** |

### 4.7 Block undo record v1

One record per connected block (except genesis). The storage spec stores it as a blob keyed by block hash; both specs MUST use this exact layout.

| Field | Type | Rule |
|---|---|---|
| `version` | u8 | MUST be 1 |
| `blockHash` | Hash256 | MUST equal the hash of the block it undoes |
| `spentCount` | CompactSize | MUST equal the number of non-coinbase inputs in the block |
| `spent[]` | coin records | in spend order: transaction order, then input order |
| `goldUndoLen` | CompactSize | ≤ 65,536 |
| `goldUndo` | bytes | format owned by the gold spec; length 0 if the block has no gold events |
| `checksum` | Hash256 | `SHA256d` of all previous bytes of this record |

Coin record:

| Field | Type | Rule |
|---|---|---|
| `height` | u32 | height of the block that created the coin |
| `flags` | u8 | bit 0 = created by a coinbase; bits 1–7 MUST be 0 |
| `amount` | u64 | 1 … MAX_MONEY |
| `scriptPubKey` | VarBytes(25) | MUST be a P2PKH script (25 bytes) |

Parsing uses the bounded reader (§11). Wrong version, wrong block hash, count mismatch, bad checksum, bad flags, a bad script or trailing bytes mean the database is corrupt: result INTERNAL (§7.1) and the node stops (§8.7). A missing record is also corruption (the old `loadUndoData` returned success for a missing row, `src/blockdb.cpp:360-376`). This fixes DB-7 (height was dropped, `src/blockdb.cpp:300-345`).

Size example: one spent input, no gold events: 1 + 32 + 1 + 39 + 1 + 32 = 106 bytes.

---

## 5. Signature hash v1 and spend verification

### 5.1 Definition

Inputs: the transaction `tx`, the input index `i`, the list `spent[k]` of coins spent by every input `k` (amount and scriptPubKey, in input order), and `genesisHash` of the network.

```
shaPrevouts      = SHA256( for each input k:  prevTxid_k ‖ LE32(prevIndex_k) )
shaAmounts       = SHA256( for each input k:  LE64(spent_k.amount) )
shaScriptPubKeys = SHA256( for each input k:  CompactSize(len) ‖ spent_k.scriptPubKey )
shaSequences     = SHA256( for each input k:  LE32(sequence_k) )
shaOutputs       = SHA256( for each output j: LE64(amount_j) ‖ CompactSize(len) ‖ scriptPubKey_j )

SigMsg(i) = genesisHash (32)
          ‖ 0x01                    (sighash type, 1 byte)
          ‖ LE32(tx.version)
          ‖ LE32(tx.lockTime)
          ‖ shaPrevouts ‖ shaAmounts ‖ shaScriptPubKeys ‖ shaSequences ‖ shaOutputs
          ‖ LE32(i)                 -> always 205 bytes

SigHash(i) = TaggedHash("Bucks/SigHash/v1", SigMsg(i))
```

- The five `sha*` values are computed once per transaction and reused for every input, so hashing cost is linear in transaction size (no quadratic sighash).
- ECDSA signs and verifies `SigHash(i)` as the 32-byte digest.
- `SHA256("Bucks/SigHash/v1")` = `08072aea4347c380bf052ff17e2ea8ffd5d3310ef262d660be6c9a4f3ee1213f`.

What it commits to and why:

| Committed data | Reason |
|---|---|
| `genesisHash` | a signature is valid on one network only; no replay to testnet or to a future fork (CRY-16) |
| sighash type | domain separation for future types |
| version, lockTime, all sequences | finality data cannot be changed |
| all prevouts, input index | every input has its own digest (old digest ignored the index, `src/transaction.cpp:229-242`, CRY-16) |
| all spent amounts and spent scripts | an offline or hardware signer can check the fee from the data it signs; a lying online host cannot make it overpay (the BIP341 lesson) |
| all outputs | payee and amounts are fixed |

Deviation from Bitcoin: one fixed message format (BIP341-style) instead of BIP143's per-input amount only; tagged hash instead of SHA256d; genesis hash included. Only `SIGHASH_ALL` exists.

### 5.2 Spend verification (rule C-SIG, for every input of every non-coinbase transaction)

1. The spent coin's `scriptPubKey` is P2PKH. (Always true, because only P2PKH outputs enter the UTXO set; if not, the database is corrupt: INTERNAL.)
2. Parse `scriptSig` with the unlock template (§4.2). Format errors were already rejected by T-UNLOCK.
3. `isValidPublicKey(pubkey)` → else `bad-pubkey`.
4. `HASH160(pubkey)` equals bytes 3 … 22 of the spent script → else `bad-pubkey-hash`.
5. `verifyECDSA(SigHash(i), der, pubkey)` → else `bad-sig`. (Phase 1 already makes `verifyECDSA` fail closed, enforce strict DER, the r/s range and low-S: `src/crypto.cpp:527-580`.)

No input is ever skipped. The old `verifyInput` (`src/transaction.cpp:265-283`) and the P2PKH-only ownership check (`src/validation.cpp:212-249`) are replaced by this rule.

### 5.3 Signing (wallet)

New module `include/sighash.h`, `src/sighash.cpp`:

```cpp
struct SpentOutput { uint64_t amount; std::vector<uint8_t> scriptPubKey; };
class SigHashCache {                 // the five sha* values, built once per tx
public:
    SigHashCache(const Transaction& tx, const std::vector<SpentOutput>& spent);
    Hash256 sigHash(uint32_t inputIndex, const Hash256& genesisHash) const;
};
bool signP2PKHInput(Transaction& tx, uint32_t i, const std::vector<SpentOutput>& spent,
                    const PrivateKey& key, const Hash256& genesisHash);   // sets scriptSig
bool verifyP2PKHInput(const Transaction& tx, uint32_t i, const SigHashCache& cache,
                      const SpentOutput& spent, const Hash256& genesisHash, ValidationState& st);
```

`Wallet::createTransaction` (`src/wallet.cpp:228-284`) MUST pass the amounts and scripts of the coins it spends (it already has them from the UTXO lookup) and the network's genesis hash; it MUST skip coinbase coins that are not yet mature and coins already spent by a mempool transaction (the UTXO query must therefore return `height` and `isCoinbase`; today `getUTXOsByAddress`, `src/utxodb.cpp:166-213`, does not). `Transaction::signInput`, `verifyInput`, `legacySignatureHash` and `verify` are deleted.

### 5.4 Test vector V-TX-1 (regtest)

Test keys only (derived from public strings; never use them for real coins):

| Item | Value |
|---|---|
| priv1 = SHA256("bucks-test-key-1") | `a4647b6f567b1eaf074395801d6a55eb2f879f7db66e168bf6047f0a30787b67` |
| pub1 | `022234063b5187e027c46ef631d200ac1ee5d43d75b8829a5048ff8983016579cf` |
| HASH160(pub1) | `2cf8fb72ece70bb7c6f945682438f2a6363fa6c2` |
| pub2 (from SHA256("bucks-test-key-2")) | `02c4af81bcbf6616ba60679b7afc46be28a54e83fcef067ff91a6b36dfced2fc4d` |
| HASH160(pub2) | `e3a42ed3976ddeecae5a126beee85549f1b11257` |
| genesisHash (regtest) | `3491115920c29000d16f7d776bbab4a3399e5065abeeaf73fa23a273406bf520` |

Transaction: version 2, lockTime 0.

| | prevTxid | prevIndex | sequence | spent amount | spent script |
|---|---|---|---|---|---|
| input 0 | SHA256d("bucks-test-prev-A") = `b5bc37193893ea8bd44b15fa740ef4a1b55178c9bd4311346a14767a8d95231e` | 1 | `ffffffff` | 1,000,000,000 | P2PKH(HASH160(pub1)) |
| input 1 | SHA256d("bucks-test-prev-B") = `b8081d4d2e3b6439ca8ecf94856e0a6226ba328e9df210e08d4a41a8ac1bede6` | 0 | `fffffffe` | 250,000,000 | P2PKH(HASH160(pub1)) |

Outputs: 1,200,000,000 to P2PKH(HASH160(pub2)); 0 to NULL_DATA payload "bucks" (`6a056275636b73`). Fee = 50,000,000.

| Value | Hex |
|---|---|
| shaPrevouts | `fe68af17573bbe41be03fc6cde72ee700ccf70117d4dd5d986fc6f6a65a2c9bb` |
| shaAmounts | `99e5fbfa681540cb4c240d12f83d87fd47adb5f3fc812f13c510d31ea61b6eda` |
| shaScriptPubKeys | `92a8962ec3f93268586e9700f4e3b33c63d385a9bc0fc59309f3c4bcd83b0957` |
| shaSequences | `7b60d99b32d116039fdaa77759a1b104b40ccfefd1322a853e799eb2a929eb2c` |
| shaOutputs | `5274df455cf3d3286857f7752a3bd8639a5396821738515333507bc6d07358f7` |
| SigHash(0) | `249b5cb1729c49206fa542cb31ba35436357cb29b2e598bd522b22c8cc66242e` |
| SigHash(1) | `a57df31365c35160c1cdff343169bd8278b128297df8ca44319d859c978e53db` |
| sig 0 (DER, RFC6979, low-S) | `304402202501be07e71d53496ba8eaf91eb75647247b9b1a6fab4c6e40902e239c79d05b02207ae641960c78565d17ca9257e5337303b3ac56789eea6fe51718a117b386620e` |
| sig 1 (DER, RFC6979, low-S) | `304402206bbdb2e3d4e972aad2e7b6f2a436d656f03bef746ac8e18afd39d0c6c98f861d02203f106868c0f5482226cb3d0a09c004d791908722d2ca37e62e024312a0cf45f2` |
| full transaction (354 bytes) | `0200000002b5bc37193893ea8bd44b15fa740ef4a1b55178c9bd4311346a14767a8d95231e010000006a47304402202501be07e71d53496ba8eaf91eb75647247b9b1a6fab4c6e40902e239c79d05b02207ae641960c78565d17ca9257e5337303b3ac56789eea6fe51718a117b386620e0121022234063b5187e027c46ef631d200ac1ee5d43d75b8829a5048ff8983016579cfffffffffb8081d4d2e3b6439ca8ecf94856e0a6226ba328e9df210e08d4a41a8ac1bede6000000006a47304402206bbdb2e3d4e972aad2e7b6f2a436d656f03bef746ac8e18afd39d0c6c98f861d02203f106868c0f5482226cb3d0a09c004d791908722d2ca37e62e024312a0cf45f20121022234063b5187e027c46ef631d200ac1ee5d43d75b8829a5048ff8983016579cffeffffff02008c8647000000001976a914e3a42ed3976ddeecae5a126beee85549f1b1125788ac0000000000000000076a056275636b7300000000` |
| txid | `4fd3e27719f43e962d304c52c2a2bff9f14444747abe9079a856f425d10d4740` |
| SigHash(0) with input-0 amount + 1 | `8ed3413f3b0362bc54e2dba1e572f26882e3d4fa1405bc248b81d38c1913e8f1` (signature must fail) |
| SigHash(0) with genesisHash = 32 zero bytes | `1294ce50e0bc49400af27097cfa899499f1fb11dcd1689206bdbc74142952f3e` (signature must fail) |

OpenSSL signs with a random nonce, so the C++ signer will produce different (equally valid) signatures; tests compare the sighash values, verify the vector signatures, and compare the full transaction bytes and txid after parsing.

---

## 6. Proof of work, chain work and difficulty

### 6.1 256-bit unsigned integer `U256`

New files `include/u256.h`, `src/u256.cpp` (no new dependency). Representation: `std::array<uint32_t, 8>`, limb 0 is the least significant (like Bitcoin's `base_uint<256>`). Required operations, all fixed-size, no heap:

| Operation | Notes |
|---|---|
| `fromBytesBE(Hash256)`, `toBytesBE()` | byte 0 = most significant (§0) |
| `==`, `<`, `<=`, `>`, `>=` | |
| `+`, `-` | modulo 2^256; callers that must not wrap check `bits()` first |
| `*= uint32_t` | used by retarget (timespan < 2^23) |
| `/ U256` | shift-and-subtract long division; division by zero is a programming error (assert) |
| `<<`, `>>` by any number of bits | a shift by 256 or more gives 0 (needed by `DecodeCompact` for large exponents) |
| `~` (bitwise not) | used by `blockProof` |
| `bits()` | position of highest set bit + 1 (0 for zero) |
| `low64()`, `isZero()` | |

Tests compare against a Python big-integer reference (vectors embedded in the test file).

### 6.2 Compact `bits` encoding

```
DecodeCompact(c) -> (target, negative, overflow)
    size = c >> 24
    word = c & 0x007FFFFF
    if size <= 3:  target = word >> (8 * (3 - size))
    else:          target = U256(word) << (8 * (size - 3))
    negative = (word != 0) and (c & 0x00800000) != 0
    overflow = (word != 0) and (size > 34 or (word > 0xFF and size > 33)
                                          or (word > 0xFFFF and size > 32))

EncodeCompact(target) -> c
    size = (target.bits() + 7) / 8
    if size <= 3:  c = target.low64() << (8 * (3 - size))
    else:          c = (target >> (8 * (size - 3))).low64()
    if c & 0x00800000: c >>= 8; size += 1
    c |= size << 24
```

Rule H-BITS (context-free): `bits` is valid only if not negative, not overflow, target ≠ 0 and target ≤ powLimit. Because H-DIFF requires `bits == GetNextWorkRequired(parent)` and that value comes from `EncodeCompact`, only canonical encodings can appear in valid headers.

| bits | target (hex) | negative | overflow | EncodeCompact(target) |
|---|---|---|---|---|
| `0x00000000` | 0 | no | no | `0x00000000` |
| `0x01003456` | 0 | no | no | `0x00000000` |
| `0x01123456` | `12` | no | no | `0x01120000` |
| `0x02123456` | `1234` | no | no | `0x02123400` |
| `0x03123456` | `123456` | no | no | `0x03123456` |
| `0x04123456` | `12345600` | no | no | `0x04123456` |
| `0x04923456` | `12345600` | **yes** | no | invalid |
| `0x05009234` | `92340000` | no | no | `0x05009234` |
| `0x20123456` | `123456` followed by 29 zero bytes | no | no | `0x20123456` |
| `0xff123456` | — | no | **yes** | invalid |
| `0x21010000` | — | no | **yes** | invalid |
| `0x1d00ffff` | `00000000ffff` followed by 26 zero bytes | no | no | `0x1d00ffff` |
| `0x207fffff` | `7fffff` followed by 29 zero bytes | no | no | `0x207fffff` |

### 6.3 Proof-of-work check (rule H-POW)

`CheckProofOfWork(hash, bits, params)`: H-BITS passes AND `U256::fromBytesBE(hash) ≤ target`. This is a full 256-bit comparison. The old 3-byte comparison after a zero prefix (`src/validation.cpp:91-143`) and its copy in `src/genesis.cpp:27-57` are deleted; they ignored the bytes below the mantissa and treated the sign bit inconsistently (CON-17).

### 6.4 Block proof and chain work

```
blockProof(bits) = floor(2^256 / (target + 1))  =  (~target / (target + 1)) + 1
chainWork(genesis) = blockProof(genesis.bits)
chainWork(b)       = chainWork(parent(b)) + blockProof(b.bits)
```

Chain work is computed only from headers that passed H-BITS, H-POW and H-DIFF, and is stored per block index entry as `U256` (persisted as 32 bytes big-endian by the storage spec, recomputed and compared at startup). It replaces `workForBits`/`accumWork` (`src/blockchain.cpp:21-42`), which returned 0 for every real target (CON-2, NET-V1).

| bits | blockProof |
|---|---|
| `0x1d00ffff` | `0x100010001` = 4,295,032,833 |
| `0x1e00ffff` | `0x1000100` = 16,777,472 |
| `0x207fffff` | 2 |
| `0x1c05a3f4` | `0x2d62f2a3b3` = 194,933,597,107 |

### 6.5 Difficulty retarget (rule H-DIFF)

```
GetNextWorkRequired(parent, params) -> bits
    if params.powNoRetargeting: return parent.bits
    h = parent.height + 1
    if h % 2016 != 0: return parent.bits
    first = parent.getAncestor(h - 2016)            // Bitcoin's window: 2016 blocks, 2015 intervals
    return CalculateNextWorkRequired(parent.bits, first.time, parent.time, params)

CalculateNextWorkRequired(lastBits, firstTime, lastTime, params) -> bits
    actual = int64(lastTime) - int64(firstTime)      // signed 64-bit, never uint32
    if actual < T/4: actual = T/4                    // T = 1,209,600
    if actual > 4*T: actual = 4*T
    target = DecodeCompact(lastBits)
    target *= uint32(actual)                         // fits: powLimit * 4T < 2^256 (§3.7)
    target  = target / U256(T)
    if target > params.powLimit: target = params.powLimit
    return EncodeCompact(target)
```

The ancestor is found through the block index (`getAncestor`), never through a height-indexed vector of the active chain (old code: `blocks[firstBlockHeight]`, `src/blockchain.cpp:703`). The old code subtracted in `uint32_t` (`src/blockchain.cpp:705`), so a negative timespan wrapped to about 4.29 × 10^9 and made difficulty 4 times *easier* (CON-7), and it scaled only the 23-bit mantissa (`src/blockchain.cpp:711-737`).

Vectors (the first four are Bitcoin Core's `pow_tests`, recomputed independently; powLimit `0x1d00ffff`):

| last bits | first time | last time | timespan used | result |
|---|---|---|---|---|
| `0x1d00ffff` | 1261130161 | 1262152739 | 1,022,578 | `0x1d00d86a` |
| `0x1d00ffff` | 1231006505 | 1233061996 | 2,055,491 | `0x1d00ffff` (powLimit clamp) |
| `0x1c05a3f4` | 1279008237 | 1279297671 | 289,434 → 302,400 | `0x1c0168fd` |
| `0x1c387f6f` | 1263163443 | 1269211443 | 6,048,000 → 4,838,400 | `0x1d00e1fd` |
| `0x1c05a3f4` | 1000000 | 1000000 + T | T | `0x1c05a3f4` |
| `0x1c05a3f4` | 1000000 | 1000000 + T/2 | T/2 | `0x1c02d1fa` |
| `0x1c05a3f4` | 1000000 | 1000000 − 3600 | −3,600 → T/4 | `0x1c0168fd` (4 times harder; old code: 4 times easier) |
| `0x1d00ffff` | 1000000 | 1000000 + 10T | 4T | `0x1d00ffff` |

### 6.6 Time rules

`MTP(index)` = median time past: take the `time` of `index` and of up to 10 ancestors (fewer near genesis), sort ascending, return element `[n / 2]` where `n` is the number taken. It is computed from the block index of the block's own branch, never from the active tip (old `getMedianTimePast`, `src/blockchain.cpp:674-683`, always used the tip, which is wrong for fork blocks: CON-9).

| Rule | Check (h = header height, all arithmetic in int64) | Reject code | Result |
|---|---|---|---|
| H-MTP | `time > MTP(parent)` | `time-too-old` | INVALID |
| H-TW1 | if `h % 2016 == 0`: `time ≥ parent.time − 600` | `time-timewarp-attack` | INVALID |
| H-TW2 | if `h % 2016 == 2015`: `time ≥ ancestor(h − 2015).time` | `time-period-negative` | INVALID |
| H-FUTURE | `time ≤ now + 7,200` | `time-too-new` | TIME_TOO_NEW |

- H-TW1 is the timewarp fix of the Great Consensus Cleanup as enforced on Bitcoin testnet4 (BIP94, 600 s). It ties the first block of each period to the previous period, so the timespan can no longer be stretched by back-dating period starts. (BIP54 uses the same rule with a 7,200 s grace; Bucks uses the stricter 600 s.)
- H-TW2 is BIP54's fix for negative-timespan games: the last block of a period cannot be earlier than the first. Together with signed arithmetic the measured timespan is never negative.
- We keep Bitcoin's 2015-interval window (the "off-by-one") because H-TW1 closes the attack it enabled, and the formula stays identical to Bitcoin's well-studied one.
- These rules apply on all networks, including regtest, so tests exercise them.
- H-FUTURE is the only header rule that depends on the local clock; it is never cached as invalid.

---

## 7. Validation: one path for every block

### 7.1 Result categories

Every validation function returns a `ValidationState`. It replaces `enum class ValidationError` (`include/validation.h:32-68`), which reported a height mismatch as `INVALID_VERSION` (`src/validation.cpp:289`) and could not express "cannot validate yet".

```cpp
enum class ValidationResult : uint8_t {
    VALID,
    INVALID,          // breaks a consensus rule; permanent
    MUTATED,          // block data does not match its own header (merkle); this copy only
    MALFORMED,        // bytes cannot be parsed (§11)
    MISSING_PARENT,   // parent header unknown: cannot validate yet
    TIME_TOO_NEW,     // header time > now + 2 h: cannot validate yet
    DUPLICATE,        // already known
    INTERNAL,         // local failure: I/O error, out of memory, database corruption
    // mempool only:
    TX_POLICY, TX_MISSING_INPUTS, TX_CONFLICT, TX_PREMATURE
};
struct ValidationState {
    ValidationResult result = ValidationResult::VALID;
    std::string code;     // reject code, e.g. "bad-txns-BIP30"
    std::string detail;   // free text for logs only
};
```

| Result | Store header? | Mark block failed? | Peer penalty (numbers: P2P spec) |
|---|---|---|---|
| INVALID at header stage (A, B) | no | — (not stored) | disconnect and discourage |
| INVALID at body stage (C, D, E) | yes (header was valid) | `FAILED_VALID`; descendants `FAILED_CHILD` | disconnect and discourage |
| MUTATED | header may be stored | **no** | disconnect and discourage |
| MALFORMED | no | no | disconnect and discourage |
| MISSING_PARENT | no | no | none; send `getheaders` with a locator; 10 unconnecting `headers` messages in a row from one peer → small penalty |
| TIME_TOO_NEW | no | no | none |
| DUPLICATE | — | — | none |
| INTERNAL | — | no | none (local problem); corruption stops the node (§8.7) |
| transaction INVALID | — | — | disconnect and discourage |
| TX_POLICY, TX_MISSING_INPUTS, TX_CONFLICT, TX_PREMATURE | — | — | none |

A block is marked `FAILED_VALID` only for a consensus failure of data that matches its header. MUTATED, MALFORMED, TIME_TOO_NEW, MISSING_PARENT and INTERNAL never mark anything, so a relayer cannot get a valid block blacklisted (CON-13). The old network code penalised every `addBlock` failure, including an unknown parent (`src/network.cpp:445-448`), so honest peers on a natural fork were banned (NET-8).

### 7.2 Entry points and the single path

Only these functions may change the block index, the active chain, the UTXO set or the gold state:

| Source | Entry point |
|---|---|
| P2P `headers` | `ProcessNewBlockHeaders(headers, peer)` |
| P2P `block` | `ProcessNewBlock(block, peer, requested)` |
| Local miner, API "mine" | `ProcessNewBlock(block, LOCAL, requested = true)` |
| Startup | `LoadChainState()` → `VerifyDB()` → `ActivateBestChain()` (§8.7) |
| Reindex | `ActivateBestChain()` over stored blocks (§8.7) |
| Template builder | `TestBlockValidity()` — same functions on a scratch view; never commits (§10) |

They all use the same pure rule functions, in this order (stages):

| Stage | Function | Needs |
|---|---|---|
| A | `CheckBlockHeader(header, params)` | nothing |
| B | `ContextualCheckBlockHeader(header, parent, now, params)` | parent index entry |
| C | `CheckBlock(block, params)` → also returns txids and sigop count | block bytes |
| D | `ContextualCheckBlock(block, parent, params)` | parent index entry |
| E | `ConnectBlock(block, index, view, undoOut, params)` | coins view at the parent |
| E⁻¹ | `DisconnectBlock(block, index, view, undo, params)` | coins view at the block |

Deleted (they bypassed rules): `Blockchain::addBlock` fork paths (`src/blockchain.cpp:190-317`), `Blockchain::connectBlock` (`319-453`), `disconnectTip` (`476-545`), `isValidBlock` (`547-553`), `loadPersistedBlocks` (`563-597`), `Validation::checkTransactionInputs`/`checkBlock`/`connectBlock`/`disconnectBlock` (`src/validation.cpp:191-574`), `Block::isValid` (`src/block.cpp:124-148`).

### 7.3 Block rules in check order

Checks stop at the first failure; the first failing rule decides the result.

**Stage A — CheckBlockHeader (context-free, before anything is stored)**

| ID | Rule | Reject code | Result |
|---|---|---|---|
| H-VER | version == 2 | `bad-version` | INVALID |
| H-BITS | bits decode valid (§6.2) | `bad-diffbits-encoding` | INVALID |
| H-POW | hash ≤ target (§6.3) | `high-hash` | INVALID |

**Stage B — ContextualCheckBlockHeader**

| ID | Rule | Reject code | Result |
|---|---|---|---|
| H-PARENT | parent is in the block index | `prev-blk-not-found` | MISSING_PARENT |
| H-PARENT-OK | parent is not `FAILED_VALID`/`FAILED_CHILD` | `bad-prevblk` | INVALID |
| H-HEIGHT | height == parent.height + 1 | `bad-height` | INVALID |
| H-DIFF | bits == `GetNextWorkRequired(parent)` | `bad-diffbits` | INVALID |
| H-MTP | time > MTP(parent) | `time-too-old` | INVALID |
| H-TW1 | timewarp rule (§6.6) | `time-timewarp-attack` | INVALID |
| H-TW2 | non-negative period rule (§6.6) | `time-period-negative` | INVALID |
| H-CKPT | (a) at a checkpoint height the hash MUST equal the checkpoint; (b) a new header MUST NOT have height below the last checkpoint present in the index | `checkpoint-mismatch` / `bad-fork-prior-to-checkpoint` | INVALID |
| H-FUTURE | time ≤ now + 7,200 (checked last) | `time-too-new` | TIME_TOO_NEW |

**Stage C — CheckBlock (context-free body)**

| ID | Rule | Reject code | Result |
|---|---|---|---|
| B-PARSE | block parses (§11), no trailing bytes; raw input larger than 1,000,000 bytes is refused before parsing (DoS guard; the consensus rule is B-SIZE) | `malformed` | MALFORMED |
| B-MERKLE | `MerkleRoot(txids)` == header.merkleRoot (root of an empty list = 32 zero bytes) | `bad-txnmrklroot` | MUTATED |
| B-MUTATED | merkle `mutated` flag is false | `bad-txns-duplicate` | MUTATED |
| B-COUNT | at least one transaction | `bad-blk-length` | INVALID |
| B-SIZE | serialized block size ≤ 1,000,000 | `bad-blk-length` | INVALID |
| B-CB-FIRST | tx 0 is a coinbase | `bad-cb-missing` | INVALID |
| B-CB-ONLY | no other transaction is a coinbase | `bad-cb-multiple` | INVALID |
| B-TX | every transaction passes `CheckTransaction` (§7.4) | the T code | INVALID |
| B-DUP-TXID | no two transactions have the same txid (hash set) | `bad-txns-duplicate` | INVALID |
| B-SIGOPS | Σ `txSigOps` ≤ 20,000 (§7.4) | `bad-blk-sigops` | INVALID |

**Stage D — ContextualCheckBlock**

| ID | Rule | Reject code | Result |
|---|---|---|---|
| B-CB-HEIGHT | first 4 bytes of the coinbase scriptSig, read as LE32, == block height | `bad-cb-height` | INVALID |
| B-FINAL | every transaction is final: `IsFinal(tx, height, MTP(parent))` | `bad-txns-nonfinal` | INVALID |

**Stage E — ConnectBlock** (pseudocode in §7.6)

| ID | Rule | Reject code | Result |
|---|---|---|---|
| C-INPUTS | every input's coin exists in the view at that point of the block | `bad-txns-inputs-missingorspent` | INVALID |
| C-MATURITY | for a coinbase coin: block height − coin height ≥ 100 | `bad-txns-premature-spend-of-coinbase` | INVALID |
| C-INVALUE | every coin amount and the running input sum ≤ MAX_MONEY | `bad-txns-inputvalues-outofrange` | INVALID |
| C-INBELOWOUT | input sum ≥ output sum | `bad-txns-in-belowout` | INVALID |
| C-FEERANGE | running block fee sum ≤ MAX_MONEY | `bad-txns-fee-outofrange` | INVALID |
| C-SIG | §5.2 for every input of every non-coinbase transaction | `bad-pubkey` / `bad-pubkey-hash` / `bad-sig` | INVALID |
| C-BIP30 | a new P2PKH outpoint must not already exist in the view | `bad-txns-BIP30` | INVALID |
| C-GOLD | the gold hook accepts every NULL_DATA output (§7.8) | `bad-gold-event` | INVALID |
| C-CB-AMOUNT | coinbase output sum ≤ subsidy(height) + fees | `bad-cb-amount` | INVALID |

A read error on the coins view is INTERNAL, never INVALID.

Why BIP30 and BIP34 both: B-CB-HEIGHT makes every coinbase txid unique (different heights give different bytes), and C-BIP30 rejects any output that would overwrite an unspent one (old `put` silently replaced it, `src/utxodb.cpp:53-55`: CON-8, API-6, DB-6). With both, the storage layer can use a plain `INSERT` and treat a conflict as a bug.

### 7.4 Transaction rules (stateless `CheckTransaction`)

Used by stage C and by the mempool. Order:

| ID | Rule | Reject code |
|---|---|---|
| T-VERSION | version == 2 | `bad-txns-version` |
| T-VIN | 1 ≤ inputs ≤ 2,000 | `bad-txns-vin-empty` / `bad-txns-vin-toomany` |
| T-VOUT | 1 ≤ outputs ≤ 3,000 | `bad-txns-vout-empty` / `bad-txns-vout-toomany` |
| T-SIZE | size ≤ 100,000 and size ≠ 64 | `bad-txns-oversize` / `bad-txns-size-64` |
| T-OUT-TYPE | every output is P2PKH or canonical NULL_DATA (§4.2) | `bad-txns-output-type` |
| T-OUT-VALUE | P2PKH: 1 ≤ amount ≤ MAX_MONEY; NULL_DATA: amount == 0 | `bad-txns-vout-zero` / `bad-txns-vout-toolarge` / `bad-txns-nulldata-value` |
| T-OUT-SUM | running output sum ≤ MAX_MONEY | `bad-txns-txouttotal-toolarge` |
| T-DUP-IN | no repeated (prevTxid, prevIndex) (hash set) | `bad-txns-inputs-duplicate` |
| T-CB | coinbase only: scriptSig 4 … 100 bytes; sequence 0xFFFFFFFF; lockTime 0 | `bad-cb-length` / `bad-cb-sequence` / `bad-cb-locktime` |
| T-PREVOUT | non-coinbase only: no input has prevTxid = 32 zero bytes | `bad-txns-prevout-null` |
| T-UNLOCK | non-coinbase only: every scriptSig matches the P2PKH unlock template with sighash byte `01` | `bad-scriptsig-format` / `bad-sighash-type` |

Old behaviour fixed here: zero-value outputs were rejected so OP_RETURN markers had to burn value and became spendable UTXOs (`src/validation.cpp:34`, GLD-11); duplicate inputs used an O(n²) loop (`src/validation.cpp:22-30`); limits came from `include/transaction.h` instead of `consensus.h` (`src/validation.cpp:45-54`, CON-20).

**Sigop count** (B-SIGOPS, template builder, mempool policy — one shared function):

`txSigOps(tx) = (isCoinbase(tx) ? 0 : number of inputs) + Σ over NULL_DATA outputs of GoldRules::classify(payload).sigOps`

This counts real signature checks. The old count of every `0xac` byte anywhere in scripts (`src/validation.cpp:332-338`) did not match the cost and did not count vault signature checks (CON-16, GLD-9). With 1,000,000 bytes and at least 85 bytes per input, ordinary spends stay below about 11,800 sigops per block; the rest of the budget bounds gold-event verification.

**Finality** (B-FINAL; BIP113):

```
IsFinal(tx, height, mtp):
    if tx.lockTime == 0: return true
    limit = (tx.lockTime < 500,000,000) ? height : mtp
    if tx.lockTime < limit: return true
    return every input has sequence == 0xFFFFFFFF
```

In a block: `height` = the block's height, `mtp` = MTP(parent). In the mempool: `height` = tip height + 1, `mtp` = MTP(tip). (`lockTime` was never enforced: CON-15.) There is no BIP68 relative lock; the mempool only accepts sequences 0xFFFFFFFE and 0xFFFFFFFF, so BIP68 could be added later as a soft fork.

**Subsidy:**

```
subsidy(h) = (h / halvingInterval >= 64) ? 0 : INITIAL_SUBSIDY >> (h / halvingInterval)
```

The old shift was undefined behaviour after 64 halvings (`src/validation.cpp:411-412`, `src/blockchain.cpp:180`, CON-20). Vectors (main): subsidy(1) = 5,000,000,000; subsidy(209,999) = 5,000,000,000; subsidy(210,000) = 2,500,000,000; subsidy(420,000) = 1,250,000,000; subsidy(6,720,000) = 1; subsidy(6,930,000) = 0; subsidy(13,440,000) = 0; subsidy(4,294,967,295) = 0. Total that can ever be issued (heights ≥ 1) = 2,099,994,997,690,000 awraq.

**MoneyRange:** `MoneyRange(v) := v ≤ MAX_MONEY` (amounts are unsigned). Every sum is built as `if (a > MAX_MONEY − b) fail; a += b;` — no unchecked addition anywhere (old input sum was checked against `UINT64_MAX` only, `src/validation.cpp:381`).

### 7.5 Coinbase rules (summary)

| Item | Rule | Where |
|---|---|---|
| position | first transaction and only the first | B-CB-FIRST, B-CB-ONLY |
| input | exactly one, prevout = (32 zero bytes, 0xFFFFFFFF) | `isCoinbase` |
| scriptSig | 4 … 100 bytes; bytes 0-3 = LE32(height); rest is free (extra nonce, miner tag) | T-CB, B-CB-HEIGHT |
| sequence / lockTime | 0xFFFFFFFF / 0 | T-CB |
| outputs | P2PKH or NULL_DATA; value ≤ subsidy + fees (miners may claim less; the rest is never created) | T rules, C-CB-AMOUNT |
| maturity | spendable only 100 blocks later | C-MATURITY |

### 7.6 ConnectBlock

```
ConnectBlock(block, index, view, undo, params) -> ValidationState
  pre: index.status has DATA_CHECKED (stages C and D passed); index is not genesis;
       view.bestBlock() == index.prev.hash
  txids  = the txids computed in stage C (computed once per block)
  undo   = BlockUndo{ version = 1, blockHash = index.hash, spent = [] }
  fees   = 0
  gold   = GoldRules::beginBlock(view.goldState())
  for t in 0 .. block.txs.size()-1:
      tx = block.txs[t]
      if t > 0:
          spent = []; inSum = 0
          for each input in of tx (in order):
              coin = view.getCoin(in.prevout)              // read error -> INTERNAL
              if no coin:            return INVALID "bad-txns-inputs-missingorspent"
              if coin.isCoinbase and index.height - coin.height < 100:
                                     return INVALID "bad-txns-premature-spend-of-coinbase"
              if coin.amount > MAX_MONEY or inSum > MAX_MONEY - coin.amount:
                                     return INVALID "bad-txns-inputvalues-outofrange"
              inSum += coin.amount; spent.push(coin)
          outSum = sum of tx output amounts               // ≤ MAX_MONEY by T-OUT-SUM
          if inSum < outSum:         return INVALID "bad-txns-in-belowout"
          fee = inSum - outSum
          if fees > MAX_MONEY - fee: return INVALID "bad-txns-fee-outofrange"
          fees += fee
          cache = SigHashCache(tx, spent)
          for k in 0 .. tx.inputs.size()-1:
              if not verifyP2PKHInput(tx, k, cache, spent[k], params.genesisHash, st): return st
          for k in 0 .. tx.inputs.size()-1:
              view.spendCoin(tx.inputs[k].prevout); undo.spent.push(spent[k])
      for j in 0 .. tx.outputs.size()-1:
          out = tx.outputs[j]
          if out is P2PKH:
              op = (txids[t], j)
              if view.haveCoin(op):  return INVALID "bad-txns-BIP30"
              view.addCoin(op, Coin{ out.amount, out.scriptPubKey, index.height, isCoinbase = (t == 0) })
          else:                                               // NULL_DATA: never enters the UTXO set
              if not GoldRules::connectOutput(tx, t, j, txids[t], index.height,
                                              view.goldState(), gold, st): return st   // "bad-gold-event"
  if sum of coinbase output amounts > subsidy(index.height) + fees:
                                     return INVALID "bad-cb-amount"
  undo.goldUndo = gold.finish()
  view.setBestBlock(index.hash)
  return VALID
```

- Outputs created by an earlier transaction of the same block can be spent by a later one (normal Bitcoin semantics; the old `checkBlock` wrongly required inputs in the pre-block set, `src/validation.cpp:373-380`).
- `ConnectBlock` writes only to the in-memory `view`; nothing reaches disk until the commit of §8.4. On failure the caller discards the view. (Old code wrote straight to SQLite inside the function, `src/validation.cpp:454-524`, and added OP_RETURN outputs to the UTXO set, `src/validation.cpp:497-510`.)
- Every input of every non-coinbase transaction is signature-checked. Coinbase inputs are not "accepted unconditionally" any more: the coinbase is bound by T-CB, B-CB-HEIGHT and C-CB-AMOUNT (old: `src/validation.cpp:197`).

### 7.7 DisconnectBlock

```
DisconnectBlock(block, index, view, undo, params) -> OK | CORRUPT
  pre: view.bestBlock() == index.hash; undo parsed and checked (§4.7):
       undo.blockHash == index.hash; undo.spent.size() == number of non-coinbase inputs
  if not GoldRules::disconnectBlock(view.goldState(), undo.goldUndo): return CORRUPT
  pos = undo.spent.size()
  for t from block.txs.size()-1 down to 0:
      tx = block.txs[t]
      for j from tx.outputs.size()-1 down to 0:
          if tx.outputs[j] is P2PKH:
              coin = view.getCoin((txids[t], j))
              if no coin, or amount/script differ, or coin.height != index.height,
                 or coin.isCoinbase != (t == 0):            return CORRUPT
              view.spendCoin((txids[t], j))
      if t > 0:
          for k from tx.inputs.size()-1 down to 0:
              pos -= 1
              if view.haveCoin(tx.inputs[k].prevout):      return CORRUPT
              view.addCoin(tx.inputs[k].prevout, undo.spent[pos])   // restores height and coinbase flag
  if pos != 0:                                              return CORRUPT
  view.setBestBlock(index.prev.hash)
  return OK
```

Reverse order (transactions, then outputs before inputs) fixes the old bug where an output created and spent in the same block came back to life (`src/validation.cpp:545-568`), and restoring `height` fixes the coinbase-maturity bypass after a reorg (DB-7). `CORRUPT` is never a peer's fault: the node logs FATAL, does not commit, and stops with instructions (§8.7). The gold state does not depend on coins, so its undo is applied once per block (format: gold spec).

### 7.8 Gold hook (contract with the gold spec)

```cpp
namespace GoldRules {
  enum class Kind { NOT_GOLD, EVENT, MALFORMED };
  struct Classification { Kind kind; uint32_t sigOps; };
  // Context-free and cheap (no signature checks). Used by B-SIGOPS, policy and the template builder.
  Classification classify(const std::vector<uint8_t>& nullDataPayload);
  // Starts the undo record of one block; finish() returns the bytes stored as goldUndo (§4.7).
  GoldUndoBuilder beginBlock(const GoldState& stateBeforeBlock);
  // Contextual. ConnectBlock calls it for every NULL_DATA output, in block order.
  bool connectOutput(const Transaction& tx, uint32_t txIndex, uint32_t vout, const Hash256& txid,
                     uint32_t height, GoldState& state, GoldUndoBuilder& undo, ValidationState& st);
  bool disconnectBlock(GoldState& state, const std::vector<uint8_t>& goldUndo);   // exact inverse
  bool checkForMempool(const Transaction& tx, const GoldState& tipState, ValidationState& st);
}
```

Guarantees this spec gives the gold module:
1. A payload classified `MALFORMED`, or an `EVENT` that `connectOutput` rejects, makes the **block invalid**. Nothing is "skipped" (fixes the connect/disconnect asymmetry of CON-11, NET-V2, CRY-V1).
2. Gold state lives in the same coins view and is committed in the same storage transaction as the UTXO changes.
3. Disconnect reverses exactly what connect applied, from the undo bytes, never by re-scanning outputs.
4. Gold signature checks count as sigops (GLD-9).
5. Until the gold spec is implemented, `classify` returns `NOT_GOLD` for every payload and the gold state is empty. The old scanners (`src/blockchain.cpp:51-96`, `333-386`, `394-431`, `482-522`) are removed from chain code in step 16 of §14, so no unvalidated reserve accounting remains in between; the API reports the reserve as "not available" during that gap.

---

## 8. Block index, chain selection and reorganisation

### 8.1 Block index

```cpp
struct BlockIndex {
    Hash256     hash;
    BlockIndex* prev = nullptr;
    BlockIndex* skip = nullptr;      // skip-list pointer for O(log n) getAncestor (Bitcoin's pskip)
    BlockHeader header;              // the 84 header bytes
    uint32_t    height = 0;
    U256        chainWork;           // cumulative, including this block
    uint32_t    status = 0;          // BlockStatus bits below
    uint32_t    txCount = 0;         // 0 until block data is stored
    bool        haveChainData = false;  // this block and all ancestors have data (Bitcoin's nChainTx > 0)
    uint64_t    sequenceId = 0;      // order in which block data arrived; 0 for blocks loaded at startup
    BlockIndex* getAncestor(uint32_t h);
    int64_t     medianTimePast() const;   // §6.6
};
```

- The index is `std::unordered_map<Hash256, std::unique_ptr<BlockIndex>>`: keyed by hash, never by height or vector position (fixes CON-6, DB-1, NET-2; old: `include/blockchain.h:90-94`).
- It holds headers only (about 250 bytes per block); block bytes stay on disk (DB-15).
- The active chain is `std::vector<BlockIndex*>` with `chain[h]->height == h`.
- `bestHeader` = the non-failed entry with the most chain work.

Status bits (canonical values; the storage spec persists this integer unchanged):

| Bit | Name | Meaning |
|---|---|---|
| 0x01 | `HEADER_VALID` | passed stages A and B |
| 0x02 | `HAVE_DATA` | block bytes stored |
| 0x04 | `DATA_CHECKED` | passed stages C and D |
| 0x08 | `CONNECTED_VALID` | `ConnectBlock` succeeded at least once (fully valid) |
| 0x10 | `HAVE_UNDO` | undo record stored |
| 0x20 | `IN_MAIN_CHAIN` | in the active chain (persisted mirror of the vector) |
| 0x40 | `FAILED_VALID` | the block itself is invalid |
| 0x80 | `FAILED_CHILD` | an ancestor is invalid |

Invariants (checked at startup, §8.7):
- I1: every stored entry has `HEADER_VALID`.
- I2: `DATA_CHECKED` ⇒ `HAVE_DATA`; `CONNECTED_VALID` ⇒ `DATA_CHECKED`; `CONNECTED_VALID` ⇒ `HAVE_UNDO` except genesis.
- I3: `IN_MAIN_CHAIN` ⇔ in the active chain vector; ⇒ `CONNECTED_VALID`.
- I4: `FAILED_*` ⇒ not `IN_MAIN_CHAIN`; every descendant of a `FAILED_*` entry has `FAILED_CHILD`.
- I5: `height = prev.height + 1`, `chainWork = prev.chainWork + blockProof(bits)`.

### 8.2 Accepting headers and blocks

```
AcceptBlockHeader(header) -> (state, entry)
  hash = SHA256d(header)
  if hash in index:
      if index[hash] has FAILED_*: return INVALID "duplicate-invalid"
      return DUPLICATE, index[hash]
  stage A; stage B                          // failure: return the state; NOTHING is stored
  e = new entry { hash, prev = parent, header, height = parent.height + 1,
                  chainWork = parent.chainWork + blockProof(bits), status = HEADER_VALID }
  storage: insert the header row
  if e.chainWork > bestHeader.chainWork: bestHeader = e
  return VALID, e

ProcessNewBlockHeaders(headers[]) -> (state, index of first failure)
  if headers.size() > 2,000 or headers[i+1].prevBlockHash != hash(headers[i]) for some i:
      return MALFORMED
  for each header: AcceptBlockHeader; stop at the first result other than VALID/DUPLICATE

AcceptBlock(block, requested) -> (state, entry)
  (st, e) = AcceptBlockHeader(block.header); if st is not VALID/DUPLICATE: return st
  if e has HAVE_DATA: return DUPLICATE
  if not requested:                          // anti-DoS; nothing stored, no penalty
      if e.chainWork <= tip.chainWork:       return DUPLICATE "unrequested-low-work"
      if e.height > tip.height + 288:        return DUPLICATE "unrequested-too-far"
  st = stage C
  if st is MUTATED or MALFORMED: return st   // header stays, nothing marked
  if st is INVALID: markFailed(e); return st
  st = stage D; if INVALID: markFailed(e); return st
  storage: write block bytes; set HAVE_DATA | DATA_CHECKED; e.txCount = n; e.sequenceId = ++counter
  if e.prev.haveChainData: set haveChainData on e and on every descendant that has data;
                           add each such entry with chainWork >= tip.chainWork to the candidates
  return VALID

ProcessNewBlock(block, source, requested) -> { state, becameTip }
  with the chain lock held: (st, e) = AcceptBlock(block, requested)
  if st != VALID: return { st, false }
  ActivateBestChain()                        // takes and releases the chain lock per step (§8.4)
  with the chain lock held:
      if e has FAILED_VALID: return { the stored reject state of e, false }
      return { VALID, tip == e }
```

The chain lock is not recursive: `ActivateBestChain` is always called without it held.

`markFailed(e)`: set `FAILED_VALID` on `e`, `FAILED_CHILD` on all descendants in the index, remove them from the candidates, recompute `bestHeader` if needed, persist the status changes, remember `(hash → reject state)` in a bounded in-memory map (1,000 entries) so the P2P layer can penalise the peer that sent the block even when `ConnectBlock` fails later.

### 8.3 Chain selection

- **Candidates**: every entry with `haveChainData`, not `FAILED_*`, and `chainWork ≥ tip.chainWork`. Ordered by chain work (highest first), then `sequenceId` (lowest first). After every tip change, entries with less work than the tip are removed.
- **Rule**: the node switches only to a candidate with **strictly more** chain work than the current tip. Among candidates with equal work, the one whose data arrived first wins. With equal work the current tip is kept (first seen).
- Chain work comes only from validated headers (H-BITS, H-POW, H-DIFF). A peer can no longer claim work with self-declared `bits` or `height` (old: `src/blockchain.cpp:224, 251-256`; CON-1, DB-2, NET-1, API-V2).

### 8.4 ActivateBestChain (atomic steps)

```
ActivateBestChain():
  loop:
    take the chain lock
    best = first candidate that is not FAILED_* and not fork-alert-skipped
    if best is none or best.chainWork <= tip.chainWork: release lock; return
    fork  = lastCommonAncestor(tip, best)
    depth = tip.height - fork.height
    if depth > params.maxAutoReorgDepth and best is not operator-approved:
        raiseForkAlert(fork, best); mark best fork-alert-skipped (in memory); continue      // §8.5
    path = entries after fork up to best, ascending
    if fork != tip:   stepEnd = first entry on path with chainWork > tip.chainWork        // reorg
                      if its position on path > 2*maxAutoReorgDepth + 32:
                          raiseForkAlert(fork, best); mark best fork-alert-skipped; continue
    else:             stepEnd = path[min(32, path.size()) - 1]                              // extension
    view = new CoinsViewCache over the committed UTXO set and gold state
    disconnected = []
    for b = tip; b != fork; b = b.prev:
        read block bytes and undo of b              // read error or bad undo -> FATAL
        if DisconnectBlock(b) == CORRUPT -> FATAL
        disconnected.prepend(block)                 // oldest first
    connected = []
    for b in path up to and including stepEnd:
        read block bytes of b                       // read error -> FATAL
        re-run stage C on the bytes read (computes txids); header hash must equal b.hash
                                                    // failure = disk corruption -> FATAL, never markFailed
        st = ConnectBlock(block, b, view, undo)
        if st is INTERNAL -> FATAL
        if st is INVALID:
            markFailed(b); drop view               // nothing was written; old chain is intact
            continue outer loop                     // pick the next best candidate
        connected.push((b, block, undo))
    COMMIT (storage spec: ONE database transaction):
        coins added/removed and gold state from view; UTXO statistics delta;
        undo record of every connected block;
        status: +CONNECTED_VALID +HAVE_UNDO +IN_MAIN_CHAIN on connected, -IN_MAIN_CHAIN on disconnected;
        best block hash = stepEnd.hash
      commit failure -> FATAL (process exits; storage recovery runs at next start)
    only after a successful commit: update the active chain vector and tip; prune candidates;
        increment tipSequence (wakes the miner); mempool.updateForStep(disconnected, connected) (§9.6)
    release the chain lock (other threads run between steps)
```

Properties (each is tested in §15):
1. **Atomic**: a step's disconnects and connects are all committed or none (CON-9; storage side: DB-5, CON-18).
2. **Never worse**: every committed tip has more chain work than the previous one.
3. **Right state**: fork blocks are validated against the fork's own UTXO view, MTP and bits (old: checked against the current tip, `src/blockchain.cpp:291-292`).
4. **Failure is clean**: an invalid block inside a branch leaves the old chain in place on disk and in memory; the next loop iteration may pick a valid prefix of that branch if it has more work than the old tip.
5. **Bounded**: a reorg step holds at most `maxAutoReorgDepth` disconnected blocks and `2 × maxAutoReorgDepth + 32` connected blocks in memory; an extension step at most 32 blocks.

### 8.5 Safety rails

**Automatic reorg limit.** A reorg that would disconnect more than `maxAutoReorgDepth` (100) blocks is not performed automatically. The node then:
1. keeps its current chain and keeps mining on it;
2. logs `CRITICAL: deep fork: <depth> blocks below tip <hash>, other branch <hash> has <x> more work` once per new best header on that branch;
3. reports `forkAlert {forkHeight, ourTip, otherTip, workDifference}` in `/api/blockchain/info` (API spec) so wallets and exchanges can stop crediting deposits;
4. switches only after operator action: start flag `--approve-deep-reorg=<blockhash>` or the authenticated admin API `reconsider(hash)`. An admin `invalidate(hash)` (sets `FAILED_VALID` with code `manual-invalidate`) exists for emergencies.

Trade-off (deviation from Bitcoin, which has no such limit): a long network partition (more than about 16 hours) needs operator action to heal, and a node syncing from scratch follows the most-work chain while running nodes refuse a deep reorg; in exchange, a hashrate majority cannot silently rewrite more than 100 blocks of history on running nodes, which matters while hashrate is small (CON-17). Owner decision on the value: §16 Q2.

**Checkpoints.** Rule H-CKPT. At launch only `{0, genesis}`. Each release adds one checkpoint at a block at least 1,000 blocks deep at release time.

**Minimum chain work and initial block download.** `IsInitialBlockDownload()` is true while `tip.chainWork < params.minimumChainWork` or `tip.time < now − 86,400`; once false it stays false until restart. `minimumChainWork` is raised each release to the chain work of a block about one week before the release. While it is true, the mempool ignores transactions from peers and the API reports "syncing".

**When the miner may work.** The template builder refuses (error "node is syncing") while `tip.chainWork < params.minimumChainWork`, or while a header chain with more work than the tip is known, not yet connected, and not under a fork alert (`bestHeader.chainWork > tip.chainWork`, ignoring fork-alert branches, so a node in fork alert keeps mining as stated above). The tip age does **not** block mining, so the launch (tip = genesis) works and a young chain whose only miner was offline for a day can restart. Regtest has no restriction.

### 8.6 Unconnected blocks, orphans and DoS bounds

- **No orphan-block pool.** A header or block whose parent is unknown returns MISSING_PARENT and nothing is stored; the P2P layer asks the peer for headers with a locator (§8.9). (Old code returned `false` and penalised the peer, `src/blockchain.cpp:316`, `src/network.cpp:445-448`.)
- **Unrequested blocks** are stored only if they have more work than the tip and are at most 288 blocks above it (§8.2).
- **Header spam** costs real work: every stored header passes H-POW at the expected difficulty (at least 2^32 hashes on main) and cannot fork below the last checkpoint. Filling 1 GB of index memory (about 4 million headers) costs at least 1.7 × 10^16 hashes at launch difficulty. Per-peer limits on unconnecting headers and request rates belong to the P2P spec (NET-7).
- **Block data is requested only for headers on the best header chain**, at most 16 in flight per peer (P2P spec). Peers can no longer make the node request arbitrary announced hashes (old: `src/network.cpp:354-359, 398-404`).
- **Known-invalid blocks** are rejected at the header stage through the persisted `FAILED_*` flags (`duplicate-invalid`, `bad-prevblk`) without re-validation.

### 8.7 Startup

```
LoadChainState(params, options):
 1. Parameter assertions (§3.7); build and check genesis (§3.5).
 2. Open storage. Empty -> initialise with genesis (§3.5) and continue at step 10.
 3. Stored network name and genesis hash == params (§3.5), else FATAL.
 4. Load all block index records. For each, parents before children:
      recompute hash from the stored 84 header bytes == key;
      stage A (H-VER, H-BITS, H-POW);
      parent exists (except genesis); height == parent.height + 1;
      recomputed chainWork == stored chainWork;
      stage B without H-FUTURE;
      invariants I1-I5.
    Any failure -> FATAL "block index corrupt at <hash>: <code>; restart with --reindex".
 5. tip = entry of the stored best block hash. It MUST have CONNECTED_VALID and IN_MAIN_CHAIN; walking from tip
    to genesis every entry has IN_MAIN_CHAIN and no other entry has it. Build the active chain vector.
    Failure -> FATAL "... restart with --reindex-chainstate".
 6. The coins store's best-block marker == tip.hash, else FATAL (--reindex-chainstate).
 7. VerifyDB(N = --checkblocks, default 24; 0 = whole chain):
      for the last min(N, tip.height) blocks: read bytes and undo; run stages C and D;
      on a scratch view: DisconnectBlock from the tip down N blocks, then ConnectBlock them back up to the tip
      with full validation (every signature);
      the scratch view MUST end equal to the committed state for every touched outpoint and for the gold state;
      the scratch view is discarded (never committed). Any failure -> FATAL with block hash and reject code.
 8. UTXO statistics: scan the whole coins table, recompute {count, totalAmount, digest}, compare with the
    stored statistics; also totalAmount ≤ Σ subsidy(h) for h = 1 … tip.height. Failure -> FATAL.
 9. GoldRules startup check (gold spec).
10. Rebuild candidates and bestHeader; ActivateBestChain() (blocks downloaded but not connected before shutdown).
11. Load the persisted mempool through AcceptToMempool (§9.7).
```

UTXO statistics (stored in chain state, updated in every commit: added coins add, spent coins subtract):
`count` (u64), `totalAmount` (u64), `digest = Σ SHA256(txid ‖ LE32(index) ‖ LE32(height) ‖ u8(isCoinbase) ‖ LE64(amount) ‖ CompactSize(len) ‖ script) mod 2^256`. The additive digest detects corruption, not attacks; MuHash could replace it later. The API reports circulating supply from `totalAmount`, not from a formula (old: `src/blockchain.cpp:169-184` counted 50 BUCKS per block even after rewards were overwritten, DB-6).

Repair modes (CLI names may be set by the storage spec):
- `--reindex-chainstate`: delete coins, undo records, gold state and statistics; clear `CONNECTED_VALID`, `HAVE_UNDO`, `IN_MAIN_CHAIN` and `FAILED_*` on all entries; best block = genesis; then `ActivateBestChain()` connects every stored block with full validation.
- `--reindex`: additionally rebuild the block index from stored block bytes, in height order, through `AcceptBlockHeader` and stages C and D.

The node never repairs silently. Today one unreadable row makes it start a new chain on top of the old UTXO set (`src/blockchain.cpp:120-131`, `src/blockdb.cpp:129-133`, DB-14).

### 8.8 What this spec needs from the storage spec

| Need | Requirement |
|---|---|
| Block index table | one row per header, primary key = block hash; header bytes, height, chain work (32 bytes big-endian), status (§8.1 bits), txCount |
| Block bytes | by hash, write-once (§4.5 serialization) |
| Undo records | by hash (§4.7 layout) |
| Coins | `getCoin`, `haveCoin` on the committed set; an in-memory `CoinsViewCache` overlay with `addCoin`, `spendCoin`, `setBestBlock`; coin = {amount, scriptPubKey, height, isCoinbase}; plain `INSERT` (a conflict is a bug) |
| Gold state | stored and committed with the coins |
| Chain state keys | network name, genesis hash, best block hash, UTXO statistics, schema version |
| Atomic commit | one database transaction per `ActivateBestChain` step containing everything in §8.4; in-memory state changes only after COMMIT succeeds |
| Lock | one chain lock held by every function in §7–§10 except the pure rule functions; never held during socket I/O or the mining hash loop; readers (HTTP, P2P) get copies, never raw pointers |

Names may differ in the storage spec; the semantics here are normative. Any mismatch between the two specs (status bits, undo layout, commit unit) is resolved before implementation starts (§16.1).

### 8.9 Headers-first helpers and interim messages

- `getLocator(entry)`: hashes of `entry` and its ancestors at heights h, h−1, … h−9, then steps doubling (h−11, h−15, h−23, …), always ending with genesis; at most 101 hashes.
- `findFork(locator)`: the first locator hash that is in the active chain; genesis if none.
- Answer to `getheaders`: up to 2,000 headers following the fork point on the active chain, stopping after `stopHash`.

Interim payload formats (the P2P spec may replace them; they replace the height-based `getheaders` and hash-list `headers` of `src/network.cpp:369-407`, NET-8):

| Message | Payload |
|---|---|
| `getheaders` | u32 version = 1 ‖ CompactSize n (1 … 101) ‖ n × Hash256 locator (newest first) ‖ Hash256 stopHash (zero = no stop) |
| `headers` | CompactSize n (0 … 2,000) ‖ n × 84-byte header |
| `block` | block serialization (§4.5), payload ≤ 1,000,000 bytes |
| `tx` | transaction serialization (§4.3), payload ≤ 100,000 bytes |

---

## 9. Mempool

The mempool is policy (local rules), but every transaction in it has passed the **same consensus checks as a block** would apply at height tip + 1. It replaces both `Blockchain::mempool` (a vector, `include/blockchain.h:100`, `src/blockchain.cpp:603-672`) and the unused `Mempool` class (`src/mempool.cpp`, DB-18).

### 9.1 Data structures

```cpp
struct MempoolEntry {
    Transaction tx;
    Hash256  txid;
    uint32_t size;          // serialized bytes
    uint64_t fee;
    uint64_t feeRate;       // fee * 1000 / size, rounded down (awraq per 1,000 bytes)
    int64_t  entryTime;
    uint32_t entryHeight;
    uint32_t sigOps;
    Hash256  spentDigest;   // SHA256 over (amount, script) of the coins it spends, used to skip
                            // signature re-checks when those coins are unchanged after a reorg
    bool     hasGoldEvent;
};
class Mempool {
    std::unordered_map<Hash256, MempoolEntry>  byTxid;
    std::unordered_map<OutPoint, Hash256>       spentBy;     // outpoint -> spending txid
    std::set<std::tuple<uint64_t, int64_t, Hash256>> byFeeRate;  // (feeRate, entryTime, txid)
    uint64_t totalBytes = 0;
    uint64_t rollingMinFeeRate = 0;
    int64_t  rollingUpdated = 0;
};
```

Every operation is O(1) or O(log n); no full scans or re-serialisation per call (DB-12).

### 9.2 AcceptToMempool (check order)

| # | Check | Failure result |
|---|---|---|
| 1 | source is a peer and the node is in initial block download | TX_POLICY `initial-block-download` |
| 2 | txid not already in the mempool | DUPLICATE |
| 3 | `CheckTransaction` (§7.4) | INVALID |
| 4 | not a coinbase | INVALID `coinbase` |
| 5 | `IsStandardTx` (§9.3) | TX_POLICY |
| 6 | `IsFinal(tx, tip.height + 1, MTP(tip))` | TX_PREMATURE `non-final` |
| 7 | no input already spent by a mempool transaction (first seen wins) | TX_CONFLICT `txn-mempool-conflict` |
| 8 | every input's coin is in the committed UTXO set at the tip | TX_MISSING_INPUTS (`mempool-parent` if a mempool tx creates it) → orphan pool (§9.5) |
| 9 | coinbase maturity at height tip + 1 | TX_PREMATURE `premature-spend-of-coinbase` |
| 10 | C-INVALUE, C-INBELOWOUT | INVALID |
| 11 | fee ≥ ceil(max(MIN_RELAY_FEERATE, rollingMinFeeRate) × size / 1000) | TX_POLICY `min-fee-not-met` |
| 12 | `txSigOps` ≤ 4,000 | TX_POLICY `too-many-sigops` |
| 13 | C-SIG for every input (§5.2) | INVALID |
| 14 | `GoldRules::checkForMempool` | INVALID or TX_POLICY (gold spec) |
| 15 | room: if totalBytes + size > MAX_MEMPOOL_BYTES, pick the lowest fee-rate entries whose fee rate is below this transaction's until enough bytes are free; if that is not possible → TX_POLICY `mempool-full` and evict nothing; otherwise evict them and set `rollingMinFeeRate = max(rollingMinFeeRate, highest evicted feeRate + INCREMENTAL_RELAY_FEERATE)` | |
| 16 | insert into all indexes; persist (storage spec) | VALID |

v1 accepts only transactions whose inputs are **confirmed** coins (step 8). There are no unconfirmed chains, so no ancestor/descendant tracking is needed and every mempool transaction is independent. RBF and CPFP are deferred (§1.3).

### 9.3 Standardness (`IsStandardTx`)

| ID | Rule | Code |
|---|---|---|
| P-VERSION | version == 2 | `version` |
| P-SIZE | size ≤ 100,000 | `tx-size` |
| P-SEQUENCE | every input sequence is 0xFFFFFFFE or 0xFFFFFFFF | `non-standard-sequence` |
| P-DUST | every P2PKH output amount ≥ 546 | `dust` |
| P-NULLDATA | at most one NULL_DATA output; payload ≤ 80 bytes unless `GoldRules::classify` says EVENT (then the gold spec's limit, ≤ 1,024) | `multi-op-return` / `datacarrier` |

### 9.4 Fee floor, expiry and trimming

- `rollingMinFeeRate` decays: on each read, `halvings = (now − rollingUpdated) / 43,200`; shift right by `min(halvings, 63)`; advance `rollingUpdated` by `halvings × 43,200`; set to 0 when below `MIN_RELAY_FEERATE / 2`.
- Entries older than 14 days are removed after each committed step.
- When the mempool is above `MAX_MEMPOOL_BYTES` after a reorg re-add, the lowest fee-rate entries are removed until it fits.

### 9.5 Orphan transaction pool

- Holds TX_MISSING_INPUTS transactions: at most 100 in total and 25 per peer, each ≤ 100,000 bytes, each expires after 20 minutes. When full, a random entry is evicted (random, so an attacker cannot target honest orphans).
- Indexed by missing outpoint. After each committed step, orphans whose missing outpoints now exist are retried through `AcceptToMempool`; accepted or INVALID ones leave the pool.
- Not persisted.

### 9.6 Updates after every committed chain step

Run under the chain lock, after the commit of §8.4:
1. **Re-add**: for each disconnected block (oldest first), each non-coinbase transaction in block order → `AcceptToMempool` with checks 1, 11 and 15 skipped; failures are dropped silently. (Old code dropped these transactions: CON-9, DB-11.)
2. **Remove confirmed and conflicting**: for each connected block, remove its transactions, and remove every mempool transaction that spends an outpoint the block spends. (Old code removed only exact txids, `src/blockchain.cpp:440-445`, so one conflicting transaction stopped mining for ever: DB-9.)
3. **Revalidate after a reorg** (any block disconnected): for every remaining entry check that each spent coin still exists with the same amount and script (compare `spentDigest`; if equal, signatures stay valid and are not re-checked), coinbase maturity and finality at the new tip + 1, and gold rules. Remove failures.
4. **Without a reorg**: re-check only entries with gold events (finality and maturity can only improve when the height grows).
5. Expire, trim (§9.4), retry orphans (§9.5).

### 9.7 Persistence and restart

The storage spec may keep a mempool table with transaction bytes only. At startup each stored transaction goes through `AcceptToMempool` (full validation; check 1 skipped); failures are dropped and deleted; at most `MAX_MEMPOOL_BYTES` are loaded. (Old code pushed rows straight into the vector and swallowed parse errors: `src/blockdb.cpp:268-287`, `src/blockchain.cpp:133-137`, DB-10.)

---

## 10. Block template and mining

### 10.1 `BlockAssembler::createNewBlock`

New files `include/miner.h`, `src/miner.cpp` (the old uncompiled `src/miner.cpp` is replaced and added to `CMakeLists.txt` and `build.sh`).

```
createNewBlock(minerP2PKHScript, minerTag (≤ 88 ASCII bytes, default "Bucks"), extraPayload (≤ 80 bytes, optional))
  under the chain lock:
  if the miner rule of §8.5 forbids mining: fail "node is syncing"
  tip = active tip; h = tip.height + 1
  header.version = 2; header.height = h; header.prevBlockHash = tip.hash
  header.bits = GetNextWorkRequired(tip)
  minTime = MTP(tip) + 1
  if h % 2016 == 0:    minTime = max(minTime, tip.time - 600)                       // H-TW1
  if h % 2016 == 2015: minTime = max(minTime, tip.getAncestor(h - 2015).time)       // H-TW2
  header.time = max(now, minTime); if header.time > now + 7,200: fail "local clock is behind the chain"
  sizeLeft  = 1,000,000 - 84 - 3 - 1,000          // header, tx count, COINBASE_RESERVED_SIZE
  sigopLeft = 20,000
  for each mempool entry, highest feeRate first (ties: older entryTime first):
      if entry.size > sizeLeft or entry.sigOps > sigopLeft: skip
      if not IsFinal(entry.tx, h, MTP(tip)): skip
      select it; sizeLeft -= size; sigopLeft -= sigOps; fees += fee
  coinbase.scriptSig = LE32(h) ‖ LE64(extraNonce = 0) ‖ minerTag            // 12 … 100 bytes
  coinbase.outputs   = [ P2PKH(miner) with amount = subsidy(h) + fees ]
                       (if subsidy(h) + fees == 0: [ NULL_DATA with empty payload, amount 0 ] instead,
                        because a 0-value P2PKH output is invalid; reachable on regtest after 33 halvings)
                       + [ NULL_DATA(extraPayload) with amount 0 ] if extraPayload is set
  coinbase.version = 2; sequence 0xFFFFFFFF; lockTime 0
  block = header + [coinbase] + selected; header.merkleRoot = MerkleRoot(txids)
  TestBlockValidity(block): stages A (without H-POW), B, C, D and E on a scratch view; never committed
      on failure: log "BUG: template invalid: <code> at tx <i>"; if i > 0 remove that tx from the
      mempool and rebuild once; otherwise fail
  return a copy of the block and the tipSequence value it was built on
```

- Size and sigops use the same functions as validation, so a template can never exceed the block limits (CON-16, API-17, DB-9). The coinbase is at most about 280 bytes, well inside the 1,000-byte reserve.
- The reward is `subsidy(h) + fees`, so halvings and fees are handled (old: fixed 5,000,000,000 at `src/server.cpp:1499, 1675`).
- Every coinbase is unique because of the height prefix and the extra nonce (old constant scriptSig: `src/server.cpp:1669-1670`, `1476`; CON-8, API-6).

### 10.2 Miner loop

```
while mining is enabled:
  (block, seq) = createNewBlock(...)            // copy; chain lock released
  target = DecodeCompact(block.header.bits)
  for nonce = 0 … 2^32 - 1:
      if SHA256d(header) ≤ target: ProcessNewBlock(block, LOCAL, requested = true); break
      every 65,536 nonces:
          if chain.tipSequence() != seq: start again with a new template
          header.time = max(header.time, now) (never below minTime, never backwards)
          every 60 s: start again with a new template (new mempool transactions)
  if all 2^32 nonces were tried: extraNonce += 1, rebuild the coinbase and merkle root, continue
```

The hash loop never holds the chain lock or the wallet lock. The old loop let the nonce wrap silently and read `chain.tip()` without a lock (`src/server.cpp:1696-1712`).

### 10.3 Server integration

`Server::mineBlock` (`src/server.cpp:1415-1553`) and `Server::autoMineLoop` (`src/server.cpp:1645-1729`) use `BlockAssembler` and `ProcessNewBlock`. Pending anchor CIDs (`src/server.cpp:1483-1494`) go into the single coinbase NULL_DATA output (at most 80 bytes per block; the rest wait for the next block), never into `scriptSig` (API-16; CID validation is the API spec's job). Every block the node mines is announced to peers (P2P spec; the auto-miner never announced, NET-17). `/api/blockchain/info` reports real `bits`, difficulty and chain work instead of the constant `"difficulty": 2` (`src/server.cpp:938`). Tests that mine through the API run on **regtest** (`test_server_auth.cpp:594-640`), because mainnet blocks now need about 2^32 hashes.

---

## 11. Parser hardening

### 11.1 Bounded reader

New files `include/serialize.h`, `src/serialize.cpp`:

```cpp
struct ParseError : std::runtime_error { using std::runtime_error::runtime_error; };

class Reader {
public:
    Reader(const uint8_t* data, size_t size);
    uint8_t  u8();  uint16_t u16();  uint32_t u32();  uint64_t u64();
    Hash256  hash256();
    uint64_t compactSize(uint64_t maxValue);        // minimal form only; maxValue ≤ 33,554,432
    std::vector<uint8_t> varBytes(size_t maxLen);   // length ≤ maxLen and ≤ remaining()
    void     bytes(uint8_t* out, size_t n);
    size_t   remaining() const;
    bool     atEnd() const;
private:
    const uint8_t* data_; size_t size_; size_t pos_ = 0;
};
class Writer { /* u8..u64, hash256, compactSize (minimal), varBytes; appends to a std::vector<uint8_t> */ };
```

| Rule | Requirement |
|---|---|
| R1 | Every read checks `remaining() >= n` (subtraction form, never `pos + n` overflow) and throws `ParseError` |
| R2 | Before allocating for a count: `count ≤ maxCount` AND `count ≤ remaining() / minElementSize`; only then `reserve(count)`. Minimum sizes: input 41, output 9, transaction 60, header 84, coin record 39. Never `reserve` an unchecked count (old: `src/block.cpp:237`, OPS-7, NET-16) |
| R3 | A top-level object MUST consume all bytes (`atEnd()`), else `ParseError("trailing bytes")` |
| R4 | Parsers check structure only; semantic checks are validation rules with reject codes |
| R5 | No logging in parsers or serializers |
| R6 | `ParseError` never crosses a trust boundary: the P2P layer, API submit, storage loads and mempool load catch it and map it to MALFORMED (peer or API data) or INTERNAL (own database) |

Parse caps:

| Object | Caps |
|---|---|
| Transaction (standalone) | total ≤ 100,000 bytes; inputs ≤ 2,000; outputs ≤ 3,000 |
| Block | total ≤ 1,000,000 bytes, checked on the message/blob length **before** parsing; tx count ≤ 15,384 and ≤ remaining / 60 |
| Header | exactly 84 bytes |
| Block undo | spent count ≤ remaining / 39; gold undo ≤ 65,536 bytes |
| `headers` payload | n ≤ 2,000; payload length == size of CompactSize(n) + 84 × n |
| `getheaders` payload | n ≤ 101; exact length |

### 11.2 P2P frame parsing (OPS-8)

Frame: magic (4) ‖ command (12) ‖ length u32 ‖ checksum (4) ‖ payload. `NetMessage::deserialize` itself (not only its caller) MUST:
1. require at least 24 bytes;
2. require magic == `params.netMagic` (old code never checked it inside `deserialize`);
3. require command = 1 … 12 characters from `[a-z0-9]` followed only by zero bytes, and match the command list **exactly** (old: `strncmp` prefix matching, `src/network.cpp:64-80`, NET-17);
4. compare lengths in `size_t`: `len > maxPayload(command)` → MALFORMED; `data.size() − 24 < len` → incomplete (old: `data.size() < 24 + len` wrapped in 32 bits, `src/network.cpp:148`, then read out of bounds at line 150);
5. check the checksum;
6. per-command caps: `block` 1,000,000, `tx` 100,000, `headers` 168,003, `getheaders` 3,269; other commands: P2P spec; absolute cap stays `MAX_MESSAGE_PAYLOAD` (4 MiB, `include/network.h:31`).

### 11.3 Fuzzing

- libFuzzer targets (built with `-DBUCKS_FUZZ=ON`, clang only): transaction, block, header, undo record, `NetMessage` frame, CompactSize.
- Properties: `parse(serialize(x)) == x`; for every accepted input `b`: `serialize(parse(b)) == b` (canonical encoding); no crash, no sanitizer report, no allocation larger than the input length × 64.
- A deterministic random-mutation gtest (`tests/perf_security/fuzz_parsers.cpp`) runs the same targets for 100,000 iterations in normal CI under ASan and UBSan (the existing fuzz test only covers `NetMessage`, `tests/perf_security/fuzz_protocol.cpp`).

---

## 12. Code to delete or replace

| Code | Action |
|---|---|
| `src/script.cpp:6-133`, `include/script.h:23-39` (`ScriptInterpreter`; `OP_CHECKSIG` with an empty sighash, line 61; uninitialised digest, lines 111-112) | delete; `script.h` keeps `using Script` and gets the template helpers of §4.2 |
| `src/pow.cpp` (dead, out-of-bounds write for exponent > 34 at lines 40-44, wrong `multiplyTarget` at 138-152), `include/pow.h` | replace with the new PoW module (§6) |
| `src/miner.cpp`, `include/miner.h` (not compiled, would not compile) | replace with §10 and add to the build |
| `src/mempool.cpp`, `include/mempool.h` (unused class that ignores its UTXO argument) | replace with §9 |
| `src/blockchain.cpp:21-42` (`workForBits`, `accumWork`) | delete |
| `src/blockchain.cpp:51-96, 333-386, 394-431, 482-522` (gold and governance scanning) | delete from chain code (gold spec re-adds through §7.8) |
| the rest of `src/blockchain.cpp` (constructor, `addBlock`, lookups, `disconnectTip`, persistence, mempool, MTP, retarget) | replace with `ChainState` (§8), `Mempool` (§9), PoW (§6); a thin read-only `Blockchain` facade returning copies may stay for `server.cpp` and `network.cpp` |
| `include/blockchain.h:16-17, 22-28, 69-71, 80, 89-100` | delete (old limits, unused `BlockIndex` with `uint64` work, `difficulty` field, position-based index) |
| `src/validation.cpp` (whole), `include/validation.h:32-182` | rewrite (§7) |
| `src/block.cpp:81-90` (convenience constructor with `bits = 0x1d00ffff` and `time(nullptr)`), `124-148` (`isValid`), `157-161` (magic), debug prints | delete |
| `include/block.h:61-62` (unused `hash`, `difficulty`) | delete |
| `include/transaction.h:14-16, 25, 45-48, 59-61, 72, 75-77, 82, 85-86` | delete (§4.3, §5.3) |
| `src/genesis.cpp:16-57, 122-155` (runtime genesis mining and its 3-byte target check) | delete |
| `include/consensus.h:108` (`MAX_SCRIPT_SIZE`, unused) | delete |

A `build_hygiene` test (`tests/build_hygiene.cmake`) fails the build if any deleted symbol (`ScriptInterpreter`, `workForBits`, `accumWork`, `legacySignatureHash`, `wtxid`, `isValidBlock`, `findGenesisNonce`) appears in `src/` or `include/` again (OPS-14, CON-19).

---

## 13. Finding → section map

| Finding | Problem (short) | Resolved in |
|---|---|---|
| CON-1 | reorg path connects blocks without header, PoW, merkle, bits or subsidy checks | §7.2, §7.3, §8.2–§8.4 |
| CON-2 | chain work is always zero | §6.4, §8.3 |
| CON-3 | non-P2PKH outputs spendable with any key | §4.2, §5.2, T-OUT-TYPE |
| CON-4 | input public keys not serialized | §4.2, §4.3 |
| CON-5 | `verifyECDSA` fail-open (fixed in Phase 1) | §5.2 relies on it; SIG-8 re-tests it through validation |
| CON-6 | side blocks overwrite main blocks on disk | §8.1, §8.2, §8.8 |
| CON-7 | retarget wraps; timewarp | §6.5, §6.6, §10.1 |
| CON-8 | identical coinbase txids overwrite outputs | B-CB-HEIGHT, C-BIP30 (§7.3), §10.1 |
| CON-9 | reorg not atomic, validated against wrong state, txs lost | §8.4, §9.6 |
| CON-10 | no chain lock | storage spec; requirements in §8.8, §10.2 |
| CON-11 | gold events replayable, asymmetric reversal | §7.8 + gold spec |
| CON-12 | weak sighash, malleability | §4.2, §4.3, §5 |
| CON-13 | merkle mutation, uncommitted witness | §4.6, §7.1, witness removed (§1.3) |
| CON-14 | CompactSize truncation | §4.1, §11.1 |
| CON-15 | lock time never enforced | §7.4 (IsFinal), §9.2 |
| CON-16 | sigop/size counting differs from the miner | §7.4 (`txSigOps`), §10.1 |
| CON-17 | launch difficulty 2^16, cheap 51% | §3.6, §8.5 |
| CON-18 | not crash-safe | storage spec; commit unit §8.4, startup checks §8.7 |
| CON-19 | dead consensus code | §12 |
| CON-20 | subsidy UB, duplicate constants, wrong error codes, genesis never asserted | §3.1, §3.5, §7.1, §7.4 |
| NET-1 | peer fork blocks trigger an unvalidated reorg | §7.2, §8.2–§8.4 |
| NET-2 | side blocks stored unvalidated by peer-chosen height | §8.1, §8.2, §8.8 |
| NET-7 | unbounded block requests | §8.6 (only validated headers' blocks); rest: P2P spec |
| NET-8 | sync by height, honest peers banned on natural forks | §7.1, §8.6, §8.9 |
| NET-16 | `reserve(txCount)` from the wire | §11.1 R2 |
| NET-17 | prefix command matching (framing part) | §11.2; transaction relay: P2P spec |
| NET-V1 | chain work zero | §6.4 |
| NET-V2 | gold reversal without signature checks | §7.8 |
| API-V2 | zero-PoW reorg with unlimited coinbase | §7.2, §7.3, §8.2–§8.4 (test CS-3) |
| API-6 | constant coinbase templates | §7.3, §10.1 |
| API-16 | anchor data in coinbase scriptSig (consensus part) | §10.3 |
| API-17 | template = whole mempool, fixed reward | §10.1 |
| DB-1 | side blocks saved unvalidated, keyed by height | §8.1, §8.2, §8.8 |
| DB-2 | DB reorg path skips `checkBlock`, trusts bits | §7.2, §8.3, §8.4 |
| DB-3 | ownership only for P2PKH | §4.2, §5.2 |
| DB-6 | `INSERT OR REPLACE` loses coins | C-BIP30, B-CB-HEIGHT, §8.8 |
| DB-7 | undo drops height, wrong order, unbounded parse | §4.7, §7.7 (shared with storage spec) |
| DB-9 | mempool never revalidated; template too big | §9.6, §10.1 |
| DB-10 | persisted mempool and blocks lose the pubkey; reload unvalidated | §4.3, §9.7 |
| DB-11 | disconnect does not return txs to the mempool | §8.4, §9.6 |
| DB-12 | O(n) mempool operations | §9.1 |
| DB-13 | no min fee, dust or eviction; fees burned | §9.2–§9.4, §10.1 (RBF deferred) |
| DB-14 | startup trusts the database | §3.5, §8.7 |
| DB-18 | dead `Mempool` class | §9, §12 |
| DB-V1 | pubkey not in the wire or disk format | §4.3 |
| CRY-4 | ownership only for exact P2PKH | §4.2, §5.2 |
| CRY-5 | sighash byte and low-S (sighash part) | T-UNLOCK, §5 |
| CRY-6 | pubkey not serialized | §4.3 |
| CRY-16 | sighash ignores index, amounts, chain | §5.1 |
| CRY-17 | address version 0x00 | §3.4 (wallet must use params) |
| CRY-V1 | unsigned gold markers reversed on disconnect | §7.8 |
| GLD-9 | vault signature checks not counted | §7.4, §7.8 |
| GLD-11 | markers must burn value and enter the UTXO set | T-OUT-VALUE, §7.6 |
| OPS-7 | attacker-controlled `reserve` | §11.1 |
| OPS-8 | frame length wraps, out-of-bounds read | §11.2 |
| OPS-13 | placeholder genesis payout | §3.5 (genesis pays nothing; unconfigured network refuses to start) |
| OPS-14 | dead security-sensitive code | §12 |

---

## 14. Implementation plan

Rules for every step: it builds with `-Wall -Wextra`, all old and new tests pass (also with `-DBUCKS_SANITIZE=address,undefined`), and `build.sh` keeps the same source list as `CMakeLists.txt` (checked by `tests/build_hygiene.cmake`). New source files are added to both. No commits are made by agents; the owner reviews each step.

From step 5 on, old data files (`blockchain.db`, `utxodb.sqlite`) cannot be read; nodes run on fresh datadirs (the chain is relaunched anyway). Steps 5–14 keep the old `Blockchain` class running with small interim edits so the node and its tests keep working; step 16 switches the node to the new code.

| # | Step | Files | Tests (§15) |
|---|---|---|---|
| 1 | **Serialization primitives**: `Reader`, `Writer`, CompactSize, `ParseError` | new `include/serialize.h`, `src/serialize.cpp`; `CMakeLists.txt`, `build.sh`, `tests/unit/CMakeLists.txt`, new `tests/unit/test_serialize.cpp` | SER-1…3 |
| 2 | **U256 and PoW math**: compact encode/decode, `CheckProofOfWork`, `blockProof`, `CalculateNextWorkRequired` | new `include/u256.h`, `src/u256.cpp`; rewrite `include/pow.h`, `src/pow.cpp`; new `tests/unit/test_u256.cpp`, `test_pow.cpp` | POW-1…5 |
| 3 | **Chain parameters and constants**: `ChainParams` for 3 networks, consolidated `consensus.h`, startup assertions | new `include/chainparams.h`, `src/chainparams.cpp`; `include/consensus.h`; `include/transaction.h:14-16` removed; `src/validation.cpp:45-54` uses `consensus.h`; new `tests/unit/test_chainparams.cpp` | PAR-1…3 |
| 4 | **Script templates**: delete `ScriptInterpreter`; add P2PKH/NULL_DATA/unlock helpers | `include/script.h`, `src/script.cpp`; new `tests/unit/test_script_templates.cpp` | TXR-1 (template part) |
| 5 | **Transaction v2 + SigHash v1**: new layout, txid, strict `isCoinbase`; `SigHashCache`, `signP2PKHInput`, `verifyP2PKHInput`; wallet signs with spent outputs and genesis hash; interim: old `checkTransactionInputs` calls `verifyP2PKHInput` with the coins it already reads | `include/transaction.h`, `src/transaction.cpp`; new `include/sighash.h`, `src/sighash.cpp`; `src/wallet.cpp` (sign, address version from params, UTXO selection skips immature coins); `include/blockchain.h`, `src/blockchain.cpp` (interim: hold `const ChainParams&`, used by the wallet and validation); `src/validation.cpp` (interim); `src/blockdb.cpp` (mempool rows); `src/server.cpp` (tx JSON, coinbase v2); rewrite `tests/unit/test_transaction.cpp`; new `tests/unit/test_sighash.cpp` | TX-1…3, SIG-1…8 |
| 6 | **Block v2 + merkle mutation flag**: no magic, no length prefixes, no debug prints, `mutated` output | `include/block.h`, `src/block.cpp`, `include/merkle.h`, `src/merkle.cpp`; rewrite `tests/unit/test_block.cpp`; new `tests/unit/test_merkle.cpp` | BLK-2, BLK-3, MRK-1 |
| 7 | **Genesis per network**: `Genesis::build/check`, no runtime mining, `bucks-mine-genesis` tool, testnet genesis mined and filled in, `--network` flag (values `main`, `test`, `regtest`) | `include/genesis.h`, `src/genesis.cpp`, `src/chainparams.cpp`, new `tools/mine_genesis.cpp` (+ CMake target), `src/blockchain.cpp:144-154` (interim), `src/main.cpp`; run scripts use `--network=regtest`; new `tests/unit/test_genesis.cpp` | BLK-1, ST-7 |
| 8 | **Coins view and undo record**: `Coin`, `CoinsView` interface, `CoinsViewCache`, UTXO statistics delta, `BlockUndo` v1 | new `include/coins.h`, `src/coins.cpp`, `include/undo.h`, `src/undo.cpp`; new `tests/unit/test_coins.cpp`, `test_undo.cpp` | CB-9 |
| 9 | **Block index helpers**: `BlockIndex`, status bits, `getAncestor` (skip list), MTP, active chain, locator, `findFork`, `lastCommonAncestor` | new `include/chain.h`, `src/chain.cpp`; new `tests/unit/test_chain_index.cpp` | HDR-6, HDR-7 helpers |
| 10 | **Rule functions, stages A–D**: `ValidationState`, `CheckTransaction`, `IsFinal`, `txSigOps`, `subsidy`, `CheckBlockHeader`, `ContextualCheckBlockHeader`, `GetNextWorkRequired(BlockIndex)`, `CheckBlock`, `ContextualCheckBlock` | new `include/validation_rules.h`, `src/validation_rules.cpp`; new `tests/unit/test_tx_rules.cpp`, `test_header_rules.cpp`, `test_block_rules.cpp` | HDR-1…8, TXR-1…6 |
| 11 | **Stage E**: `ConnectBlock`, `DisconnectBlock`; gold hook interface with a stub (`classify` → NOT_GOLD) | new `include/connect_block.h`, `src/connect_block.cpp`, `include/gold_rules.h`, `src/gold_rules_stub.cpp`; new `tests/unit/test_connect_block.cpp` | CB-1…10 |
| 12 | **ChainState on an abstract store**: index map, candidates, `AcceptBlockHeader`, `AcceptBlock`, `ProcessNewBlockHeaders`, `ProcessNewBlock`, `ActivateBestChain`, `markFailed`, fork alert, IBD; in-memory store for tests; `TestChain` helper | new `include/chainstate.h`, `src/chainstate.cpp`, `include/chainstore.h`; new `tests/util/memory_chainstore.h`, `tests/util/test_chain.h`, `tests/integration/test_chainstate.cpp` | CS-1…14, MRK-2, HDR-9 |
| 13 | **Mempool and policy** on top of `ChainState` | rewrite `include/mempool.h`, `src/mempool.cpp`; new `include/policy.h`, `src/policy.cpp`; `src/chainstate.cpp` (step hook); new `tests/integration/test_mempool.cpp` | MP-1…10 |
| 14 | **Block assembler and miner** | rewrite `include/miner.h`, `src/miner.cpp` (added to the build); new `tests/integration/test_miner.cpp` | MIN-1…5, MIN-7 |
| 15 | **SQLite store and startup** (needs storage spec Phase 2b): `LoadChainState`, `VerifyDB`, UTXO statistics, `--checkblocks`, `--reindex-chainstate`, `--reindex` | storage spec files; `src/chainstate.cpp`; new `tests/integration/test_startup.cpp` | ST-1…6, CS-4, CS-6 (persistent store) |
| 16 | **Switch the node**: `main.cpp` builds `ChainState`, `Mempool`, `BlockAssembler`; `server.cpp` read API returns copies, mining through the assembler, send through `AcceptToMempool`; `network.cpp` block/headers handling by result category, interim `getheaders`/`headers`, frame hardening; delete old chain code including the gold/governance scanners | `src/main.cpp`, `src/server.cpp`, `src/network.cpp`, `include/network.h`, `src/wallet.cpp`; delete `src/blockchain.cpp` and `include/blockchain.h` (or keep a thin read-only facade), old `src/validation.cpp`, `include/validation.h`, `src/utxodb.cpp`/`src/blockdb.cpp` (replaced by the storage spec); update `tests/system/test_full_node.cpp`, `tests/integration/test_server_auth.cpp`, `tests/integration/test_server_network.cpp` to regtest | MIN-6, NET-F1…3, P2P-1, P2P-2, all existing tests |
| 17 | **Fuzzing and sanitizer CI** | new `tests/perf_security/fuzz_parsers.cpp`; optional `tests/fuzz/*.cpp` with `BUCKS_FUZZ` option in `CMakeLists.txt` | FZ-1, FZ-2 |
| 18 | **Dead code removal and hygiene guard** | §12 list; `tests/build_hygiene.cmake` (deleted-symbol check) | HYG-1 |

The gold spec plugs in after step 11 (real `GoldRules`), the storage spec's store is needed at step 15, and the P2P spec can start from the interfaces of step 12.

---

## 15. Test plan

**Test infrastructure.**
- `tests/util/test_chain.h`: `TestChain` creates a `ChainState` on **regtest** parameters (blocks need about 2 hashes), with a mock clock (`setNow(t)`), the test key priv1 of §5.4 as miner key, and helpers: `mineBlock(txs, parent = tip, options)` (correct height, bits, time, coinbase; grinds the nonce), `mineChain(n, parent)`, `spend(coin, to, amount, fee)`, and options that break exactly one rule at a time (bad bits, bad height, extra coinbase value, …).
- Retarget and timewarp tests build synthetic `BlockIndex` chains with mainnet parameters (no PoW needed for these functions).
- `ChainParams` can be copied and modified in tests (for example `maxAutoReorgDepth = 5`, a test checkpoint, a small `MAX_MEMPOOL_BYTES`).
- Every test runs in normal CI and under ASan + UBSan.
- "Old behaviour" says what the current code does, so each test would have failed (or crashed) on it.

### 15.1 Serialization and parsing

| ID | Test | Setup | Action | Expected | Old behaviour caught |
|---|---|---|---|---|---|
| SER-1 | `Serialize.CompactSizeVectors` | — | encode/decode §4.1 values | exact bytes; `fdfc00`, `fe00010000`, values over the max rejected | writer truncated ≥ 65,536, reader threw on `FE` (CON-14) |
| SER-2 | `Serialize.ReaderBounds` | 10-byte buffer | read u64 twice; `varBytes` with length 2^32 − 1 | `ParseError`, no read past the end | unchecked offsets |
| SER-3 | `Serialize.NoTrailingBytes` | V-TX-1 bytes + 1 byte | `Transaction::deserialize` | `ParseError("trailing bytes")` | tail ignored |
| TX-1 | `TxV2.VectorRoundTrip` | V-TX-1 hex | parse, re-serialize, txid | identical bytes; txid `4fd3e277…4740` | pubkey dropped by serialize (CRY-6, DB-V1) |
| TX-2 | `TxV2.HugeCountsNoAllocation` | `02000000 fe ffffffff` + 20 bytes | parse | `ParseError` before any allocation larger than the input | unbounded loops/allocations (OPS-7) |
| TX-3 | `TxV2.Size64Invalid` | a parseable 64-byte transaction | `CheckTransaction` | `bad-txns-size-64` | no rule |
| BLK-1 | `BlockV2.RegtestGenesisVector` | regtest params | `Genesis::build`, serialize, hash | equals §3.5 | genesis mined at runtime, never asserted (CON-20) |
| BLK-2 | `BlockV2.HugeTxCount` | header + `fe ffffffff` | `Block::deserialize` | fast `ParseError`, no huge allocation | `reserve(txCount)` asked for 241 GB (OPS-7, NET-16) |
| BLK-3 | `BlockV2.OversizeRejectedBeforeParse` | 1,000,001-byte payload | block entry point | MALFORMED, parser not called | no size check on the side path (NET-16) |
| NET-F1 | `NetFrame.LengthWrap` | header with length 0xFFFFFFF0 and 30 bytes | `NetMessage::deserialize` | error, ASan clean | heap out-of-bounds read (OPS-8) |
| NET-F2 | `NetFrame.ExactCommand` | command `txanything` | parse | unknown command, not `tx` | prefix match (NET-17) |
| NET-F3 | `NetFrame.WrongMagic` | test magic on main | parse | MALFORMED | magic not checked in `deserialize` |
| FZ-1 | `Fuzz.ParsersRandomMutation` | seeds: V-TX-1, regtest genesis block, an undo record, frames | 100,000 mutations each | no crash or sanitizer report; accepted inputs re-serialize identically | OPS-7, OPS-8, CON-14 |
| FZ-2 | libFuzzer targets (nightly) | `BUCKS_FUZZ=ON` | 10 min per target | no findings | fake fuzzer (OPS-6) |

### 15.2 Proof of work and time

| ID | Test | Setup | Action | Expected | Old behaviour caught |
|---|---|---|---|---|---|
| POW-1 | `Pow.CompactVectors` | — | decode/encode §6.2 table | exact; negative/overflow flagged | sign bit handled inconsistently (CON-17) |
| POW-2 | `Pow.FullCompare` | target of `0x1d00ffff` | `CheckProofOfWork` with hash = target and target + 1 | pass / fail | 3-byte comparison ignored low bytes |
| POW-3 | `Pow.BlockProofVectors` | — | `blockProof` | §6.4 table | `workForBits` returned 0 (CON-2, NET-V1) |
| POW-4 | `Pow.RetargetVectors` | — | `CalculateNextWorkRequired` | §6.5 table; −3,600 s → `0x1c0168fd` | uint32 wrap made it 4× easier (CON-7) |
| POW-5 | `Pow.OverflowGuard` | params with `0x1f00ffff` and retargeting on | startup assertions | abort | product overflowed |
| PAR-1 | `Params.Distinct` | 3 networks | compare | magics, ports, address versions differ | one magic for all, Bitcoin's port 8333 |
| PAR-2 | `Params.PowLimitDecodes` | 3 networks | §3.7 checks | pass | — |
| PAR-3 | `Params.ChainWorkAccumulates` | `TestChain`, 10 blocks | tip chain work | 22 (2 per regtest block incl. genesis) | always 0 |
| HDR-1 | `HeaderRules.BadPow` | header with hash > target | `AcceptBlockHeader` | INVALID `high-hash`, not stored | — |
| HDR-2 | `HeaderRules.WrongBits` | `TestChain`; header with easier bits | `AcceptBlockHeader` | INVALID `bad-diffbits` | DB path trusted self-declared bits (CON-1, DB-2) |
| HDR-3 | `HeaderRules.WrongHeight` | height = parent + 2 | `AcceptBlockHeader` | INVALID `bad-height` | peer-chosen height drove the reorg path (CON-1) |
| HDR-4 | `HeaderRules.MtpBoundary` | time = MTP and MTP + 1 | `AcceptBlockHeader` | INVALID / VALID | MTP taken from the tip, not the branch |
| HDR-5 | `HeaderRules.FutureNotCached` | mock clock; time = now + 7,201 | accept; advance clock 2 s; accept again | TIME_TOO_NEW, nothing stored; then VALID | — |
| HDR-6 | `HeaderRules.TimewarpFirstBlock` | synthetic 2016 headers (main params); block 2016 time = prev − 601, − 600 | stage B | INVALID `time-timewarp-attack` / VALID | timewarp possible (CON-7) |
| HDR-7 | `HeaderRules.PeriodNotNegative` | block 2015 earlier than block 0 but above MTP | stage B | INVALID `time-period-negative` | negative span accepted |
| HDR-8 | `HeaderRules.CheckpointFork` | regtest copy with checkpoint at height 5 | header forking at height 3 | INVALID `bad-fork-prior-to-checkpoint` | no checkpoints |
| HDR-9 | `HeaderRules.UnknownParent` | header with random prev | `ProcessNewBlockHeaders` | MISSING_PARENT, nothing stored, no penalty | peer penalised 20 per block (NET-8) |

### 15.3 Transactions, signatures, connect and undo

| ID | Test | Setup | Action | Expected | Old behaviour caught |
|---|---|---|---|---|---|
| TXR-1 | `TxRules.OnlyTwoOutputTypes` | outputs: P2PK, `51` (OP_TRUE), bare multisig, P2SH, non-minimal NULL_DATA push, NULL_DATA payload 1,025 | `CheckTransaction` | `bad-txns-output-type` each | any script accepted, spendable by anyone (CON-3, CRY-4, DB-3) |
| TXR-2 | `TxRules.NullDataZeroValue` | NULL_DATA with amount 1 / 0; mine the 0 one | check; coins view | `bad-txns-nulldata-value`; 0 accepted and absent from the UTXO set | markers burned value and became UTXOs (GLD-11) |
| TXR-3 | `TxRules.MoneyRange` | output MAX_MONEY + 1; outputs summing above MAX_MONEY; inputs summing above MAX_MONEY | check / connect | range reject codes | input sum checked only against UINT64_MAX |
| TXR-4 | `TxRules.DuplicateInputs` | same prevout twice in one tx; in two txs of one block | check / connect | `bad-txns-inputs-duplicate`; `bad-txns-inputs-missingorspent` | — |
| TXR-5 | `TxRules.CoinbaseRules` | scriptSig 3 and 101 bytes; wrong height; lockTime 1; sequence 0 | stages C/D | each fails with its code | no height commitment (CON-8) |
| TXR-6 | `TxRules.Finality` | lockTime = height + 1 and = MTP + 1 with a 0xFFFFFFFE sequence; same with all-final sequences | block / mempool | `bad-txns-nonfinal` / TX_PREMATURE; final variants valid | lock time ignored (CON-15) |
| SIG-1 | `Sighash.Vector` | V-TX-1 | `sigHash(0)`, `sigHash(1)` | §5.4 values | index ignored (CRY-16) |
| SIG-2 | `Sighash.VectorSignaturesVerify` | V-TX-1 with its coins in a view | `verifyP2PKHInput` both inputs | valid | — |
| SIG-3 | `Sighash.CommitsToEverything` | V-TX-1; change one of: spent amount, spent script, a prevout, a sequence, an output, version, lockTime, index, genesis hash | verify | fails every time | no amounts/scripts/chain committed (CRY-16, CON-12) |
| SIG-4 | `Sighash.MalleationRejected` | input 0 variants: s → n − s; DER with extra zero byte; sighash byte 02 / 81; 65-byte pubkey; one extra byte in scriptSig | stages C/E | INVALID each, with the expected code | txid malleable (CRY-5, CON-12) |
| SIG-5 | `Sighash.WrongKey` | coin of pub1; signature by priv2 with pub1 in scriptSig | connect | `bad-sig` | — |
| SIG-6 | `Sighash.WrongOwner` | coin of pub1; pub2 and a valid sig2 | connect | `bad-pubkey-hash` | ownership skipped for non-template scripts |
| SIG-7 | `Sighash.CrossNetworkReplay` | tx signed for regtest | validate with test params | `bad-sig` | no chain id |
| SIG-8 | `Sighash.PlaceholderKeyForgery` | 33-byte off-curve key, forged `r = x(kG), s = e/k` | connect | `bad-pubkey` | keyless forgery accepted (CON-5) |
| CB-1 | `Connect.CoinbaseLimit` | coinbase = subsidy + fees + 1 | `ConnectBlock` | `bad-cb-amount` | not checked on the reorg path (CON-1) |
| CB-2 | `Connect.FeesClaimable` | block with fee F; coinbase = subsidy + F | connect | VALID | fees burned (DB-13) |
| CB-3 | `Connect.Halving` | regtest, height 150 | coinbase 50 / 25 BUCKS | `bad-cb-amount` / VALID | fixed 50 BUCKS (API-17) |
| CB-4 | `Connect.BIP30` | view preloaded with coin (next coinbase txid, 0) | connect | `bad-txns-BIP30` | silent overwrite (DB-6) |
| CB-5 | `Connect.UniqueCoinbases` | `TestChain` mines 200 blocks to one key | count that key's coins | 200 distinct coins | 1 surviving coin (CON-8, API-6, DB-6) |
| CB-6 | `Connect.Maturity` | spend a coinbase at +99 / +100 | connect | premature / VALID | — |
| CB-7 | `Connect.InBlockChainSpend` | tx B spends tx A's output in the same block | connect, then disconnect | VALID; after disconnect the view equals the one before | output resurrected (DB-7) |
| CB-8 | `Undo.RestoresHeight` | block spends a coinbase from height h; disconnect | read restored coin | height h, coinbase flag set | height restored as 0 (DB-7) |
| CB-9 | `Undo.CorruptionDetected` | flip a byte; truncate; wrong block hash; count − 1 | parse / disconnect | CORRUPT/INTERNAL, no crash | out-of-bounds read (DB-7) |
| CB-10 | `Connect.SigopsLimit` | stub `classify` returning sigops so a block totals 20,001 | `CheckBlock` | `bad-blk-sigops` | vault checks not counted (GLD-9, CON-16) |

### 15.4 Chain state, reorgs and startup

| ID | Test | Setup | Action | Expected | Old behaviour caught |
|---|---|---|---|---|---|
| CS-1 | `ChainState.HeavierBranchWins` | tip at 5; branch from 3 with 3 blocks | process branch | tip moves to the branch; coins equal a fresh replay of the branch | never reorged (CON-2, NET-V1) |
| CS-2 | `ChainState.EqualWorkKeepsFirst` | two blocks at the same height | process both | first stays tip | — |
| CS-3 | `ChainState.ZeroPowAttack` | API-V2 recipe: B1 (prev = tip − 1, no valid PoW), B2 (tiny bits, coinbase = MAX_MONEY) | announce and deliver | both rejected at stage A, nothing stored, tip and coins unchanged | full takeover with unlimited coinbase (API-V2, NET-1, NET-2, DB-1, DB-2, CON-1) |
| CS-4 | `ChainState.SideBlockKeptByHash` | valid side block at height 4; persistent store | process; restart | both blocks stored by hash; same tip after restart | main row overwritten (CON-6, DB-1) |
| CS-5 | `ChainState.InvalidBlockInBranch` | heavier 3-block branch, middle block pays subsidy + 1 | process | middle `FAILED_VALID`, child `FAILED_CHILD`; tip = best valid chain; UTXO statistics as before | node stranded at the ancestor (CON-9) |
| CS-6 | `ChainState.ReorgAtomic` | store hook fails the commit of a 3-deep reorg | process; restart | nothing committed; old tip and UTXO statistics intact | partial disconnects persisted (CON-9, CON-18) |
| CS-7 | `ChainState.ForkUsesForkState` | branch block spends an output created only on the branch | reorg | branch accepted | fork validated against the tip's UTXO set (CON-9) |
| CS-8 | `ChainState.DisconnectedTxsReturn` | tx confirmed at 5; reorg to a branch without it | process | tx back in the mempool | tx lost (DB-11) |
| CS-9 | `ChainState.DeepReorgRefused` | `maxAutoReorgDepth = 5`; heavier branch forking 6 deep | process; then approve | tip unchanged and fork alert set; after approval the reorg happens | unlimited reorg depth (CON-17) |
| CS-10 | `ChainState.UnrequestedFarBlock` | unrequested valid block 300 above tip | process | not stored | any announced block fetched (NET-7) |
| CS-11 | `ChainState.KnownInvalidFast` | after CS-5 resend the bad block and a child header | process | `duplicate-invalid` / `bad-prevblk` without connect work | no invalid cache |
| CS-12 | `ChainState.PubKeyTravels` | node A mines a block with a spend; node B has the same history | serialize, B processes | VALID; same tip on both | spends rejected by every peer (CRY-6, CON-4, DB-V1) |
| CS-13 | `ChainState.OrderIndependent` | 10-block chain delivered in random order (all requested) | process | same tip as in-order delivery | unknown parent = failure (NET-8) |
| CS-14 | `ChainState.MissingParentNoPenalty` | block with unknown parent from a peer | process | MISSING_PARENT, no penalty | misbehaviour 20 (NET-8) |
| MRK-2 | `Merkle.MutatedBlockNotBlacklisted` | valid block B; B′ = B plus a duplicate of its last tx | process B′ then B | B′ MUTATED, header not failed; B VALID and tip | relayer can stall a valid block (CON-13) |
| ST-1 | `Startup.GenesisMismatch` | params with a wrong genesis hash | start | exit code 3 with message | never asserted (CON-20) |
| ST-2 | `Startup.WrongNetworkDatadir` | regtest datadir opened as test | start | FATAL | — |
| ST-3 | `Startup.CorruptHeaderRow` | flip a byte in a stored header | start | FATAL "block index corrupt" | silently started a new chain (DB-14) |
| ST-4 | `Startup.VerifyDbBadUndo` | corrupt the undo of tip − 2 | start (checkblocks 24) | FATAL naming the block | undo trusted (DB-7) |
| ST-5 | `Startup.UtxoStatsMismatch` | delete one coin row directly | start | FATAL | no consistency check |
| ST-6 | `Startup.ResumesBestChain` | stop after heavier blocks were stored but not connected | start | they are connected | — |
| ST-7 | `Startup.MainnetUnconfigured` | main params with zero genesis hash | start | exit code 3 "not configured" | placeholder genesis (OPS-13) |

### 15.5 Mempool, miner, network, hygiene

| ID | Test | Setup | Action | Expected | Old behaviour caught |
|---|---|---|---|---|---|
| MP-1 | `Mempool.FullValidation` | bad signature; wrong owner; bad output type | accept | INVALID each | non-P2PKH spendable (DB-3) |
| MP-2 | `Mempool.FirstSeen` | tx1 and tx2 spend one coin | accept both | tx2 TX_CONFLICT | — |
| MP-3 | `Mempool.ConflictRemovedOnBlock` | tx1 in mempool; a block confirms tx2 (same input) | connect; build template | tx1 removed; template valid | mining stopped for ever (DB-9) |
| MP-4 | `Mempool.ReorgRemovesInvalid` | mempool tx spends an output that exists only on the old branch | reorg | tx removed | never revalidated (DB-9) |
| MP-5 | `Mempool.EvictionByFeeRate` | small byte cap; fill with low fee rates | add higher, then lower fee-rate txs | higher accepted, lowest evicted, rolling floor raised; lower rejected | rejected newcomers regardless of fee (DB-13) |
| MP-6 | `Mempool.DustAndMinFee` | output 545; fee below floor | accept | TX_POLICY `dust` / `min-fee-not-met` | zero-fee spam accepted (DB-13) |
| MP-7 | `Mempool.OrphanLimits` | 101 orphans, 26 from one peer; advance clock 20 min | add | ≤ 100 total, ≤ 25 per peer; expired removed | — |
| MP-8 | `Mempool.OrphanAfterConfirmation` | child of a mempool tx | add child; confirm parent | orphan, then accepted | rejected |
| MP-9 | `Mempool.ReloadValidated` | persisted valid tx and tampered tx; restart | load | valid reloaded; tampered dropped and deleted | raw reload, errors swallowed (DB-10) |
| MP-10 | `Mempool.NonStandardSequence` | sequence 0x12345678 | accept | TX_POLICY | — |
| MIN-1 | `Miner.TemplateRespectsLimits` | 2 MB of valid mempool txs | `createNewBlock`, then process | ≤ 1,000,000 bytes, best fee rates first, test validity passes, block accepted | whole mempool in one block (CON-16, API-17, DB-9) |
| MIN-2 | `Miner.TemplateAtHalving` | regtest tip at 149 | template | coinbase 25 BUCKS + fees | fixed 50 BUCKS |
| MIN-3 | `Miner.TimewarpAwareTime` | regtest; block 2015 dated 3,000 s ahead of the clock | template for 2016 | time ≥ tip.time − 600 | — |
| MIN-4 | `Miner.ExtraNonceRollover` | test hook: nonce space of 10 | mine | extra nonce increments, merkle root changes, block found | nonce wrapped silently |
| MIN-5 | `Miner.StaleTemplateAbandoned` | tip changes during hashing | mine | restart on the new tip; no stale submit | — |
| MIN-7 | `Miner.SyncRule` | main-like params on a synthetic index: (a) tip 2 days old, no better header; (b) a better header known but not connected | `createNewBlock` | (a) template built; (b) "node is syncing" | — |
| MIN-6 | `ServerAuth.MineToSecondWallet` (existing test, regtest) | as today | `POST /api/mining/mine` | 200, height + 1, reward to wallet 1, wallet 0 unchanged | keeps the CRY-9/OPS-3 regression |
| P2P-1 | `Network.InvalidBlockCategory` | two regtest nodes on loopback | node 1 sends a block with a bad coinbase | node 2 records category INVALID for that peer (counter, since loopback is exempt from bans) | every failure scored the same |
| P2P-2 | `Network.ConvergeAfterFork` | two nodes mine different 3-block branches, then connect | sync | both on the most-work branch, no penalties | permanent split and bans (NET-8) |
| HYG-1 | `build_hygiene` | — | grep for deleted symbols | none found | dead code compiled in (OPS-14) |

---

## 16. Open questions for the owner

Each has a default that this spec already uses; the implementation can start without an answer.

| # | Question | Default (used in this spec) |
|---|---|---|
| Q1 | **Launch difficulty.** Keep the minimum difficulty `0x1d00ffff` (about 2^32 hashes per block), or start higher because you know the launch hashrate? Too high can stall the first 2016 blocks for months. | `0x1d00ffff` |
| Q2 | **Automatic reorg limit.** Refuse automatic reorgs deeper than 100 blocks (about 17 hours) and require operator action? `0` disables the limit (pure Bitcoin behaviour). A smaller value protects exchanges sooner but makes network partitions harder to heal. | 100 |
| Q3 | **Merge mining with Bitcoin.** Not included. It would give Bucks Bitcoin-level hashrate protection, but changes the header format, mining software and the trust model. | no |
| Q4 | **Address prefix.** Base58Check version 0x1A (addresses start with `B`), or a bech32m format such as `bk1…` (better error detection, more code in the wallet)? | 0x1A |
| Q5 | **Genesis inscription and launch time** (needed only at launch). | — |
| Q6 | **Mempool v1 without replace-by-fee and child-pays-for-parent** (first-seen, confirmed inputs only). Stuck low-fee payments wait for expiry (14 days). | accepted |

### 16.1 Coordination items (for the integration lead, not owner decisions)

1. Storage spec: identical status bits (§8.1), undo layout (§4.7), commit unit = one `ActivateBestChain` step (§8.4), chain-state keys and UTXO statistics (§8.7, §8.8).
2. Gold spec: `GoldRules` interface (§7.8), `MAX_NULLDATA_PAYLOAD` = 1,024 must fit the largest vault event, gold sigop counting.
3. P2P spec: result categories and penalties (§7.1), interim `getheaders`/`headers` formats (§8.9), per-peer download limits (§8.6).

