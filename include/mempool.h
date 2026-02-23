#ifndef MEMPOOL_H
#define MEMPOOL_H

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <cstdint>

#include "transaction.h"
#include "utxodb.h"
#include "consensus.h"

namespace Bucks {

/**
 * ============================================================
 *  MEMPOOL (POLICY LAYER — NOT CONSENSUS)
 *  ------------------------------------------------------------
 *  - Holds unconfirmed transactions
 *  - Enforces local admission rules
 *  - Prevents double-spends
 *  - Supplies miner with tx candidates
 *
 *  ⚠️ Mempool rules MAY differ across nodes
 * ============================================================
 */
class Mempool {
public:
    Mempool();

    /**
     * Attempt to add a transaction to the mempool.
     *
     * Checks:
     * - Stateless tx validity
     * - UTXO availability
     * - No double-spend (local)
     * - Fee sanity
     */
    bool addTransaction(
        const Transaction& tx,
        const UTXODB& utxoDB
    );

    /**
     * Remove a transaction from the mempool.
     * (called when included in a block)
     */
    void removeTransaction(const Hash256& txid);

    /**
     * Remove transactions that conflict with a confirmed block.
     */
    void removeConflicts(const Transaction& confirmedTx);

    /**
     * Check whether a txid exists in mempool.
     */
    bool exists(const Hash256& txid) const;

    /**
     * Get transactions sorted for mining.
     * (fee-priority or FIFO for now)
     */
    std::vector<Transaction> getTransactionsForBlock(
        size_t maxBlockSize
    ) const;

    /**
     * Clear entire mempool (e.g., reorg recovery).
     */
    void clear();

    /**
     * Current mempool size.
     */
    size_t size() const;

private:
    // ---- Internal storage ----
    std::unordered_map<Hash256, Transaction> transactions;

    /**
     * Tracks spent outpoints inside mempool:
     *   (txid, vout) → spending txid
     *
     * Prevents double-spends before confirmation.
     */
    struct Outpoint {
        Hash256 txid;
        uint32_t vout;

        bool operator==(const Outpoint& other) const;
    };

    struct OutpointHasher {
        size_t operator()(const Outpoint& o) const;
    };

    std::unordered_map<Outpoint, Hash256, OutpointHasher> spentOutpoints;

    mutable std::mutex mutex;

    // ---- Internal helpers ----
    bool checkDoubleSpend(const Transaction& tx) const;
    void markInputsSpent(const Transaction& tx);
    void unmarkInputsSpent(const Transaction& tx);
};

} // namespace Bucks

#endif // MEMPOOL_H
