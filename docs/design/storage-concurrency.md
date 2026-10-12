# Bucks Storage, Crash Safety and Concurrency Specification (Phase 2b)

| | |
|---|---|
| Status | Draft for owner review. Every decision below is final unless it is listed in §17 (Open questions). |
| Date | 2026-10-08 |
| Applies to | The relaunched chain ("chain v2", new genesis). No compatibility with `blockchain.db`, `utxodb.sqlite` or the old threading code. |
| Related specs | `docs/design/consensus-validation.md` ("consensus spec"); `docs/design/gold-reserve-module.md` ("gold spec"); a future P2P spec. |
| Audit input | `audit.json` of 2026-10-07. Findings are mapped to sections in §14. |

All `file:line` references point to the tree of 2026-10-08, 12:14. Storage and network code is still the original code the audit describes. `src/server.cpp`, `src/main.cpp` and `include/server.h` were still being changed by Phase 1 review fixes while this spec was written (for example the private-directory check and the data-directory lock). If a number in those three files has drifted, search for the function or symbol named in the text.

---

## 0. Conventions

- **MUST** is a hard requirement. **SHOULD** is a default that needs a written reason to change.
- "Consensus §x" and "gold §x" point into the other two specs; their type names (`BlockIndex`, `CoinsView`, `CoinsViewCache`, `ChainState`, `U256`, `GoldState`) are used unchanged.
- **Chain lock** = `cs_main`. **Step** = one `ActivateBestChain` step (consensus §8.4). **Writer** = the one SQLite connection that may write. **Reader** = a read-only connection from the pool. Chain work is 32 bytes big-endian; other integers in blobs are little-endian.
- "Measured" = prototype run on 2026-10-08 (Apple silicon SSD, SQLite 3.53/3.54, `synchronous=FULL`, `fullfsync=ON`, 1,000,000 coins). The SQL of §6 was executed on SQLite 3.54.0 together with the constraint, query-plan and reorg-order tests of §16. In a kill experiment (SIGKILL, plus 72 WAL truncations including mid-frame cuts) the database always opened at a commit boundary.

---

## 1. Scope and main decisions

**In scope:** one database per network (schema, constraints, versioning); the atomic commit of a chain step; crash analysis, startup checks, repair; class boundaries; threads, locks, snapshot reads; memory bounds; data directory hygiene; mempool file; SQLite and CI requirements; finding map, plan, tests.

**Non-goals:** block pruning (all bodies are kept); transaction or address-history index (only "unspent coins by address"); a second database engine; multi-process access or network filesystems; encryption of chain data (it is public); wallet file format (wallet spec); consensus, gold and P2P rules; parallel signature checking (allowed later, workers take no locks).

| # | Decision | § |
|---|---|---|
| D1 | One SQLite file `chain.sqlite` per network replaces `blockchain.db` and `utxodb.sqlite`. | 4, 6 |
| D2 | WAL, `synchronous=FULL`, foreign keys, STRICT tables; CHECKs and triggers make the database refuse invalid states. | 5, 6 |
| D3 | One writer connection, four read-only readers; all writes and checkpoints go through the writer. | 5, 7 |
| D4 | One `BEGIN IMMEDIATE` transaction per chain step; memory changes only after COMMIT; a failed COMMIT is fatal. | 8 |
| D5 | Block index keyed by hash; only headers in RAM; bodies and undo are write-once blobs. | 6, 12 |
| D6 | UTXO table `WITHOUT ROWID` with plain `INSERT`; address index; statistics from real row changes. | 6, 8 |
| D7 | Undo = consensus §4.7 layout, kept after disconnect, guarded by a CHECK and its checksum. | 10 |
| D8 | Gold state = one `gold_state` blob plus `gold_events` rows, committed with the coins. | 6, 8 |
| D9 | Ranked mutexes; `cs_main` is a plain non-recursive mutex; order checked at run time and by clang. | 11 |
| D10 | HTTP and network readers use an immutable `TipSnapshot` and WAL read sessions, never `cs_main`; they get copies. | 11 |
| D11 | No lock is held during socket I/O, the proof-of-work loop, IPFS calls or wallet-file fsync. | 11 |
| D12 | The miner works on a copy and polls an atomic `tipSequence`; a new tip aborts it within 65,536 hashes. | 11.5 |
| D13 | Three startup check tiers, an unclean-shutdown flag, two reindex modes; no silent repair. | 9 |
| D14 | Datadir `0700`, files `0600`, one datadir lock, per-network subdirectory. | 4 |
| D15 | Mempool saved to `mempool.dat`, fully re-validated on load. | 13 |
| D16 | SQLite 3.51.3 or newer (WAL-reset bug, CVEs of 3.44.0). | 5.1 |

---

## 2. Root causes in the current code

1. **Two files, no shared transaction.** Undo is autocommitted (`validation.cpp:514`) before the UTXO commit (`:521`); reserve, governance rows and the block row are three more autocommits with ignored results (`blockchain.cpp:384-386, 421-425, 447-450`). DB-5, CON-18, GLD-4.
2. **Height-keyed blocks.** `INSERT OR REPLACE` by height, unvalidated side blocks stored, every row trusted at startup, whole chain in RAM (`blockdb.cpp:33-34, 81, 106-140`; `blockchain.cpp:311, 563-597`; `blockchain.h:90`). CON-6, DB-1, DB-14, DB-15.
3. **UTXO table** uses `INSERT OR REPLACE`; address lookup scans and Base58-encodes every row (`utxodb.cpp:53-55, 166-213`). DB-6, DB-15.
4. **Undo** keyed by height, drops coin height, unchecked parse, missing row counts as success, outputs erased before inputs restored (`blockdb.cpp:43-46, 300-376`; `validation.cpp:545-568`). DB-7.
5. **No locks.** Raw-pointer getters (`blockchain.cpp:164-167, 459-470, 663-665`), SQLite transactions without a lock on a shared handle (`utxodb.cpp:154-164`). Network (`network.cpp:433`, inside `peersMutex` `:802`), miner (`server.cpp:1851-1938`) and HTTP handlers (`:1135, 1387, 1671, 1716`) all call in; broadcast does blocking `send` under `peersMutex` (`network.cpp:844-851`). DB-4, CON-10, NET-6, NET-3, NET-5, NET-14, API-7, API-8.
6. **Chain files ignore the data directory:** CWD-relative database paths, no PRAGMAs, default permissions (`blockchain.h:39`, `main.cpp:122`). Phase 1 already added a private-directory check and a `.lock` file for wallet and cookie (`server.cpp:234-326`), but the chain database does not use them, and the lock is taken in the `Server` constructor (`:507-530`), after the chain was opened. DB-17.
7. Event rows never reversed and without block hash; mempool saved per row and loaded unvalidated (`blockchain.cpp:524-527, 133-137`; `blockdb.cpp:436, 238-298`). DB-16, DB-10.
8. Shutdown flag is a plain `bool` set in a signal handler (`main.cpp:12-16`). OPS-19.
9. SQLite 3.44.0 with default build options (`src/sqlite3.c`, `build.sh:59`). OPS-18. FNV-1a and XOR hashers let attackers grind txids into one bucket (`crypto.h:138-149`, `mempool.cpp:12-18`); new finding, handled in §11.3 L8.

---

## 3. Assumptions about the other specs (for reconciliation)

- **A1** Status bits and invariants I1-I5 of consensus §8.1; `IN_MAIN_CHAIN` is persisted.
- **A2** Undo record v1 exactly as consensus §4.7.
- **A3** Commit unit = one `ActivateBestChain` step; failed COMMIT is fatal; memory changes after COMMIT (consensus §8.4).
- **A4** `Coin = {amount, scriptPubKey, height, isCoinbase}`; `CoinsViewCache` follows Bitcoin Core (FRESH/DIRTY flags; `addCoin` throws on overwriting an unspent entry).
- **A5** UTXO statistics formula of consensus §8.7; here the store computes them from real row changes.
- **A6** Public `ChainState` entry points lock for themselves; `ActivateBestChain` is called with the chain lock free (consensus §8.2).
- **A7** Gold: `gold_state` blob, `gold_events` rows, `goldUndo` inside the undo blob, `GoldIndexReader` implemented by storage (gold §7.1, §8.6, §9.3-§9.5).
- **A8** The template builder returns a copy plus `tipSequence`, which rises after each committed step (consensus §10.1-§10.2).
- **A9** The mempool is a `ChainState` member guarded by `cs_main`.

**Refinements proposed to the consensus spec** (none changes a consensus rule):

| # | Refinement | Why |
|---|---|---|
| R1 | `cs_activate`: a second mutex, taken before `cs_main`, so one thread at a time runs the `ActivateBestChain` loop. | Ordered tip notifications, no duplicate work. |
| R2 | Stage E of `TestBlockValidity` (coins and signatures) runs **outside** `cs_main` on a pinned read snapshot (§11.5). Stages A-D stay inside. | A full template needs about one second of signature checking. |
| R3 | Startup step 8 (full UTXO scan) always runs after an unclean shutdown, otherwise only if the set has at most 5,000,000 coins (§9.2). | A scan of 50 million coins takes minutes; statistics are still verified in every commit. |
| R4 | `MempoolEntry::tx` is `shared_ptr<const Transaction>`; `Coin::scriptPubKey` a fixed 25-byte array; add a bounded `SigCache` and `SaltedHasher` (§12). | Cheap snapshots; about 110 bytes per cached coin; fast block connection; no hash flooding. |

---

## 4. Data directory and process hygiene

```
<datadir>/                     default ~/.bucks (POSIX), %APPDATA%\Bucks (Windows)        0700
|-- .lock                      process lock, exists today (server.cpp:285)                0600
|-- wallets.json               wallet code only; chain code never reads or writes it      0600
|-- .cookie                    API token (API spec)                                        0600
`-- <network>/                 main | test | regtest                                       0700
    |-- chain.sqlite           the one database                                           0600
    |-- chain.sqlite-wal/-shm  created by SQLite; never delete by hand                    0600
    |-- mempool.dat            §13                                                        0600
    `-- backups/               VACUUM INTO copies                                         0700
```

1. **Process start (before any thread):** `umask(077)`; raise soft `RLIMIT_NOFILE` to `min(hard, 4096)`, warn below 1,024; `sqlite3_config(SQLITE_CONFIG_LOG, ...)` so SQLite notices (such as "recovered N frames from WAL") reach the log; `sqlite3_initialize()`. Windows: no umask, directory under the user profile.
2. **Absolute paths.** `--datadir` and `--network` choose the folder; nothing is created in the current directory (old: `blockchain.h:39`, `main.cpp:122`). `main()` builds one `DataDir` first and passes its path to `ServerConfig::dataDir` (default logic exists at `server.cpp:487-517`).
3. **Directory checks** (as `ensurePrivateDir` does today, `server.cpp:234-279`): missing directories are created under `umask(077)`; an existing directory MUST be a directory owned by the effective user with no group/other access, else exit 4 with the hint `chmod 700 <dir>`. The node never changes the mode of an existing directory by itself (it may be a home or shared folder). The same applies to `<datadir>/<network>`.
4. **File checks.** `lstat` every database file: a symbolic link or non-regular file makes the node exit 4. After SQLite creates its files, any group/other bit is removed and logged.
5. **Process lock.** Keep `lockDataDir` (`server.cpp:285-326`): `<datadir>/.lock`, `open(O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC, 0600)`, `flock(LOCK_EX|LOCK_NB)`, Windows `LockFileEx`; failure message `data directory <dir> is in use by another Bucks node`, exit 5. One node per datadir (two networks at once need two `--datadir`s; this also keeps `.cookie` and `wallets.json` single-owner). The lock moves from the `Server` constructor to `main()` and is taken **before** the database is opened: SQLite alone would allow two WAL processes, but two nodes would each build and connect blocks from their own in-memory state and fight over one tip. The OS drops the lock when the process dies, so it is never stale.
6. **Free space** at start and before every step MUST be at least `--min-free-disk-mb` (default 512), else refuse to start (exit 4) or stop cleanly at run time. Large steps need more (§8.5).
7. **Local filesystem only.** If `PRAGMA journal_mode=WAL` does not answer `wal`, exit 4.
8. **Legacy files.** If `blockchain.db` or `utxodb.sqlite` exist in the working directory or datadir, log `legacy chain files are ignored` and never open them.
9. **Wallet isolation.** No chain operation writes or deletes outside `<datadir>/<network>/`.
10. **Signal handlers** only store into a lock-free `std::atomic<bool>`.

**Exit codes:** 0 clean; 1 runtime error; 2 usage error (exists, `main.cpp:117`); 3 parameter, genesis or network mismatch (consensus); 4 storage failure (I/O, disk, failed COMMIT, permissions, foreign lock); 5 datadir in use; 6 database corrupt or inconsistent (message names `--reindex` or `--reindex-chainstate`); 7 unsupported schema or SQLite version.

**Backups.** Chain data is re-downloadable; the irreplaceable file is `wallets.json` (wallet spec). `ChainDB::backupTo(path)` runs `VACUUM INTO` on a dedicated `SQLITE_OPEN_READONLY` connection (tested: works there, fails under `query_only=ON`; it reads in one read transaction, so the copy is a consistent snapshot), writes `<name>.partial`, runs `quick_check`, renames. Automatic backups only before a migration (§6.4), into `backups/chain-v<N>-<UTC time>.sqlite`, newest three kept. Copying `chain.sqlite` alone while the node runs is not a backup (data may sit in the `-wal` file).

---

## 5. SQLite configuration

### 5.1 Version and build

- **Minimum 3.51.3.** SQLite documents a WAL-reset bug in every release from 3.7.0 to 3.51.2 (fixed in 3.51.3 on 2026-03-13; backports 3.50.7 and 3.44.6). It needs two connections that write or checkpoint at the same instant. The vendored `src/sqlite3.c` is 3.44.0 (Nov 2023) and also has the CVEs of OPS-18. Replace it with the newest stable amalgamation at implementation time (3.53/3.54 in October 2026) and list it in a `THIRD_PARTY.md`.
- By design there are never two writers or two checkpointers (D3). Readers are `SQLITE_OPEN_READONLY` and cannot checkpoint (tested: "disk I/O error").
- `static_assert(SQLITE_VERSION_NUMBER >= 3051003)`; at start `sqlite3_libversion_number()` MUST equal `SQLITE_VERSION_NUMBER`, else exit 7.
- Compile options for `src/sqlite3.c` (CMake `set_source_files_properties` and `build.sh:59`, checked by `build_hygiene`): `SQLITE_THREADSAFE=1 SQLITE_DQS=0 SQLITE_DEFAULT_MEMSTATUS=0 SQLITE_OMIT_LOAD_EXTENSION SQLITE_ENABLE_API_ARMOR SQLITE_DEFAULT_FOREIGN_KEYS=1 SQLITE_DEFAULT_WAL_SYNCHRONOUS=2`. No `SECURE_DELETE`: the file holds only public data.

### 5.2 Connection setup

```
writer: SQLITE_OPEN_READWRITE|CREATE|FULLMUTEX|EXRESCODE      reader: SQLITE_OPEN_READONLY|FULLMUTEX|EXRESCODE
-- every connection
sqlite3_busy_timeout(db, 5000)
sqlite3_db_config: DEFENSIVE=1, TRUSTED_SCHEMA=0, ENABLE_FKEY=1, ENABLE_TRIGGER=1
sqlite3_limit: LENGTH 4 MiB, SQL_LENGTH 64 KiB, ATTACHED 0, TRIGGER_DEPTH 1, VARIABLE_NUMBER 64
PRAGMA cell_size_check = ON;  PRAGMA mmap_size = 0;      -- a damaged file gives an error, not SIGBUS
-- writer only, in this order
PRAGMA journal_mode = WAL;                    -- answer MUST be 'wal'
PRAGMA synchronous = FULL;
PRAGMA fullfsync = ON;  PRAGMA checkpoint_fullfsync = ON;    -- macOS; harmless elsewhere
PRAGMA wal_autocheckpoint = 4000;             -- pages = 16 MiB
PRAGMA journal_size_limit = 67108864;         -- WAL truncated to 64 MiB after a checkpoint
PRAGMA cache_size = -<writer KiB>;            -- §12
-- reader only
PRAGMA cache_size = -8192;  sqlite3_progress_handler(db, 100000, deadlineCheck)   -- 30 s session deadline
```

Schema expressions use only `substr`, `length`, `typeof` (audited built-ins), so `trusted_schema=OFF` works (tested). All statements are constant strings with bound parameters and are reset immediately after use (an unreset statement keeps a snapshot open and blocks checkpoints).

### 5.3 Why `synchronous=FULL`

Writes are rare (one commit per block, per header batch of at most 2,000, per received body). Measured commit times with full fsync: empty block 3 ms, typical 100 KB block 34 ms, worst-case 1 MB block (4,500 spends, 9,000 new coins) 325 ms. With `FULL` every commit that returned survives power loss, so §8.3 has only two states, "before" and "after", and the node never announces a tip it may lose. `NORMAL` keeps the file consistent but may roll back recent commits; a few milliseconds are not worth that. `wal_autocheckpoint=4000` instead of 1,000 cut the typical-block commit from 95 ms to 34 ms (measured).

### 5.4 Error policy

| SQLite result | On | Action |
|---|---|---|
| `BUSY`/`LOCKED` at `BEGIN IMMEDIATE` or `COMMIT` after 5 s | writer | FATAL, exit 4 (a foreign tool holds a lock). |
| `FULL`, `IOERR*`, `NOMEM` inside a transaction | writer | If `sqlite3_get_autocommit() == 0`: `ROLLBACK`. FATAL, exit 4. Never retry. |
| `CORRUPT*`, `NOTADB`, `CONSTRAINT*` | any | `ROLLBACK`, FATAL, exit 6. A constraint failure is a bug or corruption. |
| `BUSY`, `INTERRUPT` (deadline) | reader | The request fails (HTTP 503); the session closes. |

Conflict clauses: only `ON CONFLICT DO UPDATE` on `chain_state` and on the non-fresh coin upsert, and `ON CONFLICT DO NOTHING` on the undo insert (§8.1). Never `OR REPLACE` or `OR IGNORE`. **FATAL** = log the reason, set `ChainState::m_fatal` (every entry point then returns `INTERNAL`), request shutdown with the exit code, write nothing more, skip `clean_shutdown=1`.

---

## 6. Database schema v2

### 6.1 SQL

Run once on an empty file in one `BEGIN IMMEDIATE ... COMMIT`, with `PRAGMA application_id = 1112886099` (`0x42554B53`, "BUKS") and `PRAGMA user_version = 2` in the same transaction; then `PRAGMA wal_checkpoint(TRUNCATE)`.

```sql
CREATE TABLE chain_state (
  name  TEXT NOT NULL PRIMARY KEY,
  value ANY  NOT NULL,
  CHECK (CASE
    WHEN name IN ('schema_version','best_height','utxo_count','clean_shutdown','reindex_in_progress')
         THEN typeof(value) = 'integer' AND value >= 0
    WHEN name = 'utxo_total_amount'
         THEN typeof(value) = 'integer' AND value BETWEEN 0 AND 2100000000000000
    WHEN name = 'network'
         THEN typeof(value) = 'text' AND value IN ('main','test','regtest')
    WHEN name IN ('genesis_hash','best_block_hash','utxo_digest')
         THEN typeof(value) = 'blob' AND length(value) = 32
    WHEN name = 'gold_state'
         THEN typeof(value) = 'blob' AND length(value) BETWEEN 1 AND 1024
    ELSE 1 END)
) STRICT, WITHOUT ROWID;

CREATE TABLE block_index (
  hash       BLOB    NOT NULL PRIMARY KEY CHECK (length(hash) = 32),
  prev_hash  BLOB    REFERENCES block_index(hash),            -- NULL only for genesis
  height     INTEGER NOT NULL CHECK (height BETWEEN 0 AND 4294967295),
  header     BLOB    NOT NULL CHECK (length(header) = 84),
  chain_work BLOB    NOT NULL CHECK (length(chain_work) = 32),  -- big-endian U256
  status     INTEGER NOT NULL CHECK (status BETWEEN 1 AND 255), -- consensus §8.1 bits
  tx_count   INTEGER NOT NULL DEFAULT 0 CHECK (tx_count BETWEEN 0 AND 15384),
  size       INTEGER NOT NULL DEFAULT 0 CHECK (size BETWEEN 0 AND 1000000),
  CHECK ((height = 0 AND prev_hash IS NULL
          AND substr(header, 9, 32) = x'0000000000000000000000000000000000000000000000000000000000000000')
      OR (height > 0 AND prev_hash = substr(header, 9, 32))),
  CHECK ((status & 4)   = 0 OR (status & 2)  != 0),   -- DATA_CHECKED  => HAVE_DATA
  CHECK ((status & 8)   = 0 OR (status & 4)  != 0),   -- CONNECTED     => DATA_CHECKED
  CHECK ((status & 16)  = 0 OR (status & 8)  != 0),   -- HAVE_UNDO     => CONNECTED
  CHECK ((status & 8)   = 0 OR (status & 16) != 0 OR height = 0),  -- CONNECTED => HAVE_UNDO (not genesis)
  CHECK ((status & 32)  = 0 OR (status & 8)  != 0),   -- IN_MAIN_CHAIN => CONNECTED
  CHECK ((status & 192) = 0 OR (status & 32) = 0)     -- FAILED_*      => not IN_MAIN_CHAIN
) STRICT, WITHOUT ROWID;

CREATE UNIQUE INDEX block_index_main_height ON block_index(height) WHERE (status & 32) = 32;

CREATE TRIGGER block_index_frozen BEFORE UPDATE OF hash, prev_hash, height, header, chain_work ON block_index
BEGIN SELECT RAISE(ABORT, 'block_index: header fields are immutable'); END;
CREATE TRIGGER block_index_size_once BEFORE UPDATE OF tx_count, size ON block_index WHEN OLD.tx_count != 0
BEGIN SELECT RAISE(ABORT, 'block_index: tx_count and size are write-once'); END;

CREATE TABLE block_data (
  hash BLOB NOT NULL PRIMARY KEY REFERENCES block_index(hash),
  data BLOB NOT NULL CHECK (length(data) BETWEEN 85 AND 1000000)    -- consensus §4.5 serialization
) STRICT;
CREATE TRIGGER block_data_write_once BEFORE UPDATE ON block_data
BEGIN SELECT RAISE(ABORT, 'block_data is write-once'); END;

CREATE TABLE undo (
  block_hash BLOB NOT NULL PRIMARY KEY REFERENCES block_index(hash),
  data       BLOB NOT NULL CHECK (length(data) >= 67 AND substr(data, 1, 1) = x'01'
                                  AND substr(data, 2, 32) = block_hash)   -- consensus §4.7
) STRICT;
CREATE TRIGGER undo_write_once BEFORE UPDATE ON undo
BEGIN SELECT RAISE(ABORT, 'undo is write-once'); END;

CREATE TABLE utxo (
  txid        BLOB    NOT NULL CHECK (length(txid) = 32),
  vout        INTEGER NOT NULL CHECK (vout BETWEEN 0 AND 4294967295),
  amount      INTEGER NOT NULL CHECK (amount BETWEEN 1 AND 2100000000000000),   -- = MAX_MONEY
  script      BLOB    NOT NULL CHECK (length(script) = 25 AND substr(script, 1, 3) = x'76a914'
                                      AND substr(script, 24, 2) = x'88ac'),      -- P2PKH only
  height      INTEGER NOT NULL CHECK (height BETWEEN 0 AND 4294967295),
  is_coinbase INTEGER NOT NULL CHECK (is_coinbase IN (0, 1)),
  hash160     BLOB    GENERATED ALWAYS AS (substr(script, 4, 20)) VIRTUAL,
  PRIMARY KEY (txid, vout)
) STRICT, WITHOUT ROWID;
CREATE INDEX utxo_by_hash160 ON utxo(hash160, txid, vout);

CREATE TABLE gold_events (                       -- active chain only; layout owned by gold §9.3
  event_id         BLOB    NOT NULL PRIMARY KEY CHECK (length(event_id) = 32),
  block_hash       BLOB    NOT NULL REFERENCES block_index(hash),
  height           INTEGER NOT NULL CHECK (height BETWEEN 0 AND 4294967295),
  tx_index         INTEGER NOT NULL CHECK (tx_index >= 0),
  txid             BLOB    NOT NULL CHECK (length(txid) = 32),
  vout             INTEGER NOT NULL CHECK (vout >= 0),
  type             TEXT    NOT NULL CHECK (type IN ('GOLD','REDM','AUDT','KEYR','PAUS','UNPS')),
  seq              INTEGER NOT NULL CHECK (seq >= 0),
  amount_mg        INTEGER,
  reserve_after_mg INTEGER NOT NULL,
  doc_hash         BLOB    NOT NULL CHECK (length(doc_hash) = 32),
  signer_mask      INTEGER NOT NULL CHECK (signer_mask BETWEEN 0 AND 31),
  payload          BLOB    NOT NULL CHECK (length(payload) <= 520)
) STRICT, WITHOUT ROWID;
CREATE UNIQUE INDEX gold_events_pos   ON gold_events(height, tx_index);
CREATE INDEX        gold_events_block ON gold_events(block_hash);
```

`2100000000000000` equals `Consensus::MAX_MONEY` (a unit test compares). Bodies and undo are rowid tables on purpose: rows of 100 KB or more would bloat the interior pages of a `WITHOUT ROWID` b-tree. There is no governance table: the gold spec removes governance markers from the node (gold §15), which resolves the unreversed log of DB-16.

### 6.2 What the database itself enforces

1. At most one main-chain block per height (unique partial index). Hence §8.1 clears `IN_MAIN_CHAIN` on disconnected blocks **before** setting it on connected ones; the wrong order is rejected (tested).
2. Consensus invariants I1-I4 on every status value; no header without its parent; no body, undo or event without its index row; an undo blob names its own block; header fields, bodies and undo cannot be updated.
3. Only P2PKH coins with `1 <= amount <= MAX_MONEY`; a plain `INSERT` of an existing outpoint fails (old: silent overwrite, `utxodb.cpp:53-55`); `chain_state` values have the right type and range per key.

### 6.3 `chain_state` keys

`schema_version` (integer, equals `user_version`); `network` and `genesis_hash` (together the chain id; text and blob 32; MUST equal `--network` and the params, else exit 3, consensus §3.5); `best_block_hash`, `best_height` (change only in `commitStep`); `utxo_count`, `utxo_total_amount` (the circulating supply), `utxo_digest` (statistics, consensus §8.7); `gold_state` (`GoldState` v1 bytes with reserve, counters and keys, gold §7.1; replaces a separate `gold_reserve` counter; written only when it changed); `clean_shutdown` (0 while running or after a crash, 1 after an orderly shutdown); `reindex_in_progress` (§9.4). A new key needs an entry here.

### 6.4 Versioning and migration

1. `user_version` and `chain_state.schema_version` are always equal. Version 2 is the first (old files had none).
2. Any change to a table, index, trigger or CHECK is a new version with `migrate_N_to_N+1(Db&)` in `kMigrations[]`. Migrations go forward only.
3. Startup runs pending migrations one at a time, each in one `BEGIN IMMEDIATE` transaction (DDL is transactional), after an automatic backup (unless `--no-backup-before-migrate`). A crash leaves the old version. A newer or unknown version: exit 7.
4. A migration that changes the meaning of chain data performs the reset of §9.4 in the same transaction; the node then reconnects from genesis.
5. Each released version keeps a golden file `tests/data/schema/vN.sqlite`; the migration test opens each.
6. Example (not in v2): a transaction index `tx_index(txid PK, height, tx_pos)` filled in `commitStep` would be version 3 with a backfill (§17 Q2).

### 6.5 Queries (prepared once per connection)

```sql
SELECT amount, script, height, is_coinbase FROM utxo WHERE txid = ?1 AND vout = ?2;      -- writer, CoinsViewDB
SELECT hash, prev_hash, height, header, chain_work, status, tx_count, size FROM block_index;  -- startup
SELECT hash FROM block_index WHERE height = ?1 AND (status & 32) = 32;     -- uses block_index_main_height
SELECT data FROM block_data WHERE hash = ?1;
SELECT txid, vout, amount, height, is_coinbase FROM utxo
  WHERE hash160 = ?1 AND (txid, vout) > (?2, ?3) ORDER BY txid, vout LIMIT ?4;   -- keyset paging, no sort step
SELECT COALESCE(SUM(amount), 0), COUNT(*) FROM utxo WHERE hash160 = ?1;           -- balance
SELECT ... FROM gold_events WHERE height BETWEEN ?1 AND ?2 ORDER BY height, tx_index LIMIT ?3;
```

Measured (1,000,000 coins): about 149 bytes per coin including both indexes; an address with 20 coins answers in 0.04 ms; over 200,000 point lookups per second.

---

## 7. C++ class boundaries

| Class | Files | Thread rule | Role |
|---|---|---|---|
| `Mutex`, `LockGuard`, `LOCK()` | new `include/sync.h` | - | ranked mutex, thread-safety macros (§11.9) |
| `DataDir` | new `include/datadir.h`, `src/datadir.cpp` | main thread | layout, permissions, process lock |
| `Db`, `Stmt`, `WriteTxn` | new `include/sqlite_util.h`, `src/sqlite_util.cpp` | one thread at a time | RAII over `sqlite3` |
| `ChainStore` | new `include/chainstore.h` | writes need `cs_main` | abstract store (consensus plan step 12) |
| `ChainDB`, `ChainReader`, `CoinsViewDB` | new `include/chaindb.h`, `src/chaindb.cpp` | writer: `cs_dbwriter`; readers pooled | SQLite store |
| `CoinsViewCache`, `Coin`, `BlockUndo` | consensus `coins.h`, `undo.h` | local to one step | overlay |
| `ChainState`, `BlockIndex`, `Mempool` | consensus | `cs_main` (mempool's own mutex removed) | |
| `MempoolFile`, `SigCache`, `SaltedHasher` | new `mempool_file.h`, `sigcache.h`, `hasher.h` | main / `cs_main` | §12, §13 |

### 7.1 `ChainStore` (also implemented by the test `MemoryChainStore`)

```cpp
struct IndexRecord {                               // one block_index row
    Hash256 hash, prevHash;                        // prevHash = 32 zero bytes for genesis
    std::array<uint8_t, 84> header;  uint32_t height;  U256 chainWork;  // chainWork stored big-endian
    uint32_t status, txCount, size;                // consensus §8.1 bits; txCount/size 0 until HAVE_DATA
};
struct StatusUpdate { Hash256 hash; uint32_t setBits, clearBits; };
struct CoinsDelta {                                // net effect of one step on the committed set
    struct Add { OutPoint op; Coin coin; bool fresh; };
    std::vector<OutPoint> spent;                   // each MUST exist
    std::vector<Add>      added;                   // fresh: plain INSERT; not fresh: upsert
};
struct GoldEventRow { Hash256 eventId; uint32_t txIndex; Hash256 txid; uint32_t vout; std::string type;
                      uint64_t seq; std::optional<int64_t> amountMg; int64_t reserveAfterMg;
                      Hash256 docHash; uint8_t signerMask; std::vector<uint8_t> payload; };  // = gold AppliedEvent (§8.6, §9.3)
struct ConnectedBlock { Hash256 hash; uint32_t height; std::vector<uint8_t> undo; std::vector<GoldEventRow> goldEvents; };
struct StepCommit {
    Hash256 expectedOldBest, newBest;  uint32_t newBestHeight;
    std::vector<Hash256> disconnected;             // tip first
    std::vector<ConnectedBlock> connected;         // ascending height
    CoinsDelta coins;
    std::optional<std::vector<uint8_t>> goldState; // only if it changed
};
struct UtxoStats   { uint64_t count, totalAmount; Hash256 digest; };
struct StoredState { std::string network; Hash256 genesis, best; uint32_t bestHeight, schema;
                     UtxoStats stats; bool cleanShutdown; std::vector<uint8_t> goldState; };
struct StoreFatal : std::runtime_error { int exitCode; };    // §5.4
struct StoreBusy  : std::runtime_error {};                   // reader pool exhausted or deadline

class ChainStore {
public:
    virtual ~ChainStore() = default;
    virtual bool isEmpty() const = 0;
    virtual void initGenesis(const IndexRecord&, const std::vector<uint8_t>& genesisBlock,
                             const std::vector<uint8_t>& initialGoldState) = 0;           // one txn
    virtual StoredState loadState() const = 0;
    virtual std::vector<IndexRecord> loadBlockIndex() const = 0;
    virtual void putHeaders(const std::vector<IndexRecord>&) = 0;                          // one txn, <= 2,000
    virtual void putBlock(const Hash256&, const std::vector<uint8_t>& bytes, uint32_t txCount) = 0;
                                                    // one txn: body row, status |= HAVE_DATA|DATA_CHECKED (6)
    virtual void putStatus(const std::vector<StatusUpdate>&) = 0;                          // markFailed
    virtual bool readBlock(const Hash256&, std::vector<uint8_t>&) const = 0;
    virtual bool readUndo (const Hash256&, std::vector<uint8_t>&) const = 0;
    virtual const CoinsView& committedCoins() const = 0;
    virtual void commitStep(const StepCommit&) = 0;                                        // §8.1
    virtual UtxoStats scanStats() const = 0;
    virtual void resetChainState(const std::vector<uint8_t>& initialGoldState) = 0;        // §9.4
    virtual void setCleanShutdown(bool) = 0;
};
```

### 7.2 `ChainDB`, `ChainReader`

```cpp
struct OpenOptions { std::string dir; NetworkId network; Hash256 genesis; uint32_t dbcacheMiB = 128;
                     bool checkDb = false, reindexChainstate = false, reindex = false; };
class ChainDB final : public ChainStore, public GoldIndexReader {   // GoldIndexReader: gold §8.6
public:
    static std::unique_ptr<ChainDB> open(const OpenOptions&);       // §9.1; throws StoreFatal(exitCode)
    void setChainLock(const Mutex* cs_main);                        // debug assertion: writes hold it
    std::unique_ptr<ChainReader> openReader() const;                // pool of 4; waits 5 s, then StoreBusy
    void backupTo(const std::string& path) const;
    void checkpointTruncate();                                      // shutdown only
};
class ChainReader {      // ONE snapshot; not thread-safe; destroy before writing the HTTP reply
public:                  // openReader() only checks a connection out; the snapshot is pinned by the first query
    void pin();          // or explicitly: BEGIN + SELECT best_block_hash. Code that needs "the snapshot equals the
                         // tip" calls openReader() BEFORE taking cs_main and pin() INSIDE it (§11.5)
    StoredState state() const;
    std::optional<IndexRecord> indexRecord(const Hash256&) const;
    std::optional<Hash256>     activeHashAt(uint32_t height) const;
    std::vector<IndexRecord>   activeRange(uint32_t from, uint32_t count) const;     // count <= 1,000
    bool block(const Hash256&, std::vector<uint8_t>& out) const;
    std::optional<CoinRow> coin(const OutPoint&) const;
    std::vector<CoinRow>   coinsByHash160(const Hash160&, size_t limit, const std::optional<OutPoint>& after) const;
    AddressSummary         summaryByHash160(const Hash160&) const;                   // balance, coin count
    std::vector<GoldEventView> goldEvents(uint32_t fromHeight, uint32_t toHeight, size_t limit) const;
};
```

A session has a 30 s deadline (`sqlite3_progress_handler`); a long session would hold back WAL checkpoints. The pool logs sessions older than 10 s.

### 7.3 Coins overlay contract

`CoinsViewDB : CoinsView` reads `utxo` through the writer connection and is valid only while `cs_main` is held (no other writer exists, so no snapshot is needed). The step overlay `CoinsViewCache` on top follows Bitcoin Core: flags `FRESH` (not in the committed set), `DIRTY` (differs from it), `SPENT`. `addCoin(op, coin, possibleOverwrite=false)` throws `std::logic_error` on an unspent entry and sets `FRESH` only if no `DIRTY` entry exists for `op` (a coin spent earlier in the step and re-created by the new branch must be an upsert). `takeDelta()` drops `SPENT`+`FRESH`, puts `SPENT` non-fresh into `spent`, and unspent `DIRTY` into `added` with their flag. `entryCount()` and `memoryUsage()` drive the limits of §8.5.

### 7.4 `ChainState`: locking contract

```cpp
class ChainState {
public:
    // from any thread, WITHOUT cs_main held; each takes its locks itself
    ProcessResult ProcessNewBlockHeaders(const std::vector<BlockHeader>&, PeerId) LOCKS_EXCLUDED(cs_main);
    ProcessResult ProcessNewBlock(const Block&, BlockSource, bool requested)       LOCKS_EXCLUDED(cs_main, cs_activate);
    TxResult      AcceptTransaction(const Transaction&, TxSource)                  LOCKS_EXCLUDED(cs_main);
    BlockTemplate createBlockTemplate(const AssemblerOptions&)                     LOCKS_EXCLUDED(cs_main);
    void          periodic(int64_t now)                                            LOCKS_EXCLUDED(cs_main);   // main thread, 1 Hz
    // no cs_main: safe from every thread
    std::shared_ptr<const TipSnapshot> tipSnapshot() const;      // takes only cs_tip
    uint64_t tipSequence() const noexcept;                       // atomic acquire load
    std::unique_ptr<ChainReader> openReader() const;             // takes only cs_dbpool
    // short cs_main sections; always COPIES
    std::vector<BlockHeader> headersAfter(const std::vector<Hash256>& locator, const Hash256& stop, size_t max) const;
    std::vector<Hash256>     locator() const;
    std::vector<MempoolTxInfo> mempoolSnapshot(size_t max) const;
    std::shared_ptr<const Transaction> mempoolGet(const Hash256& txid) const;
    std::vector<CoinRow>     spendableCoins(const Hash160&, size_t max) const;   // reader snapshot + mempool filter + maturity
    std::vector<Hash256>     drainRelayQueue(size_t max);                        // txids accepted since the last call
    mutable Mutex cs_activate{RANK_ACTIVATE, "cs_activate"};
    mutable Mutex cs_main{RANK_CHAIN, "cs_main"};
private:  // block index, active chain, candidates, failed map, coin read cache, mempool, orphans, gold state,
          // fork alert, IBD flag: GUARDED_BY(cs_main). BlockIndex* never leaves this class.
};
struct TipSnapshot {                 // immutable; replaced as a whole after every committed step
    Hash256 tipHash;  uint32_t height, time, bits;  std::array<uint8_t, 32> chainWork;
    uint64_t sequence;               // = tipSequence() at publication
    UtxoStats stats;                 // totalAmount = circulating supply
    std::vector<uint8_t> goldState;  // the gold API computes normalize(copy, tip + 1) from this
    bool initialBlockDownload;  std::optional<ForkAlert> forkAlert;  int64_t publishedAt;
};
struct BlockTemplate { Block block; uint32_t height; Hash256 prevHash; uint64_t tipSequence; int64_t minTime; uint64_t fees; };
```

Deleted: `Blockchain::tip()`, `getBlock()`, `getBlockByHeight()`, `getMempool()`, `utxo()`, `utxoDB`, `blockDB` (`blockchain.h:42-52, 81-82`).

---

## 8. Atomic connect and disconnect

### 8.1 The commit (`ChainDB::commitStep`)

Called by `ActivateBestChain` with `cs_main` held, after the whole step was validated on the overlay (nothing written yet). Any failure: `ROLLBACK` and FATAL (§5.4).

```
 1  BEGIN IMMEDIATE
 2  SELECT value FROM chain_state WHERE name='best_block_hash'     -- MUST equal expectedOldBest, else FATAL (bug)
 3  for each disconnected block, tip first:
      UPDATE block_index SET status = status & ~32 WHERE hash=?1 AND (status & 32) != 0    -- changes() = 1
      DELETE FROM gold_events WHERE block_hash=?1                                          -- gold §9.4 rule 1
 4  for each coin in coins.spent:
      DELETE FROM utxo WHERE txid=?1 AND vout=?2 RETURNING amount, script, height, is_coinbase   -- exactly 1 row
 5  for each coin in coins.added:
      fresh:      INSERT INTO utxo(txid,vout,amount,script,height,is_coinbase) VALUES (...)      -- conflict = FATAL
      not fresh:  SELECT the old row if one exists (for statistics), then INSERT ... ON CONFLICT(txid, vout)
                  DO UPDATE SET amount=excluded.amount, script=excluded.script,
                                height=excluded.height, is_coinbase=excluded.is_coinbase
 6  for each connected block, ascending height:
      INSERT INTO undo(block_hash, data) VALUES (?1, ?2) ON CONFLICT(block_hash) DO NOTHING
           -- the row exists when a block is connected a second time; debug builds compare the bytes
      UPDATE block_index SET status = status | 56 WHERE hash=?1                            -- 8+16+32; changes() = 1
      INSERT INTO gold_events(...) for each GoldEventRow
 7  upsert chain_state: best_block_hash, best_height, utxo_count, utxo_total_amount, utxo_digest,
                        and gold_state if StepCommit.goldState is set
 8  COMMIT                                                         -- WAL frames written and fsync'd (FULL)
```

**Statistics (step 7)** come from steps 4-5: a removed row subtracts, an inserted row adds, an upsert is a removal of the old row plus an addition of the new one. `count' = count - removed + added`; `total' = total - sum(removed.amount) + sum(added.amount)`, which MUST stay within `0..MAX_MONEY` (else FATAL); `digest' = digest - sum H(removed) + sum H(added)` modulo 2^256 with `H(coin) = SHA256(txid || LE32(vout) || LE32(height) || u8(isCoinbase) || LE64(amount) || CompactSize(25) || script)` read as a big-endian number (consensus §8.7). Deriving them from the real row effects keeps them right even if the overlay has a bug.

**Re-connecting** a block seen before finds its undo row present. Undo is a pure function of the block, so keeping it is safe.

**After COMMIT returns** (still under `cs_main`):

1. Update the active-chain vector, tip and `BlockIndex::status` bits; prune candidates. (Vectors were reserved beforehand.)
2. Update the coin read cache: erase spent and overwritten entries, insert added ones. On `bad_alloc` clear the whole cache (it is only a cache).
3. Swap in the in-memory gold state; publish the `TipSnapshot` (built **before** the commit); `tipSequence.fetch_add(1, release)`.
4. `mempool.updateForStep(...)` (consensus §9.6) and queue relay entries. On `bad_alloc` clear the mempool (policy data) instead of failing.

Items 1 and 3 use only pointer swaps, erases and atomics, so they cannot throw. In debug and sanitizer builds, and every 1,000th commit otherwise, `ChainDB` re-reads `best_block_hash` and compares it with the in-memory tip.

### 8.2 Writes outside a step

| Operation | Transaction | Notes |
|---|---|---|
| `putHeaders` | one per call, at most 2,000 rows | `status = 1`; parent must exist (foreign key) |
| `putBlock` | `INSERT INTO block_data`, then `UPDATE block_index SET status = status \| 6, tx_count = ?, size = ?` | caller checked `HAVE_DATA` first; a duplicate is a constraint error |
| `putStatus` (`markFailed`) | one for all rows | sets bits 64/128; never on a row that is `IN_MAIN_CHAIN` |
| `setCleanShutdown` | one | §9.1 |

All run under `cs_main`; the free-space rule (§4 rule 6) is checked before each body write.

### 8.3 Crash analysis

"Process crash" = `kill -9`, abort, segfault: the OS keeps everything already written. "Power loss": only synced data survives. SQLite checks every WAL frame with a checksum and salt; a transaction exists only if its commit frame and all frames before it survived.

| Point | Process crash | Power loss | Recovery |
|---|---|---|---|
| C0 before `BEGIN` | old | old | none |
| C1 statements 1-7 running | old | old | WAL recovery ignores frames without a commit record |
| C2 inside `COMMIT`, commit frame incomplete | old | old | same |
| C3 commit frame written, `fsync` not finished | **new** (bytes are in the OS cache) | old **or** new | none |
| C4 `COMMIT` returned | new | new | none |
| C5 after `COMMIT`, before the memory update | new (memory is rebuilt from the file) | new | startup; the new tip is announced after the next handshake |
| X1 inside an automatic checkpoint (tail of `COMMIT`) | consistent | consistent | WAL kept and replayed; checkpoints are idempotent |
| X2 `putBlock` | body absent or present | same | startup `ActivateBestChain` connects it (consensus step 10) |
| X3 `putHeaders` / `putStatus` | none or all rows | same | peers re-send; a failed block is re-checked when it arrives again |
| X4 `mempool.dat` write | old or new file | same (temp file, fsync, rename, directory fsync) | load validates (§13) |
| X5 migration or reset (§6.4, §9.4) | old | old | rerun |

A crash loses only memory state (mempool since the last dump, orphan pool, peer connections). At no point do the UTXO set, best block, undo, reserve and event rows disagree. (Old: a crash between `validation.cpp:521` and `blockchain.cpp:448` left UTXO set N with block table N-1, DB-5.) If a disk or virtual machine acknowledges `fsync` without writing, no software can give this guarantee: no `nobarrier`, no "unsafe" VM cache mode.

### 8.4 Failure inside a step

A validation failure (`INVALID`) happens on the overlay before step 1: nothing written, overlay dropped (consensus property 4). `bad_alloc` while building the overlay: drop it, mark nothing invalid, raise the fork alert "step needs more memory". A failed commit: FATAL; memory was never touched, so the process just exits and the next start runs §9.

### 8.5 Step size limits and disk need

- **Extension step** ends after the block that pushes the overlay above `STEP_SOFT_MAX_COINS = 250,000` entries (any prefix of the path is a valid state). This also bounds WAL growth.
- **Reorg step** cannot be split (atomic, CON-9). Hard limit `STEP_HARD_MAX_COINS = 6,000,000` entries (about 1 GB); beyond it the reorg is refused with a fork alert.
- **Disk:** a step writes about `min(changedRows * 2, totalPages) * 4096` bytes to the WAL. Free space MUST be at least twice that plus 256 MiB, else the step is refused like a memory-limit failure. After the next checkpoint the WAL is cut to 64 MiB.

### 8.6 Sequence: a block arrives from a peer

```
network thread                 ChainState                                ChainDB writer        other threads
frames cut under cs_peers; lock released
ProcessNewBlock ─────────────► LOCK cs_main: AcceptBlock (stages A-D) ─► putBlock BEGIN..COMMIT
                               UNLOCK cs_main
                               LOCK cs_activate
                               loop: LOCK cs_main
                                       overlay over CoinsViewDB; Disconnect/ConnectBlock (signatures)
                                       commitStep ──────────────────────► BEGIN IMMEDIATE .. COMMIT (fsync)
                                       swap memory state; publish TipSnapshot; tipSequence++   ──► miner restarts; HTTP reads
                                       mempool.updateForStep; queue relay                           the new snapshot
                                     UNLOCK cs_main      (yield: other threads may enter between steps)
                               UNLOCK cs_activate
◄── {state, becameTip}
INV / reply through queues (no lock held)
```

---

## 9. Startup, recovery and repair

### 9.1 Open sequence

| # | Step | Failure |
|---|---|---|
| O1 | Process setup and `DataDir` (§4): umask, directories, symlink checks, `.lock`, free space, legacy-file warning. | exit 4 / 5 |
| O2 | `ChainDB::open`: if `chain.sqlite` is missing or has no schema, create schema v2 (§6.1) and checkpoint; else open it. | exit 4 |
| O3 | Connection setup (§5.2); WAL active; SQLite version check. | exit 4 / 7 |
| O4 | `application_id == 0x42554B53`; `user_version == chain_state.schema_version`; equals 2 (older: migrate; newer: exit 7). | exit 6 / 7 |
| O5 | `network` and `genesis_hash` equal the params. | exit 3 |
| O6 | Choose the check tier (§9.2). A missing `-wal` while `clean_shutdown = 0`, or a logged SQLite "WAL recovered" notice, selects tier 2. | |
| O7 | Write `clean_shutdown = 0` (one durable commit). From here a crash is detectable. | exit 4 |
| O8 | `LoadChainState` (consensus §8.7), mapped in §9.3. | exit 3 / 6 |
| O9 | Orderly shutdown (after all threads stopped, §11.8): dump `mempool.dat`, `setCleanShutdown(true)`, `wal_checkpoint(TRUNCATE)`, close readers, then the writer, release `.lock`. | |

A new database is initialised by `LoadChainState` step 2 through `initGenesis`: genesis index row with status `47` (1+2+4+8+32), its body, `best_block_hash` = genesis, zero statistics, `GoldRules::initialState`, `clean_shutdown = 0`.

### 9.2 Check tiers

| Check | Tier 1 (last run clean) | Tier 2 (unclean, or `--checkdb`) | Tier 3 (`--checkdb=full`) |
|---|---|---|---|
| identity, version, network, genesis | yes | yes | yes |
| load all `block_index` rows; per row: hash from the 84 bytes, PoW, linkage, `height = parent + 1`, recomputed chain work, stage B, invariants I1-I5 | yes | yes | yes |
| best block is `CONNECTED_VALID`; the `IN_MAIN_CHAIN` rows are exactly the chain from tip to genesis | yes | yes | yes |
| `VerifyDB` depth (`--checkblocks`) | 24 | 288 | whole chain |
| `HAVE_DATA` rows = `block_data` rows; `HAVE_UNDO` rows = `undo` rows; gold startup check | yes | yes | yes |
| UTXO statistics recomputed by full scan | only if `utxo_count <= 5,000,000` | always | always |
| `PRAGMA quick_check`, `foreign_key_check` | no | yes | `integrity_check` |
| every `block_data` row re-hashed against its key | no | no | yes |

Any failed check is FATAL and names the row, hash and rule; exit code per §4. Nothing is repaired silently.

### 9.3 Mapping to consensus §8.7

Steps 1-3: before O2 / O2 / O5. Step 4: `loadBlockIndex()` returns the rows; the loader sorts them by height in memory (about 1 s per million) and checks each. Step 5: `loadState()` plus the tier-1 rows above. Step 6 ("coins store's best-block marker"): true by construction, since `best_block_hash` and the coins commit together; it remains as the check that the row equals the unique main-chain tip. Step 7: `readBlock`/`readUndo` and a scratch overlay. Step 8: the tier rule (R3). Step 9: gold's `startupCheck` through `GoldIndexReader`, which cross-checks `gold_state` against the last `gold_events` row (`reserve_after_mg`) and the per-type counts; on mismatch the node stops and `--reindex-chainstate` recomputes both from the blocks (old: reserve loaded blindly, `blockchain.cpp:588-593`). Steps 10-11 unchanged.

### 9.4 Repair modes

**`--reindex-chainstate`** (derived data only; one transaction; index and bodies stay):

```sql
BEGIN IMMEDIATE;
DROP TABLE utxo;   -- then CREATE TABLE utxo and CREATE INDEX utxo_by_hash160 exactly as in §6.1
DELETE FROM undo;  DELETE FROM gold_events;
UPDATE block_index SET status = status & ~(8+16+32+64+128);        -- clear every derived flag
UPDATE block_index SET status = status | 8 | 32 WHERE height = 0;    -- genesis stays connected
-- upsert chain_state: best = genesis, height 0, zero statistics, gold_state = GoldRules::initialState
COMMIT;
```

Then `ActivateBestChain()` connects every stored block with full validation. A crash during the reset leaves the old state; a crash later leaves a valid shorter chain that startup continues.

**`--reindex`** (index damaged, bodies readable): (1) outside a transaction `PRAGMA foreign_keys = OFF` (only this tool, under the process lock); (2) one transaction: `reindex_in_progress = 1`, delete all rows of `block_index`, `undo`, `gold_events`, reset `utxo` and `chain_state` as above, re-insert genesis (its body stays); (3) for every `block_data` row sorted by the height in its 84 header bytes, run `AcceptBlockHeader` and stages C-D (consensus §8.7); valid blocks get an index row with `HAVE_DATA|DATA_CHECKED` (1,000 per transaction); unparsable or invalid rows are deleted and logged; (4) `PRAGMA foreign_key_check` MUST return nothing; delete bodies without an index row; `PRAGMA foreign_keys = ON`; `reindex_in_progress = 0`; (5) `ActivateBestChain()`. If the process dies in (2)-(4) the flag stays 1 and the next start refuses to run without `--reindex` (exit 6).

### 9.5 Operator runbook

| Symptom | Node behaviour | Operator action |
|---|---|---|
| Power loss or `kill -9` | WAL recovery, tier-2 checks, resumes | none |
| exit 6 "block index corrupt" | stops, writes nothing | `--reindex`; if SQLite itself reports corruption, restore a backup or delete `<network>/` and resync (wallets unaffected) |
| exit 6 "chainstate / undo / tip mismatch" | stops | `--reindex-chainstate` |
| exit 4 "disk space low" | stops cleanly between steps | free space, restart |
| exit 5 / exit 4 "database is locked" | another node / a foreign tool | stop it; close the `sqlite3` shell |
| exit 7 | binary and database versions differ | use the matching binary |
| Moving or copying data | | stop the node; copy the whole `<network>/` folder; never delete `-wal`/`-shm` by hand (SQLite documents loss or corruption) |

---

## 10. Undo data

**Layout:** exactly consensus §4.7: `version u8 = 1`, `blockHash` (32), `spentCount` (CompactSize), then per spent coin `{height u32, flags u8 (bit 0 = coinbase), amount u64, scriptPubKey VarBytes(25)}` in spend order (transaction order, then input order), `goldUndoLen` (CompactSize, at most 65,536), `goldUndo` (gold §9.1; empty if the gold state did not change), `checksum` = `SHA256d` of all previous bytes. One spent input, no gold change = 106 bytes (about 40 bytes per spent coin).

1. Keyed by block hash (old: by height, `blockdb.cpp:43-46`), written once, kept after disconnect and reorg (§8.1 step 6).
2. The CHECK of §6.1 already guarantees first byte `01`, bytes 2-33 equal to the key, length at least 67.
3. `readUndo` returns raw bytes. Parsing uses the bounded `Reader` (consensus §11.1): each read checks remaining length; `spentCount` is checked against `remaining / 39` before any allocation; the checksum is verified before the content is used; trailing bytes are an error.
4. A **missing** row, wrong version, hash, count or flags, bad checksum or bad script is database corruption: `INTERNAL`, FATAL exit 6 (old: missing row = success, `blockdb.cpp:360-376`; unchecked reads, `:322-345`).
5. **Restore order** (consensus §7.7): transactions in reverse; within a transaction outputs are removed first, then inputs restored in reverse spend order, each with its stored `height` and coinbase flag (old: height 0 gave a maturity bypass after reorgs, DB-7). The overlay produces the net `CoinsDelta`; storage never replays raw undo into the table.
6. `VerifyDB` exercises this path for the last 24 blocks at every start.

---

## 11. Threads and locks

### 11.1 Threads (no others may be created; each thread sets a name)

| Thread | Created | Does | Chain-facing calls | Locks |
|---|---|---|---|---|
| main | process | startup, 1 Hz `periodic` (mempool dump every 15 min), shutdown | `LoadChainState`, `periodic` | `cs_activate`, `cs_main` (inside calls) |
| network | `network.cpp:565` | accept, `select`, framing, message handling (P2P spec may split socket and handler threads) | `ProcessNewBlockHeaders`, `ProcessNewBlock`, `AcceptTransaction`, `headersAfter`, `locator`, `openReader` (`getdata`), `tipSequence` | `cs_peers` briefly; chain locks only inside calls; never both |
| miner | `server.cpp:1851` | template, hash loop, submit | `createBlockTemplate`, `ProcessNewBlock`, `tipSequence` | `cs_wallet` (copy script); none while hashing |
| HTTP accept | `server.cpp:782` | `listen_after_bind` | none | none |
| HTTP workers (httplib, 8 or more) | httplib | handlers | `tipSnapshot`, `openReader`, `mempoolSnapshot`, `spendableCoins`, `AcceptTransaction`, `createBlockTemplate` (regtest "mine") | `cs_wallet` (copy); `cs_main` inside calls |

### 11.2 Locks (a thread may take a lock only if its rank is higher than every lock it holds)

| Rank | Lock | Protects | Notes |
|---|---|---|---|
| 5 | `cs_activate` | the `ActivateBestChain` loop; admin invalidate/reconsider | held across steps |
| 10 | `cs_main` | block index, active chain, candidates, failed map, coin read cache, mempool, orphans, gold state, fork alert, IBD flag, sigcache | plain non-recursive mutex |
| 20 | `cs_wallet` (`Server::walletMutex`) | wallet list and file | may span the wallet-file fsync, nothing else |
| 30 | `cs_dbwriter` | the writer connection | inside every `ChainDB` write and `CoinsViewDB` read |
| 31 | `cs_dbpool` | reader pool free list | checkout only; never held while a query runs |
| 40 | `cs_tip` | `TipSnapshot` pointer | copy one `shared_ptr` |
| 50 / 52 / 54 / 56 | `cs_peers` (`peersMutex`) / per-peer send queue / `cs_relay` / `cs_security` | peer table / outbound queue / relay queue / ban list | map and queue operations only |
| 60-90 | `sessionMutex`, `anchorMutex`, `RateLimiter::mutex`, `wsMutex`, `bip_8192.cpp` `g_mutex` | own data | leaf locks |
| 100 | any other mutex | | leaf |

### 11.3 Rules

1. **L1 Order.** With `BUCKS_LOCK_DEBUG` (Debug, sanitizer and test builds) `lock()` aborts with `LOCK ORDER VIOLATION` if a held lock has an equal or higher rank. This also catches taking the same mutex twice (old: `network.cpp:802` then `:465`, a self-deadlock, NET-3).
2. **L2 One chain lock, plain mutex.** Not a shared mutex: the default glibc read-write lock prefers readers, so a stream of readers could starve block connection, and heavy readers bypass `cs_main` anyway. Not recursive: recursion hides ordering bugs, and consensus §8.2 needs `ActivateBestChain` called with the lock free, which rank 5 < 10 enforces.
3. **L3 Entry points lock for themselves** (`LOCKS_EXCLUDED(cs_main)`); internal helpers are `EXCLUSIVE_LOCKS_REQUIRED(cs_main)`; members are `GUARDED_BY(cs_main)`.
4. **L4 Never while holding any lock:** socket I/O, the proof-of-work loop, IPFS or other HTTP-client calls (`server.cpp:1662-1669, 1738-1747`), sleeping, waiting on a foreign condition variable, building large JSON replies. `AssertLockNotHeld(cs_main)` sits in the socket wrappers.
5. **L5 Wallet.** Never call chain or database code under `cs_wallet`; copy address, hash160, script (and the key where signing needs it), release, then query (old: `server.cpp:1190-1203, 1509-1556`).
6. **L6 Copies only.** No raw pointer or reference into a container crosses a lock boundary; return structs, vectors or `shared_ptr<const T>`.
7. **L7 Network.** Only the socket thread(s) touch sockets. Other threads call `Network::queueBroadcast`, `announceBlock`, `announceTx`, which append to a bounded queue (8 MiB per peer; a full queue disconnects the peer). `cs_peers` guards the table only and is never held while receiving, sending, handling a message or calling the chain. The network thread polls `tipSequence()` and `drainRelayQueue()` each loop (at most 100 ms) to announce.
8. **L8 Hashing.** Every `unordered_map`/`unordered_set` keyed by a txid, outpoint or block hash uses `SaltedHasher` (random 128-bit key from `RAND_bytes`), not FNV-1a.
9. **L9 Atomics.** `tipSequence` (release/acquire), the shutdown flag, miner `enabled`, counters (relaxed) and any `Peer` field seen by two threads. A plain `bool` or `std::string` shared between threads is a bug (old: `main.cpp:12`; `Server::minerAddress` read at `server.cpp:1569, 1584`, written at `:1843`).
10. **L10** Condition variables use a ranked `Mutex` and a predicate loop.
11. **L11** After FATAL no thread starts new chain work.
12. **L12 Validation runs on the caller's thread.** One chain lock means one validation at a time; a queue would only add latency. The P2P spec should keep socket I/O on a different thread from message handling so seconds of validation do not stall pings (NET-14).
13. **L13 Readers before the chain lock.** A thread that needs a snapshot tied to the tip checks the reader out first and calls `pin()` after taking `cs_main`; it never waits for the reader pool while holding `cs_main`.

### 11.4 Who calls what

| Function | Called by | Locks, in order | Notes |
|---|---|---|---|
| `ProcessNewBlockHeaders` | network | `cs_main` > `cs_dbwriter` | one `putHeaders` per call |
| `ProcessNewBlock` | network, miner, HTTP "mine" | `cs_main` (AcceptBlock), released; then `cs_activate` > loop { `cs_main` > `cs_dbwriter` } | `cs_main` again briefly for the result |
| `AcceptTransaction` | network, HTTP send | `cs_main` (> `cs_dbwriter` for coin reads) | signatures checked under `cs_main`, as in Bitcoin Core |
| `createBlockTemplate` | miner, HTTP "mine" | `cs_dbpool` (checkout, released at once); `cs_main` (select, stages A-D, `pin()`); released; stage E on the snapshot, no lock | §11.5 |
| `tipSnapshot` | any | `cs_tip` | |
| `openReader()` and its methods | HTTP, network (`getdata`) | `cs_dbpool` at checkout | |
| `headersAfter`, `locator`, `mempoolSnapshot`, `mempoolGet` | network, HTTP | `cs_main` | copies |
| `spendableCoins` | HTTP send | `cs_dbpool` (checkout); `cs_main` (`pin()`, copy the set of outpoints spent by the mempool); released; query the snapshot, filter, check maturity, no lock | |
| wallet list / signing | HTTP, miner | `cs_wallet` alone | |
| `periodic`, mempool dump | main | `cs_main` (copy pointers); none while writing the file | |

### 11.5 Miner: template snapshot and abort on a new tip

```
miner thread                                  ChainState                           other threads
copy coinbase script (cs_wallet, released)
tpl = createBlockTemplate(opts) ──────────►   check out a reader (may wait; no lock held)
                                              LOCK cs_main
                                                tip, MTP, bits, minTime; select txs (size, sigops, finality); stages A-D
                                                reader.pin(): BEGIN + SELECT best block, equals tip
                                              UNLOCK cs_main
                                              stage E (coins + signatures) on an overlay over the snapshot, no lock;
                                              a bad tx i > 0: LOCK, remove it from the mempool, rebuild once
◄── BlockTemplate{block copy, tipSequence}
hash loop on a local header copy; every 65,536 nonces (about 20-60 ms):
   shutdown or disabled?            -> return
   chain.tipSequence() != tpl.seq   -> new template      ◄────── committed step: tipSequence++ (release)
   60 s elapsed?                    -> new template
found: ProcessNewBlock(block, LOCAL, requested=true) -> network.announceBlock(hash)    (no lock held)
```

The reader is checked out before `cs_main` is taken, so a thread holding `cs_main` never waits for the pool (four slow HTTP clients could otherwise stall the chain lock for 5 s). The snapshot is pinned while `cs_main` is held, and commits also run under `cs_main`, so it is exactly the tip the template was built on. Stage E outside the lock (R2) removes about one second of lock time for a full template. A stale template is harmless: `ProcessNewBlock` stores it as a side block.

### 11.6 How HTTP handlers read state

| Endpoint family | Source | Chain locks |
|---|---|---|
| `/api/blockchain/info`, `/api/economy/status`, `/api/gold/status`, `/api/mining/status` | `tipSnapshot()` (height, bits, work, supply, gold state, fork alert); atomics for the miner | none (`cs_tip` only) |
| `/api/blocks/recent`, `/hash/..`, `/height/..` | one `ChainReader`: `activeRange`, `indexRecord`, `activeHashAt` (header fields and `tx_count` come from `block_index`; a body is read only to list transactions) | none |
| `/api/transactions` | one `ChainReader`; at most 200 active-chain blocks per request, bodies one at a time, at most `limit` entries (old: unbounded walk, `server.cpp:1316-1346`) | none |
| `/api/wallets`, `/api/wallets/primary` | copy wallets under `cs_wallet`; then `summaryByHash160` / `coinsByHash160` on one `ChainReader` | none |
| `/api/transactions/send` | copy wallet; `spendableCoins`; build and sign with no lock; `AcceptTransaction`; `announceTx` | see §11.4 |

### 11.7 Required changes in the current code

| Where | Problem | Fix |
|---|---|---|
| `network.cpp:802-820` | `peersMutex` held over `recv`, `processBuffer`, `handleMessage`, `chain.addBlock` (`:433`) | phase 1 under `cs_peers`: read ready sockets, cut complete frames; phase 2, no lock: handle messages; replies via per-peer queues |
| `network.cpp:215-229, 844-851` | blocking `::send` under locks from any thread | queues drained by the socket thread with non-blocking sends (transport details: P2P spec, NET-5) |
| `network.cpp:465, 945` | handlers relock `peersMutex` (NET-3) | disappears with phase 2; rank check as safety net |
| `network.cpp:301-321, 355, 376, 398, 412` | unlocked `chain.height()`, `getBlock`, `getBlockByHeight` | `ChainState` read calls (§7.4) |
| `server.cpp:1133-1146, 1167-1180` | `tip()`, `getBlockByHeight` pointers | `ChainReader` |
| `server.cpp:1190-1203, 1509-1556` | `walletMutex` held across balance scans and the UTXO scan | rule L5 |
| `server.cpp:1387-1391, 1750-1753` | `network->broadcast` from HTTP threads | `announceTx` / `announceBlock` |
| `server.cpp:1671-1676, 1716-1717, 1864-1875, 1894-1895, 1920` | tip pointer, mempool reference, unlocked tip polling | `createBlockTemplate`, `tipSequence` |
| `server.cpp:1569, 1584` vs `:1843` | `minerAddress` race (read by HTTP threads, written under `walletMutex`) | `std::atomic<std::shared_ptr<const std::string>>`, or read under `cs_wallet` |
| `main.cpp:12-16` | plain `bool` in signal handler | `std::atomic<bool>` |
| `mempool.h:103` | own mutex | removed |
| `utxodb.cpp`, `blockdb.cpp` | unlocked transactions on a shared handle | classes deleted (S14) |
| `crypto.h:138-149`, `mempool.cpp:12-18` | FNV-1a / XOR hashers | `SaltedHasher` |
| `network.h:185` | `getChain()` returns `Blockchain&` | returns `ChainState&` |

### 11.8 Shutdown order

(1) Set the atomic flag; main wakes. (2) `Server::stop` (joins accept thread and worker pool; in-flight handlers finish because they take only bounded locks). (3) Stop and join the miner (checks the flag every 65,536 nonces). (4) Stop and join the network thread, close sockets. (5) Only main runs: write `mempool.dat`, `setCleanShutdown(true)`, `checkpointTruncate()`, destroy readers then the writer, release `.lock`. (6) Exit with the stored code (API-21).

### 11.9 Tooling

- **`include/sync.h`** (prototype compiled and run for this spec): `Mutex` wraps `std::mutex`, takes `(rank, name)`, carries clang capability attributes. `LOCK(cs)` builds its variable name with `__COUNTER__` (`__LINE__` fails when two locks share a line). With `BUCKS_LOCK_DEBUG` a `thread_local` vector of held locks implements the rank check and `AssertLockHeld/NotHeld`; otherwise it is a zero-cost wrapper. Macros: `GUARDED_BY`, `EXCLUSIVE_LOCKS_REQUIRED`, `LOCKS_EXCLUDED`, `ACQUIRE`, `RELEASE`, `NO_THREAD_SAFETY_ANALYSIS` (empty on non-clang).
- **CMake options:** `BUCKS_LOCK_DEBUG` (default ON for Debug and sanitizer builds); `BUCKS_TSA` (clang `-Wthread-safety -Wthread-safety-beta -Werror=thread-safety`; tested: flags both an unguarded read and a call without the lock); `BUCKS_FAULT_INJECTION` (tests only; a hygiene test fails if `build.sh` defines it). `BUCKS_SANITIZE=thread` already exists (`CMakeLists.txt:18-52`).
- **ThreadSanitizer notes.** Data-race detection was verified on this Mac (Apple clang 21, arm64). TSan's lock-order detector did not report an inversion there, so the rank check (L1) is the lock-order tool on every platform and TSan is the data-race tool. With the default symbolizer the report path hung; set `TSAN_OPTIONS=symbolize=0` and resolve addresses with `atos`. On Linux, older TSan runtimes may abort at start with high `vm.mmap_rnd_bits`; the CI job runs `sudo sysctl vm.mmap_rnd_bits=28`.

---

## 12. Memory bounds and caches

| Item | Bound | Notes |
|---|---|---|
| Block index | about 300 bytes per header incl. map overhead (1M headers = 300 MB) | headers only; each costs at least 2^32 hashes (consensus §8.6); bodies never loaded at startup (old: `blockdb.cpp:106-140`) |
| Active chain, candidates, failed map | 8 bytes per block; entries with data and at least tip work; 1,000 reject states | |
| Step overlay | extension: soft 250,000 entries (about 42 MB); reorg: hard 6,000,000 (about 1 GB) | about 170 bytes per entry; about 110 with a 25-byte array (R4) |
| Coin read cache | `0.4 * dbcache` (51 MiB); cleared completely when full | write-through, never dirty, so clearing is always safe: this is the flush policy |
| SQLite page cache | writer `0.5 * dbcache` (64 MiB); 4 readers x 8 MiB | `--dbcache=<MiB>`, default 128, range 16-4096 |
| Signature cache | 524,288 entries (about 32 MiB), two generations; the older is dropped when the newer fills | key = `SHA256` of salt, sighash, pubkey, signature. The sighash commits to genesis hash, amounts and scripts (consensus §5.1), so a hit is sound in any context. Filled by mempool acceptance; a block of known transactions then connects in milliseconds. |
| Mempool | 100 MB serialized (consensus §3.2), about 250 MB heap | |
| Orphan transactions | 100 x 100 KB | |
| Peer buffers | at most 2 MiB receive + 2 MiB send per peer (125 peers: 500 MB) | today 4 MiB (`network.h:32`); the P2P spec sets final values |
| HTTP | body at most 1 MiB (`server.h:56`); replies bounded by `limit` (at most 1,000) and block ranges (at most 200) | |
| Block bodies in RAM | only blocks of the current step plus transient reads | |

Typical resident memory with defaults: about 400 MB plus mempool and peers. **Disk:** UTXO about 150 bytes per coin; undo about 40 bytes per spent coin; index about 300 bytes per block; bodies = block sizes (at most 144 MB/day if every block is full); WAL at most 16 MiB plus the largest step. **Constants:** reader pool 4, reader wait 5 s, session deadline 30 s, full-scan limit 5,000,000 coins, mempool dump every 15 min.

---

## 13. Mempool persistence

`mempool.dat` replaces the `mempool` table (`blockdb.cpp:238-298`).

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `BKMP` |
| 4 | 4 | version u32 = 1 |
| 8 | 32 | genesis hash (a file from another network is ignored) |
| 40 | 8 | count u64, at most 200,000 |
| 48 | per entry | `entryTime` i64, `txLen` u32 (at most 100,000), transaction bytes (consensus §4.3) |
| end - 32 | 32 | `SHA256d` of all previous bytes |

1. **Dump** at shutdown and every 15 minutes if changed. Under `cs_main` copy only `{entryTime, shared_ptr<const Transaction>}`; release; write outside any lock to `mempool.dat.new` (`0600`), `fsync`, `rename`, fsync the directory.
2. **Load** (startup step 11): file size at most `MAX_MEMPOOL_BYTES + 16 MiB`; bounded `Reader`; bad magic, version, genesis or checksum means the whole file is ignored (logged). Every transaction goes through `AcceptTransaction` (consensus §9.7); failures are dropped.
3. **Why a file:** mempool writes would compete with block commits for the single writer, rewriting 100 MB would block chain work, and a damaged mempool must never affect chain data.

---

## 14. Finding to section map

| Finding | Resolved in |
|---|---|
| DB-4 no lock, shared SQLite transaction state | §5 (one writer), §11 |
| DB-5 connect spread over two files and several autocommits | §6, §8.1, §8.3 |
| DB-7 undo: no height, wrong order, unbounded parse, missing row ok | §10, §6.1 (CHECKs), consensus §4.7, §7.7 |
| DB-11 disk and memory chains diverge after reorg | §6.1 (`IN_MAIN_CHAIN` + unique index), §8.1 step 3; mempool re-add: consensus §9.6 |
| DB-14 startup trusts the database | §9 |
| DB-15 whole chain in RAM; address scans | §12, §6.1 (`utxo_by_hash160`), §6.5 |
| DB-17 paths, permissions, PRAGMAs | §4, §5 |
| CON-6 side blocks overwrite main blocks | §6.1 (hash-keyed, write-once), §8.2 |
| CON-9 reorg not atomic (storage side) | §8.1, §8.5 with consensus §8.4 |
| CON-10 no chain lock | §11 |
| CON-18 not crash-safe | §8, §9 |
| NET-6 unlocked shared state across threads | §11.3 L1-L7, §11.7 |
| API-7 HTTP threads race with P2P and miner | §11.6, §11.7 |
| API-8 wallet lock held through proof of work (pointer part fixed in Phase 1) | §11.3 L4-L5 |
| GLD-4 reserve persisted outside the block commit | §6.1 (`gold_state`, `gold_events`), §8.1 steps 3, 6, 7 |
| Related: DB-1, DB-2 | §6.1, §8.2 (validation: consensus spec) |
| DB-6 `INSERT OR REPLACE` | §6.2 item 5, §8.1 step 5 |
| DB-8 reserve is a mutable counter | §6.3, §8.1, §9.3 |
| DB-10 unvalidated mempool reload | §13 |
| DB-16 events kept after reorg, no block hash | §6.1, §8.1 step 3 |
| NET-3 self-deadlock | §11.3 L1, §11.7 |
| NET-5, NET-14 blocking I/O and heavy work under the global lock | §11.3 L4, L7, L12 (transport: P2P spec) |
| OPS-9 no CI | §16.5 |
| OPS-18 outdated SQLite | §5.1 |
| OPS-19 signal flag | §4 rule 10, §11.3 L9 |
| API-21 miner lifecycle | §11.8 |

---

## 15. Implementation plan

Rules for every step: builds with `-Wall -Wextra`; all old and new tests pass, also under `-DBUCKS_SANITIZE=address,undefined`; new files go into `CMakeLists.txt` and `build.sh` (checked by `build_hygiene`); no commits by agents. "C-n" = step n of the consensus plan.

| # | Step | Files | Needs | Tests |
|---|---|---|---|---|
| S1 | **Lock primitives:** `Mutex`, ranks, `LOCK`, TSA macros, `AssertLockHeld/NotHeld`; options `BUCKS_LOCK_DEBUG`, `BUCKS_TSA` | new `include/sync.h`, `tests/unit/test_sync.cpp`, `CMakeLists.txt` | - | LCK-1, LCK-2 |
| S2 | **File hygiene:** move `writePrivateFileAtomic`, `ensurePrivateDir`, `lockDataDir` out of `server.cpp:182-326` with their behaviour unchanged; `DataDir` adds the network subdirectory, database-file `lstat` checks, free-space check, legacy warning and exit codes, and is built first in `main()`; atomic shutdown flag | new `include/fsutil.h`, `src/fsutil.cpp`, `include/datadir.h`, `src/datadir.cpp`; `server.cpp`, `main.cpp` | C-7 for `--network` | FS-1 to FS-3 |
| S3 | **SQLite upgrade and flags:** replace `src/sqlite3.c`, `include/sqlite3.h` (3.51.3 or newer); definitions in CMake and `build.sh:59`; `THIRD_PARTY.md`; version asserts | those files, `tests/build_hygiene.cmake`, `tests/unit/test_build_config.cpp` | - | REC-4 |
| S4 | **SQLite helpers and schema v2:** `Db`, `Stmt`, `WriteTxn`, §5.2 setup, §6.1 creation, identity and version checks, empty migration registry, SQLite log hook | new `include/sqlite_util.h`, `src/sqlite_util.cpp`, `include/chaindb_schema.h` | S3 | DBT-1, DBT-2 |
| S5 | **`ChainStore` and `MemoryChainStore`** (shared with consensus step 12; includes a failure-injecting decorator for CS-6 and GC-4) | new `include/chainstore.h`, `tests/util/memory_chainstore.h` | C-8, C-9 | consensus CS-* |
| S6 | **`ChainDB` writer side:** `initGenesis`, `putHeaders`, `putBlock`, `putStatus`, `commitStep` with statistics, `loadState`, `loadBlockIndex`, `readBlock`, `readUndo`, `CoinsViewDB`, `scanStats`, fault points | new `include/chaindb.h`, `src/chaindb.cpp` | S4, S5, C-8 | DBT-3 to DBT-9 |
| S7 | **Read side:** reader pool, `ChainReader`, address and gold queries, deadlines, `backupTo`, `GoldIndexReader` | `src/chaindb_reader.cpp` | S6 | DBT-5, DBT-10, FS-4 |
| S8 | **Startup and recovery:** §9 sequence, tiers, unclean flag, `--reindex-chainstate`, `--reindex`, `--checkdb`, `--checkblocks`; hook into `LoadChainState` | `src/chaindb_recovery.cpp`, `chainstate.cpp`, `main.cpp` | S6, S7, C-12 | REC-1 to REC-5 |
| S9 | **Crash harness:** `FAULT_POINT`, `bucks_crash_child`, runner, WAL-truncation test | new `include/fault.h`, `tests/crash/*` | S6, S8 | CRASH-1 to CRASH-9 |
| S10 | **Threading in `ChainState`/`Mempool`/`Miner`:** `cs_activate` + `cs_main`, `TipSnapshot`, `tipSequence`, reader facade replacing raw pointers, template snapshot protocol, `SigCache`, `SaltedHasher`, mempool mutex removed | `chainstate.cpp`, `mempool.cpp`, `miner.cpp`, `crypto.h`, new `sigcache.h`, `hasher.h` | C-12 to C-14, S1, S7 | CONC-3 to CONC-5, CONC-7, MEM-1 to MEM-3 |
| S11 | **Network and server fixes** (§11.7): phased socket loop, send and relay queues, wallet-lock copies, atomics, signal flag, shutdown order | `network.cpp/.h`, `server.cpp/.h`, `main.cpp`, `wallet.cpp` | S10, C-16 | CONC-1, CONC-2, CONC-6 |
| S12 | **`mempool.dat`** | new `mempool_file.h`, `.cpp` | C-13, S2 | MP-9, CRASH-7 |
| S13 | **CI:** `tools/ci.sh <job>`, GitHub workflow, empty TSan suppression file, nightly crash job | new `tools/ci.sh`, `.github/workflows/ci.yml`, `tests/concurrency/tsan.supp` | S9 to S11 | §16.5 |
| S14 | **Delete old storage:** `blockdb.cpp/.h`, `utxodb.cpp/.h`; `.gitignore`: `*.sqlite-wal`, `*.sqlite-shm`, `*.dat`; deleted-symbol list: `BlockDB`, `UTXODB`, `saveGoldReserve` | listed | C-16 | HYG-1 |

S1-S4 can start at once. S5 is needed by consensus step 12; S6-S9 deliver consensus step 15 and gold step G5. Old data files cannot be read from S6 on; nodes use fresh datadirs.

---

## 16. Test plan

**Infrastructure.** gtest; the consensus `TestChain` (regtest, mock clock) drives chain tests. New executables: `crash_tests` (label `crash`) with helper `bucks_crash_child`; `concurrency_tests` (label `concurrency`). `FAULT_POINT("name")` exists only with `BUCKS_FAULT_INJECTION`: `BUCKS_FAULT=name[:n]` makes the n-th hit call `raise(SIGKILL)` (no destructors, like `kill -9`); `BUCKS_FAULT_THROW=name[:n]` throws `StoreFatal`. Points: `commit.begin.before`, `commit.disconnect.after_status`, `commit.coins.after_delete`, `commit.coins.after_insert`, `commit.undo.after_insert`, `commit.connect.after_status`, `commit.state.after_write`, `commit.before_commit`, `commit.after_commit`, `accept.body.after_insert`, `accept.body.after_commit`, `headers.after_insert`, `mempool.tmp_written`, `mempool.renamed`, `reset.after_drop`, `migrate.after_ddl`. "Old behaviour" says what the current code does, so each test would fail or crash on it.

### 16.1 Locks and files

| ID | Test | Setup | Action | Expected | Old behaviour caught |
|---|---|---|---|---|---|
| LCK-1 | `Sync.RankOrder` (death test) | `cs_main`, `cs_wallet`, `cs_peers` | chain then wallet; wallet then chain; same mutex twice | first passes, others abort `LOCK ORDER VIOLATION` | `network.cpp:465` relocks `peersMutex` and hangs (NET-3) |
| LCK-2 | `Sync.GuardedAccessWontCompile` | clang `try_compile`, `-Werror=thread-safety` | read a `GUARDED_BY(cs_main)` member without the lock | compile error | unlocked `blocks`/`mempool` (DB-4) |
| FS-1 | `DataDir.ModesAndOwner` | umask 022; one existing datadir with mode 0755; a new datadir; a symlinked `chain.sqlite` | start each | the 0755 directory is refused with the `chmod 700` hint (exit 4); the new datadir gets dirs 0700 and db, wal, shm, `mempool.dat` 0600; the symlink is refused (exit 4) | files 0644 (DB-17) |
| FS-2 | `DataDir.SingleInstance` | node A running | start B on the same datadir; `kill -9` A; start B | B exits 5 within 1 s, before it opens the database; after A dies B starts | two nodes on one `./blockchain.db`; the existing lock is taken only after the chain was opened |
| FS-3 | `DataDir.PathsAndWallet` | other working directory; `wallets.json` with known hash | start; `--reindex`; `--reindex-chainstate`; delete `<network>/` | same datadir, nothing in the working directory; wallet file byte-identical | CWD-relative DB (`blockchain.h:39`) |
| FS-4 | `Backup.VacuumInto` | writer commits 50 steps meanwhile | `backupTo` | opens, `quick_check` ok, equals one committed state, mode 0600, no `.partial` | - |

### 16.2 Database and store

| ID | Test | Setup | Action | Expected | Old behaviour caught |
|---|---|---|---|---|---|
| DBT-1 | `Schema.IdentityAndVersion` | new dir; file with `user_version = 3`; foreign SQLite file | open | v2 created with `application_id`; v3 exit 7; foreign exit 6 | no version or identity |
| DBT-2 | `Schema.DatabaseRejectsBadData` | schema v2 | 14 bad statements: 31-byte hash; `prev_hash` differing from header; unknown parent; status 0; `CONNECTED` without `DATA_CHECKED`; second main block at a height; `FAILED` on a main row; header update; second `tx_count` write; non-P2PKH coin; amount 0 and above `MAX_MONEY`; undo naming another block; wrong `chain_state` type | all rejected | `INSERT OR REPLACE`, anything accepted (DB-1, DB-6) |
| DBT-3 | `Store.CoinConflictIsFatal` | step INSERTing an existing outpoint as `fresh` | `commitStep` | `StoreFatal`; database and statistics unchanged | silent overwrite (DB-6) |
| DBT-4 | `Store.StatsMatchScan` | 2,000 random steps with spends, adds, upserts, reorgs | compare statistics with `scanStats()` every 50 steps | equal | no statistics; full `COUNT(*)` |
| DBT-5 | `Store.AddressIndex` | 1,000,000 coins, 50,000 addresses | page through one address; balance; `EXPLAIN QUERY PLAN` | exact set; plan uses `utxo_by_hash160`; under 5 ms | full scan with Base58 per row (DB-15) |
| DBT-6 | `Undo.CorruptionDetected` | undo of a block with 3 spends | flip each byte; truncate; wrong version, count, hash; missing row | CORRUPT or absent: FATAL path, no crash, ASan clean | out-of-bounds read; missing row ok (DB-7) |
| DBT-7 | `Undo.RestoresHeightAndFlag` | block spends a coinbase coin of height h | disconnect | restored coin has height h and coinbase flag | height 0 (DB-7) |
| DBT-8 | `Store.GoldAtomicWithCoins` | block with a gold event | throw at `commit.state.after_write`; then SIGKILL there | after restart coins, `gold_state`, `gold_events`, best block all at the old tip; without the fault all at the new tip | reserve saved first, block row last (GLD-4, DB-5) |
| DBT-9 | `Store.GoldEventsFollowReorg` | event in A5; reorg to B5-B6 without it; back | rows after each | rows only while the block is main-chain; same event may return | rows never deleted (DB-16) |
| DBT-10 | `Store.ReaderSnapshot` | reader pinned at tip T; writer commits 10 steps | read; open a new reader; time the commits | old reader sees T until closed; new one sees the newest; commit time unaffected | one connection, global mutex |

### 16.3 Crash and recovery

| ID | Test | Setup | Action | Expected | Old behaviour caught |
|---|---|---|---|---|---|
| CRASH-1 | `Crash.EachCommitPoint` | child applies a 3-block extension step | for every `commit.*`/`accept.*` point: SIGKILL there; parent opens the datadir | state exactly old up to `commit.before_commit`, new at `commit.after_commit`; startup checks pass; finishing the sync gives the same tip, digest and gold state as an uninterrupted run | UTXO N with block table N-1 (DB-5, CON-18) |
| CRASH-2 | `Crash.EachPointOfReorg` | step with 2 disconnects, 3 connects | same | same | partial reorg persisted (CON-9) |
| CRASH-3 | `Crash.WalTruncation` | child runs 200 steps, killed | copy files; cut `chain.sqlite-wal` at 100 offsets (frame boundaries +/-1, random) | always opens at the tip of some step k; scan, invariants, `quick_check` pass | - |
| CRASH-4 | `Crash.KillLoop` | child connects blocks without pause | 100 times: kill after 20-300 ms, reopen with tier 2, continue | no check fails; final state equals a replay | - |
| CRASH-5 | `Crash.BodyWithoutConnect` | kill after `accept.body.after_commit` | restart | the block is connected at startup | - |
| CRASH-6 | `Crash.DiskFull` | `PRAGMA max_page_count` just above file size | commit a big step | `SQLITE_FULL`, rollback, child exits 4; state unchanged; resumes after raising the limit | ignored `saveBlock` failure (`blockchain.cpp:447-452`) |
| CRASH-7 | `Crash.MempoolDump` | kill at `mempool.tmp_written` and `mempool.renamed` | restart | old or new file loads; leftover `.new` removed | - |
| CRASH-8 | `Crash.MigrationInterrupted` | test migration 2 to 3 with a fault inside | kill; rerun | rolled back; second run succeeds; backup exists | - |
| CRASH-9 | `Crash.ReindexInterrupted` | kill during `--reindex` step 3 | start without, then with the flag | exit 6; then completes at the same tip | - |
| REC-1 | `Startup.CorruptRows` | flip a byte in: a header, a `chain_work`, the best-block key, an `IN_MAIN_CHAIN` flag, a coin, an undo blob, `gold_state` | start | exit 3 or 6, failing check named, nothing written | silent new chain on old UTXO set (DB-14) |
| REC-2 | `Startup.UncleanTriggersDeepCheck` | kill -9 a node; separately a clean stop | start with the scan limit lowered | after the kill tier 2 runs (log lines); after a clean stop tier 1 | - |
| REC-3 | `Startup.LegacyFilesIgnored` | old `blockchain.db`, `utxodb.sqlite` in the datadir | start | warning; mtimes unchanged | - |
| REC-4 | `Startup.SqliteVersion` | test hook faking an old `sqlite3_libversion_number` | start | exit 7 | WAL-reset bug exposure (OPS-18) |
| REC-5 | `Startup.ReindexChainstate` | chain with reorgs and gold events | run the reset | same tip, digest, `gold_state`, `gold_events` | no reindex (GLD-4) |

### 16.4 Concurrency and memory

| ID | Test | Setup | Action | Expected | Old behaviour caught |
|---|---|---|---|---|---|
| CONC-1 | `Locks.NoChainLockDuringSocketIO` | two regtest nodes on loopback; `AssertLockNotHeld(cs_main)` in the socket wrappers | relay blocks and transactions for 10 s | no abort | `network.cpp:802-820, 844-851` |
| CONC-2 | `Locks.WalletLockNotAcrossChain` | `BUCKS_LOCK_DEBUG`; a wallet exists | call every API endpoint once | no abort | `server.cpp:1190-1203, 1509-1556` |
| CONC-3 | `Concurrency.StressMixed` | regtest; 3 "network" threads feeding pre-mined blocks of 2 competing branches in random order; 1 miner; 6 "HTTP" threads (snapshot, `activeRange`, address queries, mempool snapshot, `spendableCoins`); 1 wallet thread sending; `BUCKS_STRESS_SECONDS` (10 s CI, 60 s nightly) | run | no crash or sanitizer report; at the end tip = most work, statistics = scan, chain linked, mempool entries valid; every HTTP read self-consistent (returned ranges linked, heights monotonic per reader); every template's `prevHash` equals its pinned snapshot's best block | use-after-free on `tip()`, `getBlockByHeight`, `getMempool` (NET-6, API-7, CON-10) |
| CONC-4 | `Concurrency.ReaderDoesNotBlockWriter` | a reader session held 20 s | writer commits 100 steps | median commit time within 2x of the no-reader case; WAL back under 64 MiB within 3 commits after the reader closes | one connection, global mutex (DB-4) |
| CONC-5 | `Concurrency.MinerAbortsOnNewTip` | regtest, huge target, hash loop running | commit a competing block from another thread | new template on the new tip within 250 ms; no stale submit | `server.cpp:1920` polls an unlocked pointer |
| CONC-6 | `Concurrency.ShutdownOrder` | node under HTTP load with miner and peers | request stop | exit within 10 s, code 0, `clean_shutdown = 1`, no TSan report | unjoined threads (API-21) |
| CONC-7 | `Hash.SaltedHasherResists` | 50,000 txids crafted to collide under FNV-1a | insert into the mempool index | O(1) average; total under 200 ms | FNV-1a and XOR hashers (`crypto.h:138`, `mempool.cpp:12`) |
| MEM-1 | `Memory.IndexOnlyHeaders` | counting store with 200,000 headers, 2,000 bodies | startup | `readBlock` called only for the last 24 blocks; under 400 bytes per header | whole chain in RAM (DB-15) |
| MEM-2 | `Memory.StepLimits` | `STEP_SOFT_MAX_COINS` lowered to 50 | connect 40 blocks; then a reorg above the hard limit | extension ends in several commits with the same final state; the oversized reorg is refused with a fork alert, nothing changes | - |
| MEM-3 | `Memory.Caches` | coin cache limit 1,000; sigcache capacity 8 | 10,000 lookups; 100 signature triples; flip one signature byte | results equal uncached; sizes never above the limits; a changed triple misses | - |

### 16.5 CI jobs (OPS-9: there is no CI today)

New: `tools/ci.sh <job>` (runs on a Mac or Linux), `.github/workflows/ci.yml` calling it, `tests/concurrency/tsan.supp`. A suppression needs a linked issue and an expiry date; the file starts empty.

| Job | Setup | Action | Expected | Would have caught |
|---|---|---|---|---|
| `tsan` | `cmake -DBUCKS_SANITIZE=thread -DBUCKS_LOCK_DEBUG=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo`; `TSAN_OPTIONS="halt_on_error=1 history_size=5 suppressions=<repo>/tests/concurrency/tsan.supp"` (+ `symbolize=0` on macOS); `BUCKS_STRESS_SECONDS=10` | `ctest -L "concurrency\|integration" --timeout 900 --output-on-failure` | zero reports, exit 0 | races on `blocks`, `blockIndex`, `mempool`, `goldReserve` (`blockchain.h:90-100`); `running` (`main.cpp:12`); `minerAddress` (`server.cpp:1584` vs `:1843`); `Peer` fields |
| `tsa` | clang 14 or newer, `-DBUCKS_TSA=ON` | build only | zero thread-safety warnings | guarded member read without the lock |
| `asan-stress` | `-DBUCKS_SANITIZE=address,undefined` | `ctest -L concurrency` | no report | use-after-free of `tip()` pointers after `blocks.push_back` |
| `lockdebug` | Debug build, `BUCKS_LOCK_DEBUG=ON` | whole `ctest` | no abort | rank violations, self-deadlock (NET-3) |
| `crash-nightly` | RelWithDebInfo, `BUCKS_FAULT_INJECTION=ON` | `ctest -L crash` with 1,000 kill-loop iterations | pass | DB-5, CON-18 |
| `hygiene` | - | `build_hygiene` | pass | `build.sh` drift (SQLite flags, fault injection), `.gitignore` gaps |

---

## 17. Open questions for the owner

Each has a default this spec already uses; implementation can start without an answer.

| # | Question | Default |
|---|---|---|
| Q1 | **Disk growth.** Keep every block body forever (up to 144 MB/day if all blocks are full, about 52 GB/year worst case; a young chain is far smaller)? Pruning would be a later feature. | Keep all; no pruning. |
| Q2 | **Transaction index.** Do you want "find a transaction by id" and per-address history (block explorer, wallet history)? It costs about 40 bytes per transaction and is added by a schema migration (version 3), so it can wait. | Not in v2. |
| Q3 | **Windows nodes.** The repo has PowerShell scripts and `WIN32` branches. This spec defines the Windows equivalents (`LockFileEx`, no file modes), but crash tests and CI run only on macOS and Linux. Is a supported Windows node needed at launch? | Best effort; no Windows CI. |
| Q4 | **CI host.** Is GitHub Actions acceptable for the jobs of §16.5, with the crash job nightly? `tools/ci.sh` also runs locally. | Yes. |

Defaults not asked: free-space floor 512 MiB; no periodic chain backups (only before migrations); `--dbcache` 128 MiB.
