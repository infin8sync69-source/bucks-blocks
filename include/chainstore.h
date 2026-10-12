#ifndef BUCKS_CHAINSTORE_H
#define BUCKS_CHAINSTORE_H

#include <cstdint>
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
    CoinChanges coins;                                         // includes the best block
    GoldRules::GoldState gold;
    std::vector<std::pair<Hash256, BlockUndo>> undos;          // connected blocks
    std::vector<std::pair<Hash256, uint32_t>> statuses;        // new status of touched entries
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
};

} // namespace Bucks

#endif // BUCKS_CHAINSTORE_H
