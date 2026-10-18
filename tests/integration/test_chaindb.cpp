#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <random>

#include "chaindb.h"
#include "consensus.h"
#include "persistent_chain.h"
#include "sqlite3.h"

using namespace Bucks;
using namespace Bucks::TestUtil;

// Spec: docs/design/storage-concurrency.md section 16.1 and 16.2 (FS-1, DBT-1 ... DBT-4).

namespace {

// Direct access to the database file, as an attacker or a damaged disk would have.
class RawDb {
public:
    explicit RawDb(const std::string& path) {
        EXPECT_EQ(sqlite3_open(path.c_str(), &db_), SQLITE_OK);
        sqlite3_exec(db_, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr);
    }
    ~RawDb() { sqlite3_close(db_); }
    bool run(const std::string& sql) { return sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK; }
    std::string error() const { return sqlite3_errmsg(db_); }
    int64_t scalar(const std::string& sql) {
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(db_, sql.c_str(), -1, &s, nullptr);
        int64_t v = sqlite3_step(s) == SQLITE_ROW ? sqlite3_column_int64(s, 0) : -1;
        sqlite3_finalize(s);
        return v;
    }
    sqlite3* handle() { return db_; }

private:
    sqlite3* db_ = nullptr;
};


OutPoint op(uint8_t fill, uint32_t index) {
    OutPoint o;
    o.txid.fill(fill);
    o.index = index;
    return o;
}

Coin coin(uint64_t amount, uint8_t key, uint32_t height, bool cb) {
    Hash160 h;
    h.fill(key);
    Coin c;
    c.amount = amount;
    c.scriptPubKey = makeP2PKH(h);
    c.height = height;
    c.isCoinbase = cb;
    return c;
}

} // namespace

// DBT-1
TEST(ChainDb, IdentityAndVersion) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    c.close(true);
    {
        RawDb raw(c.dbPath());
        EXPECT_EQ(raw.scalar("PRAGMA application_id"), 1112886099);
        EXPECT_EQ(raw.scalar("PRAGMA user_version"), 2);
        EXPECT_EQ(raw.scalar("SELECT value FROM chain_state WHERE name = 'schema_version'"), 2);
        EXPECT_EQ(raw.scalar("PRAGMA journal_mode = WAL") >= 0, true);
    }

    // A newer schema is refused with exit code 7.
    {
        RawDb raw(c.dbPath());
        ASSERT_TRUE(raw.run("PRAGMA user_version = 3"));
    }
    std::string err;
    int code = 0;
    EXPECT_EQ(SqliteChainStore::open(c.dir(), c.params(), err, code), nullptr);
    EXPECT_EQ(code, EXIT_UNSUPPORTED_VERSION) << err;

    // A foreign SQLite file is refused with exit code 6.
    std::filesystem::remove_all(c.dir());
    std::filesystem::create_directory(c.dir());
    chmod(c.dir().c_str(), 0700);
    {
        RawDb raw(c.dbPath());
        ASSERT_TRUE(raw.run("CREATE TABLE notes(id INTEGER)"));
    }
    EXPECT_EQ(SqliteChainStore::open(c.dir(), c.params(), err, code), nullptr);
    EXPECT_EQ(code, EXIT_DATABASE_CORRUPT) << err;
}

// One process per data directory: a second open while the first is alive is refused,
// and the lock is released when the first closes.
TEST(ChainDb, SecondOpenOfSameDirectoryIsRefused) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    std::string err;
    int code = 0;
    EXPECT_EQ(SqliteChainStore::open(c.dir(), c.params(), err, code), nullptr);
    EXPECT_EQ(code, EXIT_STORAGE_FAILURE);
    EXPECT_NE(err.find("already using"), std::string::npos) << err;
    c.close(true);
    auto again = SqliteChainStore::open(c.dir(), c.params(), err, code);
    EXPECT_NE(again, nullptr) << err;
}

// DBT-2
TEST(ChainDb, DatabaseRejectsBadData) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    c.extend();
    c.extend();
    c.close(true);

    RawDb raw(c.dbPath());
    const std::string h32 = "x'" + std::string(64, 'a') + "'";
    const std::string hdr84 = "x'" + std::string(168, '0') + "'";
    const std::string genesis = "(SELECT hash FROM block_index WHERE height = 0)";
    const std::string tip = "(SELECT hash FROM block_index WHERE height = 2)";

    struct Bad { const char* what; std::string sql; const char* because; };
    const std::vector<Bad> bad = {
        {"31-byte hash", "INSERT INTO block_index(hash, prev_hash, height, header, chain_work, status) VALUES (x'" +
                             std::string(62, 'a') + "', NULL, 9, " + hdr84 + ", " + h32 + ", 1)", "length(hash) = 32"},
        {"prev_hash differs from the header", "INSERT INTO block_index(hash, prev_hash, height, header, chain_work, status) "
                                              "VALUES (" + h32 + ", " + genesis + ", 9, " + hdr84 + ", " + h32 + ", 1)", "prev_hash"},
        {"unknown parent", "INSERT INTO block_index(hash, prev_hash, height, header, chain_work, status) VALUES (" + h32 +
                               ", x'" + std::string(64, 'b') + "', 9, x'" + std::string(16, '0') + std::string(64, 'b') +
                               std::string(88, '0') + "', " + h32 + ", 1)", "FOREIGN KEY"},
        {"status 0", "UPDATE block_index SET status = 0 WHERE height = 1", "status BETWEEN 1 AND 255"},
        {"CONNECTED without DATA_CHECKED", "UPDATE block_index SET status = 9 WHERE height = 1", "(status & 8)"},
        {"second main block at a height", "UPDATE block_index SET status = 47 WHERE height = 1 AND 0 = 1", ""},
        {"FAILED on a main row", "UPDATE block_index SET status = status | 64 WHERE height = 1", "(status & 192)"},
        {"header update", "UPDATE block_index SET header = " + hdr84 + " WHERE height = 1", "immutable"},
        {"non-P2PKH coin", "INSERT INTO utxo(txid, vout, amount, script, height, is_coinbase) VALUES (" + h32 +
                               ", 0, 5, x'51', 1, 0)", "length(script) = 25"},
        {"zero amount", "INSERT INTO utxo(txid, vout, amount, script, height, is_coinbase) VALUES (" + h32 +
                            ", 0, 0, x'76a914" + std::string(40, '1') + "88ac', 1, 0)", "amount BETWEEN"},
        {"amount above MAX_MONEY", "INSERT INTO utxo(txid, vout, amount, script, height, is_coinbase) VALUES (" + h32 +
                                       ", 0, 2100000000000001, x'76a914" + std::string(40, '1') + "88ac', 1, 0)", "amount BETWEEN"},
        {"undo naming another block", "INSERT INTO undo(block_hash, data) VALUES (" + tip + ", x'01" + std::string(64, 'c') +
                                          std::string(70, '0') + "')", "substr(data, 2, 32)"},
        {"wrong chain_state type", "UPDATE chain_state SET value = 'text' WHERE name = 'utxo_count'", "CASE"},
        {"block_data update", "UPDATE block_data SET data = data WHERE hash = " + genesis, "write-once"},
    };
    for (const Bad& b : bad) {
        if (std::string(b.what) == "second main block at a height") continue;   // covered below
        EXPECT_FALSE(raw.run(b.sql)) << b.what;
        EXPECT_NE(raw.error().find(b.because), std::string::npos) << b.what << ": " << raw.error();
    }

    // A height can hold only one main-chain block: a sibling at height 1 cannot also be main.
    ASSERT_TRUE(raw.run("DROP TRIGGER block_index_frozen"));
    EXPECT_FALSE(raw.run("INSERT INTO block_index(hash, prev_hash, height, header, chain_work, status) "
                         "SELECT x'" + std::string(64, 'd') + "', prev_hash, height, header, chain_work, status "
                         "FROM block_index WHERE height = 1"));
    EXPECT_NE(raw.error().find("UNIQUE constraint failed: block_index.height"), std::string::npos) << raw.error();

    // Second tx_count write is refused (write-once).
    EXPECT_FALSE(raw.run("UPDATE block_index SET tx_count = 7 WHERE height = 1"));
    EXPECT_NE(raw.error().find("write-once"), std::string::npos) << raw.error();
    // And nothing above changed the statistics.
    EXPECT_EQ(raw.scalar("SELECT COUNT(*) FROM utxo"), raw.scalar("SELECT value FROM chain_state WHERE name = 'utxo_count'"));
}

// DBT-3
TEST(ChainDb, CoinConflictIsFatal) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    c.extend();
    SqliteChainStore& store = c.store();
    const UtxoStats before = store.stats();
    const Hash256 best = store.coins().bestBlock();

    // A step that "creates" a coin that already exists in the set.
    OutPoint existing{};
    {
        RawDb raw(c.dbPath());
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(raw.handle(), "SELECT txid, vout FROM utxo LIMIT 1", -1, &s, nullptr);
        ASSERT_EQ(sqlite3_step(s), SQLITE_ROW);
        std::copy_n(static_cast<const uint8_t*>(sqlite3_column_blob(s, 0)), 32, existing.txid.begin());
        existing.index = uint32_t(sqlite3_column_int64(s, 1));
        sqlite3_finalize(s);
    }
    StepCommit step;
    step.expectedOldBest = best;
    step.coins.added.emplace_back(existing, coin(5, 1, 1, false));
    step.coins.bestBlock = best;
    EXPECT_FALSE(store.commitStep(step));
    EXPECT_NE(store.lastError().find("coin insert failed"), std::string::npos) << store.lastError();
    EXPECT_EQ(store.stats(), before);
    EXPECT_EQ(store.coins().bestBlock(), best);

    // A step computed on another tip is refused.
    StepCommit stale;
    stale.expectedOldBest = Hash256{};
    stale.coins.bestBlock = best;
    EXPECT_FALSE(store.commitStep(stale));
    EXPECT_NE(store.lastError().find("parent"), std::string::npos) << store.lastError();

    // A spent coin that is not in the set, or differs from it, is refused.
    StepCommit ghost;
    ghost.expectedOldBest = best;
    ghost.coins.removed.emplace_back(op(0x77, 0), coin(5, 1, 1, false));
    ghost.coins.bestBlock = best;
    EXPECT_FALSE(store.commitStep(ghost));
    UtxoStats scanned;
    std::string err;
    ASSERT_TRUE(store.scanStats(scanned, err)) << err;
    EXPECT_EQ(scanned, store.stats());
}

// DBT-4
TEST(ChainDb, StatsMatchScan) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    SqliteChainStore& store = c.store();
    const Hash256 best = store.coins().bestBlock();

    std::mt19937 rng(2024);
    MemoryCoinsView reference;
    std::vector<OutPoint> live;
    uint32_t next = 1;
    for (int i = 0; i < 400; ++i) {
        CoinsViewCache view(&store.coins());
        std::vector<OutPoint> now = live;
        const int n = 1 + rng() % 8;
        for (int k = 0; k < n; ++k) {
            if (!now.empty() && rng() % 3 == 0) {
                const size_t j = rng() % now.size();
                ASSERT_TRUE(view.spendCoin(now[j]));
                now.erase(now.begin() + j);
            } else {
                OutPoint o = op(uint8_t(next >> 8), next);
                o.txid[5] = uint8_t(next >> 16);
                ++next;
                ASSERT_TRUE(view.addCoin(o, coin(1 + rng() % 1'000'000, uint8_t(rng()), rng() % 500, rng() % 7 == 0)));
                now.push_back(o);
            }
        }
        StepCommit step;
        step.expectedOldBest = best;
        step.coins = view.changes();
        step.coins.bestBlock = best;
        ASSERT_TRUE(store.commitStep(step)) << i << " " << store.lastError();
        ASSERT_TRUE(reference.apply(step.coins));
        live = now;
        if (i % 20 == 0) {
            UtxoStats scanned;
            std::string err;
            ASSERT_TRUE(store.scanStats(scanned, err)) << err;
            ASSERT_EQ(scanned, store.stats()) << i;
            ASSERT_EQ(store.stats(), reference.stats()) << i;
        }
    }
    // The statistics survive a restart.
    const UtxoStats finalStats = store.stats();
    c.close(true);
    RawDb raw(c.dbPath());
    EXPECT_EQ(uint64_t(raw.scalar("SELECT value FROM chain_state WHERE name = 'utxo_count'")), finalStats.count);
    EXPECT_EQ(uint64_t(raw.scalar("SELECT value FROM chain_state WHERE name = 'utxo_total_amount'")), finalStats.totalAmount);
}

TEST(ChainDb, OneWritePerBlockAndWriteOnce) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    Block b = c.build(c.state().tip());
    const Hash256 id = b.getHash();
    ASSERT_TRUE(c.submit(b).valid());
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(c.store().getBlock(id, bytes));
    EXPECT_EQ(bytes, b.serialize());
    EXPECT_FALSE(c.store().putBlock(id, bytes));            // bodies are write-once
    EXPECT_FALSE(c.store().putBlock(Hash256{}, bytes));      // no index row: foreign key
    BlockHeader orphanHeader = b.header;
    orphanHeader.prevBlockHash = Hash256{};
    orphanHeader.height = 5;
    EXPECT_FALSE(c.store().putIndexRow(orphanHeader.hash(), orphanHeader, 1));   // parent unknown
    std::vector<uint8_t> junk(100, 7);
    EXPECT_FALSE(c.store().putBlock(id, junk));              // does not parse
}

// FS-1 (the parts of the data directory rules that exist so far)
TEST(ChainDb, DirectoryAndFileModes) {
    PersistentChain c;
    std::string err;
    int code = 0;

    // A new directory is created private; the database files are private.
    auto store = SqliteChainStore::open(c.dir() + "/regtest", c.params(), err, code);
    ASSERT_NE(store, nullptr) << err;
    struct stat st;
    ASSERT_EQ(stat((c.dir() + "/regtest").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0700u);
    ASSERT_EQ(stat((c.dir() + "/regtest/chain.sqlite").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600u);
    store.reset();

    // An existing directory open to others is refused, with the hint.
    chmod((c.dir() + "/regtest").c_str(), 0755);
    EXPECT_EQ(SqliteChainStore::open(c.dir() + "/regtest", c.params(), err, code), nullptr);
    EXPECT_EQ(code, EXIT_STORAGE_FAILURE);
    EXPECT_NE(err.find("chmod 700"), std::string::npos) << err;
    chmod((c.dir() + "/regtest").c_str(), 0700);

    // A symbolic link in place of the database is refused.
    ASSERT_EQ(unlink((c.dir() + "/regtest/chain.sqlite").c_str()), 0);
    ASSERT_EQ(symlink("/etc/hosts", (c.dir() + "/regtest/chain.sqlite").c_str()), 0);
    EXPECT_EQ(SqliteChainStore::open(c.dir() + "/regtest", c.params(), err, code), nullptr);
    EXPECT_EQ(code, EXIT_STORAGE_FAILURE);
    EXPECT_NE(err.find("symbolic link"), std::string::npos) << err;
}

// ST-2
TEST(ChainDb, WrongNetworkDatadir) {
    PersistentChain c;                       // regtest
    ASSERT_TRUE(c.open().ok);
    c.close(true);
    std::string err;
    int code = 0;
    EXPECT_EQ(SqliteChainStore::open(c.dir(), chainParams(NetworkId::TEST), err, code), nullptr);
    EXPECT_EQ(code, EXIT_PARAMS_MISMATCH) << err;
    EXPECT_NE(err.find("regtest"), std::string::npos) << err;
}
