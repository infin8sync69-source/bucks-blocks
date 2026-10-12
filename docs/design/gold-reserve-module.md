# Bucks Gold Reserve Module Specification (Phase 3)

| | |
|---|---|
| Status | Draft for owner review. Every decision below is final unless it is listed in §23 (Open questions). |
| Date | 2026-10-08 |
| Applies to | The relaunched chain (new genesis). No compatibility with the old `GOLD`/`REDM` markers, the old `chain_state` row `gold_reserve` or the old governance log. |
| Related specs | `docs/design/consensus-validation.md` ("consensus spec"); `docs/design/storage-concurrency.md` ("storage spec", not yet written when this spec was finished: assumptions in §22); Phase 1 crypto (`src/crypto.cpp`, fail-closed, strict DER, low-S). |
| Audit input | `audit.json` of 2026-10-07. Finding IDs are mapped to sections in §18. |

All `file:line` references point to the current tree (2026-10-08). The gold code is still the original code that the audit describes.

---

## 0. Conventions

- The conventions of the consensus spec §0 apply: integers are little-endian (LE); `Hash256` is printed as hex in stored byte order; `TaggedHash(tag, x) = SHA256(SHA256(tag) ‖ SHA256(tag) ‖ x)`; `‖` is concatenation; MUST is a hard rule.
- **mg** = milligram of gold. Every reserve amount is a `uint64` number of mg. 1 g = 1,000 mg. **1 mithqal = 4.25 g = 4,250 mg.**
- **Committee** = the five vault keyholders. **Index** = key slot 0…4. `GOLD_STANDARD.md` numbers the keyholders 1…5; index = number − 1.
- **Reject codes** are short ASCII strings used in logs, the API and tests.

---

## 1. Scope and main decisions

### 1.1 What the module is

The gold reserve is a **number in consensus state**: the milligrams of gold that the vault committee reports it holds. Every node computes the same number from the chain. Gold events do **not** create, destroy or lock coins. Coins are created only by mining. Coins cannot be exchanged for gold through this software. The chain proves only *who signed which number, for which chain, in which order*. The UI calls the figure "reported reserve" and shows it as **coverage**: reserve ÷ (coins in circulation × 4,250 mg).

### 1.2 Main decisions

| # | Decision | Section |
|---|---|---|
| G1 | Reserve, sequence counters, vault keyset, pause flag and window counters are consensus state. They live in the coins view and are committed in the same database transaction as the UTXO changes. | §7, §9 |
| G2 | One payload format: magic `BVLT`, version byte `01`, fixed layout per type, exact length, little-endian, amounts in mg. | §5 |
| G3 | Six event types: `GOLD` (reserve increase), `REDM` (reserve decrease), `AUDT` (auditor attestation), `KEYR` (key rotation), `PAUS` (freeze), `UNPS` (unfreeze). | §5 |
| G4 | Signed message: `TaggedHash("Bucks-Vault-v1", genesisHash ‖ keysetHash ‖ unsigned payload)`. The event id is this digest. | §6 |
| G5 | Replay protection: strict sequence numbers kept in chain state, an expiry height, a chain tag plus the full genesis hash, and the keyset hash. | §6, §8 |
| G6 | Exactly the required number of signatures, key indexes strictly increasing. `GOLD` must include the Independent Auditor (index 1). `REDM` must include the Standards Board (index 3). `AUDT` is signed by the auditor alone. | §3.2, §5.5 |
| G7 | An invalid gold payload makes its transaction and its block **invalid**. Nothing is skipped. No clamping. All arithmetic is checked. | §8 |
| G8 | Undo = the gold state before the block plus the hash of the state after it. Disconnect restores the snapshot. | §9 |
| G9 | On-chain key management: `KEYR` needs 4 of 5 and activates 1,008 blocks later; `PAUS` needs 3 of 5 and acts at once; `UNPS` needs 4 of 5. A disaster (2 lost keys) is fixed by a hard fork through a parameter list. | §11 |
| G10 | A mainnet node refuses to start without real, valid vault keys (OPS-13). | §4.4 |
| G11 | Anyone may carry an event in a normal transaction. Never in a coinbase. At most one event per transaction. | §8.5 |
| G12 | API: `GET /api/gold/status`, `GET /api/gold/events` (public, read-only); `POST /api/gold/submit` (authenticated). All amounts are decimal strings. Coverage is computed with integers. | §13 |
| G13 | UI: one "Gold reserve (reported)" panel. No USD values, no "backed", no "redeem", no per-wallet gold. The headline uses events with 6 confirmations. | §14 |
| G14 | Governance markers are removed from the node (not consensus, not stored). | §15 |
| G15 | Tooling: `bucks-vault` and `bucks-vault-offline` (no network code); RFC 6979 deterministic signatures; encrypted key files. | §12 |

Deferred features are listed in §21.

---

## 2. What is wrong today

1. **Fail-open keys.** The five placeholders are 68 hex characters (34 bytes) and not curve points (`include/consensus.h:170-176`). The old `verifyECDSA` ignored the decode failure and accepted keyless forgeries (GLD-1, CRY-2, CON-5). Phase 1 made `verifyECDSA` fail closed (`src/crypto.cpp:527-580`), but the placeholders are still compiled in and nothing stops a mainnet start with them (OPS-13).
2. **Replayable message.** Signers sign `sha256(marker_LE4 ‖ amount_LE8)` (`src/blockchain.cpp:56-60`): no chain, no sequence, no expiry, no domain tag (GLD-2, CRY-3).
3. **Skip on connect, reverse on disconnect.** Events with bad signatures are skipped (`src/blockchain.cpp:357-360`). `disconnectTip` re-scans the outputs and reverses every marker without checking signatures (`src/blockchain.cpp:482-522`). Connect clamps REDM to 0 (`371-377`) but disconnect adds the full amount back (`513`). `+=` is unchecked (`365`, `513`) (GLD-3, CRY-V1, NET-V2, DB-8, CON-11).
4. **Node-local counter.** `goldReserve` is one `chain_state` integer (`src/blockdb.cpp:380-416`), written outside the UTXO commit (`src/blockchain.cpp:384-386, 520-522`), loaded blindly at startup (`588-593`), never recomputed (GLD-4, CON-18).
5. **Loose encoding.** The marker is read big-endian (`src/blockchain.cpp:341-344`) but hashed little-endian (`58`); byte 6 is never checked; trailing bytes are ignored; the doc says "grams × 1000" (`GOLD_STANDARD.md:39`) but the code, API and UI say grams (`src/blockchain.cpp:364`, `src/server.cpp:1406`, `web/vault.jsx:76-79`) (GLD-10).
6. **Documented rules that do not exist:** Standards Board on every REDM (`GOLD_STANDARD.md:82-83`), 6 confirmations (`71`), signing "the unsigned transaction hash" (`65`), rotation (`88-100`), revocation (`104-112`) (GLD-6).
7. **Unauthenticated governance log**, never reversed and never read (`src/blockchain.cpp:388-431, 524-527`; `src/blockdb.cpp:418-512`) (GLD-5, DB-16).
8. **The UI claims more than the chain proves:** "Your gold" and "Redeem physical gold" (`web/wallet.jsx:249-277`), USD from a hard-coded $84.5/g (`web/data.jsx:10-42`), a 0.0675 mg "peg" (`web/auth.jsx:8-16`), "mg Au minted daily" (`web/configure.jsx:190`), "Direct gold settlement" (`web/data.jsx:66`) (GLD-7, API-18).

---

## 3. Model and trust

### 3.1 Roles

| Index | Role | Special duty |
|---|---|---|
| 0 | Founding Team | prepares events (coordinator) |
| 1 | Independent Auditor | MUST sign every `GOLD`; signs `AUDT` alone |
| 2 | Community Representative | — |
| 3 | Standards Board | MUST sign every `REDM` |
| 4 | Geographic Reserve | custodian of the metal |

One person or organisation MUST NOT hold two roles. The chain cannot enforce this; the key ceremony record does (§11.1).

### 3.2 Event types and thresholds

| Type | Meaning | Signatures (exactly) | Required index | Counter | Valid while paused |
|---|---|---|---|---|---|
| `GOLD` | reserve increase (deposit) | 3 | 1 | committee | no |
| `REDM` | reserve decrease (withdrawal) | 3 | 3 | committee | no |
| `AUDT` | auditor attestation | 1 | exactly {1} | audit | yes |
| `KEYR` | propose or cancel a key rotation | 4 | — | committee | yes |
| `PAUS` | freeze `GOLD` and `REDM` | 3 | — | committee | no |
| `UNPS` | end the freeze | 4 | — | committee | only while paused |

Why:
- `GOLD` needs the auditor: no reserve increase without the party that checks the metal (`GOLD_STANDARD.md:18, 61-62`).
- `REDM` needs the Standards Board, as documented (`GOLD_STANDARD.md:82-83`).
- `AUDT` is the auditor's own statement. It must not need the committee's permission, especially when the news is bad. It never changes the reserve.
- `KEYR` keeps the documented 4-of-5 (`GOLD_STANDARD.md:95-96`).
- `PAUS` 3 / `UNPS` 4: a majority can freeze at once; undoing a freeze needs more keys than a 3-key attacker holds.
- "Exactly N signatures" (as in Bitcoin's m-of-n `CHECKMULTISIG`) bounds the verification cost and removes signature-set malleability.

### 3.3 What the chain proves and what it does not

The chain proves that the required keys signed exactly this event, for this network and this keyset, once, in order. It does **not** prove that gold exists, its purity or its location, or that the referenced documents are true. The `AUDT` events and the document hashes make the claims checkable later; they do not make them true. This trust limitation (GLD-8) is stated in the UI (§16).

---

## 4. Parameters

### 4.1 `include/consensus.h`

Delete `include/consensus.h:137-176` (`GOLD_MARKER`, `REDEEM_MARKER`, `VAULT_MULTISIG_THRESHOLD`, `VAULT_MULTISIG_TOTAL`, `VAULT_AUTHORITY_PUBKEYS`) and `178-191` (`GOV_PREFIX`, `GOV_*`). Add:

```cpp
/* ---------------- Gold reserve module (gold spec) ---------------- */
inline constexpr std::array<uint8_t, 4> GOLD_MAGIC = {0x42, 0x56, 0x4C, 0x54}; // "BVLT"
constexpr uint8_t  GOLD_VERSION       = 1;
constexpr size_t   GOLD_HEADER_SIZE   = 25;
constexpr size_t   MAX_GOLD_PAYLOAD   = 520;   // KEYR propose with four 72-byte signatures
constexpr int      VAULT_KEY_COUNT    = 5;
constexpr uint8_t  ROLE_FOUNDING_TEAM = 0, ROLE_AUDITOR = 1, ROLE_COMMUNITY = 2,
                   ROLE_STANDARDS_BOARD = 3, ROLE_GEOGRAPHIC_RESERVE = 4;
constexpr uint64_t MAX_RESERVE_MG     = 1'000'000'000'000'000ULL; // 10^15 mg = 1,000,000 t (< 2^53)
inline constexpr char VAULT_TAG[]     = "Bucks-Vault-v1";
```

`MITHQAL_MG = 4250` is a display constant in `include/gold.h`. It is never used by validation.

### 4.2 `ChainParams` additions

The consensus spec §3.3 reserves room for this ("Gold module parameters … are added by the gold spec").

```cpp
using CompressedPubKey = std::array<uint8_t, 33>;
struct KeysetOverride {            // emergency hard fork only (§11.5)
    uint32_t height;               // first block height where it applies
    uint32_t epoch;                // multiple of 1,000,000
    std::array<CompressedPubKey, 5> keys;
};
struct GoldParams {
    std::array<CompressedPubKey, 5> genesisKeys;   // index = role; all-zero = not configured
    uint32_t keyrActivationDelay;                  // blocks
    uint64_t maxEventMg;                           // per GOLD or REDM event
    uint32_t windowBlocks;                         // cap window length
    uint64_t maxWindowMg;                          // per window, per direction
    uint32_t maxExpiryDelta;                       // expiryHeight − height ≤ this
    std::vector<KeysetOverride> keysetOverrides;   // empty at launch
};
// struct ChainParams { ... GoldParams gold; };
```

### 4.3 Network values

| Field | main | test | regtest |
|---|---|---|---|
| `genesisKeys` | from the key ceremony (§11.1) | rehearsal keys, generated and held by five team members at implementation time | `pub(SHA256("bucks-regtest-vault-key-<i>"))`, i = 0…4 (public test keys, §17) |
| `keyrActivationDelay` | 1,008 (≈ 7 days) | 144 | 10 |
| `maxEventMg` | 1,000,000,000 (1 t) | same | same |
| `windowBlocks` | 1,008 | 1,008 | 144 |
| `maxWindowMg` | 10,000,000,000 (10 t) | same | same |
| `maxExpiryDelta` | 4,032 (≈ 4 weeks) | 4,032 | 4,032 |
| `keysetOverrides` | none | none | none |

### 4.4 Startup checks (OPS-13)

`GoldRules::checkParams(params)` runs in step 1 of `LoadChainState` (consensus spec §8.7). Any failure: `FATAL: <network> vault keys are not configured or invalid: <reason>`, exit code 3 (same convention as the consensus spec §3.5).

1. All-zero `genesisKeys` → "not configured".
2. Every key is 33 bytes and passes `isValidPublicKey` (`include/crypto.h:73`).
3. The five keys are pairwise distinct.
4. On main and test: no key equals a regtest key, the old placeholder pattern (prefix `02`, bytes 1…32 all `aa`), or `k·G` for k = 1…256. On main: no key equals a testnet key.
5. `1 ≤ maxEventMg ≤ maxWindowMg ≤ MAX_RESERVE_MG`; `windowBlocks ≥ 1`; `keyrActivationDelay ≥ 1`; `1 ≤ maxExpiryDelta ≤ 52,560`.
6. `keysetOverrides`: ascending heights; epochs are multiples of 1,000,000 and strictly increasing; each keyset passes rules 2–4.

**Release guard:** the CMake option `BUCKS_RELEASE=ON` adds a build step that runs `bucks-vault-offline verify-params --network main --strict` and fails the build if the check fails. A release build with placeholder keys therefore cannot be produced, and a non-release build cannot start mainnet with them.

---

## 5. Event format

### 5.1 Where events live

An event is the payload of a NULL_DATA output (consensus spec §4.2): amount 0, script = `OP_RETURN` plus one minimal push, never added to the UTXO set. Every NULL_DATA payload goes through `classify` (§5.5):
- payload does not start with `BVLT` → **not gold** (free data; policy limits it to 80 bytes);
- payload starts with `BVLT` → it MUST be a valid event, otherwise the transaction is invalid. The magic is reserved.

### 5.2 Common header (25 bytes)

| Offset | Size | Field | Encoding and rule |
|---|---|---|---|
| 0 | 4 | `magic` | `42 56 4C 54` ("BVLT") |
| 4 | 1 | `version` | `01`; any other value: `bad-gold-format` |
| 5 | 4 | `type` | ASCII: `GOLD` `47 4F 4C 44`, `REDM` `52 45 44 4D`, `AUDT` `41 55 44 54`, `KEYR` `4B 45 59 52`, `PAUS` `50 41 55 53`, `UNPS` `55 4E 50 53` |
| 9 | 4 | `chainTag` | bytes 0–3 of the network's genesis hash (stored order) |
| 13 | 8 | `seq` | u64; committee types: next committee number; `AUDT`: next audit number |
| 21 | 4 | `expiryHeight` | u32; last block height at which the event is valid |

### 5.3 Bodies (start at offset 25)

**`GOLD` and `REDM`** (unsigned part = 65 bytes)

| Offset | Size | Field | Rule |
|---|---|---|---|
| 25 | 8 | `amountMg` | u64, ≥ 1 |
| 33 | 32 | `docHash` | SHA-256 of the deposit (`GOLD`) or withdrawal (`REDM`) record; not all zero |

**`AUDT`** (unsigned part = 77 bytes)

| Offset | Size | Field | Rule |
|---|---|---|---|
| 25 | 8 | `auditedReserveMg` | u64; the amount the auditor counted (0 allowed) |
| 33 | 4 | `asOfHeight` | u32; the chain height the count refers to |
| 37 | 8 | `auditTime` | u64 UNIX seconds (UTC) of the count, as stated by the auditor; ≥ 1; informational |
| 45 | 32 | `docHash` | SHA-256 of the audit report; not all zero |

**`KEYR`** (unsigned part = 223 bytes for propose, 58 for cancel)

| Offset | Size | Field | Rule |
|---|---|---|---|
| 25 | 1 | `mode` | `01` = propose, `02` = cancel |
| 26 | 165 | `newKeys` | propose only: five 33-byte compressed keys, slot order 0…4 |
| 191 / 26 | 32 | `docHash` | SHA-256 of the rotation record; not all zero |

**`PAUS` and `UNPS`** (unsigned part = 57 bytes)

| Offset | Size | Field | Rule |
|---|---|---|---|
| 25 | 32 | `docHash` | SHA-256 of the incident note; not all zero |

### 5.4 Signature section (starts right after the body, at offset U)

| Size | Field | Rule |
|---|---|---|
| 1 | `count` | MUST equal the type's signature count R (§3.2) |
| R × | entry | `keyIndex` (1 byte, 0…4, strictly greater than the previous entry's index) ‖ `derLen` (1 byte, 8…72) ‖ `der` (`derLen` bytes) |

The payload MUST end exactly after the last entry. DER strictness, the r/s range and low-S are checked by `verifyECDSA` (`src/crypto.cpp:527-580`) in stage E.

| Type | Unsigned part | R | Size with 71-byte signatures | Maximum size | sigOps |
|---|---|---|---|---|---|
| `GOLD`, `REDM` | 65 | 3 | 285 | 288 | 3 |
| `AUDT` | 77 | 1 | 151 | 152 | 1 |
| `KEYR` propose | 223 | 4 | 516 | **520** | 4 |
| `KEYR` cancel | 58 | 4 | 351 | 355 | 4 |
| `PAUS` | 57 | 3 | 277 | 280 | 3 |
| `UNPS` | 57 | 4 | 350 | 354 | 4 |

Every event fits the consensus NULL_DATA limit of 1,024 bytes (consensus spec §3.1).

### 5.5 `classify` (context-free, cheap, no signature checks)

`Classification classify(const std::vector<uint8_t>& payload)` returns `{kind, sigOps, code}`. Checks in this order; the first failure decides:

| # | Check | Result on failure |
|---|---|---|
| C1 | length ≥ 4 and bytes 0–3 = `BVLT` | NOT_GOLD, sigOps 0 |
| C2 | length ≤ 520 | MALFORMED `bad-gold-format` |
| C3 | length ≥ 25; version = 1; type is one of the six tags | MALFORMED `bad-gold-format` |
| C4 | the payload holds the full body of its type and the `count` byte (`KEYR`: mode `01` or `02` decides the body length; any other mode fails) | MALFORMED `bad-gold-format` |
| C5 | `count` = R(type) | MALFORMED `bad-gold-format` |
| C6 | each entry: index ≤ 4, index > previous index, 8 ≤ derLen ≤ 72, bytes present | MALFORMED `bad-gold-format` |
| C7 | no trailing bytes | MALFORMED `bad-gold-format` |
| C8 | `docHash` ≠ 32 zero bytes; `GOLD`/`REDM`: amount ≥ 1; `AUDT`: auditTime ≥ 1 | MALFORMED `bad-gold-format` |
| C9 | roles: `GOLD` indexes contain 1; `REDM` indexes contain 3; `AUDT` indexes = {1} | MALFORMED `bad-gold-roles` |
| — | all passed | EVENT, sigOps = R |

The consensus spec counts `classify(payload).sigOps` in `txSigOps` (consensus spec §7.4), so every vault signature check is inside the 20,000 per-block budget (GLD-9).

---

## 6. Signed message and event id

```
unsignedPart = payload[0 .. U)                       // header + body; everything before `count`
keysetHash   = SHA256( LE32(keysetEpoch) ‖ key0 ‖ key1 ‖ key2 ‖ key3 ‖ key4 )   // 169 bytes in
VaultMsg     = TaggedHash("Bucks-Vault-v1", genesisHash(32) ‖ keysetHash(32) ‖ unsignedPart)
eventId      = VaultMsg
```

- `keysetHash` is computed from the keyset that is active at the block that includes the event (after `normalize`, §7.3).
- Each signer produces a DER ECDSA signature over the 32-byte `VaultMsg`, low-S (secp256k1).
- `SHA256("Bucks-Vault-v1")` = `518360807b3a966b2cee509624fdbfe090512c820dcc201e3a7aca6f92ecc251`.

| Bound data | Stops |
|---|---|
| tag `Bucks-Vault-v1` | use of a vault signature as a transaction signature or a wallet message signature (those use `Bucks/SigHash/v1` and `doubleSha256(varstr("Bucks Signed Message:\n") ‖ …)`, `src/server.cpp:292-293`; the preimages differ in structure and length) |
| full `genesisHash` (+ `chainTag` in the payload) | replay across main, test, regtest and copied chains |
| `keysetHash` (epoch + keys) | use of signatures collected before a rotation |
| `seq` | replay inside one chain (one use per number, kept in state) |
| `expiryHeight` | old, signed but unsubmitted events being used much later |
| type and body | changing the amount, document or keys |

**Why not the txid.** The signatures sit inside the transaction, so signing its txid is circular. Binding a funding outpoint instead would force signers to know the coin in advance and would make the event useless if that coin is spent elsewhere. The sequence number already makes each event single-use. The carrier transaction therefore does not matter (§8.5).

---

## 7. Gold state

### 7.1 Fields and serialization (`GoldState` v1)

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `version` | `01` |
| 1 | 1 | `flags` | bit 0 = paused; bit 1 = rotation pending; other bits MUST be 0 |
| 2 | 8 | `reserveMg` | reported reserve |
| 10 | 8 | `seq` | last committee sequence number used (0 = none) |
| 18 | 8 | `auditSeq` | last audit sequence number used (0 = none) |
| 26 | 4 | `lastAuditAsOfHeight` | `asOfHeight` of the last `AUDT` (0 = none) |
| 30 | 4 | `keysetEpoch` | 0 at genesis, +1 per rotation |
| 34 | 165 | `keys[5]` | active compressed keys, slot order |
| 199 | 4 | `windowIndex` | `height / windowBlocks` of the last `GOLD`/`REDM` |
| 203 | 8 | `windowGoldMg` | `GOLD` total in that window |
| 211 | 8 | `windowRedmMg` | `REDM` total in that window |
| 219 | 4 | `pendingActivationHeight` | only if flag bit 1 |
| 223 | 165 | `pendingKeys[5]` | only if flag bit 1 |

Length: 219 bytes, or 388 with a pending rotation. The parser rejects any other length, unknown flags, version ≠ 1 or invalid keys. `stateHash = SHA256(serialization)`.

Display data (audit details, proposal height, pause height) is **not** in consensus state. It comes from the event index (§9.3).

### 7.2 Initial state

`GoldRules::initialState(params)`: all numbers 0, not paused, no pending rotation, `keysetEpoch = 0`, `keys = params.gold.genesisKeys`. It is written when a datadir is initialised (consensus spec §3.5, rule 4). Genesis carries no events.

### 7.3 `normalize(S, h)` (lazy activation)

Called by `connectOutput` before the contextual checks of each event, and by the API on a copy:

```
normalize(S, h):
  if S.pending and h >= S.pending.activationHeight:
      S.keys = S.pending.keys; S.keysetEpoch += 1; S.pending = none
  for o in params.gold.keysetOverrides (ascending):
      if h >= o.height and S.keysetEpoch < o.epoch:
          S.keys = o.keys; S.keysetEpoch = o.epoch; S.pending = none
```

The state changes only when an event is processed. A block without gold events never changes the gold state, so it never needs gold undo data. The keyset that validates an event at height `h` is always `normalize(S, h).keys`. Readers (API, `bucks-vault status`) show `normalize(copy, tip + 1)`.

---

## 8. Validation

### 8.1 Stage C: `GoldRules::checkTransaction(tx, st)` (new rule T-GOLD)

Called by `CheckTransaction` (consensus spec §7.4) right after T-OUT-TYPE. Context-free.

| # | Rule | Reject code |
|---|---|---|
| T-GOLD-1 | every NULL_DATA payload: `classify` ≠ MALFORMED | the classify code (`bad-gold-format` / `bad-gold-roles`) |
| T-GOLD-2 | at most one payload classified EVENT per transaction | `bad-gold-multiple` |
| T-GOLD-3 | a coinbase carries no EVENT | `bad-gold-coinbase` |

### 8.2 Stage E: `connectOutput` (contextual), in this order

`ConnectBlock` calls it for every NULL_DATA output in block order (consensus spec §7.6). `h` = block height, `S` = gold state of the view.

| # | Check | Reject code |
|---|---|---|
| E0 | `classify`: NOT_GOLD → return OK, state untouched. MALFORMED or coinbase → fail (defensive; stage C already rejects) | as T-GOLD |
| E1 | `chainTag` = `params.genesisHash[0..3]` | `gold-wrong-chain` |
| E2 | `h ≤ expiryHeight`; `expiryHeight − h ≤ maxExpiryDelta` | `gold-expired` / `gold-expiry-too-far` |
| E3 | `normalize(S, h)` | — |
| E4 | committee types: `seq = S.seq + 1`; `AUDT`: `seq = S.auditSeq + 1` (a counter at 2^64 − 1 cannot advance) | `gold-bad-seq` |
| E5 | type rules (table below) | see table |
| E6 | for each entry in order: `verifyECDSA(VaultMsg, der, S.keys[keyIndex])`; the first failure stops | `gold-bad-sig` |
| E7 | apply the effects (§8.3); record the event for the index (§9.3) | — |

E5 type rules (`wG`, `wR` = window totals for `h / windowBlocks`: the stored counters if the window index equals, else 0):

| Type | Rule (in order) | Reject code |
|---|---|---|
| `GOLD` | not paused | `gold-paused` |
| | `amountMg ≤ maxEventMg` | `gold-amount-too-large` |
| | `amountMg ≤ MAX_RESERVE_MG − S.reserveMg` | `gold-reserve-overflow` |
| | `amountMg ≤ maxWindowMg − wG` | `gold-window-cap` |
| `REDM` | not paused | `gold-paused` |
| | `amountMg ≤ maxEventMg` | `gold-amount-too-large` |
| | `amountMg ≤ S.reserveMg` (a REDM can never exceed the reserve) | `gold-redm-exceeds-reserve` |
| | `amountMg ≤ maxWindowMg − wR` | `gold-window-cap` |
| `AUDT` | `auditedReserveMg ≤ MAX_RESERVE_MG` | `gold-audit-range` |
| | `S.lastAuditAsOfHeight < asOfHeight < h` | `gold-audit-height` |
| `KEYR` propose | no pending rotation | `gold-rotation-pending` |
| | every new key passes `isValidPublicKey` | `gold-bad-new-key` |
| | the five new keys are pairwise distinct | `gold-duplicate-new-key` |
| | for each slot i: `new[i] = cur[i]` or `new[i]` is not one of the current keys (no moving a key to another role) | `gold-key-moved` |
| | at least one slot changes | `gold-rotation-noop` |
| `KEYR` cancel | a rotation is pending | `gold-no-rotation` |
| `PAUS` | not paused | `gold-already-paused` |
| `UNPS` | paused | `gold-not-paused` |

The consensus spec's C-GOLD row (`bad-gold-event`) stands for all codes in this section; `ValidationState.code` carries the specific code.

### 8.3 Effects (E7)

| Type | Effect |
|---|---|
| `GOLD` | if `h / windowBlocks ≠ S.windowIndex`: set `windowIndex`, zero both window counters; `reserveMg += amountMg`; `windowGoldMg += amountMg` |
| `REDM` | same window roll; `reserveMg −= amountMg`; `windowRedmMg += amountMg` |
| `AUDT` | `lastAuditAsOfHeight = asOfHeight` (the reserve does not change) |
| `KEYR` propose | `pending = { activationHeight = h + keyrActivationDelay, keys = newKeys }` |
| `KEYR` cancel | `pending = none` |
| `PAUS` / `UNPS` | `paused = true` / `false` |
| all | committee types: `S.seq = seq`; `AUDT`: `S.auditSeq = seq` |

All checks of E5 are done before the effects of E7, so the arithmetic cannot overflow or underflow. There is no clamping anywhere (old: `src/blockchain.cpp:371-377, 505-510`). If any check fails, `ConnectBlock` fails and its view (including the `normalize` of E3) is discarded (consensus spec §7.6); `checkForMempool` always works on a copy.

### 8.4 DoS bounds

| Bound | Value |
|---|---|
| payload size | ≤ 520 bytes, exact length per type |
| signatures per event | exactly R ≤ 4, counted as sigops (inside 20,000 per block) |
| events per transaction | ≤ 1 (T-GOLD-2) |
| failed verification | the first bad signature makes the block invalid; the miner loses the block reward |
| key decoding | five active keys, parsed once per event from 33 bytes (no placeholder strings) |
| mempool | at most one pending event per counter (§10) |
| logging | no per-output logging in validation (old: `src/blockchain.cpp:90-92, 358`) |

### 8.5 Who may include events

Any non-coinbase transaction may carry one event. The carrier is not authenticated: the event's own signatures and sequence number are the authority, as in Bitcoin where validity does not depend on the relayer. Normally the committee's coordinator submits through `POST /api/gold/submit` (§13.3) and the node's wallet pays the fee. If a miner copies an event into another transaction, the effect is identical and the original transaction becomes invalid (its `seq` is used); nothing is gained. Old-format markers (`6a 04 'GOLD' 08 …`) are two pushes, so they are not canonical NULL_DATA and are invalid by T-OUT-TYPE (consensus spec §4.2).

### 8.6 Hook interface (final)

This extends the consensus spec §7.8 (changes marked; coordination in §22):

```cpp
namespace GoldRules {
  struct Classification { Kind kind; uint32_t sigOps; const char* code; };          // + code
  Classification classify(const std::vector<uint8_t>& payload);
  bool checkTransaction(const Transaction& tx, ValidationState& st);                // NEW (stage C)
  GoldUndoBuilder beginBlock(const GoldState& stateBeforeBlock);                     // unchanged
  bool connectOutput(const ChainParams& params, const Transaction& tx, uint32_t txIndex,
                     uint32_t vout, const Hash256& txid, uint32_t height, GoldState& state,
                     GoldUndoBuilder& undo, ValidationState& st);                    // + params
  bool disconnectBlock(GoldState& state, const std::vector<uint8_t>& goldUndo);      // unchanged
  bool checkForMempool(const ChainParams& params, const Transaction& tx,
                       const GoldState& tipState, uint32_t nextHeight,
                       ValidationState& st);                                         // + params, nextHeight
  bool checkParams(const ChainParams& params, std::string& err);                     // NEW (startup step 1)
  bool startupCheck(const ChainParams& params, const GoldState& s,
                    const GoldIndexReader& index, std::string& err);                 // NEW (startup step 9)
  // GoldIndexReader: read-only access to gold_events (§9.3): row count per type, last row,
  // and (height, block_hash) of every row. Implemented by the storage layer.
  GoldState initialState(const ChainParams& params);                                 // NEW (genesis)
}
class GoldUndoBuilder {            // keeps a copy of the state before the block and a pointer to the live state
public:
  std::vector<uint8_t> finish() const;                     // goldUndo bytes (§9.1)
  const std::vector<AppliedEvent>& events() const;         // rows for the event index (§9.3)
};
```

---

## 9. Undo, reorganisation and startup

### 9.1 `goldUndo` format (inside the block undo record, consensus spec §4.7)

| Field | Type | Rule |
|---|---|---|
| `version` | u8 | `01` |
| `before` | CompactSize length + bytes | the `GoldState` serialization before the block (219 or 388 bytes) |
| `afterHash` | Hash256 | `SHA256` of the `GoldState` serialization after the block |

`finish()` returns **0 bytes** if the state after the block is byte-identical to the state before; otherwise the record above (253 bytes, or 424 bytes when `before` holds a pending rotation: CompactSize 388 = `fd 84 01`).

### 9.2 Disconnect

```
disconnectBlock(S, goldUndo):
  if goldUndo is empty: return OK                    // block did not change the gold state
  parse strictly (version, length, valid GoldState, no trailing bytes) else CORRUPT
  if SHA256(serialize(S)) != afterHash: return CORRUPT
  S = before; return OK
```

The block's events are never re-scanned. Disconnect restores exactly what connect produced (fixes GLD-3, CRY-V1, NET-V2, DB-8, CON-11). `CORRUPT` stops the node (consensus spec §7.7).

### 9.3 Event index (derived data, for the API)

```sql
CREATE TABLE gold_events (
    event_id         BLOB    PRIMARY KEY CHECK (length(event_id) = 32),
    block_hash       BLOB    NOT NULL CHECK (length(block_hash) = 32),
    height           INTEGER NOT NULL,
    tx_index         INTEGER NOT NULL,
    txid             BLOB    NOT NULL CHECK (length(txid) = 32),
    vout             INTEGER NOT NULL,
    type             TEXT    NOT NULL CHECK (type IN ('GOLD','REDM','AUDT','KEYR','PAUS','UNPS')),
    seq              INTEGER NOT NULL,
    amount_mg        INTEGER,            -- GOLD/REDM amount; AUDT audited figure; else NULL
    reserve_after_mg INTEGER NOT NULL,   -- committee reserve after this event (every type)
    doc_hash         BLOB    NOT NULL CHECK (length(doc_hash) = 32),
    signer_mask      INTEGER NOT NULL,   -- bit i set = key index i signed
    payload          BLOB    NOT NULL
) WITHOUT ROWID;
CREATE UNIQUE INDEX gold_events_pos ON gold_events(height, tx_index);
```

All values fit SQLite's signed 64-bit integers (amounts ≤ 10^15). The table contains only events of the active chain.

### 9.4 Commit requirements (one transaction per `ActivateBestChain` step, consensus spec §8.4)

1. Delete the `gold_events` rows of every disconnected block (`WHERE block_hash = ?`) **before** inserting the rows of connected blocks (the same event may come back on the new branch).
2. Insert one row per `AppliedEvent` of every connected block.
3. Write the new state: `chain_state` key `gold_state` = the `GoldState` bytes (BLOB value).
4. All of this in the same database transaction as coins, undo records, status bits and the best block hash. In-memory copies change only after COMMIT.

### 9.5 Startup and repair

`GoldRules::startupCheck` (consensus spec §8.7 step 9). Any failure: FATAL with the reason and the advice `restart with --reindex-chainstate`.
1. `gold_state` exists and parses strictly.
2. If `keysetEpoch = 0`: `keys = params.gold.genesisKeys` (catches a binary whose vault keys differ from the datadir).
3. Rows with type ≠ `AUDT` = `seq`; rows with type `AUDT` = `auditSeq`; the last row's `reserve_after_mg` = `reserveMg` (0 rows → reserve 0); every row's `block_hash` is in the active chain at its `height`.

`VerifyDB` (consensus spec §8.7 step 7) disconnects and reconnects the last N blocks on a scratch view and compares the gold state bytes. `--reindex-chainstate` deletes `gold_state` and `gold_events`, writes `initialState`, and rebuilds both by connecting every block. Nothing is ever loaded "blindly" (old: `src/blockchain.cpp:588-593`).

---

## 10. Mempool, relay and mining

1. **One pending event per counter** (new check 7a of AcceptToMempool, consensus spec §9.2: right after the first-seen conflict check, before any signature work). `MempoolEntry::hasGoldEvent` (consensus spec §9.1) becomes `uint8_t goldCounter` (0 = none, 1 = committee, 2 = audit). A second event for the same counter: `TX_POLICY gold-event-pending`. The coordinator submits the next event after the previous one is mined.
2. **Check 14** calls `checkForMempool(params, tx, tipState, tip + 1, st)`: it runs E1–E6 on a copy of the tip state. Any failure is `TX_POLICY` with the stage E code (the event may be valid on another branch or later; no peer penalty). A MALFORMED payload was already INVALID at check 3.
3. **Size policy:** P-NULLDATA allows an EVENT payload up to `MAX_GOLD_PAYLOAD` (520); other payloads stay at 80 bytes.
4. **After every committed step** (consensus spec §9.6 step 4) gold entries are re-checked against the new tip; failures (sequence used, expired, paused, keyset changed) are removed.
5. **Template:** gold transactions are selected like others; their sigops come from `classify`. `createNewBlock`'s `extraPayload` MUST classify as NOT_GOLD, and `POST /api/anchor` (`src/server.cpp:1269-1291`) rejects a CID starting with `BVLT`.

---

## 11. Key management lifecycle

### 11.1 Key ceremony checklist (mainnet; rehearse it first on testnet)

Preparation
1. The owner appoints the five keyholders. No person or organisation holds two roles. The appointment letter names the role and index.
2. Each keyholder prepares an **air-gapped** computer: wiped disk, wireless hardware removed or disabled, operating system from verified media, never connected to a network again.
3. `bucks-vault-offline` is built from the release tag; two people compare its SHA-256 with the published checksum.
4. Each keyholder prepares two backup locations (for example two bank safe-deposit boxes).

Generation (each keyholder, alone, on the air-gapped computer)
5. `bucks-vault-offline keygen --network main --index <i> --out vault-key-<i>.json` asks for a passphrase twice (at least 12 characters) and prints the compressed public key (66 hex characters) and its fingerprint (first 8 bytes of `SHA256(pubkey)`, 16 hex characters).
6. Copy the encrypted key file to two backup USB sticks; write the passphrase on paper; seal each in a separate envelope for the two backup locations.
7. `bucks-vault-offline sign-statement --key vault-key-<i>.json --text identity-<i>.txt --out statement-<i>.json` (proof that the holder controls the key; §11.6).
8. Write the public key hex and fingerprint on the paper ceremony form and sign it.

Integration
9. Each keyholder sends the public key and statement to the release manager through **two independent channels** (in person and by signed e-mail). The release manager compares both.
10. The release manager enters the five keys in `src/chainparams.cpp` (main) and publishes a release candidate with its keyset hash.
11. Each keyholder builds the candidate (or checks its checksum) and runs `bucks-vault-offline verify-params --network main --strict`. They check that their slot shows exactly their key and fingerprint and that the keyset hash equals the published one, then sign the ceremony record.
12. Only after five confirmations is the release tagged. The node now passes §4.4.
13. Publish: roles, fingerprints, keyset hash, identity statements and the ceremony record.
14. Before launch, run the full flow (`GOLD`, `REDM`, `AUDT`, `PAUS`, `UNPS`, `KEYR`) on testnet with the rehearsal keys.

### 11.2 Planned rotation (annual review, change of keyholder)

1. The new keyholder generates a key (steps 5–8).
2. Coordinator: `bucks-vault new-event --type KEYR --mode propose --new-key <i>=<hex> --doc rotation-record.pdf …`.
3. Four keyholders sign (any four, from the current keyset); combine; submit.
4. The UI shows "Key change for <role> activates at block A". At block A the new key is active (`normalize`); signatures made for the old keyset stop working.
5. A mistaken proposal is cancelled with `KEYR` mode `02` (four signatures) before block A.

### 11.3 Emergency: one key compromised

1. At once: three honest keyholders sign `PAUS`. `GOLD` and `REDM` are frozen; `AUDT` and `KEYR` still work.
2. Four keyholders sign `KEYR` propose replacing the compromised slot.
3. After activation, four keyholders (with the new key) sign `UNPS`.

`GOLD_STANDARD.md:110-111` ("reject the compromised key from the moment the revocation transaction is confirmed") is replaced by this: the freeze is immediate; the key change follows after the delay.

### 11.4 Lost keys

| Keys left | What still works | Action |
|---|---|---|
| 4 | `KEYR` (any 4), `PAUS`, `UNPS`; `GOLD` only if the auditor is among the 4; `REDM` only if the Standards Board is | rotate the lost slot (§11.2) |
| 3 | `PAUS` (and `GOLD`/`REDM`/`AUDT` if the roles allow) | sign `PAUS`, then hard fork (§11.5) |
| ≤ 2 | nothing that changes the reserve | hard fork (§11.5) |

### 11.5 Hard-fork override (disaster only)

A new release adds one `KeysetOverride {height, epoch, keys}` to the network's `keysetOverrides`. `height` is at least 2,016 blocks after the release date. `epoch` is the next multiple of 1,000,000. At `height` the override keys replace the keyset (§7.3), even while paused; the new committee then signs `UNPS` (4 of 5). Nodes that do not upgrade reject the first event signed by the new keys, so exchanges and node operators must upgrade before `height`. The override cannot change the reserve; a wrong reserve is corrected by a normal `REDM` or `GOLD` from the new committee, with a document explaining it.

### 11.6 Identity statements and the annual check

`statement message = TaggedHash("Bucks-Vault-Statement-v1", genesisHash ‖ u8(index) ‖ pubkey(33) ‖ SHA256(text))`. Each keyholder signs a statement at the ceremony and once a year. A keyholder who cannot sign the yearly statement is treated as a lost key (§11.4). Statements are published, never put on chain.

---

## 12. `bucks-vault` tooling

### 12.1 Binaries and commands

Two CMake targets from `tools/bucks_vault.cpp`:
- `bucks-vault-offline`: compiled with `BUCKS_VAULT_OFFLINE`; no socket or HTTP client code. Used on air-gapped computers.
- `bucks-vault`: all commands, including `status` and `submit` (cpp-httplib client).

| Command | Binary | Purpose |
|---|---|---|
| `keygen --network N --index I --out F` | both | new key (RAND via `generatePrivateKey`), encrypted key file (§12.5) |
| `pubkey --key F` | both | print public key and fingerprint |
| `import-test-key --network regtest --index I --priv-hex H --out F` | both | **regtest only**; refuses other networks |
| `verify-params --network N [--strict]` | both | print compiled keys, fingerprints, keyset hash; `--strict` runs §4.4 |
| `new-event --status S --type T [type fields] --doc D [--expiry-blocks B] --out E` | both | build an unsigned event file (§12.2) |
| `show --event E [--doc D]` | both | human display: network, type, seq, expiry, amount in mg, g, kg and mithqal, document hash (recomputed from D), keys, event id |
| `sign --event E --key F --doc D --out SH` | both | offline signing (§12.4) |
| `sign --event E --hw-stub --index I --out SH` | both | prints the event id for an external signer, reads a DER signature (hex) from stdin, verifies it against key I before writing |
| `combine --event E --shares SH… --out BU` | both | verify shares, choose exactly R with the role rules, build and verify the payload |
| `sign-statement`, `verify-statement` | both | §11.6 |
| `status --node URL [--save S]` | `bucks-vault` | `GET /api/gold/status`; prints reserve, coverage, last audit, pending rotation; `--save` writes a status file |
| `submit --bundle BU --node URL --token-file T [--from ADDR] [--dry-run]` | `bucks-vault` | `POST /api/gold/submit` |

Type fields of `new-event`: `GOLD`/`REDM`: `--amount-mg N` or `--amount-g X.YYY` (at most 3 decimals, converted exactly); `AUDT`: `--audited-mg N --as-of-height H --audit-time <ISO-8601 UTC>`; `KEYR`: `--mode propose --new-key I=HEX` (repeatable; other slots unchanged) or `--mode cancel`; `PAUS`/`UNPS`: none. `--expiry-blocks` defaults to 1,008 and must be ≤ `maxExpiryDelta`. `seq` comes from the status file (`next_seq` or `next_audit_seq`).

### 12.2 File formats (JSON, UTF-8; hex is lowercase; u64 values are decimal strings)

Status file (written by `status --save`, read offline):
```json
{ "format": "bucks-vault-status-v1", "network": "main", "genesis_hash": "<64 hex>",
  "tip_height": 12345, "keyset": { "epoch": 0, "hash": "<64 hex>", "keys": ["<66 hex>", "...5 keys"] },
  "next_seq": "13", "next_audit_seq": "3", "paused": false,
  "pending_rotation": null, "saved_at": "2026-10-08T12:00:00Z" }
```

Unsigned event:
```json
{ "format": "bucks-vault-event-v1", "network": "regtest",
  "genesis_hash": "3491115920c29000d16f7d776bbab4a3399e5065abeeaf73fa23a273406bf520",
  "keyset": { "epoch": 0, "hash": "b0ef296f29a2e3fda51853b7c79669c32cb36cb0224104f72d18a7025479e676",
              "keys": ["028c86…fc02", "02813a…3832", "032355…f823", "0305fc…3845", "0209aa…21c7"] },
  "event": { "type": "GOLD", "seq": "1", "expiry_height": 1008, "amount_mg": "4250000",
             "doc_hash": "38de25e4651c71eede8dbdbdf743a8c6fce8ea7d0d7687f477bc34a9d076621d" },
  "unsigned_payload_hex": "42564c5401474f4c44…",
  "event_id": "0cd67c000cb1a9a49bebea89a9367b5322f99fff66ea0b94aa9035362dcb78a0",
  "required": { "signatures": 3, "must_include": [1] },
  "note": "free text, not signed" }
```

Signature share:
```json
{ "format": "bucks-vault-share-v1", "event_id": "<64 hex>", "key_index": 1,
  "pubkey": "<66 hex>", "signature_der": "<hex>" }
```

Bundle (input of `submit`):
```json
{ "format": "bucks-vault-bundle-v1", "network": "regtest", "event_id": "<64 hex>",
  "type": "GOLD", "seq": "1", "payload_hex": "<full payload hex>" }
```

Every reader recomputes `unsigned_payload_hex` and `event_id` from the fields and refuses the file if they differ. The node re-parses `payload_hex` and cross-checks `type`, `seq`, `event_id` and `network`.

### 12.3 Offline signing workflow (example: a deposit)

1. The vault receives the metal. The Geographic Reserve and the Independent Auditor produce the deposit record (PDF: bar list, weights in mg, assay certificates).
2. Coordinator (online): `bucks-vault status --node http://127.0.0.1:8080 --save status.json`.
3. Coordinator: `bucks-vault new-event --status status.json --type GOLD --amount-mg 4250000 --doc deposit.pdf --out gold-0001.json`.
4. The coordinator carries `gold-0001.json` and `deposit.pdf` to each signer on a USB stick (neither file is secret).
5. Each signer, on the air-gapped computer: `bucks-vault-offline show --event gold-0001.json --doc deposit.pdf`; compares amount and document with the physical paperwork; then `bucks-vault-offline sign --event gold-0001.json --key vault-key-<i>.json --doc deposit.pdf --out share-<i>.json`.
6. Shares come back on the USB stick (they are not secret).
7. Coordinator: `bucks-vault combine --event gold-0001.json --shares share-*.json --out bundle.json`.
8. `bucks-vault submit --bundle bundle.json --node … --token-file ~/.bucks/.cookie --dry-run`, then without `--dry-run`.
9. Wait for 6 confirmations (`bucks-vault status`); publish the deposit record and the bundle on the project website.

The online status file is trusted only for liveness. A lying node can at worst make signers sign an event that will be invalid (wrong `seq` or keyset). It cannot change what the signer sees on the offline screen.

### 12.4 Signing rules (`sign`)

The tool MUST refuse to sign unless all hold:
1. The file format and version are known; `unsigned_payload_hex` and `event_id` equal the values recomputed from the fields.
2. `network` is one of the compiled networks and `genesis_hash` equals its compiled genesis hash.
3. The key file's public key equals `keyset.keys[key_index]` of the event, and the key file's network equals the event's network.
4. `--doc` is given and its SHA-256 equals `doc_hash` (every event type; the signer must hold the document it signs for).
5. The signer types the amount in mg (`GOLD`, `REDM`, `AUDT`) or the changed slot numbers (`KEYR`) exactly as displayed.
6. `expiry_height − status tip ≤ maxExpiryDelta`; if a pending rotation activates at or before `expiry_height`, the tool warns that the event may become invalid.
The tool never signs any other message type and never prints or exports a private key (except `import-test-key`, regtest only).

### 12.5 Key files

```json
{ "format": "bucks-vault-key-v1", "network": "main", "key_index": 1, "pubkey": "<66 hex>",
  "kdf":    { "name": "scrypt", "N": 262144, "r": 8, "p": 1, "salt": "<64 hex>" },
  "cipher": { "name": "aes-256-gcm", "nonce": "<24 hex>", "ciphertext": "<64 hex>", "tag": "<32 hex>" } }
```
- Key = scrypt(passphrase, salt, N = 2^18, r = 8, p = 1, 32 bytes) via OpenSSL `EVP_PBE_scrypt` (memory limit 512 MiB). AES-256-GCM with a random 12-byte nonce. AAD = ASCII `bucks-vault-key-v1` ‖ network name ‖ u8(key_index) ‖ pubkey (33 bytes).
- After decryption the tool checks that the private key derives `pubkey`. Wrong passphrase, changed metadata or a wrong key: refuse.
- Files are written atomically with mode 0600. Secrets are wiped with `OPENSSL_cleanse` after use. No new dependency (OpenSSL only).

### 12.6 Deterministic signatures

The current `signECDSA` uses a random nonce (`src/crypto.cpp:489`). A weak random source on an air-gapped computer could leak a vault key, so the vault tool uses a new function `signECDSA_RFC6979(digest, priv, sig)`: RFC 6979 §3.2 with HMAC-SHA256, then low-S normalisation, then `verifyECDSA` of the result. Use OpenSSL ≥ 3.2 deterministic ECDSA (`nonce-type = 1`, digest SHA-256) or implement §3.2 directly with OpenSSL HMAC (about 40 lines); either way the output MUST equal the vectors in §17. The node itself never needs vault private keys, and vault keys MUST never be imported into a node wallet.

---

## 13. REST API

Routes are added to `Server::setupRoutes` (`src/server.cpp`); JSON builders live in new `src/gold_api.cpp` so they can be unit-tested without HTTP. `GET /api/gold/status` and `GET /api/gold/events` are added to the public read-only allowlist (`Server::isPublicRoute`, `src/server.cpp:701-718`). `POST /api/gold/submit` is not public: the existing pre-routing handler requires a token or a session cookie, and for cookies the `X-Bucks-CSRF: 1` header and a matching Origin (`src/server.cpp:539-569`). Handlers copy the gold state, tip and UTXO statistics under the chain lock (or in a storage read transaction pinned to the same best block) and build JSON after releasing it. Every u64 amount is a decimal **string** (no precision loss in JavaScript, GLD-10).

### 13.1 `GET /api/gold/status`

Example (regtest, events of §17 at heights 5, 6 and 7, tip 13, every coinbase claiming 50 BUCKS):
```json
{ "success": true, "available": true, "network": "regtest",
  "genesis_hash": "3491115920c29000d16f7d776bbab4a3399e5065abeeaf73fa23a273406bf520",
  "tip": { "height": 13, "hash": "<64 hex>", "time": 1767320000 },
  "mithqal_mg": 4250,
  "standard": { "name": "mithqal", "mg_per_coin": 4250 },
  "reserve_status": "reported",
  "reserve_mg": "3250000", "reserve_tip_mg": "3250000",
  "confirmations_required": 6, "confirmed_height": 8, "pending_events": 0,
  "supply_awraq": "65000000000", "supply": "650.00000000",
  "coverage_ratio": "1.17647058", "gold_per_buck_mg": "5000.000000",
  "paused": false, "paused_since_height": null,
  "last_audit": { "event_id": "615cad43…db5b", "height": 7, "txid": "<64 hex>",
                  "audited_reserve_mg": "3250000", "as_of_height": 6,
                  "reported_reserve_at_as_of_mg": "3250000",
                  "audit_time": 1767312000, "audit_time_utc": "2026-01-02T00:00:00Z",
                  "report_hash": "82675afd…fba1",
                  "auditor": { "index": 1, "role": "Independent Auditor", "fingerprint": "f5a2cceb097da54e" },
                  "status": "match" },
  "vault": { "total": 5, "epoch": 0, "keyset_hash": "b0ef296f…e676",
             "threshold": { "GOLD": 3, "REDM": 3, "AUDT": 1, "KEYR": 4, "PAUS": 3, "UNPS": 4 },
             "required_roles": { "GOLD": [1], "REDM": [3], "AUDT": [1] },
             "keys": [ { "index": 0, "role": "Founding Team", "pubkey": "028c86…fc02", "fingerprint": "b38dee1d6378e05b" },
                       "… indexes 1-4 …" ],
             "next_seq": "3", "next_audit_seq": "2", "pending_rotation": null },
  "limits": { "max_event_mg": "1000000000", "window_blocks": 144, "max_window_mg": "10000000000",
              "window": { "index": 0, "gold_mg": "4250000", "redm_mg": "1000000" } },
  "state_hash": "72b34d445c946ac037b4996a6916060c0d93eb0957f1f27affdfce00eb28523e" }
```

Field rules:
- `confirmed_height` = max(0, tip − 5). `reserve_mg` = **confirmed** reserve: `reserve_after_mg` of the last index row with `height ≤ confirmed_height` (0 if none). This implements the documented 6-confirmation rule (`GOLD_STANDARD.md:71`) for display. `reserve_tip_mg` = `S.reserveMg`. `pending_events` = rows with fewer than 6 confirmations.
- `supply_awraq` = UTXO statistics `totalAmount` (consensus spec §8.7), not a formula (old: `src/blockchain.cpp:169-184`).
- `coverage_ratio` = `floor(reserve_mg × 10^16 / (supply_awraq × 4250))` printed with 8 decimals; `gold_per_buck_mg` = `floor(reserve_mg × 10^14 / supply_awraq)` printed with 6 decimals. Computed with `U256` (consensus spec §6.1); the numerator needs at most 103 bits. Both are `null` when `supply_awraq` = 0. No floating point anywhere.
- `last_audit.status`: `none` (no `AUDT`), else `differs` if `audited_reserve_mg ≠ reported_reserve_at_as_of_mg` (the reserve after the last event at height ≤ `as_of_height`), else `stale` if `tip.time − audit_time > 8,640,000` s (100 days), else `match`.
- `vault` shows `normalize(copy, tip + 1)`. `pending_rotation` = `{ "proposed_height", "activation_height", "blocks_left", "event_id", "changed": [{ "index", "role", "old_fingerprint", "new_fingerprint", "new_pubkey" }] }`.
- `state_hash` = SHA-256 of the stored `GoldState` (not normalised), so operators can compare nodes.
- Before the module is wired in (consensus spec §7.8 point 5): `{ "success": true, "available": false }`.

### 13.2 `GET /api/gold/events?from=&to=&type=&order=&limit=`

- `from` (default 0), `to` (default tip) are heights, `from ≤ to`; `type` is one of the six tags (optional); `order` is `asc` (default) or `desc`; `limit` 1…500 (default 100). Invalid values: 400 `bad-query`.
- Events in chain order `(height, tx_index)`, or the reverse for `desc`. A page never splits a height; if more events exist, `"more": true` and `"next_from": <height>` (`asc`) or `"next_to": <height>` (`desc`) for the next call.

```json
{ "success": true, "tip_height": 13, "from": 0, "to": 13, "more": false, "next_from": null,
  "events": [ { "event_id": "0cd67c00…78a0", "type": "GOLD", "seq": "1", "height": 5,
                "block_hash": "<64 hex>", "txid": "<64 hex>", "vout": 0,
                "confirmations": 9, "status": "confirmed",
                "amount_mg": "4250000", "reserve_before_mg": "0", "reserve_after_mg": "4250000",
                "doc_hash": "38de25e4…621d", "expiry_height": 1008,
                "signers": [ { "index": 0, "role": "Founding Team" },
                             { "index": 1, "role": "Independent Auditor" },
                             { "index": 2, "role": "Community Representative" } ] } ] }
```
Type-specific additions: `AUDT` → `"audit": { "audited_reserve_mg", "as_of_height", "audit_time", "reported_reserve_at_as_of_mg" }`; `KEYR` → `"rotation": { "mode", "activation_height", "changed": [...] }`.

### 13.3 `POST /api/gold/submit` (authenticated)

Request: `{ "bundle": { …bundle-v1… }, "from": "<node wallet address, optional; default primary>", "dry_run": false }`. Body limit 8 KiB.

Steps: (1) parse and cross-check the bundle (400 `bundle-format`, `bundle-mismatch`, `wrong-network`); (2) `classify` (400 with its code); (3) under the chain lock: a pending event for the same counter (409 `gold-event-pending`); (4) `checkForMempool` against the tip: 409 for state conflicts (`gold-bad-seq` with `expected_seq`, `gold-expired`, `gold-paused`, `gold-already-paused`, `gold-not-paused`, `gold-redm-exceeds-reserve`, `gold-reserve-overflow`, `gold-window-cap`, `gold-audit-height`, `gold-rotation-pending`, `gold-no-rotation`), 400 for every other stage E code (the bundle must be rebuilt); (5) if `dry_run`: 200 `{ "success": true, "dry_run": true, "event_id", "valid_at_height", "reserve_after_mg" }`; (6) build the carrier transaction from the wallet: confirmed P2PKH inputs, outputs = [NULL_DATA(payload), change], fee at least the minimum relay fee; sign; (7) `AcceptToMempool` and relay.

Success: `{ "success": true, "txid": "<64 hex>", "event_id": "<64 hex>", "type": "GOLD", "seq": "1", "fee_awraq": "700" }`. Other errors: 409 `insufficient-funds`, 503 `syncing` or `wallet-unavailable`. The node never sees a vault private key.

### 13.4 Changes to existing endpoints

`/api/economy/status` (`src/server.cpp:926-930, 1394-1413`) keeps `success` and `supply` (now from UTXO statistics) and drops `gold_reserve`, `gold_reserve_unit`, `reserve_source` and `price_per_buck`. It stays public (`tests/integration/test_server_auth.cpp:158` keeps passing).

---

## 14. UI

### 14.1 "Gold reserve (reported)" panel

New file `web/gold.jsx` (component `GoldReservePanel`), loaded in `web/index.html` after `auth.jsx` and before `dashboard.jsx`. It replaces `GoldVault` and `BackingBar` (`web/vault.jsx:52-137`) on the dashboard. Data: `GET /api/gold/status` every 30 s and whenever the height changes; `GET /api/gold/events?order=desc&limit=20` for the list (newest first). Numbers are formatted from the decimal strings with `BigInt` (never `Number` for mg).

| Element | Content |
|---|---|
| Title | "Gold reserve (reported)" |
| Reserve | `reserve_mg` as grams with 3 decimals, and as mithqal with 3 decimals (`mg ÷ 4250`) |
| Coverage | `coverage_ratio × 100` as a percentage with 4 decimals, "of the 4.25 g (1 mithqal) per coin standard"; bar width = min(100 %, value); above 100 % the bar is full and the label shows the exact value; `null` → "n/a" |
| Per coin | `gold_per_buck_mg` mg per coin |
| Last audit | auditor-stated date (`audit_time_utc`), audited grams, "as of block N", status chip: Matches / Differs by X g / Stale (N days) / No audit yet; report hash (first 16 hex characters, copy button) |
| Banners | paused (red): "Gold events are paused by the vault committee since block N"; rotation (amber): "Key change for <role> takes effect at block A (in N blocks)"; pending (grey): "N event(s) waiting for 6 confirmations" |
| Vault | five roles with fingerprints; the rule line "Deposit 3 of 5 incl. Independent Auditor · Withdrawal 3 of 5 incl. Standards Board · Key change 4 of 5 + 1,008-block delay · Pause 3 of 5 · Resume 4 of 5 · Audit: Independent Auditor"; epoch |
| Events | height, type (Deposit, Withdrawal, Audit, Key change, Pause, Resume), amount, reserve after, signer roles, document hash, Confirmed / Pending n/6 |
| Disclosure | the §16 text, always visible (not in a tooltip) |
| States | loading; node syncing; `available: false` → "Gold reserve data is not available on this node" |

Words never used: "backed", "fully backed", "allocated", "redeem", "your gold", "minted", "settlement", any USD or other currency value.

### 14.2 Removals (both `web/` and the generated `frontend/src/main.jsx`)

| Place | Remove or change |
|---|---|
| `web/data.jsx:10-42` | `GOLD_USD_PER_G`, `PEG_G_PER_BUX`, `PEG_MG_PER_BUX`, `BUX_USD` |
| `web/data.jsx:66, 69` | pool blurbs "Direct gold settlement", "Mine direct to the vault" |
| `web/data.jsx:164, 258-262, 282` | `economy` mapping of `gold_reserve`/`price_per_buck`; `window.__bucks_state`; replace with a `gold` state from `/api/gold/status` |
| `web/data.jsx:336-342` | `toGoldEquivalent` |
| `web/auth.jsx:8-20` | `TARGET_PEG_MG_PER_BUX`, `reserveStats`, `coverageLabel` |
| `web/vault.jsx:52-139` | `GoldVault`, `BackingBar` (keep `CoinStack` only as decoration with level = min(1, coverage)) |
| `web/dashboard.jsx:121-122, 130, 133, 157-158` | balance in gold and USD, "Net value", "Gold spot" |
| `web/wallet.jsx:60, 75, 160, 198-199, 249-277` | gold and USD equivalents, "Your gold", "Redeem physical gold" card |
| `web/configure.jsx:11-12, 190-204, 219` | USD projections and "mg Au minted daily", "Reported reserve per BUX" |
| `web/app.jsx:89-90, 234, 287, 302` | gold spot price chip, `mg` per reward, "the gold it mints" |
| `web/onboarding.jsx:7, 25-35` | stats now from `/api/gold/status`; text "Mine BUX. The vault committee reports a gold reserve on-chain; coins are not exchangeable for gold." |
| legacy pages `web/wallet.js:13, 72-76, 138-152, 315-316` (+ `web/wallet.html`), `web/mywallet.js:10, 171, 201-206, 256-263` (+ `web/mywallet.html:156, 217`) | per-wallet "≈ … g gold" and "1 BUCK ≈ … gold" figures built from `price_per_buck`; replace with a link to the Gold reserve panel |
| `concat_app.js` | add `auth.jsx` and `gold.jsx` to the file list; regenerate `frontend/src/main.jsx`; the owner rebuilds `frontend/dist` (`npm ci && npm run build`) |

### 14.3 Claims check (`tests/ui_claims.cmake`, run by ctest)

Fails if any of these appear (case-insensitive) in `web/*.jsx`, `web/*.js`, `web/*.html` or `frontend/src/*`: `fully backed`, `backed by`, `100% backed`, `allocated gold`, `redeem`, `your gold`, `minted daily`, `gold it mints`, `gold settlement`, `GOLD_USD_PER_G`, `PEG_G_PER_BUX`, `BUX_USD`, `price_per_buck`, `0.0675`. It also fails if `web/gold.jsx` does not contain the sentence "The blockchain proves only that the committee signed these numbers." (§16).

---

## 15. Governance markers (GLD-5): removed

Decision: **remove** governance from the node until a separate governance spec exists. Delete the scanner (`src/blockchain.cpp:388-431`, also deleted by consensus spec step 16), `GovernanceEvent` and its functions (`include/blockdb.h:19-25, 60-65`; `src/blockdb.cpp:418-512`), the `governance_events` table (`src/blockdb.cpp:52-59`) and `GOV_*` (`include/consensus.h:178-191`). Payloads starting with `VGKB`/`BKGV` are ordinary free data with no meaning. `GOLD_STANDARD.md:19` ("Elected by coin-holders") is replaced by "appointed by the owner; the appointment is published".

A future governance spec MUST have: votes signed by UTXO keys and weighted by coins at a snapshot height (sybil resistance); proposal ids and voting windows; tallies recomputed from the active chain (reorg-safe); no automatic effect on consensus or on the vault keyset (a key change still needs `KEYR` 4 of 5).

---

## 16. What the gold figure means (disclosure text for the UI and docs)

> **What this gold figure means**
> The gold reserve shown here is reported by the Bucks vault committee: five keyholders (Founding Team, Independent Auditor, Community Representative, Standards Board, Geographic Reserve). Every change to the reserve needs signatures from three of them and is recorded on the Bucks blockchain.
> The blockchain proves only that the committee signed these numbers. It cannot see or weigh gold. The Independent Auditor publishes separate attestations; the latest one is shown with its date and the hash of the audit report.
> Coverage compares the reported reserve with one mithqal (4.25 g of gold) for every BUCKS coin in circulation. It is information only and can be far below 100%.
> BUCKS coins are created by mining, not by gold deposits. Holding BUCKS gives you no claim on this gold, and coins are not exchangeable for gold through this software.
> Figures update after 6 confirmations. No price in any currency is shown.

---

## 17. Test vectors

Computed by an independent pure-Python implementation (secp256k1, RFC 6979 with HMAC-SHA256, low-S). The same implementation reproduces the consensus spec's V-TX-1 signature 0 and the public RFC 6979 vector (private key 1, message "Satoshi Nakamoto": r = `934b1ea1…e3d8`, s = `2442ce9d…d9e5`). Regtest only; never use these keys for value.

**Regtest vault keys** (`priv_i = SHA256("bucks-regtest-vault-key-<i>")`):

| i | Public key | Fingerprint |
|---|---|---|
| 0 | `028c86529d8225c1a7a6c5ef56201467c1ba701f20e9a087f24fae7101556afc02` | `b38dee1d6378e05b` |
| 1 | `02813a405ca08f0c72e97f7ffe6c94e10512e0024e316647fae1b7333fae773832` | `f5a2cceb097da54e` |
| 2 | `032355b299a047d9c501aeed03b567c484e8e6b810a1f0b48e24b89c9071caf823` | `63a4d4306ff3f885` |
| 3 | `0305fcf1c2fa8698fb12edff4cbe5f5930f5239d62a7fe465ac6c0b5600f2c3845` | `44a85bdae658917c` |
| 4 | `0209aab6aa9d9f475cc9753a6b46a044ca653b3ef5ff2117d5c86b3074c4a421c7` | `e1d9b1d06d6bbdb3` |

Regtest genesis hash `3491115920c29000d16f7d776bbab4a3399e5065abeeaf73fa23a273406bf520` (consensus spec §3.5), so `chainTag = 34 91 11 59`. Keyset hash (epoch 0) = `b0ef296f29a2e3fda51853b7c79669c32cb36cb0224104f72d18a7025479e676`.

**V-GOLD-1** — seq 1, expiry 1008, 4,250,000 mg, `docHash = SHA256("bucks-test-deposit-record-1")`, signers 0, 1, 2:

| Item | Value |
|---|---|
| unsigned part (65) | `42564c5401474f4c44349111590100000000000000f003000090d940000000000038de25e4651c71eede8dbdbdf743a8c6fce8ea7d0d7687f477bc34a9d076621d` |
| event id | `0cd67c000cb1a9a49bebea89a9367b5322f99fff66ea0b94aa9035362dcb78a0` |
| sig 0 (70) | `304402207f3c26cb3b5ff9f7b5e176e9ae8b7b54dc6ba0f6cdc457f1dbb892e95a54f46c02202d614b20c225cea99300ec171c67892cc533f3b4149bfcb46c7d1d0048bb75ba` |
| sig 1 (71) | `30450221008aa01caba5b55c3fcd01874ab89e65758e443ab13627f4ce894313945769b3f202202259124f08352fd3c38085aa3a97403b4dcf3ade187faccb4a870ab55f236a8d` |
| sig 2 (71) | `304502210096b465737c7729204a113c93cb08cf6627f5e395d685d764afe7a59f6b9857d402207629656b886847778a765e076135d70c950149257b5cf165ba917aed82e9375c` |
| full payload (284) | unsigned part ‖ `03` ‖ `00 46` sig 0 ‖ `01 47` sig 1 ‖ `02 47` sig 2; SHA-256 = `b582ee1669da535688df13b4436a2f8ed91f265e36522b2a43a4267bd82e5310` |
| scriptPubKey | `6a 4d 1c01` ‖ full payload |

**V-REDM-1** — seq 2, expiry 1008, 1,000,000 mg, `docHash = SHA256("bucks-test-withdrawal-record-1")`, signers 0, 2, 3:

| Item | Value |
|---|---|
| unsigned part (65) | `42564c54015245444d349111590200000000000000f003000040420f0000000000b68c9edf010b5a5c8cf0fe6479072f763a3e31802e0c296ae333d26077efa70b` |
| event id | `4aae7b524a437025333982cf5554c73800fde4f1fe51c2ea966367a0fdf3e859` |
| sig 0 (70) | `304402207ec4c2b7f48c3be0f7ea2eec0aa46e909c8eeb8fffada2a05dd4068f45204a0902202d16281ab99bcda78f1dd16a777d408dc52ee12fbbadbc40f04961fe8da1c113` |
| sig 2 (71) | `3045022100ed56925aa8bc0b0d123a3de8df1aadc18d209f189079aa1f8a700fa8ccd5ddbe0220160b60eb39285c65ba6f4447556bfc8a828af63be3c04c78f8d93b04db2ec6f2` |
| sig 3 (71) | `3045022100fafd55823f1bb3ef25ecf4c06fadf5720d3b648e6c45f3b44c01dd4f9453b3fc0220316cfa46adba70b3d2a0c63a1d928cc173c18cfc64f5456681a5632775f777a7` |
| full payload (284) | unsigned part ‖ `03` ‖ `00 46` sig 0 ‖ `02 47` sig 2 ‖ `03 47` sig 3 |

**V-AUDT-1** — audit seq 1, expiry 1008, audited 3,250,000 mg, as-of height 6, audit time 1767312000 (2026-01-02 00:00 UTC), `docHash = SHA256("bucks-test-audit-report-1")`, signer 1:

| Item | Value |
|---|---|
| unsigned part (77) | `42564c540141554454349111590100000000000000f0030000509731000000000006000000800a57690000000082675afde23cfd8646c68bcfc99e741e616d8242e362f71fb9002453527afba1` |
| event id | `615cad435be0c399c4b75e57995507f81076878790174dbfa7d3ed4dc277db5b` |
| sig 1 (70) | `304402201c9cc34f74411123a5eb7d64f83c2855443bb38da58f6b0054aba93aebc6923b02205cf6bb73efa7cd6196f9c887fe398fd16f3c15fb2c90bab756bcfb42ac55c239` |
| full payload (150) | unsigned part ‖ `01` ‖ `01 46` sig 1; scriptPubKey `6a 4c 96` ‖ payload |

**V-KEYR-1** (alternative third committee event) — seq 3, expiry 1008, propose: slot 4 = `02814c3fe6084d9fcf0e4d6b09b84e65bfa413c7b53d7166ecb2fb006e098d69ba` (`SHA256("bucks-regtest-vault-key-4-rotated")`, fingerprint `7371ec87a1fad8f0`), other slots unchanged, `docHash = SHA256("bucks-test-rotation-record-1")` = `2af179c1…0b68`, signers 0–3: event id `fe1e90ec2bce99157c4828563c3f9d68e56516c11d353b05537509a5f95d2153`; payload 516 bytes, SHA-256 `71aa863eedb49a4fd5e103ab47f4339f9c634f7b9c4ae79d7c51b09009f061ad`; scriptPubKey prefix `6a 4d 0402`; keyset hash after activation (epoch 1) `c7455f36f45d5410791dbfb1661dbf00979ffc440fe8f80c6a8913a4fa1ab28a`.

**V-PAUS-1** (alternative third committee event) — seq 3, expiry 1008, `docHash = SHA256("bucks-test-incident-note-1")`, signers 0, 1, 2: event id `86695f756cf81d60c8a41cefc2b87173e004bb6c3f1cb6cad5f1b2178d5c02b0`; payload 275 bytes, SHA-256 `52d62aca5b32d7a963842adfb8538619e8d74de0517251fa1c95d0b4d1646414`.

**V-STATE** (regtest, `windowBlocks` 144; GOLD-1 at height 5, REDM-1 at 6, AUDT-1 at 7):

| State | Bytes | `stateHash` |
|---|---|---|
| S0 initial | 219 | `2a1d819f54fd3c0ff2a055d10c631ea85b92dc1a5b0058eb290ac6b7725c60ef` |
| S1 after block 5 | 219 | `9cf506b250a2690ba3ba144a99fa1c9906d637857f8ad1ee6d9616221f7d1876` |
| S2 after block 6 | 219 | `0e264f5d1488429debde6a091fb8ada620389b62ff7c918fc2c94b62dc5dda6e` |
| S3 after block 7 | 219 | `72b34d445c946ac037b4996a6916060c0d93eb0957f1f27affdfce00eb28523e` |

S3 = `0100 5097310000000000 0200000000000000 0100000000000000 06000000 00000000` ‖ keys 0–4 ‖ `00000000 90d9400000000000 40420f0000000000`. `goldUndo` of block 7 = `01` ‖ `db` ‖ S2 ‖ SHA256(S3): 253 bytes, SHA-256 `25d6f4f91a86961cf10f12cd556abf77032d38ae5e4cd3152b02c87e67bbc38b`.

**V-COVERAGE** (`coverage_ratio`, `gold_per_buck_mg`):

| reserve_mg | supply_awraq | coverage_ratio | gold_per_buck_mg |
|---|---|---|---|
| 3,250,000 | 100,000,000,000 (1,000 coins) | `0.76470588` | `3250.000000` |
| 3,250,000 | 2,100,000,000,000,000 (21 M coins) | `0.00003641` | `0.154761` |
| 4,250 | 100,000,000 (1 coin) | `1.00000000` | `4250.000000` |
| 0 | 5,000,000,000 | `0.00000000` | `0.000000` |
| 10^15 | 2,099,994,997,690,000 | `11204.50848244` | `47619161.050383` |
| any | 0 | `null` | `null` |

---

## 18. Finding → section map

| Finding | Problem (short) | Resolved in |
|---|---|---|
| GLD-1 | placeholder keys + fail-open verify allow keyless 3-of-5 | Phase 1 `verifyECDSA`; §4.4 (no start, release guard); §5.4/§8.2 E6 (exactly R strictly increasing indexes); tests GP-2, GR-4, GR-5 |
| GLD-2, CRY-3 | signatures replayable without limit | §6 (tag, genesis, keyset, seq, expiry), §8.2 E1–E4; GR-2, GR-3, GR-8 |
| GLD-3, CRY-V1, NET-V2, DB-8, CON-11 | unsigned markers reversed on disconnect; clamp vs add-back; overflow | §8 (invalid block, no clamp, checked bounds), §9.1–9.2 (snapshot undo); GR-7, GR-9, GR-18, GC-1, GC-2 |
| GLD-4, CON-18 (gold part) | node-local, non-atomic, never recomputed | §7, §9.4 (same commit), §9.5 (startup check, VerifyDB, reindex); GC-4–GC-8 |
| GLD-5, DB-16 | unauthenticated governance log | §15 (removed) |
| GLD-6 | rotation, revocation, roles, confirmations, ceremony not implemented | §3.2, §5.5 C9, §11, §13.1 (6-confirmation display), §12.3 (signers sign the event id, not a tx hash) |
| GLD-7, API-18 (gold part) | UI shows a full, redeemable peg with constants | §14, §16; GU-1 (the password and 2FA parts of API-18 belong to the Phase 1 API work) |
| GLD-8 | no proof of reserves, trusted committee | `AUDT` (§5.3), document hash on every event, auditor required on `GOLD`, identity statements (§11.6), disclosure (§16); residual trust stated (§3.3) |
| GLD-9 | unbounded signature parsing, uncounted verifies | §5.4, §5.5, §8.4; GR-20 |
| GLD-10 | loose encoding, unit mismatch | §5 (one LE layout, mg), §13 (strings); GF-1, GF-2 |
| GLD-11 | markers burn value and become UTXOs | consensus spec T-OUT-VALUE and §7.6 (0-value NULL_DATA never enters the UTXO set); §5.1 relies on it |
| GLD-12 | no tests | §20 |
| OPS-13 | mainnet possible with placeholder keys | §4.4; GP-2 |

---

## 19. Implementation plan

Rules for every step: builds with `-Wall -Wextra`; all old and new tests pass, also with `-DBUCKS_SANITIZE=address,undefined`; new sources are added to both `CMakeLists.txt` and `build.sh`; no commits by agents. "C-n" = step n of the consensus spec §14.

| # | Step | Needs | Files | Tests |
|---|---|---|---|---|
| G1 | **Formats**: constants, event parse/serialize, `classify`, role table, `GoldState` and `goldUndo` serialization, `keysetHash`, `VaultMsg`, event id | C-1, C-3 | new `include/gold.h`, `src/gold.cpp`; `include/consensus.h` (add §4.1); new `tests/unit/test_gold_format.cpp` | GF-1…6 |
| G2 | **Deterministic signing** `signECDSA_RFC6979` | Phase 1 | `include/crypto.h`, `src/crypto.cpp`; new `tests/unit/test_crypto_rfc6979.cpp` | CR-1 |
| G3 | **Parameters**: `GoldParams` for 3 networks, `checkParams`, regtest keys, testnet rehearsal keys; delete old gold and governance constants; rewrite the vault test helper | C-3, C-7 | `include/chainparams.h`, `src/chainparams.cpp`, `include/consensus.h:137-191`, `tests/unit/test_crypto_ecdsa.cpp:207-272` (local placeholder array instead of `Consensus::VAULT_AUTHORITY_PUBKEYS`); new `tests/unit/test_gold_params.cpp` | GP-1…3 |
| G4 | **Rules engine**: `normalize`, `checkTransaction`, `connectOutput`, `GoldUndoBuilder`, `disconnectBlock`, `checkForMempool`, `initialState`, `startupCheck` | C-4, C-5, C-11 | `include/gold_rules.h`; new `src/gold_rules.cpp` replaces `src/gold_rules_stub.cpp`; new `tests/util/gold_test_util.h` (event builder signing with regtest keys), `tests/unit/test_gold_rules.cpp` | GR-1…20 |
| G5 | **Chain integration**: T-GOLD in `CheckTransaction`; params passed to the hook; commit of `gold_state` and `gold_events`; startup step 9; VerifyDB gold compare; reindex | C-12, C-15, storage spec | `src/validation_rules.cpp`, `src/connect_block.cpp`, `src/chainstate.cpp`, storage files; new `tests/integration/test_gold_chain.cpp` | GC-1…8 |
| G6 | **Mempool, template, anchors** | C-13, C-14 | `include/mempool.h`, `src/mempool.cpp`, `src/policy.cpp`, `src/miner.cpp`, `src/server.cpp` (`/api/anchor`); new `tests/integration/test_gold_mempool.cpp` | GM-1…5 |
| G7 | **REST API** | C-16 | new `include/gold_api.h`, `src/gold_api.cpp`; `src/server.cpp` (routes, `isPublicRoute` 701-718, `handleEconomyStatus` 1394-1413); new `tests/integration/test_gold_api.cpp` | GA-1…5 |
| G8 | **`bucks-vault` and `bucks-vault-offline`** | G1–G3 (offline part), G7 (`status`, `submit`) | new `tools/bucks_vault.cpp`, `include/vault_files.h`, `src/vault_files.cpp`; `CMakeLists.txt` (two targets, `BUCKS_RELEASE` guard); new `tests/integration/test_bucks_vault.cpp` | GV-1…6 |
| G9 | **UI** | G7 | new `web/gold.jsx`; `web/index.html`, `web/data.jsx`, `web/auth.jsx`, `web/vault.jsx`, `web/dashboard.jsx`, `web/wallet.jsx`, `web/configure.jsx`, `web/app.jsx`, `web/onboarding.jsx`, legacy `web/wallet.js`, `web/wallet.html`, `web/mywallet.js`, `web/mywallet.html`; `concat_app.js`; regenerated `frontend/src/main.jsx`; new `tests/ui_claims.cmake` (+ `tests/CMakeLists.txt`) | GU-1, GU-3 |
| G10 | **Documents and cleanup** | all | rewrite `GOLD_STANDARD.md` (§2 format, §3–4 ceremony = §12.3, roles §3.1, rotation/revocation = §11, delete "minting", "every gram … backing", "elected by coin-holders"); new `docs/vault-ceremony.md` (printable §11.1 form); delete remaining old code (`include/blockchain.h:84-85, 96-97`, `src/blockdb.cpp:46-59, 377-512`, `include/blockdb.h:19-25, 54-65`); add deleted symbols to `tests/build_hygiene.cmake` (`validateVaultMultisig`, `saveGoldReserve`, `loadGoldReserve`, `getGoldReserve`, `VAULT_AUTHORITY_PUBKEYS`, `REDEEM_MARKER`, `GOV_PREFIX`, `saveGovernanceEvent`, `price_per_buck`) | GU-2 |

G1–G4 and the offline part of G8 need no chain code and can be built and tested right after C-5 and C-11. Until G5 lands, the API reports `available: false`.

---

## 20. Test plan

Infrastructure: `tests/util/gold_test_util.h` builds payloads for every type and signs them with the regtest keys (deterministic, so tests compare bytes with §17); integration tests use the consensus spec's `TestChain` on regtest with a copy of `ChainParams` when small limits are needed. "Old behaviour" says what the current code does, so each test would fail on it.

### 20.1 Formats and crypto (unit)

| ID | Test | Setup | Action | Expected | Old behaviour caught |
|---|---|---|---|---|---|
| GF-1 | `GoldFormat.Vectors` | §17 payloads | parse, `classify`, event id, re-serialize | EVENT, sigOps 3/3/1/4/3; ids and bytes equal §17 | no versioned format; BE/LE mix (GLD-10) |
| GF-2 | `GoldFormat.Malformed` | V-GOLD-1 mutated: version 2, type `GOLX`, 1 byte short, 1 trailing byte, count 2, indexes [0,0,1], [1,0,2], index 5, derLen 7 and 73, zero docHash, amount 0, `KEYR` mode 03, 521-byte payload | `classify` | MALFORMED `bad-gold-format` each | silent stop, trailing bytes ignored, duplicates re-verified (GLD-9, GLD-10) |
| GF-3 | `GoldFormat.Roles` | `GOLD` [0,2,3]; `REDM` [0,1,2]; `AUDT` [0] | `classify` | `bad-gold-roles` each | any 3 indexes accepted (GLD-6) |
| GF-4 | `GoldFormat.NotGold` | payloads "bucks", "BVL", a CID, the old `GOLD`+amount bytes in one push | `classify` | NOT_GOLD, sigOps 0 | — |
| GF-5 | `GoldState.Vectors` | S0–S3 of §17 | serialize, hash, parse; mutate flags bit 2, version 2, length 220 | hashes equal; mutations rejected | single integer counter (GLD-4) |
| GF-6 | `GoldMsg.DomainSeparation` | V-GOLD-1 | change one byte of genesis, keyset hash, payload; compare with `signedMessageHash` of the same bytes | every digest differs | `sha256(marker‖amount)` without tag or chain (GLD-2, CRY-3) |
| CR-1 | `Rfc6979.Vectors` | priv 1 + "Satoshi Nakamoto"; regtest keys + V-GOLD-1 | `signECDSA_RFC6979` | exact public vector; exact §17 signatures; always low-S | random nonce on an air-gapped RNG |

### 20.2 Parameters (unit)

| ID | Test | Setup | Action | Expected | Old behaviour caught |
|---|---|---|---|---|---|
| GP-1 | `GoldParams.RegtestKeys` | regtest params | derive keys, keyset hash | equal §17 | — |
| GP-2 | `GoldParams.MainnetPlaceholderRefusesStart` | main params copies: all-zero keys; the old `02aa…01` strings parsed; a regtest key; `1·G`; two equal keys | `LoadChainState` | FATAL exit code 3 with the reason, nothing opened | placeholders compiled and used silently (OPS-13, GLD-1) |
| GP-3 | `GoldParams.Assertions` | `maxEventMg` 0; `maxWindowMg < maxEventMg`; override epoch 5 | startup | abort | — |

### 20.3 Rules engine (unit, state machine without a chain)

| ID | Test | Setup | Action | Expected | Old behaviour caught |
|---|---|---|---|---|---|
| GR-1 | `GoldRules.GoldApplies` | S0 | V-GOLD-1 at h 5 | state = S1; undo 253 bytes | — |
| GR-2 | `GoldRules.ReplaySameChain` | after GR-1 | same payload in another tx at h 6; twice in one block | `gold-bad-seq` both | every copy added (GLD-2, CRY-3, CON-11) |
| GR-3 | `GoldRules.ReplayAcrossChains` | V-GOLD-1; a regtest params copy with another genesis hash (same keys) | validate on the copy; then with `chainTag` patched to the copy's genesis | `gold-wrong-chain`; `gold-bad-sig` | no chain id (GLD-2) |
| GR-4 | `GoldRules.TwoValidOneForged` | sig 2 replaced by a keyless forgery (`r = x(kG), s = e/k`) and, separately, by key 2's signature of another event | connect | `gold-bad-sig`; block invalid | bad events skipped, later reversed (CRY-V1) |
| GR-5 | `GoldRules.DuplicateKeyIndex` | key 0's valid sig placed under indexes 0, 1, 2; separately, indexes [0,0,1] | connect / `classify` | `gold-bad-sig` / `bad-gold-format` | 3 copies of one forged sig passed (GLD-1) |
| GR-6 | `GoldRules.RedmNeedsStandardsBoard` | reserve 4,250,000 | `REDM` signed [0,1,2]; then [0,2,3] | `bad-gold-roles`; valid | any 3 indexes (GLD-6) |
| GR-7 | `GoldRules.RedmExceedsReserve` | reserve 4,250,000 | `REDM` 4,250,001; then 4,250,000 | `gold-redm-exceeds-reserve`; reserve 0 | clamped to 0, fully added back on disconnect (GLD-3) |
| GR-8 | `GoldRules.SequenceGapAndRewind` | seq 1 used | events with seq 3, 1, 2; `AUDT` audit seq 1 | `gold-bad-seq`, `gold-bad-seq`, valid; `AUDT` valid on its own counter | no sequence |
| GR-9 | `GoldRules.Overflow` | state built directly with reserve `MAX_RESERVE_MG − 10` | `GOLD` 11; `GOLD` `maxEventMg + 1` | `gold-reserve-overflow`; `gold-amount-too-large` | unchecked `+=` wrapped (GLD-3, DB-8) |
| GR-10 | `GoldRules.WindowCap` | `maxWindowMg` 2 t, window 144 | `GOLD` 1 t, 1 t, 1 mg at h 10–12; 1 t at h 150; `REDM` 1 t at h 151 | third fails `gold-window-cap`; the others valid | no limits |
| GR-11 | `GoldRules.Expiry` | — | event at `expiry + 1`; expiry = h + 4,033; at `h = expiry` | `gold-expired`; `gold-expiry-too-far`; valid | signatures valid forever |
| GR-12 | `GoldRules.KeyRotationActivation` | V-GOLD-1 at h 1, V-REDM-1 at h 2, V-KEYR-1 at h 3 (delay 10 → activation 13) | `GOLD` seq 4 signed by new keys at h 12 and h 13; by old keys at h 12 and h 13 (each case starts from the state after `KEYR`) | new: `gold-bad-sig`, valid; old: valid, `gold-bad-sig`; epoch 1, keyset hash as §17 | compile-time keys only (GLD-6) |
| GR-13 | `GoldRules.KeyRotationRules` | — | `KEYR` with 3 sigs; second propose while pending; key moved to another slot; same keys; off-curve key; duplicate keys; cancel without pending; cancel with pending, then h ≥ activation | `bad-gold-format`, `gold-rotation-pending`, `gold-key-moved`, `gold-rotation-noop`, `gold-bad-new-key`, `gold-duplicate-new-key`, `gold-no-rotation`; old keys stay | — |
| GR-14 | `GoldRules.PauseUnpause` | V-GOLD-1, V-REDM-1, then V-PAUS-1 (seq 3) applied | `GOLD`, `REDM`; `AUDT`, `KEYR`; `PAUS`; `UNPS` with 3 sigs; `UNPS` with 4 | `gold-paused` ×2; valid ×2; `gold-already-paused`; `bad-gold-format`; unpaused | no freeze |
| GR-15 | `GoldRules.AuditAttestation` | S2 at h 7 | V-AUDT-1; again with as-of 6; as-of = h | S3, reserve unchanged; `gold-audit-height` ×2 | no attestation (GLD-8) |
| GR-16 | `GoldRules.CoinbaseAndMultiple` | — | event in a coinbase; two events in one tx | `bad-gold-coinbase`; `bad-gold-multiple` | coinbase markers counted (CRY-2 exploit path) |
| GR-17 | `GoldRules.KeysetOverride` | params copy: override at h 20, epoch 1,000,000, paused state | `UNPS` by override keys at h 19 and h 20 | `gold-bad-sig`; valid | hard fork needed code changes |
| GR-18 | `GoldUndo.ExactInverse` | 200 random valid events in 100 blocks | connect all, then disconnect all in reverse | each step restores the previous bytes; final = S0; blocks without events have empty undo | reversal by re-scan (GLD-3, DB-8) |
| GR-19 | `GoldUndo.CorruptionDetected` | undo of block 7 | flip a byte of `before`; wrong `afterHash`; truncate; version 2 | CORRUPT each, no crash | unbounded parse |
| GR-20 | `GoldRules.Sigops` | block with 100 `GOLD` txs | `CheckBlock` sigop count | +300 sigops; a block over 20,000 is `bad-blk-sigops` | vault checks not counted (GLD-9) |

### 20.4 Chain integration (integration, `TestChain` regtest)

| ID | Test | Setup | Action | Expected | Old behaviour caught |
|---|---|---|---|---|---|
| GC-1 | `GoldChain.ReorgRevertsGold` | V-GOLD-1 mined at 5 | heavier branch from 4 without it; then include the same bundle at 7 on the new branch; reorg back | reserve 0, seq 0, no index rows; then valid again; states always equal a fresh replay | clamp/add-back, persisted outside the UTXO commit (GLD-3, GLD-4, DB-8) |
| GC-2 | `GoldChain.UnsignedMarkerBlockInvalid` | miner puts an unsigned `REDM` (count 0) and, separately, a `REDM` with bad signatures in its coinbase or a tx | process block | block INVALID (`bad-gold-format` / `bad-gold-coinbase` / `gold-bad-sig`), `FAILED_VALID`; reserve never changes on any later reorg | skipped on connect, added on disconnect (CRY-V1, NET-V2, CON-11, DB-8) |
| GC-3 | `GoldChain.OldFormatRejected` | old layout `6a 04 'GOLD' 08 <amount> …` | process | `bad-txns-output-type` | old wire format accepted |
| GC-4 | `GoldChain.AtomicWithUtxo` | store hook fails the commit of the block with V-GOLD-1 | restart | reserve, seq, index and UTXO all at the previous tip | separate commits (GLD-4, CON-18) |
| GC-5 | `GoldChain.StartupDetectsTamper` | edit `gold_state` (reserve + 1); separately delete one `gold_events` row | restart | FATAL naming the check | reserve loaded blindly (DB-8) |
| GC-6 | `GoldChain.ReindexRecomputes` | chain with all §17 events and a reorg | `--reindex-chainstate` | `gold_state` and `gold_events` byte-identical | no reindex (GLD-4) |
| GC-7 | `GoldChain.ParamsKeysMismatch` | datadir made with regtest keys | restart with one genesis key changed | FATAL "vault keys in this build differ from the datadir" | — |
| GC-8 | `GoldChain.TwoNodesAgree` | two regtest nodes, events, a fork and a reorg | compare `/api/gold/status` | same `state_hash`, reserve and index | per-node drift (GLD-4) |

### 20.5 Mempool, API, tool, UI

| ID | Test | Setup | Action | Expected | Old behaviour caught |
|---|---|---|---|---|---|
| GM-1 | `GoldMempool.OnePendingPerCounter` | — | add `GOLD` seq 1, `REDM` seq 2, `AUDT` | accepted; `gold-event-pending`; accepted | — |
| GM-2 | `GoldMempool.InvalidatedByBlock` | `GOLD` seq 2 in mempool | block confirms `PAUS` seq 2 | entry removed | — |
| GM-3 | `GoldMempool.Expiry` | entry with expiry = tip + 1 | mine 2 blocks | removed | — |
| GM-4 | `GoldMempool.Policy` | 516-byte `KEYR`; 81-byte non-gold payload; two NULL_DATA outputs | accept | accepted; `datacarrier`; `multi-op-return` | — |
| GM-5 | `Miner.IncludesGoldEvent` | gold tx in mempool | template, mine | block valid, event applied | — |
| GA-1 | `GoldApi.StatusShapes` | chain of §13.1 | `GET /api/gold/status` without auth | 200; strings; values of §13.1 and §17 | float `price_per_buck`, unit "g", constants (GLD-7, GLD-10) |
| GA-2 | `GoldApi.ConfirmationRule` | `GOLD` mined at tip | status now and after 5 more blocks | `reserve_mg` old value, `reserve_tip_mg` new, pending 1; then confirmed | applied at 0 confirmations (`GOLD_STANDARD.md:71`) |
| GA-3 | `GoldApi.EventsQuery` | 3 events; 501 synthetic events | `from > to`; `type=XYZ`; `limit=600`; paging | 400 `bad-query` ×3; pages never split a height | — |
| GA-4 | `GoldApi.SubmitAuth` | regtest node | submit without token; cookie without CSRF header; token + `dry_run`; wrong network; stale seq | 401; 403; 200 no mempool change; 400 `wrong-network`; 409 `gold-bad-seq` with `expected_seq` | — |
| GA-5 | `GoldApi.EconomyTrimmed` | — | `GET /api/economy/status` | no `price_per_buck`, `gold_reserve` | misleading price (GLD-7) |
| GV-1 | `BucksVault.EndToEndRegtest` | 5 regtest keys via `import-test-key` | `status --save` → `new-event` → 3 × `sign` → `combine` → `submit` → mine 6 | status shows the reserve; event id matches | — |
| GV-2 | `BucksVault.SignRefuses` | edited `unsigned_payload_hex`; wrong genesis; key not in keyset; wrong `--doc`; wrong typed amount | `sign` | refused, no share written | ceremony signed a tx hash that never verified (GLD-6) |
| GV-3 | `BucksVault.CombineRules` | 4 `GOLD` shares incl. 1; shares without 1; one invalid share; `REDM` without 3 | `combine` | picks [0,1,2]; error; invalid share reported; error | — |
| GV-4 | `BucksVault.KeyFile` | — | encrypt/decrypt; wrong passphrase; changed `key_index`; file mode | round trip; refused; refused; 0600 | — |
| GV-5 | `BucksVault.DeterministicShares` | V-GOLD-1 event file | `sign` with regtest key 0 | share equals §17 sig 0 | — |
| GV-6 | `BucksVault.OfflineHasNoNetwork` | built `bucks-vault-offline` | run `submit`; inspect symbols | "unknown command"; no httplib client symbols | — |
| GU-1 | `ui_claims` (ctest) | tree | grep §14.3 | no forbidden phrase; disclosure present | "fully backed", "Redeem physical gold", hard-coded $/g (GLD-7, API-18) |
| GU-2 | `build_hygiene` | tree | deleted-symbol grep (§19 G10) | none found | dead gold/governance code (GLD-5) |
| GU-3 | Manual UI checklist | regtest node | coverage below and above 100 %, supply 0, paused, pending rotation, stale and differing audit | each state shown as §14.1 | fixed "100 %" bar (API-18) |

---

## 21. Deferred features

| Feature | Why deferred |
|---|---|
| Commitment of `stateHash` in the coinbase or header (light-client proofs) | Not needed for consensus (every node computes the state); can be added later as a soft fork. |
| On-chain governance and elections | Removed (§15); needs its own spec. |
| Hardware wallets that display event fields | v1 uses encrypted key files on air-gapped computers; the `--hw-stub` path covers devices that sign a digest. |
| Several auditors or several vault locations | Needs a new payload version (hard fork). |
| A delay-and-veto window for `GOLD`/`REDM` (GLD-6 fix idea) | `PAUS` (3 of 5, immediate) covers emergencies; revisit if the committee grows. |
| Fiat prices | Needs a signed price source; showing a constant is misleading. |
| Bar-list Merkle commitments | The document hash already commits to the full report. |

---

## 22. Assumptions about the other specs and coordination items

Assumptions (checked against the consensus spec of 2026-10-08; the storage spec did not exist yet):
- A1 (consensus) NULL_DATA outputs carry 0 value, use one minimal push, have payloads ≤ 1,024 bytes and never enter the UTXO set (§4.2, D1, T-OUT-VALUE, §7.6).
- A2 (consensus) One validation path; `ConnectBlock` calls the hook for every NULL_DATA output in block order, including the coinbase (§7.2, §7.6).
- A3 (consensus) `TaggedHash` (§0), `params.genesisHash`, the block undo record with `goldUndo ≤ 65,536` bytes (§4.7), `txSigOps` using `classify` (§7.4), UTXO statistics `totalAmount` (§8.7), `U256` (§6.1).
- A4 (storage) A `chain_state` key/value table whose value can be a BLOB; key `gold_state`.
- A5 (storage) One SQLite transaction per `ActivateBestChain` step holds coins, undo, status bits, best block, `gold_state` and `gold_events` changes; in-memory state changes only after COMMIT; `--reindex-chainstate` clears gold data with the coins.
- A6 (storage) One chain lock; HTTP handlers copy state under it (or use a read transaction pinned to the same best block); never hold it during socket I/O.

Changes requested from the consensus spec (small; listed for the integration lead):
1. Add rule T-GOLD (`GoldRules::checkTransaction`) to `CheckTransaction` after T-OUT-TYPE (§8.1).
2. Pass `const ChainParams&` to `connectOutput` and `checkForMempool`; pass `nextHeight` to `checkForMempool`; add `code` to `Classification` (§8.6).
3. Replace `MempoolEntry::hasGoldEvent` with `goldCounter` and add the one-pending-per-counter policy as check 7a, right after the conflict check (§10).
4. The §8.4 COMMIT also writes `gold_events` rows from `GoldUndoBuilder::events()` and deletes rows of disconnected blocks first (§9.4).
5. `LoadChainState` step 1 calls `GoldRules::checkParams`; step 9 is `GoldRules::startupCheck`; datadir initialisation stores `GoldRules::initialState` (§4.4, §9.5, §7.2).
6. P-NULLDATA's EVENT limit is `MAX_GOLD_PAYLOAD` = 520; `createNewBlock`'s `extraPayload` must be NOT_GOLD (§10).

---

## 23. Open questions for the owner

Each has a default that this spec already uses; implementation can start without an answer, except Q1 before any public launch.

| # | Question | Default |
|---|---|---|
| Q1 | **Legal and regulatory clearance (required).** Holding gold for a project, accepting gold or money from the public, and advertising a coin together with a gold reserve can be regulated (custody, commodities, securities, e-money, consumer protection, anti-money-laundering) in each country where the gold is held or the coin is offered. You must obtain legal advice and any licences **before** accepting any deposit, before publishing reserve figures to the public, and before mainnet launch. | The software only reports a figure, accepts no deposits and states that coins are not exchangeable for gold. |
| Q2 | **Coverage standard.** One mithqal (4,250 mg) per coin means 21 M coins would need 89,250 t of gold (about 40 % of all gold ever mined), so coverage will show small percentages for a long time. Keep this standard and the word "coverage"? | Keep; show 4 decimals of a percent. |
| Q3 | **Limits.** 1 t per event and 10 t per 1,008-block window in each direction. | As stated. |
| Q4 | **Key change delay.** 1,008 blocks (about 7 days) between a `KEYR` proposal and its activation. | 1,008. |
| Q5 | **Auditor on every deposit.** `GOLD` must include the Independent Auditor's signature. | Yes. |
| Q6 | **Keyholders.** Who holds each role, and how the Community Representative is chosen (there is no on-chain election). | Appointed by the owner; appointments and identity statements published. |
| Q7 | **Where documents are published** (deposit records, withdrawal records, audit reports whose hashes are on chain). | Project website plus an IPFS pin; the chain stores only the hash. |
| Q8 | **Audit frequency.** | Quarterly; the UI shows "stale" after 100 days. |
