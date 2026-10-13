#include "chaindb.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <map>

#include "consensus.h"
#include "pow.h"
#include "serialize.h"
#include "sqlite3.h"

namespace Bucks {


static_assert(SQLITE_VERSION_NUMBER >= 3051003,
              "storage spec requires SQLite 3.51.3 or newer (see THIRD_PARTY.md)");
namespace {

constexpr int kSchemaVersion = 2;
constexpr int64_t kApplicationId = 1112886099;   // 0x42554B53, "BUKS"

// Schema v2 (docs/design/storage-concurrency.md section 6.1).
const char* kSchemaSql = R"SQL(
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
  prev_hash  BLOB    REFERENCES block_index(hash),
  height     INTEGER NOT NULL CHECK (height BETWEEN 0 AND 4294967295),
  header     BLOB    NOT NULL CHECK (length(header) = 84),
  chain_work BLOB    NOT NULL CHECK (length(chain_work) = 32),
  status     INTEGER NOT NULL CHECK (status BETWEEN 1 AND 255),
  tx_count   INTEGER NOT NULL DEFAULT 0 CHECK (tx_count BETWEEN 0 AND 15384),
  size       INTEGER NOT NULL DEFAULT 0 CHECK (size BETWEEN 0 AND 1000000),
  CHECK ((height = 0 AND prev_hash IS NULL
          AND substr(header, 9, 32) = x'0000000000000000000000000000000000000000000000000000000000000000')
      OR (height > 0 AND prev_hash = substr(header, 9, 32))),
  CHECK ((status & 4)   = 0 OR (status & 2)  != 0),
  CHECK ((status & 8)   = 0 OR (status & 4)  != 0),
  CHECK ((status & 16)  = 0 OR (status & 8)  != 0),
  CHECK ((status & 8)   = 0 OR (status & 16) != 0 OR height = 0),
  CHECK ((status & 32)  = 0 OR (status & 8)  != 0),
  CHECK ((status & 192) = 0 OR (status & 32) = 0)
) STRICT, WITHOUT ROWID;

CREATE UNIQUE INDEX block_index_main_height ON block_index(height) WHERE (status & 32) = 32;

CREATE TRIGGER block_index_frozen BEFORE UPDATE OF hash, prev_hash, height, header, chain_work ON block_index
BEGIN SELECT RAISE(ABORT, 'block_index: header fields are immutable'); END;
CREATE TRIGGER block_index_size_once BEFORE UPDATE OF tx_count, size ON block_index WHEN OLD.tx_count != 0
BEGIN SELECT RAISE(ABORT, 'block_index: tx_count and size are write-once'); END;

CREATE TABLE block_data (
  hash BLOB NOT NULL PRIMARY KEY REFERENCES block_index(hash),
  data BLOB NOT NULL CHECK (length(data) BETWEEN 85 AND 1000000)
) STRICT;
CREATE TRIGGER block_data_write_once BEFORE UPDATE ON block_data
BEGIN SELECT RAISE(ABORT, 'block_data is write-once'); END;

CREATE TABLE undo (
  block_hash BLOB NOT NULL PRIMARY KEY REFERENCES block_index(hash),
  data       BLOB NOT NULL CHECK (length(data) >= 67 AND substr(data, 1, 1) = x'01'
                                  AND substr(data, 2, 32) = block_hash)
) STRICT;
CREATE TRIGGER undo_write_once BEFORE UPDATE ON undo
BEGIN SELECT RAISE(ABORT, 'undo is write-once'); END;

CREATE TABLE gold_events (
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
)SQL";

// Kept separate: --reindex-chainstate drops and recreates exactly this.
const char* kUtxoSql = R"SQL(
CREATE TABLE utxo (
  txid        BLOB    NOT NULL CHECK (length(txid) = 32),
  vout        INTEGER NOT NULL CHECK (vout BETWEEN 0 AND 4294967295),
  amount      INTEGER NOT NULL CHECK (amount BETWEEN 1 AND 2100000000000000),
  script      BLOB    NOT NULL CHECK (length(script) = 25 AND substr(script, 1, 3) = x'76a914'
                                      AND substr(script, 24, 2) = x'88ac'),
  height      INTEGER NOT NULL CHECK (height BETWEEN 0 AND 4294967295),
  is_coinbase INTEGER NOT NULL CHECK (is_coinbase IN (0, 1)),
  hash160     BLOB    GENERATED ALWAYS AS (substr(script, 4, 20)) VIRTUAL,
  PRIMARY KEY (txid, vout)
) STRICT, WITHOUT ROWID;
CREATE INDEX utxo_by_hash160 ON utxo(hash160, txid, vout);
)SQL";

// Prepared statement with RAII finalisation.
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &s_, nullptr) != SQLITE_OK) {
            error_ = sqlite3_errmsg(db);
            s_ = nullptr;
        }
    }
    ~Stmt() { if (s_) sqlite3_finalize(s_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    bool ok() const { return s_ != nullptr; }
    const std::string& error() const { return error_; }
    void reset() { sqlite3_reset(s_); sqlite3_clear_bindings(s_); }

    void blob(int i, const void* p, size_t n) { sqlite3_bind_blob(s_, i, p, int(n), SQLITE_TRANSIENT); }
    void blob(int i, const Hash256& h) { blob(i, h.data(), h.size()); }
    void blob(int i, const std::vector<uint8_t>& v) { blob(i, v.empty() ? "" : (const void*)v.data(), v.size()); }
    void num(int i, int64_t v) { sqlite3_bind_int64(s_, i, v); }
    void text(int i, const std::string& v) { sqlite3_bind_text(s_, i, v.c_str(), int(v.size()), SQLITE_TRANSIENT); }
    void null(int i) { sqlite3_bind_null(s_, i); }

    int step() { return sqlite3_step(s_); }
    int64_t i64(int c) const { return sqlite3_column_int64(s_, c); }
    int bytes(int c) const { return sqlite3_column_bytes(s_, c); }
    const uint8_t* ptr(int c) const { return static_cast<const uint8_t*>(sqlite3_column_blob(s_, c)); }
    std::vector<uint8_t> vec(int c) const {
        const uint8_t* p = ptr(c);
        return p ? std::vector<uint8_t>(p, p + bytes(c)) : std::vector<uint8_t>();
    }
    bool hash(int c, Hash256& out) const {
        if (bytes(c) != 32 || !ptr(c)) return false;
        std::copy(ptr(c), ptr(c) + 32, out.begin());
        return true;
    }
    std::string str(int c) const {
        const unsigned char* t = sqlite3_column_text(s_, c);
        return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
    }

private:
    sqlite3_stmt* s_ = nullptr;
    std::string error_;
};

// Runs a single-column PRAGMA that must answer "ok".
bool pragmaOk(sqlite3* db, const char* pragma, std::string& err) {
    Stmt s(db, pragma);
    if (!s.ok()) { err = s.error(); return false; }
    int rc;
    while ((rc = s.step()) == SQLITE_ROW) {
        if (s.str(0) != "ok") { err = std::string(pragma) + ": " + s.str(0); return false; }
    }
    if (rc != SQLITE_DONE) { err = sqlite3_errmsg(db); return false; }
    return true;
}

// Fills `err` and returns false if the pragma produced any row (foreign_key_check).
bool pragmaEmpty(sqlite3* db, const char* pragma, std::string& err) {
    Stmt s(db, pragma);
    if (!s.ok()) { err = s.error(); return false; }
    if (s.step() == SQLITE_ROW) { err = std::string(pragma) + " reported a violation in " + s.str(0); return false; }
    return true;
}

bool countsFromBlock(const std::vector<uint8_t>& bytes, uint32_t& txCount) {
    try {
        Reader r(bytes);
        BlockHeader::parse(r);
        txCount = uint32_t(r.compactSize(Consensus::MAX_BLOCK_TXS_PARSE));
        return true;
    } catch (const ParseError&) {
        return false;
    }
}

} // namespace

// ---------------------------------------------------------------- coins view

class SqliteChainStore::CoinsViewDB : public CoinsView {
public:
    explicit CoinsViewDB(sqlite3* db) : get_(db, "SELECT amount, script, height, is_coinbase FROM utxo WHERE txid = ?1 AND vout = ?2") {}

    bool ok() const { return get_.ok(); }

    bool getCoin(const OutPoint& op, Coin& out) const override {
        auto& s = const_cast<Stmt&>(get_);
        s.reset();
        s.blob(1, op.txid);
        s.num(2, op.index);
        bool found = false;
        if (s.step() == SQLITE_ROW) {
            out.amount = uint64_t(s.i64(0));
            out.scriptPubKey = s.vec(1);
            out.height = uint32_t(s.i64(2));
            out.isCoinbase = s.i64(3) != 0;
            found = true;
        }
        s.reset();
        return found;
    }
    Hash256 bestBlock() const override { return best; }

    Hash256 best{};

private:
    Stmt get_;
};

// ---------------------------------------------------------------- open / schema

bool SqliteChainStore::exec(const char* sql, std::string* err) const {
    char* msg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &msg) != SQLITE_OK) {
        lastError_ = msg ? msg : "sqlite error";
        if (err) *err = lastError_;
        sqlite3_free(msg);
        return false;
    }
    return true;
}

SqliteChainStore::~SqliteChainStore() {
    coins_.reset();   // finalises its statement before the connection closes
    if (db_) sqlite3_close(db_);
    if (lockFd_ >= 0) ::close(lockFd_);   // after the database: the next process may open it now
}

bool SqliteChainStore::createSchema(const ChainParams& params, std::string& err) {
    if (!exec("BEGIN IMMEDIATE", &err)) return false;
    auto fail = [&]() { exec("ROLLBACK"); return false; };
    if (!exec(kSchemaSql, &err) || !exec(kUtxoSql, &err)) return fail();
    if (!exec(("PRAGMA application_id = " + std::to_string(kApplicationId)).c_str(), &err)) return fail();
    if (!exec(("PRAGMA user_version = " + std::to_string(kSchemaVersion)).c_str(), &err)) return fail();

    auto put = [&](const char* name, auto bind) {
        Stmt s(db_, "INSERT INTO chain_state(name, value) VALUES (?1, ?2)");
        s.text(1, name);
        bind(s);
        return s.step() == SQLITE_DONE;
    };
    const Hash256 zero{};
    const std::vector<uint8_t> goldInit = GoldRules::GoldState{}.serialize();
    bool ok = put("schema_version", [&](Stmt& s) { s.num(2, kSchemaVersion); }) &&
              put("network", [&](Stmt& s) { s.text(2, params.name); }) &&
              put("genesis_hash", [&](Stmt& s) { s.blob(2, params.genesisHash); }) &&
              put("clean_shutdown", [&](Stmt& s) { s.num(2, 0); }) &&
              put("reindex_in_progress", [&](Stmt& s) { s.num(2, 0); }) &&
              put("utxo_count", [&](Stmt& s) { s.num(2, 0); }) &&
              put("utxo_total_amount", [&](Stmt& s) { s.num(2, 0); }) &&
              put("utxo_digest", [&](Stmt& s) { s.blob(2, zero); }) &&
              put("gold_state", [&](Stmt& s) { s.blob(2, goldInit); });
    if (!ok) {
        err = sqlite3_errmsg(db_);
        return fail();
    }
    if (!exec("COMMIT", &err)) return fail();
    exec("PRAGMA wal_checkpoint(TRUNCATE)");
    return true;
}

bool SqliteChainStore::loadCached(std::string& err) {
    StoredState st;
    if (!loadState(st, err)) return false;
    stats_ = st.stats;
    coins_->best = st.best;
    Stmt g(db_, "SELECT value FROM chain_state WHERE name = 'gold_state'");
    if (g.ok() && g.step() == SQLITE_ROW) {
        if (!GoldRules::GoldState::deserialize(g.vec(0), gold_)) {
            err = "gold_state does not parse";
            return false;
        }
    }
    return true;
}

std::unique_ptr<SqliteChainStore> SqliteChainStore::open(const std::string& dir, const ChainParams& params,
                                                         std::string& err, int& exitCode) {
    namespace fs = std::filesystem;
    exitCode = EXIT_STORAGE_FAILURE;
    if (dir.empty()) {
        err = "no data directory given";
        return nullptr;
    }

    // Directory: created private; an existing one must already be private and ours.
    std::error_code ec;
    const bool existed = fs::exists(dir, ec);
    if (!existed) {
        const mode_t old = umask(077);
        fs::create_directories(dir, ec);
        umask(old);
        if (ec) {
            err = "cannot create " + dir + ": " + ec.message();
            return nullptr;
        }
        chmod(dir.c_str(), 0700);
    } else {
        struct stat st;
        if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            err = dir + " is not a directory";
            return nullptr;
        }
        if (st.st_uid != geteuid() || (st.st_mode & 077) != 0) {
            err = dir + " must be owned by the current user and closed to others: chmod 700 " + dir;
            return nullptr;
        }
    }

    const std::string path = dir + "/chain.sqlite";
    struct stat fst;
    if (lstat(path.c_str(), &fst) == 0 && !S_ISREG(fst.st_mode)) {
        err = path + " is a symbolic link or not a regular file";
        return nullptr;
    }

    std::unique_ptr<SqliteChainStore> store(new SqliteChainStore());
    store->path_ = path;

    // One process per data directory. The lock dies with the process, so a crash never leaves it stuck.
    {
        const std::string lockPath = dir + "/chain.lock";
        const mode_t oldMask = umask(077);
        store->lockFd_ = ::open(lockPath.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
        umask(oldMask);
        if (store->lockFd_ < 0) {
            err = "cannot open " + lockPath + ": " + std::strerror(errno);
            return nullptr;
        }
        if (flock(store->lockFd_, LOCK_EX | LOCK_NB) != 0) {
            err = "another Bucks process is already using " + dir + " (lock held on " + lockPath + ")";
            return nullptr;
        }
    }
    const mode_t old = umask(077);
    int rc = sqlite3_open_v2(path.c_str(), &store->db_,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_EXRESCODE,
                             nullptr);
    umask(old);
    if (rc != SQLITE_OK) {
        err = std::string("cannot open ") + path + ": " + (store->db_ ? sqlite3_errmsg(store->db_) : "out of memory");
        return nullptr;
    }
    sqlite3* db = store->db_;
    sqlite3_busy_timeout(db, 5000);
    sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
    sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_FKEY, 1, nullptr);
    sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_TRIGGER, 1, nullptr);
    sqlite3_limit(db, SQLITE_LIMIT_LENGTH, 4 * 1024 * 1024);

    for (const char* p : {"PRAGMA cell_size_check = ON", "PRAGMA mmap_size = 0"}) {
        if (!store->exec(p, &err)) return nullptr;
    }
    {
        Stmt s(db, "PRAGMA journal_mode = WAL");
        if (!s.ok() || s.step() != SQLITE_ROW || s.str(0) != "wal") {
            err = "the database filesystem does not support WAL (local filesystem required)";
            return nullptr;
        }
    }
    for (const char* p : {"PRAGMA synchronous = FULL", "PRAGMA fullfsync = ON", "PRAGMA checkpoint_fullfsync = ON",
                          "PRAGMA wal_autocheckpoint = 4000", "PRAGMA journal_size_limit = 67108864",
                          "PRAGMA cache_size = -131072"}) {
        if (!store->exec(p, &err)) return nullptr;
    }

    // Identity and version.
    int64_t appId = 0, userVersion = 0, tables = 0;
    {
        Stmt a(db, "PRAGMA application_id");
        if (a.ok() && a.step() == SQLITE_ROW) appId = a.i64(0);
        Stmt v(db, "PRAGMA user_version");
        if (v.ok() && v.step() == SQLITE_ROW) userVersion = v.i64(0);
        Stmt t(db, "SELECT COUNT(*) FROM sqlite_master");
        if (t.ok() && t.step() == SQLITE_ROW) tables = t.i64(0);
    }
    if (tables == 0 && appId == 0 && userVersion == 0) {
        if (!store->createSchema(params, err)) return nullptr;
    } else {
        if (appId != kApplicationId) {
            exitCode = EXIT_DATABASE_CORRUPT;
            err = path + " is not a Bucks chain database";
            return nullptr;
        }
        if (userVersion != kSchemaVersion) {
            exitCode = EXIT_UNSUPPORTED_VERSION;
            err = "unsupported database schema version " + std::to_string(userVersion) +
                  " (this build supports " + std::to_string(kSchemaVersion) + ")";
            return nullptr;
        }
    }

    store->coins_ = std::make_unique<CoinsViewDB>(db);
    if (!store->coins_->ok()) {
        exitCode = EXIT_DATABASE_CORRUPT;
        err = "database schema is damaged";
        return nullptr;
    }

    // Chain id: network and genesis must equal the parameters.
    StoredState state;
    if (!store->loadState(state, err)) {
        exitCode = EXIT_DATABASE_CORRUPT;
        return nullptr;
    }
    if (state.network != params.name || state.genesis != params.genesisHash) {
        exitCode = EXIT_PARAMS_MISMATCH;
        err = "datadir belongs to network " + state.network + " with another genesis";
        return nullptr;
    }
    if (!store->loadCached(err)) {
        exitCode = EXIT_DATABASE_CORRUPT;
        return nullptr;
    }

    for (const char* suffix : {"", "-wal", "-shm"}) chmod((path + suffix).c_str(), 0600);
    exitCode = EXIT_STORAGE_OK;
    return store;
}

// ---------------------------------------------------------------- reads

const CoinsView& SqliteChainStore::coins() const { return *coins_; }

bool SqliteChainStore::isEmpty() const {
    Stmt s(db_, "SELECT 1 FROM chain_state WHERE name = 'best_block_hash'");
    return !(s.ok() && s.step() == SQLITE_ROW);
}

bool SqliteChainStore::loadState(StoredState& out, std::string& err) const {
    Stmt s(db_, "SELECT name, value FROM chain_state");
    if (!s.ok()) { err = s.error(); return false; }
    int rc;
    bool haveNetwork = false, haveGenesis = false;
    while ((rc = s.step()) == SQLITE_ROW) {
        const std::string name = s.str(0);
        if (name == "network") { out.network = s.str(1); haveNetwork = true; }
        else if (name == "genesis_hash") haveGenesis = s.hash(1, out.genesis);
        else if (name == "best_block_hash") s.hash(1, out.best);
        else if (name == "best_height") out.bestHeight = uint32_t(s.i64(1));
        else if (name == "utxo_count") out.stats.count = uint64_t(s.i64(1));
        else if (name == "utxo_total_amount") out.stats.totalAmount = uint64_t(s.i64(1));
        else if (name == "utxo_digest") {
            Hash256 d;
            if (s.hash(1, d)) out.stats.digest = U256::fromBytesBE(d);
        }
        else if (name == "clean_shutdown") out.cleanShutdown = s.i64(1) != 0;
        else if (name == "reindex_in_progress") out.reindexInProgress = s.i64(1) != 0;
    }
    if (rc != SQLITE_DONE) { err = sqlite3_errmsg(db_); return false; }
    if (!haveNetwork || !haveGenesis) { err = "chain_state has no network or genesis"; return false; }
    return true;
}

bool SqliteChainStore::loadIndex(std::vector<StoredIndexRow>& out, std::string& err) const {
    Stmt s(db_, "SELECT hash, header, chain_work, status, tx_count FROM block_index");
    if (!s.ok()) { err = s.error(); return false; }
    int rc;
    while ((rc = s.step()) == SQLITE_ROW) {
        StoredIndexRow row;
        Hash256 work;
        if (!s.hash(0, row.hash) || !s.hash(2, work)) { err = "malformed block_index row"; return false; }
        try {
            row.header = BlockHeader::deserialize(s.vec(1));
        } catch (const ParseError&) {
            err = "malformed header bytes in block_index";
            return false;
        }
        row.chainWork = U256::fromBytesBE(work);
        row.status = uint32_t(s.i64(3));
        row.txCount = uint32_t(s.i64(4));
        out.push_back(std::move(row));
    }
    if (rc != SQLITE_DONE) { err = sqlite3_errmsg(db_); return false; }
    return true;
}

bool SqliteChainStore::getBlock(const Hash256& hash, std::vector<uint8_t>& bytes) const {
    Stmt s(db_, "SELECT data FROM block_data WHERE hash = ?1");
    if (!s.ok()) return false;
    s.blob(1, hash);
    if (s.step() != SQLITE_ROW) return false;
    bytes = s.vec(0);
    return true;
}

bool SqliteChainStore::getUndo(const Hash256& hash, BlockUndo& undo) const {
    Stmt s(db_, "SELECT data FROM undo WHERE block_hash = ?1");
    if (!s.ok()) return false;
    s.blob(1, hash);
    if (s.step() != SQLITE_ROW) return false;
    try {
        undo = BlockUndo::deserialize(s.vec(0));
    } catch (const ParseError& e) {
        lastError_ = std::string("undo record is corrupt: ") + e.what();
        return false;
    }
    if (undo.blockHash != hash) {
        lastError_ = "undo record names another block";
        return false;
    }
    return true;
}

bool SqliteChainStore::scanStats(UtxoStats& out, std::string& err) const {
    Stmt s(db_, "SELECT txid, vout, amount, script, height, is_coinbase FROM utxo");
    if (!s.ok()) { err = s.error(); return false; }
    out = UtxoStats{};
    int rc;
    while ((rc = s.step()) == SQLITE_ROW) {
        OutPoint op;
        Coin c;
        if (!s.hash(0, op.txid)) { err = "malformed utxo row"; return false; }
        op.index = uint32_t(s.i64(1));
        c.amount = uint64_t(s.i64(2));
        c.scriptPubKey = s.vec(3);
        c.height = uint32_t(s.i64(4));
        c.isCoinbase = s.i64(5) != 0;
        out.count += 1;
        out.totalAmount += c.amount;
        out.digest += coinDigest(op, c);
    }
    if (rc != SQLITE_DONE) { err = sqlite3_errmsg(db_); return false; }
    return true;
}

bool SqliteChainStore::selfCheck(int tier, std::string& err) const {
    auto count = [&](const char* sql) -> int64_t {
        Stmt s(db_, sql);
        return (s.ok() && s.step() == SQLITE_ROW) ? s.i64(0) : -1;
    };
    if (count("SELECT COUNT(*) FROM block_index WHERE (status & 2) != 0") != count("SELECT COUNT(*) FROM block_data")) {
        err = "HAVE_DATA rows and block_data rows differ";
        return false;
    }
    if (count("SELECT COUNT(*) FROM block_index WHERE (status & 16) != 0") != count("SELECT COUNT(*) FROM undo")) {
        err = "HAVE_UNDO rows and undo rows differ";
        return false;
    }
    if (tier >= 2) {
        if (!pragmaOk(db_, "PRAGMA quick_check", err)) return false;
        if (!pragmaEmpty(db_, "PRAGMA foreign_key_check", err)) return false;
    }
    if (tier >= 3) {
        if (!pragmaOk(db_, "PRAGMA integrity_check", err)) return false;
        Stmt s(db_, "SELECT hash, data FROM block_data");
        if (!s.ok()) { err = s.error(); return false; }
        while (s.step() == SQLITE_ROW) {
            Hash256 key;
            const std::vector<uint8_t> data = s.vec(1);
            if (!s.hash(0, key) || data.size() < Consensus::BLOCK_HEADER_SIZE ||
                doubleSha256(std::vector<uint8_t>(data.begin(), data.begin() + Consensus::BLOCK_HEADER_SIZE)) != key) {
                err = "stored block bytes do not match their hash";
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------- writes outside a step

bool SqliteChainStore::putBlock(const Hash256& hash, const std::vector<uint8_t>& bytes) {
    uint32_t txCount = 0;
    if (!countsFromBlock(bytes, txCount) || bytes.size() > Consensus::MAX_BLOCK_SIZE) {
        lastError_ = "block bytes do not parse";
        return false;
    }
    if (!exec("BEGIN IMMEDIATE")) return false;
    bool ok = false;
    {
        Stmt ins(db_, "INSERT INTO block_data(hash, data) VALUES (?1, ?2)");
        ins.blob(1, hash);
        ins.blob(2, bytes);
        Stmt upd(db_, "UPDATE block_index SET tx_count = ?2, size = ?3 WHERE hash = ?1");
        upd.num(2, txCount);
        upd.num(3, int64_t(bytes.size()));
        upd.blob(1, hash);
        ok = ins.ok() && upd.ok() && ins.step() == SQLITE_DONE && upd.step() == SQLITE_DONE;
        if (!ok) lastError_ = sqlite3_errmsg(db_);
    }
    if (!ok) {
        exec("ROLLBACK");
        return false;
    }
    return exec("COMMIT");
}

bool SqliteChainStore::putIndexRow(const Hash256& hash, const BlockHeader& header, uint32_t status) {
    int64_t txCount = -1;
    bool exists = false;
    {
        Stmt q(db_, "SELECT tx_count FROM block_index WHERE hash = ?1");
        q.blob(1, hash);
        if (q.ok() && q.step() == SQLITE_ROW) {
            exists = true;
            txCount = q.i64(0);
        }
    }
    if (exists) {
        Stmt u(db_, "UPDATE block_index SET status = ?2 WHERE hash = ?1");
        u.blob(1, hash);
        u.num(2, status);
        if (!u.ok() || u.step() != SQLITE_DONE) {
            lastError_ = sqlite3_errmsg(db_);
            return false;
        }
        // A body stored before the sizes were known (--reindex): fill them in once.
        if (txCount == 0 && (status & HAVE_DATA)) {
            std::vector<uint8_t> bytes;
            uint32_t n = 0;
            if (getBlock(hash, bytes) && countsFromBlock(bytes, n)) {
                Stmt f(db_, "UPDATE block_index SET tx_count = ?2, size = ?3 WHERE hash = ?1");
                f.blob(1, hash);
                f.num(2, n);
                f.num(3, int64_t(bytes.size()));
                f.step();
            }
        }
        return true;
    }

    // New row: the chain work is the parent's plus this block's proof.
    U256 work = Pow::blockProof(header.bits);
    if (header.height != 0) {
        Stmt p(db_, "SELECT chain_work FROM block_index WHERE hash = ?1");
        p.blob(1, header.prevBlockHash);
        Hash256 parentWork;
        if (!p.ok() || p.step() != SQLITE_ROW || !p.hash(0, parentWork)) {
            lastError_ = "parent of the header is not stored";
            return false;
        }
        work = U256::fromBytesBE(parentWork) + work;
    }
    Stmt ins(db_, "INSERT INTO block_index(hash, prev_hash, height, header, chain_work, status) "
                  "VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
    ins.blob(1, hash);
    if (header.height == 0) ins.null(2); else ins.blob(2, header.prevBlockHash);
    ins.num(3, header.height);
    ins.blob(4, header.serialize());
    ins.blob(5, work.toBytesBE());
    ins.num(6, status);
    if (!ins.ok() || ins.step() != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

bool SqliteChainStore::coinsByHash160(const Hash160& key, size_t limit, std::vector<CoinRow>& out) const {
    Stmt s(db_, "SELECT txid, vout, amount, script, height, is_coinbase FROM utxo "
                "WHERE hash160 = ?1 ORDER BY txid, vout LIMIT ?2");
    if (!s.ok()) return false;
    s.blob(1, key.data(), key.size());
    s.num(2, int64_t(limit));
    int rc;
    while ((rc = s.step()) == SQLITE_ROW) {
        CoinRow row;
        if (!s.hash(0, row.op.txid)) return false;
        row.op.index = uint32_t(s.i64(1));
        row.coin.amount = uint64_t(s.i64(2));
        row.coin.scriptPubKey = s.vec(3);
        row.coin.height = uint32_t(s.i64(4));
        row.coin.isCoinbase = s.i64(5) != 0;
        out.push_back(std::move(row));
    }
    return rc == SQLITE_DONE;
}

bool SqliteChainStore::summaryByHash160(const Hash160& key, uint64_t& balance, uint64_t& count) const {
    Stmt s(db_, "SELECT COALESCE(SUM(amount), 0), COUNT(*) FROM utxo WHERE hash160 = ?1");
    if (!s.ok()) return false;
    s.blob(1, key.data(), key.size());
    if (s.step() != SQLITE_ROW) return false;
    balance = uint64_t(s.i64(0));
    count = uint64_t(s.i64(1));
    return true;
}

bool SqliteChainStore::setCleanShutdown(bool clean) {
    Stmt s(db_, "INSERT INTO chain_state(name, value) VALUES ('clean_shutdown', ?1) "
                "ON CONFLICT(name) DO UPDATE SET value = excluded.value");
    s.num(1, clean ? 1 : 0);
    if (!s.ok() || s.step() != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- the step commit

bool SqliteChainStore::commitStep(const StepCommit& step) {
    if (!exec("BEGIN IMMEDIATE")) return false;
    auto fail = [&](const std::string& why) {
        lastError_ = why;
        exec("ROLLBACK");
        return false;
    };

    // The in-database best block must be the one this step was computed on.
    {
        Stmt q(db_, "SELECT value FROM chain_state WHERE name = 'best_block_hash'");
        Hash256 stored{};
        const bool have = q.ok() && q.step() == SQLITE_ROW && q.hash(0, stored);
        if (stored != step.expectedOldBest || (!have && step.expectedOldBest != Hash256{}))
            return fail("commit: best block in the database is not the step's parent");
    }

    // Spent coins first (this also covers overwritten ones). The rows that
    // really disappear feed the statistics.
    CoinChanges actual;
    {
        Stmt del(db_, "DELETE FROM utxo WHERE txid = ?1 AND vout = ?2 RETURNING amount, script, height, is_coinbase");
        if (!del.ok()) return fail(del.error());
        for (const auto& r : step.coins.removed) {
            del.reset();
            del.blob(1, r.first.txid);
            del.num(2, r.first.index);
            int rows = 0, rc;
            Coin got;
            while ((rc = del.step()) == SQLITE_ROW) {
                got.amount = uint64_t(del.i64(0));
                got.scriptPubKey = del.vec(1);
                got.height = uint32_t(del.i64(2));
                got.isCoinbase = del.i64(3) != 0;
                ++rows;
            }
            if (rc != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
            if (rows != 1) return fail("commit: a spent coin is not in the set");
            if (got != r.second) return fail("commit: a spent coin differs from the set");
            actual.removed.push_back(r);
        }
    }
    {
        Stmt ins(db_, "INSERT INTO utxo(txid, vout, amount, script, height, is_coinbase) VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
        if (!ins.ok()) return fail(ins.error());
        for (const auto& a : step.coins.added) {
            ins.reset();
            ins.blob(1, a.first.txid);
            ins.num(2, a.first.index);
            ins.num(3, int64_t(a.second.amount));
            ins.blob(4, a.second.scriptPubKey);
            ins.num(5, a.second.height);
            ins.num(6, a.second.isCoinbase ? 1 : 0);
            if (ins.step() != SQLITE_DONE) return fail(std::string("commit: coin insert failed: ") + sqlite3_errmsg(db_));
            actual.added.push_back(a);
        }
    }
    UtxoStats next = stats_;
    if (!next.apply(actual)) return fail("commit: statistics left their range");

    // Undo records, then the status of every touched block, disconnected first
    // (a height has at most one main-chain block).
    {
        Stmt u(db_, "INSERT INTO undo(block_hash, data) VALUES (?1, ?2) ON CONFLICT(block_hash) DO NOTHING");
        if (!u.ok()) return fail(u.error());
        for (const auto& undo : step.undos) {
            u.reset();
            u.blob(1, undo.first);
            u.blob(2, undo.second.serialize());
            if (u.step() != SQLITE_DONE) return fail(std::string("commit: undo insert failed: ") + sqlite3_errmsg(db_));
        }
    }
    {
        Stmt s(db_, "UPDATE block_index SET status = ?2 WHERE hash = ?1");
        if (!s.ok()) return fail(s.error());
        for (const auto& st : step.statuses) {
            s.reset();
            s.blob(1, st.first);
            s.num(2, st.second);
            if (s.step() != SQLITE_DONE || sqlite3_changes(db_) != 1)
                return fail(std::string("commit: status update failed: ") + sqlite3_errmsg(db_));
        }
    }

    // Chain state.
    const Hash256 newBest = step.coins.bestBlock ? *step.coins.bestBlock : step.expectedOldBest;
    {
        auto upsert = [&](const char* name, auto bind) {
            Stmt s(db_, "INSERT INTO chain_state(name, value) VALUES (?1, ?2) "
                        "ON CONFLICT(name) DO UPDATE SET value = excluded.value");
            s.text(1, name);
            bind(s);
            return s.ok() && s.step() == SQLITE_DONE;
        };
        int64_t height = 0;
        {
            Stmt h(db_, "SELECT height FROM block_index WHERE hash = ?1");
            h.blob(1, newBest);
            if (!h.ok() || h.step() != SQLITE_ROW) return fail("commit: new best block has no index row");
            height = h.i64(0);
        }
        const bool ok =
            upsert("best_block_hash", [&](Stmt& s) { s.blob(2, newBest); }) &&
            upsert("best_height", [&](Stmt& s) { s.num(2, height); }) &&
            upsert("utxo_count", [&](Stmt& s) { s.num(2, int64_t(next.count)); }) &&
            upsert("utxo_total_amount", [&](Stmt& s) { s.num(2, int64_t(next.totalAmount)); }) &&
            upsert("utxo_digest", [&](Stmt& s) { s.blob(2, next.digest.toBytesBE()); }) &&
            upsert("gold_state", [&](Stmt& s) { s.blob(2, step.gold.serialize()); });
        if (!ok) return fail(std::string("commit: chain_state write failed: ") + sqlite3_errmsg(db_));
    }

    if (failNextCommit) {   // test hook: everything was written, now take it all back
        failNextCommit = false;
        return fail("commit: injected failure before COMMIT");
    }
    if (!exec("COMMIT")) {
        exec("ROLLBACK");
        return false;
    }

    // The database changed; only now does the cached view follow.
    stats_ = next;
    coins_->best = newBest;
    gold_ = step.gold;
    return true;
}

// ---------------------------------------------------------------- repair

bool SqliteChainStore::resetChainState(std::string& err) {
    if (!exec("BEGIN IMMEDIATE", &err)) return false;
    auto fail = [&]() { exec("ROLLBACK"); return false; };

    Hash256 genesis{};
    {
        Stmt g(db_, "SELECT hash FROM block_index WHERE height = 0");
        if (!g.ok() || g.step() != SQLITE_ROW || !g.hash(0, genesis)) {
            err = "no genesis row in block_index";
            return fail();
        }
    }
    if (!exec("DROP TABLE utxo", &err) || !exec(kUtxoSql, &err) || !exec("DELETE FROM undo", &err) ||
        !exec("DELETE FROM gold_events", &err) ||
        !exec("UPDATE block_index SET status = status & ~248", &err) ||    // 8+16+32+64+128
        !exec("UPDATE block_index SET status = status | 40 WHERE height = 0", &err))   // genesis: 8+32
        return fail();

    auto upsert = [&](const char* name, auto bind) {
        Stmt s(db_, "INSERT INTO chain_state(name, value) VALUES (?1, ?2) "
                    "ON CONFLICT(name) DO UPDATE SET value = excluded.value");
        s.text(1, name);
        bind(s);
        return s.ok() && s.step() == SQLITE_DONE;
    };
    const bool ok = upsert("best_block_hash", [&](Stmt& s) { s.blob(2, genesis); }) &&
                    upsert("best_height", [&](Stmt& s) { s.num(2, 0); }) &&
                    upsert("utxo_count", [&](Stmt& s) { s.num(2, 0); }) &&
                    upsert("utxo_total_amount", [&](Stmt& s) { s.num(2, 0); }) &&
                    upsert("utxo_digest", [&](Stmt& s) { s.blob(2, Hash256{}); }) &&
                    upsert("gold_state", [&](Stmt& s) { s.blob(2, GoldRules::GoldState{}.serialize()); });
    if (!ok) {
        err = sqlite3_errmsg(db_);
        return fail();
    }
    if (!exec("COMMIT", &err)) return fail();

    // The coins statement refers to the dropped table: build a new one.
    coins_.reset();
    coins_ = std::make_unique<CoinsViewDB>(db_);
    return loadCached(err);
}

bool SqliteChainStore::reindexBegin(const BlockHeader& genesisHeader, const Hash256& genesisHash, std::string& err) {
    if (!exec("PRAGMA foreign_keys = OFF", &err)) return false;   // outside a transaction, only this tool
    if (!exec("BEGIN IMMEDIATE", &err)) return false;
    auto fail = [&]() { exec("ROLLBACK"); exec("PRAGMA foreign_keys = ON"); return false; };

    std::vector<uint8_t> genesisBytes;
    if (!getBlock(genesisHash, genesisBytes)) {
        err = "the genesis block bytes are missing";
        return fail();
    }
    uint32_t txCount = 0;
    if (!countsFromBlock(genesisBytes, txCount)) {
        err = "the genesis block bytes do not parse";
        return fail();
    }
    if (!exec("DELETE FROM undo", &err) || !exec("DELETE FROM gold_events", &err) ||
        !exec("DELETE FROM block_index", &err) || !exec("DROP TABLE utxo", &err) || !exec(kUtxoSql, &err))
        return fail();

    {
        Stmt ins(db_, "INSERT INTO block_index(hash, prev_hash, height, header, chain_work, status, tx_count, size) "
                      "VALUES (?1, NULL, 0, ?2, ?3, 47, ?4, ?5)");
        ins.blob(1, genesisHash);
        ins.blob(2, genesisHeader.serialize());
        ins.blob(3, Pow::blockProof(genesisHeader.bits).toBytesBE());
        ins.num(4, txCount);
        ins.num(5, int64_t(genesisBytes.size()));
        if (!ins.ok() || ins.step() != SQLITE_DONE) {
            err = sqlite3_errmsg(db_);
            return fail();
        }
    }
    auto upsert = [&](const char* name, auto bind) {
        Stmt s(db_, "INSERT INTO chain_state(name, value) VALUES (?1, ?2) "
                    "ON CONFLICT(name) DO UPDATE SET value = excluded.value");
        s.text(1, name);
        bind(s);
        return s.ok() && s.step() == SQLITE_DONE;
    };
    const bool ok = upsert("reindex_in_progress", [&](Stmt& s) { s.num(2, 1); }) &&
                    upsert("best_block_hash", [&](Stmt& s) { s.blob(2, genesisHash); }) &&
                    upsert("best_height", [&](Stmt& s) { s.num(2, 0); }) &&
                    upsert("utxo_count", [&](Stmt& s) { s.num(2, 0); }) &&
                    upsert("utxo_total_amount", [&](Stmt& s) { s.num(2, 0); }) &&
                    upsert("utxo_digest", [&](Stmt& s) { s.blob(2, Hash256{}); }) &&
                    upsert("gold_state", [&](Stmt& s) { s.blob(2, GoldRules::GoldState{}.serialize()); });
    if (!ok) {
        err = sqlite3_errmsg(db_);
        return fail();
    }
    if (!exec("COMMIT", &err)) return fail();

    coins_.reset();
    coins_ = std::make_unique<CoinsViewDB>(db_);
    return loadCached(err);
}

bool SqliteChainStore::listStoredBlocks(std::vector<std::pair<Hash256, BlockHeader>>& out, std::string& err) const {
    Stmt s(db_, "SELECT hash, substr(data, 1, 84) FROM block_data");
    if (!s.ok()) { err = s.error(); return false; }
    while (s.step() == SQLITE_ROW) {
        Hash256 h;
        if (!s.hash(0, h)) continue;
        try {
            out.emplace_back(h, BlockHeader::deserialize(s.vec(1)));
        } catch (const ParseError&) {
            // unreadable header bytes: reported as a block that cannot be indexed
            out.emplace_back(h, BlockHeader());
            out.back().second.height = 0xFFFFFFFFu;
        }
    }
    return true;
}

bool SqliteChainStore::deleteBlockData(const Hash256& hash) {
    Stmt s(db_, "DELETE FROM block_data WHERE hash = ?1");
    s.blob(1, hash);
    return s.ok() && s.step() == SQLITE_DONE;
}

bool SqliteChainStore::reindexFinish(std::string& err) {
    if (!exec("DELETE FROM block_data WHERE hash NOT IN (SELECT hash FROM block_index)", &err)) return false;
    if (!pragmaEmpty(db_, "PRAGMA foreign_key_check", err)) return false;
    if (!exec("PRAGMA foreign_keys = ON", &err)) return false;
    Stmt s(db_, "INSERT INTO chain_state(name, value) VALUES ('reindex_in_progress', 0) "
                "ON CONFLICT(name) DO UPDATE SET value = excluded.value");
    if (!s.ok() || s.step() != SQLITE_DONE) {
        err = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

} // namespace Bucks
