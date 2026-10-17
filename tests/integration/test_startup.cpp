#include <gtest/gtest.h>

#include "chaindb.h"
#include "consensus.h"
#include "persistent_chain.h"
#include "sqlite3.h"

using namespace Bucks;
using namespace Bucks::TestUtil;

// Spec: docs/design/consensus-validation.md section 15.4 (ST-1 ... ST-6, CS-4, CS-6)
// and docs/design/storage-concurrency.md section 16.3 (REC).

namespace {

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
    std::vector<uint8_t> blob(const std::string& sql) {
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(db_, sql.c_str(), -1, &s, nullptr);
        std::vector<uint8_t> v;
        if (sqlite3_step(s) == SQLITE_ROW) {
            const uint8_t* p = static_cast<const uint8_t*>(sqlite3_column_blob(s, 0));
            v.assign(p, p + sqlite3_column_bytes(s, 0));
        }
        sqlite3_finalize(s);
        return v;
    }

private:
    sqlite3* db_ = nullptr;
};

std::string hexOf(const Hash256& h) { return hexStr(std::vector<uint8_t>(h.begin(), h.end())); }

// A chain of `n` coinbase-only blocks on a fresh database.
// The block is extended before the tip is read: the operands of == are unsequenced, and GCC
// reads the right-hand side first, which compared the new block with the old tip.
void grow(PersistentChain& c, int n) {
    for (int i = 0; i < n; ++i) {
        const Block b = c.extend();
        ASSERT_TRUE(b.getHash() == c.state().tip()->hash);
    }
}

} // namespace

// A new database starts at genesis and a restart finds the same chain.
TEST(Startup, NewDatabaseThenRestart) {
    PersistentChain c;
    LoadResult r = c.open();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(c.state().tip()->height, 0u);
    grow(c, 6);
    const Hash256 tip = c.state().tip()->hash;
    const UtxoStats stats = c.store().stats();
    EXPECT_EQ(stats.count, 6u);                                  // six coinbase outputs
    EXPECT_EQ(stats.totalAmount, 6 * 50 * Consensus::COIN);

    c.close(true);
    r = c.open();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(c.state().tip()->hash, tip);
    EXPECT_EQ(c.state().tip()->height, 6u);
    EXPECT_EQ(c.store().stats(), stats);
    EXPECT_EQ(c.store().coins().bestBlock(), tip);

    // The chain goes on after the restart.
    ASSERT_TRUE(c.submit(c.build(c.state().tip())).valid());
    EXPECT_EQ(c.state().tip()->height, 7u);
}

// CS-4
TEST(Startup, SideBlockKeptByHash) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    grow(c, 6);
    const BlockIndex* fork = c.state().activeChain()[3];
    const Block side = c.build(fork, {}, 1);                // a valid block at height 4 on a side branch
    ASSERT_TRUE(c.submit(side).valid());
    const Hash256 mainAt4 = c.state().activeChain()[4]->hash;
    const Hash256 tip = c.state().tip()->hash;
    ASSERT_NE(side.getHash(), mainAt4);
    EXPECT_EQ(c.state().tip()->hash, tip);                   // equal work: first seen stays

    c.close(true);
    ASSERT_TRUE(c.open().ok);
    EXPECT_EQ(c.state().tip()->hash, tip);
    // Both height-4 blocks are stored by hash, and both are in the index.
    std::vector<uint8_t> bytes;
    EXPECT_TRUE(c.store().getBlock(side.getHash(), bytes));
    EXPECT_TRUE(c.store().getBlock(mainAt4, bytes));
    ASSERT_NE(c.state().find(side.getHash()), nullptr);
    ASSERT_NE(c.state().find(mainAt4), nullptr);
    EXPECT_FALSE(c.state().activeChain().contains(c.state().find(side.getHash())));
    EXPECT_TRUE(c.state().activeChain().contains(c.state().find(mainAt4)));
}

// CS-6 and ST-6
TEST(Startup, ReorgAtomicAndResumedAfterRestart) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    grow(c, 5);
    const Hash256 oldTip = c.state().tip()->hash;
    const UtxoStats oldStats = c.store().stats();
    const BlockIndex* fork = c.state().activeChain()[2];

    // A side branch of three blocks reaches the tip's work (heights 3..5): no reorg yet.
    Block b1 = c.build(fork, {}, 1);
    ASSERT_TRUE(c.submit(b1).valid());
    Block b2 = c.build(c.state().find(b1.getHash()), {}, 1);
    ASSERT_TRUE(c.submit(b2).valid());
    Block b3 = c.build(c.state().find(b2.getHash()), {}, 1);
    ASSERT_TRUE(c.submit(b3).valid());
    EXPECT_EQ(c.state().tip()->hash, oldTip);

    // One more block makes it heavier. The commit of the three-deep reorg fails after
    // every write was made, so the transaction is rolled back.
    Block heavy = c.build(c.state().find(b3.getHash()), {}, 1);
    c.store().failNextCommit = true;
    ValidationState st = c.submit(heavy);
    EXPECT_EQ(st.result, ValidationResult::INTERNAL);
    EXPECT_FALSE(c.store().lastError().empty());

    // Nothing was committed: the database still names the old tip with the old statistics.
    {
        RawDb raw(c.dbPath());
        EXPECT_EQ(raw.blob("SELECT value FROM chain_state WHERE name = 'best_block_hash'"),
                  std::vector<uint8_t>(oldTip.begin(), oldTip.end()));
        EXPECT_EQ(raw.scalar("SELECT COUNT(*) FROM block_index WHERE (status & 32) != 0"), 6);   // heights 0..5
        EXPECT_EQ(uint64_t(raw.scalar("SELECT value FROM chain_state WHERE name = 'utxo_count'")), oldStats.count);
    }
    EXPECT_EQ(c.store().stats(), oldStats);
    EXPECT_EQ(c.store().coins().bestBlock(), oldTip);

    // The failed commit is fatal: the node stops. After a restart the heavier blocks are
    // in the database but not connected, and startup connects them (ST-6).
    c.close(false);
    LoadResult r = c.open();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(c.state().tip()->hash, heavy.getHash());
    EXPECT_EQ(c.state().tip()->height, 6u);
    EXPECT_EQ(c.store().stats().count, 6u);                  // coinbases at heights 1,2 and 3..6 of the new branch
}

// ST-1
TEST(Startup, GenesisMismatch) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    c.close(true);

    std::string err;
    int code = 0;
    auto store = SqliteChainStore::open(c.dir(), c.params(), err, code);
    ASSERT_NE(store, nullptr) << err;
    ChainParams wrong = c.params();
    wrong.genesisHash[3] ^= 1;
    ChainState state(wrong, *store, [] { return int64_t(2'000'000'000); });
    LoadResult r = state.load();
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.exitCode, EXIT_PARAMS_MISMATCH);
    EXPECT_NE(r.error.find("genesis"), std::string::npos) << r.error;

    // And a build without a configured genesis (mainnet today) cannot start at all.
    ChainState mainState(chainParams(NetworkId::MAIN), *store, [] { return int64_t(2'000'000'000); });
    r = mainState.load();
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.exitCode, EXIT_PARAMS_MISMATCH);
    EXPECT_NE(r.error.find("not configured"), std::string::npos) << r.error;
}

// ST-3
TEST(Startup, CorruptHeaderRow) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    grow(c, 5);
    c.close(true);
    {
        RawDb raw(c.dbPath());
        ASSERT_TRUE(raw.run("DROP TRIGGER block_index_frozen"));
        // Flip a nonce byte of the header at height 3: the hash no longer matches the key.
        ASSERT_TRUE(raw.run("UPDATE block_index SET header = CAST(substr(header, 1, 80) || x'0badf00d' AS BLOB) WHERE height = 3")) << raw.error();
    }
    LoadResult r = c.open();
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.exitCode, EXIT_DATABASE_CORRUPT);
    EXPECT_NE(r.error.find("block index corrupt"), std::string::npos) << r.error;
    EXPECT_NE(r.error.find("--reindex"), std::string::npos) << r.error;
}

// ST-4
TEST(Startup, VerifyDbBadUndo) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    grow(c, 6);
    const Hash256 victim = c.state().activeChain()[4]->hash;   // tip - 2
    c.close(true);
    {
        RawDb raw(c.dbPath());
        ASSERT_TRUE(raw.run("DROP TRIGGER undo_write_once"));
        // Change the checksum byte at the end of the record.
        ASSERT_TRUE(raw.run("UPDATE undo SET data = CAST(substr(data, 1, length(data) - 1) || x'00' AS BLOB) "
                            "WHERE block_hash = (SELECT hash FROM block_index WHERE height = 4)")) << raw.error();
    }
    LoadResult r = c.open();
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.exitCode, EXIT_DATABASE_CORRUPT);
    EXPECT_NE(r.error.find(hexOf(victim)), std::string::npos) << r.error;   // names the block
    EXPECT_NE(r.error.find("undo"), std::string::npos) << r.error;

    // Deeper than the check depth it is not looked at.
    PersistentChain deep;
    ASSERT_TRUE(deep.open().ok);
    grow(deep, 6);
    deep.close(true);
    {
        RawDb raw(deep.dbPath());
        ASSERT_TRUE(raw.run("DROP TRIGGER undo_write_once"));
        ASSERT_TRUE(raw.run("UPDATE undo SET data = CAST(substr(data, 1, length(data) - 1) || x'00' AS BLOB) "
                            "WHERE block_hash = (SELECT hash FROM block_index WHERE height = 2)"));
    }
    LoadOptions shallow;
    shallow.checkBlocks = 2;
    EXPECT_TRUE(deep.open(shallow).ok);                      // clean shutdown, depth 2: height 2 is not checked
    deep.close(true);
    LoadOptions whole;
    whole.checkBlocks = 0;
    EXPECT_FALSE(deep.open(whole).ok);                       // the whole chain is
}

// ST-5
TEST(Startup, UtxoStatsMismatch) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    grow(c, 6);
    c.close(true);
    {
        RawDb raw(c.dbPath());
        // Remove the coin of block 1: far below the VerifyDB depth, so only the statistics notice.
        ASSERT_TRUE(raw.run("DELETE FROM utxo WHERE height = 1")) << raw.error();
    }
    LoadOptions o;
    o.checkBlocks = 1;
    LoadResult r = c.open(o);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.exitCode, EXIT_DATABASE_CORRUPT);
    EXPECT_NE(r.error.find("statistics"), std::string::npos) << r.error;
    EXPECT_NE(r.error.find("--reindex-chainstate"), std::string::npos) << r.error;
}

TEST(Startup, TipAndMainChainInvariants) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    grow(c, 4);
    c.close(true);
    {
        // The best block is named in chain_state, but its main-chain flag is gone.
        RawDb raw(c.dbPath());
        ASSERT_TRUE(raw.run("UPDATE block_index SET status = status & ~32 WHERE height = 2")) << raw.error();
    }
    LoadResult r = c.open();
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.exitCode, EXIT_DATABASE_CORRUPT);
    EXPECT_NE(r.error.find("main-chain flag"), std::string::npos) << r.error;
}

TEST(Startup, MissingBodyIsCaughtBySelfCheck) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    grow(c, 4);
    c.close(true);
    {
        RawDb raw(c.dbPath());
        ASSERT_TRUE(raw.run("DELETE FROM block_data WHERE hash = (SELECT hash FROM block_index WHERE height = 2)"));
    }
    LoadResult r = c.open();
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.exitCode, EXIT_DATABASE_CORRUPT);
    EXPECT_NE(r.error.find("block_data"), std::string::npos) << r.error;
}

TEST(Startup, UncleanShutdownRunsHeavierChecks) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    grow(c, 4);
    c.close(false);                                          // crash: the clean flag stays 0
    {
        RawDb raw(c.dbPath());
        EXPECT_EQ(raw.scalar("SELECT value FROM chain_state WHERE name = 'clean_shutdown'"), 0);
    }
    LoadResult r = c.open();
    EXPECT_TRUE(r.ok) << r.error;
    c.close(true);
    {
        RawDb raw(c.dbPath());
        EXPECT_EQ(raw.scalar("SELECT value FROM chain_state WHERE name = 'clean_shutdown'"), 1);
    }
    ASSERT_TRUE(c.open().ok);
    {
        RawDb raw(c.dbPath());
        EXPECT_EQ(raw.scalar("SELECT value FROM chain_state WHERE name = 'clean_shutdown'"), 0);   // running again
    }
}

// REC: --reindex-chainstate rebuilds all derived data from the stored blocks.
TEST(Startup, ReindexChainstate) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    grow(c, 8);
    const Hash256 tip = c.state().tip()->hash;
    const UtxoStats stats = c.store().stats();
    c.close(true);
    {
        RawDb raw(c.dbPath());
        ASSERT_TRUE(raw.run("DELETE FROM utxo WHERE height < 5")) << raw.error();   // damaged derived data
    }
    EXPECT_FALSE(c.open().ok);                               // the normal start refuses

    LoadOptions o;
    o.reindexChainstate = true;
    LoadResult r = c.open(o);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(c.state().tip()->hash, tip);                   // every block was connected again
    EXPECT_EQ(c.store().stats(), stats);
    c.close(true);
    EXPECT_TRUE(c.open().ok);                                // and the next normal start is fine
}

// REC: --reindex rebuilds the block index from the stored block bytes.
TEST(Startup, ReindexRebuildsTheIndex) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    grow(c, 6);
    const BlockIndex* fork = c.state().activeChain()[3];
    const Block side = c.build(fork, {}, 1);
    ASSERT_TRUE(c.submit(side).valid());
    const Hash256 tip = c.state().tip()->hash;
    const UtxoStats stats = c.store().stats();
    c.close(true);
    {
        RawDb raw(c.dbPath());
        ASSERT_TRUE(raw.run("DROP TRIGGER block_index_frozen"));
        ASSERT_TRUE(raw.run("UPDATE block_index SET header = CAST(substr(header, 1, 80) || x'0badf00d' AS BLOB) WHERE height = 3"));
    }
    EXPECT_FALSE(c.open().ok);                               // the damaged index is refused ...

    LoadOptions o;
    o.reindex = true;
    LoadResult r = c.open(o);                                // ... and rebuilt from the blocks
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(c.state().tip()->hash, tip);
    EXPECT_EQ(c.store().stats(), stats);
    ASSERT_NE(c.state().find(side.getHash()), nullptr);      // the side block is indexed again
    c.close(true);
    {
        RawDb raw(c.dbPath());
        EXPECT_EQ(raw.scalar("SELECT value FROM chain_state WHERE name = 'reindex_in_progress'"), 0);
        EXPECT_EQ(raw.scalar("SELECT COUNT(*) FROM block_index"), 8);   // genesis, 6 main, 1 side
    }
    EXPECT_TRUE(c.open().ok);
}

TEST(Startup, InterruptedReindexRefusesNormalStart) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    grow(c, 2);
    c.close(true);
    {
        RawDb raw(c.dbPath());
        ASSERT_TRUE(raw.run("UPDATE chain_state SET value = 1 WHERE name = 'reindex_in_progress'"));
    }
    LoadResult r = c.open();
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.exitCode, EXIT_DATABASE_CORRUPT);
    EXPECT_NE(r.error.find("--reindex"), std::string::npos) << r.error;
    LoadOptions o;
    o.reindex = true;
    EXPECT_TRUE(c.open(o).ok);
}

// A spend must survive restart, VerifyDB and a rebuild of the chain state.
TEST(Startup, SpendsSurviveRestartAndReindex) {
    PersistentChain c;
    ASSERT_TRUE(c.open().ok);
    std::vector<Hash256> coinbases;
    for (int i = 0; i < 101; ++i) coinbases.push_back(c.extend().transactions[0].txid());
    // The coinbase of block 1 is mature at height 101.
    Transaction spend = spendTx(OutPoint{coinbases[0], 0}, 50 * Consensus::COIN, 3,
                                50 * Consensus::COIN - 10'000, 5, c.params());
    ASSERT_TRUE(c.submit(c.build(c.state().tip(), {spend})).valid());
    ASSERT_EQ(c.state().tip()->height, 102u);
    const UtxoStats stats = c.store().stats();
    EXPECT_EQ(stats.count, 102u + 1 - 1);        // 102 coinbases, the spent one replaced by the payment

    c.close(true);
    ASSERT_TRUE(c.open().ok);                    // VerifyDB disconnects and reconnects the spend
    EXPECT_EQ(c.store().stats(), stats);

    LoadOptions o;
    o.reindexChainstate = true;
    c.close(true);
    ASSERT_TRUE(c.open(o).ok);
    EXPECT_EQ(c.store().stats(), stats);
    Coin paid;
    EXPECT_TRUE(c.store().coins().getCoin(OutPoint{spend.txid(), 0}, paid));
    EXPECT_EQ(paid.amount, 50 * Consensus::COIN - 10'000);
    EXPECT_FALSE(c.store().coins().haveCoin(OutPoint{coinbases[0], 0}));
}
