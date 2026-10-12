#ifndef BUCKS_CHAINSTORE_H
#define BUCKS_CHAINSTORE_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "blockheader.h"
#include "coins.h"
#include "gold_rules.h"
#include "undo.h"

namespace Bucks {

// Storage contract of the chain state (docs/design/consensus-validation.md
// section 8.8). The SQLite implementation is plan step 15; tests use an
// in-memory store. Names may differ from the storage spec, the semantics are
// the same.

// Everything one ActivateBestChain step writes. It is committed in one
// transaction: all of it, or nothing.
struct StepCommit {
    Hash256 expectedOldBest{};                                 // the store's best block must equal this
    CoinChanges coins;                                         // includes the best block
    GoldRules::GoldState gold;
    std::vector<std::pair<Hash256, BlockUndo>> undos;          // connected blocks
    std::vector<std::pair<Hash256, uint32_t>> statuses;        // new status of touched entries
};

// A coin with its outpoint, as returned by address queries.
struct CoinRow {
    OutPoint op;
    Coin coin;
};

// One stored block-index row (consensus section 8.1, storage spec section 6).
struct StoredIndexRow {
    Hash256 hash{};
    BlockHeader header;
    U256 chainWork;          // as stored; the loader recomputes and compares it
    uint32_t status = 0;
    uint32_t txCount = 0;
};

// The chain-state keys (storage spec section 6.3).
struct StoredState {
    std::string network;
    Hash256 genesis{};
    Hash256 best{};
    uint32_t bestHeight = 0;
    UtxoStats stats;
    bool cleanShutdown = true;
    bool reindexInProgress = false;
};

class ChainStore {
public:
    virtual ~ChainStore() = default;

    // Block bytes are write-once, keyed by block hash.
    virtual bool putBlock(const Hash256& hash, const std::vector<uint8_t>& bytes) = 0;
    virtual bool getBlock(const Hash256& hash, std::vector<uint8_t>& bytes) const = 0;

    virtual bool getUndo(const Hash256& hash, BlockUndo& undo) const = 0;

    // One index row per header: the 84 header bytes and the status bits.
    virtual bool putIndexRow(const Hash256& hash, const BlockHeader& header, uint32_t status) = 0;

    // Atomic. Returns false when nothing was committed.
    virtual bool commitStep(const StepCommit& step) = 0;

    // Committed coin set (includes the best block marker).
    virtual const CoinsView& coins() const = 0;
    virtual GoldRules::GoldState gold() const = 0;

    // ---- Persistent stores only (defaults: not supported) ----
    // True for a store that was never initialised with a genesis block.
    virtual bool isEmpty() const { return true; }
    virtual bool loadState(StoredState&, std::string& err) const { err = "not supported"; return false; }
    virtual bool loadIndex(std::vector<StoredIndexRow>&, std::string& err) const { err = "not supported"; return false; }
    // Statistics maintained from the real row changes of every commit.
    virtual UtxoStats stats() const { return UtxoStats{}; }
    // Statistics recomputed by scanning every coin.
    virtual bool scanStats(UtxoStats&, std::string& err) const { err = "not supported"; return false; }
    virtual bool setCleanShutdown(bool) { return true; }
    // Unspent coins paid to a key hash, ordered by outpoint (uses the hash160 index).
    virtual bool coinsByHash160(const Hash160&, size_t /*limit*/, std::vector<CoinRow>&) const { return false; }
    // Balance and number of unspent coins of a key hash.
    virtual bool summaryByHash160(const Hash160&, uint64_t& /*balance*/, uint64_t& /*count*/) const { return false; }
    // Database-level consistency checks. tier 1: row counts; tier 2: also quick_check and
    // foreign_key_check; tier 3: also integrity_check and a re-hash of every stored block.
    virtual bool selfCheck(int /*tier*/, std::string&) const { return true; }
    // --reindex-chainstate: drop all derived data, keep index and block bytes (spec 9.4).
    virtual bool resetChainState(std::string& err) { err = "not supported"; return false; }
    // --reindex, step 2: rebuild the index from stored block bytes (spec 9.4).
    virtual bool reindexBegin(const BlockHeader&, const Hash256&, std::string& err) { err = "not supported"; return false; }
    virtual bool listStoredBlocks(std::vector<std::pair<Hash256, BlockHeader>>&, std::string& err) const { err = "not supported"; return false; }
    virtual bool deleteBlockData(const Hash256&) { return false; }
    virtual bool reindexFinish(std::string& err) { err = "not supported"; return false; }
};

} // namespace Bucks

#endif // BUCKS_CHAINSTORE_H
