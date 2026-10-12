#ifndef MEMPOOL_H
#define MEMPOOL_H

#include <cstdint>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "block.h"
#include "chainparams.h"
#include "chainstate.h"
#include "policy.h"
#include "transaction.h"
#include "validation_rules.h"

namespace Bucks {

// Mempool and orphan pool (docs/design/consensus-validation.md section 9).
// This is policy, but every transaction in it passed the same consensus
// checks a block would apply at height tip + 1. Version 1 accepts only
// transactions whose inputs are confirmed coins, so entries are independent
// (no ancestor or descendant tracking). Not thread-safe: the chain lock of
// plan step 16 covers it.

struct MempoolEntry {
    Transaction tx;
    Hash256 txid{};
    uint32_t size = 0;        // serialized bytes
    uint64_t fee = 0;
    uint64_t feeRate = 0;     // fee * 1000 / size (awraq per 1,000 bytes)
    int64_t entryTime = 0;
    uint32_t entryHeight = 0;
    uint32_t sigOps = 0;
    Hash256 spentDigest{};    // SHA256 over (amount, script) of the coins it spends
    bool hasGoldEvent = false;
};

struct AcceptOptions {
    bool fromPeer = false;            // relayed by a peer (not our own wallet or API)
    bool inInitialBlockDownload = false;
    bool reAdd = false;               // after a reorg: skips checks 1, 11 and 15
    bool allowOrphans = true;         // park TX_MISSING_INPUTS in the orphan pool
    uint64_t peerId = 0;              // for the per-peer orphan limit
};

struct OutPointHasher {
    size_t operator()(const OutPoint& o) const {
        size_t v = o.index * 0x9E3779B97F4A7C15ull;
        for (size_t i = 0; i < sizeof(size_t); ++i) v ^= size_t(o.txid[i]) << (8 * i);
        return v;
    }
};

class Mempool {
public:
    using Clock = std::function<int64_t()>;

    explicit Mempool(const ChainParams& params, Clock clock = {},
                     uint64_t maxBytes = Policy::MAX_MEMPOOL_BYTES);

    // The 16 checks of section 9.2, in order. VALID means the transaction is
    // now in the pool. TX_MISSING_INPUTS may also park it in the orphan pool.
    ValidationState accept(const Transaction& tx, const ChainState& chain,
                           const AcceptOptions& options = {});

    // Section 9.6, run after every committed chain step.
    void updateForStep(const std::vector<Block>& disconnected,
                       const std::vector<Block>& connected, const ChainState& chain);

    // Section 9.7: parse and fully validate stored transactions. Indexes of
    // rows that failed (to be deleted from storage) are appended to `dropped`.
    size_t reloadValidated(const std::vector<std::vector<uint8_t>>& rawTxs,
                           const ChainState& chain, std::vector<size_t>& dropped);

    bool remove(const Hash256& txid);
    bool contains(const Hash256& txid) const { return byTxid_.count(txid) != 0; }
    const MempoolEntry* find(const Hash256& txid) const;
    size_t size() const { return byTxid_.size(); }
    uint64_t totalBytes() const { return totalBytes_; }
    bool spends(const OutPoint& op) const { return spentBy_.count(op) != 0; }

    // Copies, highest fee rate first (block template input, plan step 14).
    std::vector<MempoolEntry> byFeeRateDescending() const;

    // Rolling minimum fee rate after decay (raised by evictions).
    uint64_t minFeeRate();

    // Orphan pool.
    size_t orphanCount() const { return orphans_.size(); }
    bool hasOrphan(const Hash256& txid) const { return orphans_.count(txid) != 0; }
    size_t orphanCountForPeer(uint64_t peerId) const {
        size_t n = 0;
        for (const auto& [id, o] : orphans_) n += (o.peerId == peerId);
        return n;
    }

private:
    struct Orphan {
        Transaction tx;
        uint64_t peerId = 0;
        int64_t expires = 0;
    };

    void insert(MempoolEntry&& entry);
    void trimToSize();
    void expireEntries();
    void revalidate(const ChainState& chain);
    void addOrphan(const Transaction& tx, const Hash256& txid, uint64_t peerId);
    void expireOrphans();
    void retryOrphans(const ChainState& chain);
    bool evictOrphan(const std::vector<Hash256>& candidates);

    const ChainParams& params_;
    Clock clock_;
    uint64_t maxBytes_;

    std::unordered_map<Hash256, MempoolEntry, Hash256Hasher> byTxid_;
    std::unordered_map<OutPoint, Hash256, OutPointHasher> spentBy_;
    std::set<std::tuple<uint64_t, int64_t, Hash256>> byFeeRate_;   // (feeRate, entryTime, txid)
    uint64_t totalBytes_ = 0;
    uint64_t rollingMinFeeRate_ = 0;
    int64_t rollingUpdated_ = 0;

    std::unordered_map<Hash256, Orphan, Hash256Hasher> orphans_;
    std::mt19937_64 rng_;
};

} // namespace Bucks

#endif // MEMPOOL_H
