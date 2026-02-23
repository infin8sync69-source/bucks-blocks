#include "mempool.h"

namespace Bucks {

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------

Mempool::Mempool(UTXODB& db)
    : utxoDB(db) {}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

std::string Mempool::outpointKey(const OutPoint& op) const {
    return std::string(
        reinterpret_cast<const char*>(op.txid.data()),
        32
    ) + std::to_string(op.index);
}

// -----------------------------------------------------------------------------
// Add transaction (POLICY checks)
// -----------------------------------------------------------------------------

bool Mempool::addTransaction(const Transaction& tx) {
    std::lock_guard<std::mutex> lock(mtx);

    // Coinbase is NEVER allowed in mempool
    if (tx.isCoinbase())
        return false;

    // Already exists
    if (pool.count(tx.txid))
        return false;

    uint64_t inputSum = 0;
    uint64_t outputSum = 0;

    // -------------------------------------------------------------------------
    // Inputs
    // -------------------------------------------------------------------------

    for (size_t i = 0; i < tx.inputs.size(); ++i) {
        const TransactionInput& in = tx.inputs[i];
        OutPoint op{in.prevTxid, in.prevIndex};

        std::string key = outpointKey(op);

        // Already spent by another mempool tx
        if (spent.count(key))
            return false;

        // Must exist in UTXO set
        UTXO prev;
        if (!utxoDB.get(op, prev))
            return false;

        // Verify signature
        if (!tx.verifyInput(i))
            return false;

        inputSum += prev.amount;
    }

    // -------------------------------------------------------------------------
    // Outputs
    // -------------------------------------------------------------------------

    for (const auto& out : tx.outputs) {
        if (out.amount == 0)
            return false;
        outputSum += out.amount;
    }

    // Fee sanity (policy rule)
    if (inputSum < outputSum)
        return false;

    // -------------------------------------------------------------------------
    // Accept transaction
    // -------------------------------------------------------------------------

    pool.emplace(tx.txid, tx);

    // Mark spent outpoints
    for (const auto& in : tx.inputs) {
        OutPoint op{in.prevTxid, in.prevIndex};
        spent.insert(outpointKey(op));
    }

    return true;
}

// -----------------------------------------------------------------------------
// Remove transaction
// -----------------------------------------------------------------------------

void Mempool::removeTransaction(const TxID& txid) {
    std::lock_guard<std::mutex> lock(mtx);

    auto it = pool.find(txid);
    if (it == pool.end())
        return;

    // Free spent outpoints
    for (const auto& in : it->second.inputs) {
        OutPoint op{in.prevTxid, in.prevIndex};
        spent.erase(outpointKey(op));
    }

    pool.erase(it);
}

// -----------------------------------------------------------------------------
// Queries
// -----------------------------------------------------------------------------

bool Mempool::exists(const TxID& txid) const {
    std::lock_guard<std::mutex> lock(mtx);
    return pool.count(txid) != 0;
}

size_t Mempool::size() const {
    std::lock_guard<std::mutex> lock(mtx);
    return pool.size();
}

// -----------------------------------------------------------------------------
// Miner interface
// -----------------------------------------------------------------------------

std::vector<Transaction> Mempool::collect(size_t maxCount) const {
    std::lock_guard<std::mutex> lock(mtx);

    std::vector<Transaction> txs;
    txs.reserve(maxCount);

    for (const auto& it : pool) {
        txs.push_back(it.second);
        if (txs.size() >= maxCount)
            break;
    }

    return txs;
}

} // namespace Bucks
