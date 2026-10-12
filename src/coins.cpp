#include "coins.h"

#include <algorithm>

#include "consensus.h"
#include "serialize.h"

namespace Bucks {

U256 coinDigest(const OutPoint& op, const Coin& coin) {
    Writer w;
    w.hash256(op.txid).u32(op.index).u32(coin.height).u8(coin.isCoinbase ? 1 : 0)
        .u64(coin.amount).varBytes(coin.scriptPubKey);
    return U256::fromBytesBE(sha256(w.data()));
}

bool UtxoStats::apply(const CoinChanges& changes) {
    UtxoStats next = *this;
    for (const auto& r : changes.removed) {
        if (next.count == 0 || next.totalAmount < r.second.amount) return false;
        next.count -= 1;
        next.totalAmount -= r.second.amount;
        next.digest -= coinDigest(r.first, r.second);
    }
    for (const auto& a : changes.added) {
        if (a.second.amount > Consensus::MAX_MONEY ||
            next.totalAmount > Consensus::MAX_MONEY - a.second.amount)
            return false;
        next.count += 1;
        next.totalAmount += a.second.amount;
        next.digest += coinDigest(a.first, a.second);
    }
    *this = next;
    return true;
}

// ---------------------------------------------------------------------------
// CoinsViewCache
// ---------------------------------------------------------------------------

CoinsViewCache::Entry& CoinsViewCache::fetch(const OutPoint& op) const {
    auto it = map_.find(op);
    if (it != map_.end()) return it->second;
    Entry e;
    Coin c;
    if (base_.getCoin(op, c)) e.original = c;
    e.current = e.original;
    return map_.emplace(op, std::move(e)).first->second;
}

bool CoinsViewCache::getCoin(const OutPoint& op, Coin& out) const {
    const Entry& e = fetch(op);
    if (!e.current) return false;
    out = *e.current;
    return true;
}

Hash256 CoinsViewCache::bestBlock() const {
    return best_ ? *best_ : base_.bestBlock();
}

bool CoinsViewCache::addCoin(const OutPoint& op, const Coin& coin) {
    Entry& e = fetch(op);
    if (e.current) return false;
    e.current = coin;
    return true;
}

bool CoinsViewCache::spendCoin(const OutPoint& op, Coin* spent) {
    Entry& e = fetch(op);
    if (!e.current) return false;
    if (spent) *spent = *e.current;
    e.current.reset();
    return true;
}

CoinChanges CoinsViewCache::changes() const {
    CoinChanges out;
    for (const auto& [op, e] : map_) {
        if (e.original == e.current) continue;  // untouched, or restored exactly
        if (e.original) out.removed.emplace_back(op, *e.original);
        if (e.current) out.added.emplace_back(op, *e.current);
    }
    out.bestBlock = best_;
    return out;
}

// ---------------------------------------------------------------------------
// MemoryCoinsView
// ---------------------------------------------------------------------------

bool MemoryCoinsView::getCoin(const OutPoint& op, Coin& out) const {
    auto it = coins_.find(op);
    if (it == coins_.end()) return false;
    out = it->second;
    return true;
}

bool MemoryCoinsView::apply(const CoinChanges& changes) {
    UtxoStats next = stats_;
    if (!next.apply(changes)) return false;

    for (const auto& r : changes.removed) {
        auto it = coins_.find(r.first);
        if (it == coins_.end() || it->second != r.second) return false;
    }
    // Added coins may reuse an outpoint that is being removed in this change.
    std::map<OutPoint, bool, OutPointLess> removing;
    for (const auto& r : changes.removed) removing[r.first] = true;
    std::map<OutPoint, bool, OutPointLess> adding;
    for (const auto& a : changes.added) {
        if (coins_.count(a.first) && !removing.count(a.first)) return false;
        if (!adding.emplace(a.first, true).second) return false;
    }

    for (const auto& r : changes.removed) coins_.erase(r.first);
    for (const auto& a : changes.added) coins_.emplace(a.first, a.second);
    if (changes.bestBlock) best_ = *changes.bestBlock;
    stats_ = next;
    return true;
}

void MemoryCoinsView::insertForTest(const OutPoint& op, const Coin& coin) {
    CoinChanges c;
    c.added.emplace_back(op, coin);
    apply(c);
}

UtxoStats MemoryCoinsView::recomputeStats() const {
    UtxoStats s;
    for (const auto& [op, coin] : coins_) {
        s.count += 1;
        s.totalAmount += coin.amount;
        s.digest += coinDigest(op, coin);
    }
    return s;
}

} // namespace Bucks
