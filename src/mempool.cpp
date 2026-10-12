#include "mempool.h"
#include "validation.h"

namespace Bucks {

Mempool::Mempool() {}

bool Mempool::Outpoint::operator==(const Outpoint& other) const {
    return txid == other.txid && vout == other.vout;
}

size_t Mempool::OutpointHasher::operator()(const Outpoint& o) const {
    size_t h = 0;
    for (size_t i = 0; i < 8 && i < o.txid.size(); ++i)
        h ^= static_cast<size_t>(o.txid[i]) << (i * 8);
    h ^= std::hash<uint32_t>{}(o.vout);
    return h;
}

bool Mempool::addTransaction(const Transaction& tx, const UTXODB& utxoDB) {
    std::lock_guard<std::mutex> lock(mutex);

    Hash256 txid = tx.txid();

    if (transactions.count(txid))
        return false;

    if (checkDoubleSpend(tx))
        return false;

    transactions.emplace(txid, tx);
    markInputsSpent(tx);
    return true;
}

void Mempool::removeTransaction(const Hash256& txid) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = transactions.find(txid);
    if (it == transactions.end()) return;
    unmarkInputsSpent(it->second);
    transactions.erase(it);
}

void Mempool::removeConflicts(const Transaction& confirmedTx) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& in : confirmedTx.inputs) {
        Outpoint op{in.prevTxid, in.prevIndex};
        auto sit = spentOutpoints.find(op);
        if (sit != spentOutpoints.end()) {
            Hash256 spendingTxid = sit->second;
            auto tit = transactions.find(spendingTxid);
            if (tit != transactions.end()) {
                unmarkInputsSpent(tit->second);
                transactions.erase(tit);
            }
        }
    }
}

bool Mempool::exists(const Hash256& txid) const {
    std::lock_guard<std::mutex> lock(mutex);
    return transactions.count(txid) != 0;
}

std::vector<Transaction> Mempool::getTransactionsForBlock(size_t maxBlockSize) const {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<Transaction> result;
    for (const auto& [id, tx] : transactions) {
        result.push_back(tx);
        if (result.size() >= maxBlockSize) break;
    }
    return result;
}

void Mempool::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    transactions.clear();
    spentOutpoints.clear();
}

size_t Mempool::size() const {
    std::lock_guard<std::mutex> lock(mutex);
    return transactions.size();
}

bool Mempool::checkDoubleSpend(const Transaction& tx) const {
    for (const auto& in : tx.inputs) {
        Outpoint op{in.prevTxid, in.prevIndex};
        if (spentOutpoints.count(op)) return true;
    }
    return false;
}

void Mempool::markInputsSpent(const Transaction& tx) {
    Hash256 txid = tx.txid();
    for (const auto& in : tx.inputs) {
        Outpoint op{in.prevTxid, in.prevIndex};
        spentOutpoints.emplace(op, txid);
    }
}

void Mempool::unmarkInputsSpent(const Transaction& tx) {
    for (const auto& in : tx.inputs) {
        Outpoint op{in.prevTxid, in.prevIndex};
        spentOutpoints.erase(op);
    }
}

} // namespace Bucks
