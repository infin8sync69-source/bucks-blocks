#ifndef BUCKS_CHAINDB_H
#define BUCKS_CHAINDB_H

#include <memory>
#include <string>

#include "chainparams.h"
#include "chainstore.h"

struct sqlite3;

namespace Bucks {

// SQLite implementation of ChainStore (docs/design/storage-concurrency.md):
// schema v2 with constraints and write-once triggers, WAL with
// synchronous=FULL, one BEGIN IMMEDIATE transaction per chain step, and
// statistics computed from the rows that really changed.
//
// One connection, used by one thread at a time (the chain lock of plan
// step 16). The reader pool, backups and the crash harness of the storage
// spec are later steps.

// Process exit codes of the storage spec section 4.
enum StorageExit : int {
    EXIT_STORAGE_OK = 0,
    EXIT_PARAMS_MISMATCH = 3,    // network or genesis does not match the database
    EXIT_STORAGE_FAILURE = 4,    // I/O, permissions, symlink, failed commit
    EXIT_DATABASE_CORRUPT = 6,   // checks failed; --reindex / --reindex-chainstate
    EXIT_UNSUPPORTED_VERSION = 7
};

struct OpenResult;

class SqliteChainStore final : public ChainStore {
public:
    // Opens <dir>/chain.sqlite, creating the directory (mode 0700) and the
    // database (mode 0600) if needed. Returns nullptr and fills err/exitCode on failure.
    static std::unique_ptr<SqliteChainStore> open(const std::string& dir, const ChainParams& params,
                                                  std::string& err, int& exitCode);
    ~SqliteChainStore() override;

    // ---- ChainStore ----
    bool putBlock(const Hash256& hash, const std::vector<uint8_t>& bytes) override;
    bool getBlock(const Hash256& hash, std::vector<uint8_t>& bytes) const override;
    bool getUndo(const Hash256& hash, BlockUndo& undo) const override;
    bool putIndexRow(const Hash256& hash, const BlockHeader& header, uint32_t status) override;
    bool commitStep(const StepCommit& step) override;
    const CoinsView& coins() const override;
    GoldRules::GoldState gold() const override { return gold_; }

    bool isEmpty() const override;
    bool loadState(StoredState& out, std::string& err) const override;
    bool loadIndex(std::vector<StoredIndexRow>& out, std::string& err) const override;
    UtxoStats stats() const override { return stats_; }
    bool scanStats(UtxoStats& out, std::string& err) const override;
    bool setCleanShutdown(bool clean) override;
    bool coinsByHash160(const Hash160& key, size_t limit, std::vector<CoinRow>& out) const override;
    bool summaryByHash160(const Hash160& key, uint64_t& balance, uint64_t& count) const override;
    bool selfCheck(int tier, std::string& err) const override;
    bool resetChainState(std::string& err) override;
    bool reindexBegin(const BlockHeader& genesisHeader, const Hash256& genesisHash, std::string& err) override;
    bool listStoredBlocks(std::vector<std::pair<Hash256, BlockHeader>>& out, std::string& err) const override;
    bool deleteBlockData(const Hash256& hash) override;
    bool reindexFinish(std::string& err) override;

    // ---- Test hooks and diagnostics ----
    // The next commitStep performs every write, then rolls back instead of committing.
    bool failNextCommit = false;
    const std::string& lastError() const { return lastError_; }
    std::string path() const { return path_; }

private:
    class CoinsViewDB;
    SqliteChainStore() = default;

    bool exec(const char* sql, std::string* err = nullptr) const;
    bool createSchema(const ChainParams& params, std::string& err);
    bool loadCached(std::string& err);

    sqlite3* db_ = nullptr;
    int lockFd_ = -1;   // exclusive flock on <dir>/chain.lock: one process per data directory
    std::string path_;
    std::unique_ptr<CoinsViewDB> coins_;
    UtxoStats stats_;
    GoldRules::GoldState gold_;
    mutable std::string lastError_;
};

} // namespace Bucks

#endif // BUCKS_CHAINDB_H
