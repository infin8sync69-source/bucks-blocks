#include "mempool.h"

#include <algorithm>
#include <ctime>

#include "consensus.h"
#include "gold_rules.h"
#include "serialize.h"
#include "sighash.h"

namespace Bucks {

namespace {

bool addMoney(uint64_t& sum, uint64_t amount) {
    if (amount > Consensus::MAX_MONEY || sum > Consensus::MAX_MONEY - amount) return false;
    sum += amount;
    return true;
}

Hash256 digestOfSpent(const std::vector<Coin>& coins) {
    Writer w;
    for (const Coin& c : coins) w.u64(c.amount).varBytes(c.scriptPubKey);
    return sha256(w.data());
}

bool hasGoldEventOutput(const Transaction& tx) {
    for (const TransactionOutput& out : tx.outputs) {
        std::vector<uint8_t> payload;
        if (parseNullData(out.scriptPubKey, &payload) &&
            GoldRules::classify(payload).kind == GoldRules::Kind::EVENT)
            return true;
    }
    return false;
}

} // namespace

Mempool::Mempool(const ChainParams& params, Clock clock, uint64_t maxBytes)
    : params_(params), clock_(std::move(clock)), maxBytes_(maxBytes), rng_(std::random_device{}()) {
    if (!clock_) clock_ = [] { return int64_t(std::time(nullptr)); };
}

const MempoolEntry* Mempool::find(const Hash256& txid) const {
    auto it = byTxid_.find(txid);
    return it == byTxid_.end() ? nullptr : &it->second;
}

uint64_t Mempool::minFeeRate() {
    const int64_t now = clock_();
    if (rollingMinFeeRate_ == 0) {
        rollingUpdated_ = now;
        return 0;
    }
    const int64_t halvings = (now - rollingUpdated_) / Policy::ROLLING_FEE_HALFLIFE;
    if (halvings > 0) {
        rollingMinFeeRate_ = halvings >= 63 ? 0 : (rollingMinFeeRate_ >> halvings);
        rollingUpdated_ += halvings * Policy::ROLLING_FEE_HALFLIFE;
    }
    if (rollingMinFeeRate_ < Policy::MIN_RELAY_FEERATE / 2) rollingMinFeeRate_ = 0;
    return rollingMinFeeRate_;
}

void Mempool::insert(MempoolEntry&& entry) {
    for (const TransactionInput& in : entry.tx.inputs)
        spentBy_[OutPoint{in.prevTxid, in.prevIndex}] = entry.txid;
    byFeeRate_.insert({entry.feeRate, entry.entryTime, entry.txid});
    totalBytes_ += entry.size;
    const Hash256 id = entry.txid;
    byTxid_.emplace(id, std::move(entry));
}

bool Mempool::remove(const Hash256& txid) {
    auto it = byTxid_.find(txid);
    if (it == byTxid_.end()) return false;
    const MempoolEntry& e = it->second;
    for (const TransactionInput& in : e.tx.inputs) spentBy_.erase(OutPoint{in.prevTxid, in.prevIndex});
    byFeeRate_.erase({e.feeRate, e.entryTime, e.txid});
    totalBytes_ -= e.size;
    byTxid_.erase(it);
    return true;
}

std::vector<MempoolEntry> Mempool::byFeeRateDescending() const {
    std::vector<MempoolEntry> out;
    out.reserve(byTxid_.size());
    for (auto it = byFeeRate_.rbegin(); it != byFeeRate_.rend(); ++it)
        out.push_back(byTxid_.at(std::get<2>(*it)));
    return out;
}

// ---------------------------------------------------------------- accept

ValidationState Mempool::accept(const Transaction& tx, const ChainState& chain,
                                const AcceptOptions& opt) {
    using R = ValidationResult;
    ValidationState st;

    const BlockIndex* tip = chain.tip();
    if (!tip) {
        st.invalid(R::INTERNAL, "no-tip");
        return st;
    }
    const uint32_t nextHeight = tip->height + 1;
    const int64_t now = clock_();

    // 1. A peer sends transactions while we are still catching up.
    if (!opt.reAdd && opt.fromPeer && opt.inInitialBlockDownload) {
        st.invalid(R::TX_POLICY, "initial-block-download");
        return st;
    }

    const std::vector<uint8_t> raw = tx.serialize();
    const Hash256 txid = doubleSha256(raw);
    const uint32_t size = uint32_t(raw.size());

    // 2.
    if (byTxid_.count(txid)) {
        st.invalid(R::DUPLICATE, "txn-already-in-mempool");
        return st;
    }
    // 3.
    if (!Rules::CheckTransaction(tx, st)) return st;
    // 4.
    if (tx.isCoinbase()) {
        st.invalid(R::INVALID, "coinbase");
        return st;
    }
    // 5.
    if (!Policy::IsStandardTx(tx, st)) return st;
    // 6.
    if (!Rules::IsFinal(tx, nextHeight, tip->medianTimePast())) {
        st.invalid(R::TX_PREMATURE, "non-final");
        return st;
    }
    // 7. First seen wins.
    for (const TransactionInput& in : tx.inputs) {
        if (spentBy_.count(OutPoint{in.prevTxid, in.prevIndex})) {
            st.invalid(R::TX_CONFLICT, "txn-mempool-conflict");
            return st;
        }
    }

    // 8. Every input must be a confirmed coin at the tip.
    std::vector<Coin> coins;
    coins.reserve(tx.inputs.size());
    for (const TransactionInput& in : tx.inputs) {
        Coin coin;
        if (!chain.coins().getCoin(OutPoint{in.prevTxid, in.prevIndex}, coin)) {
            auto parent = byTxid_.find(in.prevTxid);
            const bool inPool = parent != byTxid_.end() && in.prevIndex < parent->second.tx.outputs.size();
            st.invalid(R::TX_MISSING_INPUTS, inPool ? "mempool-parent" : "missing-inputs");
            if (opt.allowOrphans && !opt.reAdd) addOrphan(tx, txid, opt.peerId);
            return st;
        }
        coins.push_back(std::move(coin));
    }

    // 9. Coinbase maturity at the height of the next block.
    for (const Coin& c : coins) {
        if (c.isCoinbase && int64_t(nextHeight) - int64_t(c.height) < int64_t(Consensus::COINBASE_MATURITY)) {
            st.invalid(R::TX_PREMATURE, "premature-spend-of-coinbase");
            return st;
        }
    }

    // 10. C-INVALUE, C-INBELOWOUT.
    uint64_t inSum = 0, outSum = 0;
    for (const Coin& c : coins) {
        if (!addMoney(inSum, c.amount)) {
            st.invalid(R::INVALID, "bad-txns-inputvalues-outofrange");
            return st;
        }
    }
    for (const TransactionOutput& out : tx.outputs) {
        if (!addMoney(outSum, out.amount)) {
            st.invalid(R::INVALID, "bad-txns-txouttotal-toolarge");
            return st;
        }
    }
    if (inSum < outSum) {
        st.invalid(R::INVALID, "bad-txns-in-belowout");
        return st;
    }
    const uint64_t fee = inSum - outSum;
    const uint64_t feeRate = Policy::FeeRate(fee, size);

    // 11. Fee floor (relay minimum or the rolling floor raised by evictions).
    if (!opt.reAdd) {
        const uint64_t rate = std::max(Policy::MIN_RELAY_FEERATE, minFeeRate());
        if (fee < Policy::FeeForSize(rate, size)) {
            st.invalid(R::TX_POLICY, "min-fee-not-met");
            return st;
        }
    }

    // 12.
    const uint64_t sigOps = Rules::TxSigOps(tx);
    if (sigOps > Policy::MAX_STANDARD_TX_SIGOPS) {
        st.invalid(R::TX_POLICY, "too-many-sigops");
        return st;
    }

    // 13. C-SIG for every input.
    std::vector<SpentOutput> spentOut;
    spentOut.reserve(coins.size());
    for (const Coin& c : coins) spentOut.push_back(SpentOutput{c.amount, c.scriptPubKey});
    const SigHashCache cache(tx, spentOut);
    for (size_t k = 0; k < tx.inputs.size(); ++k) {
        const SpendError e = verifyP2PKHInput(tx, uint32_t(k), cache, spentOut[k], params_.genesisHash);
        if (e != SpendError::OK) {
            st.invalid(R::INVALID, spendErrorCode(e));
            return st;
        }
    }

    // 14.
    if (!GoldRules::checkForMempool(tx, chain.gold(), nextHeight, st)) return st;

    // 15. Make room by evicting cheaper entries, or refuse.
    if (!opt.reAdd && totalBytes_ + size > maxBytes_) {
        const uint64_t need = totalBytes_ + size - maxBytes_;
        uint64_t freed = 0, highest = 0;
        std::vector<Hash256> victims;
        for (const auto& key : byFeeRate_) {
            if (std::get<0>(key) >= feeRate || freed >= need) break;
            const MempoolEntry& e = byTxid_.at(std::get<2>(key));
            victims.push_back(e.txid);
            freed += e.size;
            highest = std::get<0>(key);
        }
        if (freed < need) {
            st.invalid(R::TX_POLICY, "mempool-full");
            return st;
        }
        for (const Hash256& v : victims) remove(v);
        rollingMinFeeRate_ = std::max(rollingMinFeeRate_, highest + Policy::INCREMENTAL_RELAY_FEERATE);
        rollingUpdated_ = now;
    }

    // 16.
    MempoolEntry entry;
    entry.tx = tx;
    entry.txid = txid;
    entry.size = size;
    entry.fee = fee;
    entry.feeRate = feeRate;
    entry.entryTime = now;
    entry.entryHeight = tip->height;
    entry.sigOps = uint32_t(sigOps);
    entry.spentDigest = digestOfSpent(coins);
    entry.hasGoldEvent = hasGoldEventOutput(tx);
    insert(std::move(entry));
    return st;
}

// ---------------------------------------------------------------- chain steps

void Mempool::trimToSize() {
    while (totalBytes_ > maxBytes_ && !byFeeRate_.empty())
        remove(std::get<2>(*byFeeRate_.begin()));
}

void Mempool::expireEntries() {
    const int64_t now = clock_();
    std::vector<Hash256> old;
    for (const auto& [id, e] : byTxid_)
        if (e.entryTime + Policy::MEMPOOL_EXPIRY < now) old.push_back(id);
    for (const Hash256& id : old) remove(id);
}

void Mempool::revalidate(const ChainState& chain) {
    const BlockIndex* tip = chain.tip();
    if (!tip) return;
    const uint32_t nextHeight = tip->height + 1;
    const int64_t mtp = tip->medianTimePast();
    const GoldRules::GoldState gold = chain.gold();

    std::vector<Hash256> bad;
    for (const auto& [id, e] : byTxid_) {
        bool ok = Rules::IsFinal(e.tx, nextHeight, mtp);
        std::vector<Coin> coins;
        for (size_t k = 0; ok && k < e.tx.inputs.size(); ++k) {
            Coin c;
            const OutPoint op{e.tx.inputs[k].prevTxid, e.tx.inputs[k].prevIndex};
            if (!chain.coins().getCoin(op, c)) { ok = false; break; }
            if (c.isCoinbase && int64_t(nextHeight) - int64_t(c.height) < int64_t(Consensus::COINBASE_MATURITY))
                ok = false;
            coins.push_back(std::move(c));
        }
        // Same amounts and scripts means the signatures are still valid.
        if (ok && digestOfSpent(coins) != e.spentDigest) ok = false;
        ValidationState gst;
        if (ok && !GoldRules::checkForMempool(e.tx, gold, nextHeight, gst)) ok = false;
        if (!ok) bad.push_back(id);
    }
    for (const Hash256& id : bad) remove(id);
}

void Mempool::updateForStep(const std::vector<Block>& disconnected,
                            const std::vector<Block>& connected, const ChainState& chain) {
    // 1. Transactions of disconnected blocks come back (oldest block first).
    AcceptOptions readd;
    readd.reAdd = true;
    readd.allowOrphans = false;
    for (const Block& b : disconnected)
        for (size_t t = 1; t < b.transactions.size(); ++t) accept(b.transactions[t], chain, readd);

    // 2. Confirmed transactions and everything that conflicts with the new blocks.
    for (const Block& b : connected) {
        for (const Transaction& tx : b.transactions) {
            remove(tx.txid());
            if (tx.isCoinbase()) continue;
            for (const TransactionInput& in : tx.inputs) {
                auto it = spentBy_.find(OutPoint{in.prevTxid, in.prevIndex});
                if (it != spentBy_.end()) remove(it->second);
            }
        }
    }

    // 3/4. After a reorg every entry is checked again; otherwise only gold
    // events can have become invalid (finality and maturity only improve).
    if (!disconnected.empty()) {
        revalidate(chain);
    } else {
        const BlockIndex* tip = chain.tip();
        if (tip) {
            const GoldRules::GoldState gold = chain.gold();
            std::vector<Hash256> bad;
            for (const auto& [id, e] : byTxid_) {
                ValidationState gst;
                if (e.hasGoldEvent && !GoldRules::checkForMempool(e.tx, gold, tip->height + 1, gst))
                    bad.push_back(id);
            }
            for (const Hash256& id : bad) remove(id);
        }
    }

    // 5.
    expireEntries();
    trimToSize();
    expireOrphans();
    retryOrphans(chain);
}

size_t Mempool::reloadValidated(const std::vector<std::vector<uint8_t>>& rawTxs,
                                const ChainState& chain, std::vector<size_t>& dropped) {
    size_t loaded = 0;
    AcceptOptions opt;
    opt.allowOrphans = false;
    for (size_t i = 0; i < rawTxs.size(); ++i) {
        try {
            const Transaction tx = Transaction::deserialize(rawTxs[i]);
            if (accept(tx, chain, opt).valid()) {
                ++loaded;
                continue;
            }
        } catch (const ParseError&) {
        }
        dropped.push_back(i);
    }
    return loaded;
}

// ---------------------------------------------------------------- orphans

bool Mempool::evictOrphan(const std::vector<Hash256>& candidates) {
    if (candidates.empty()) return false;
    orphans_.erase(candidates[rng_() % candidates.size()]);
    return true;
}

void Mempool::expireOrphans() {
    const int64_t now = clock_();
    for (auto it = orphans_.begin(); it != orphans_.end();) {
        if (it->second.expires <= now) it = orphans_.erase(it);
        else ++it;
    }
}

void Mempool::addOrphan(const Transaction& tx, const Hash256& txid, uint64_t peerId) {
    if (tx.serialize().size() > Policy::MAX_ORPHAN_TX_SIZE) return;
    if (orphans_.count(txid)) return;
    expireOrphans();

    // A peer that is over its share only displaces its own orphans, so it
    // cannot push honest ones out. Eviction is random so it cannot be targeted.
    std::vector<Hash256> mine;
    for (const auto& [id, o] : orphans_)
        if (o.peerId == peerId) mine.push_back(id);
    if (mine.size() >= Policy::MAX_ORPHAN_TXS_PER_PEER) evictOrphan(mine);

    if (orphans_.size() >= Policy::MAX_ORPHAN_TXS) {
        std::vector<Hash256> all;
        all.reserve(orphans_.size());
        for (const auto& [id, o] : orphans_) all.push_back(id);
        evictOrphan(all);
    }
    orphans_[txid] = Orphan{tx, peerId, clock_() + Policy::ORPHAN_TX_EXPIRY};
}

void Mempool::retryOrphans(const ChainState& chain) {
    std::vector<Hash256> ids;
    ids.reserve(orphans_.size());
    for (const auto& [id, o] : orphans_) ids.push_back(id);

    AcceptOptions opt;
    opt.allowOrphans = false;
    for (const Hash256& id : ids) {
        auto it = orphans_.find(id);
        if (it == orphans_.end()) continue;
        const Transaction tx = it->second.tx;   // accept() may touch the pool
        const ValidationState st = accept(tx, chain, opt);
        // Accepted, invalid or duplicate: it leaves. Still missing inputs: it stays.
        if (st.result != ValidationResult::TX_MISSING_INPUTS) orphans_.erase(id);
    }
}

} // namespace Bucks
