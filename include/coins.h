#ifndef BUCKS_COINS_H
#define BUCKS_COINS_H

#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "crypto.h"
#include "script.h"
#include "u256.h"
#include "utxo.h"

namespace Bucks {

// An unspent output (docs/design/consensus-validation.md sections 4.7, 8.8).
struct Coin {
    uint64_t amount = 0;
    Script scriptPubKey;      // always P2PKH once in the set
    uint32_t height = 0;      // height of the block that created it
    bool isCoinbase = false;

    friend bool operator==(const Coin& a, const Coin& b) {
        return a.amount == b.amount && a.scriptPubKey == b.scriptPubKey &&
               a.height == b.height && a.isCoinbase == b.isCoinbase;
    }
    friend bool operator!=(const Coin& a, const Coin& b) { return !(a == b); }
};

struct OutPointLess {
    bool operator()(const OutPoint& a, const OutPoint& b) const {
        if (a.txid != b.txid) return a.txid < b.txid;
        return a.index < b.index;
    }
};

// The net effect of a block, a reorg step or a transaction on the coin set,
// ordered by outpoint. An overwritten coin appears in both lists.
struct CoinChanges {
    std::vector<std::pair<OutPoint, Coin>> removed;  // as they were in the base
    std::vector<std::pair<OutPoint, Coin>> added;
    std::optional<Hash256> bestBlock;                // set if the view changed it
};

// SHA256(txid || LE32(index) || LE32(height) || u8(isCoinbase) || LE64(amount)
//        || CompactSize(len) || script), read as a big-endian number.
U256 coinDigest(const OutPoint& op, const Coin& coin);

// Statistics kept in the chain state and updated in every commit: count,
// total amount (the circulating supply) and an additive digest modulo 2^256.
struct UtxoStats {
    uint64_t count = 0;
    uint64_t totalAmount = 0;
    U256 digest;

    // Applies a change set. Returns false (leaving *this unchanged) if the
    // count would go negative or the total would leave 0..MAX_MONEY.
    bool apply(const CoinChanges& changes);

    friend bool operator==(const UtxoStats& a, const UtxoStats& b) {
        return a.count == b.count && a.totalAmount == b.totalAmount && a.digest == b.digest;
    }
    friend bool operator!=(const UtxoStats& a, const UtxoStats& b) { return !(a == b); }
};

// Read access to a coin set.
class CoinsView {
public:
    virtual ~CoinsView() = default;
    virtual bool getCoin(const OutPoint& op, Coin& out) const = 0;
    virtual bool haveCoin(const OutPoint& op) const {
        Coin c;
        return getCoin(op, c);
    }
    virtual Hash256 bestBlock() const = 0;
};

// In-memory overlay over a base view. Nothing is written to the base: the
// caller takes changes() and commits them (or drops the cache).
class CoinsViewCache : public CoinsView {
public:
    // Takes a pointer so that stacking a cache on a cache is explicit (a
    // reference would silently select the copy constructor). Not copyable.
    explicit CoinsViewCache(const CoinsView* base) : base_(*base) {}
    CoinsViewCache(const CoinsViewCache&) = delete;
    CoinsViewCache& operator=(const CoinsViewCache&) = delete;

    bool getCoin(const OutPoint& op, Coin& out) const override;
    Hash256 bestBlock() const override;

    // Plain insert: fails (false) if the outpoint is currently unspent.
    bool addCoin(const OutPoint& op, const Coin& coin);
    // Fails (false) if the outpoint is not currently unspent. *spent receives
    // the removed coin (needed for the undo record).
    bool spendCoin(const OutPoint& op, Coin* spent = nullptr);
    void setBestBlock(const Hash256& hash) { best_ = hash; }

    CoinChanges changes() const;
    size_t cacheSize() const { return map_.size(); }

private:
    struct Entry {
        std::optional<Coin> original;  // state in the base when first touched
        std::optional<Coin> current;
    };
    Entry& fetch(const OutPoint& op) const;

    const CoinsView& base_;
    mutable std::map<OutPoint, Entry, OutPointLess> map_;
    std::optional<Hash256> best_;
};

// Simple committed set for tests and tools. Statistics follow every applied
// change set; recomputeStats() scans the set and must always agree.
class MemoryCoinsView : public CoinsView {
public:
    bool getCoin(const OutPoint& op, Coin& out) const override;
    Hash256 bestBlock() const override { return best_; }

    // All-or-nothing: false (and no change) if a removed coin is missing or
    // differs from the stored one, an added coin collides, or the statistics
    // would leave their range.
    bool apply(const CoinChanges& changes);
    void insertForTest(const OutPoint& op, const Coin& coin);

    const UtxoStats& stats() const { return stats_; }
    UtxoStats recomputeStats() const;
    size_t size() const { return coins_.size(); }

private:
    std::map<OutPoint, Coin, OutPointLess> coins_;
    Hash256 best_{};
    UtxoStats stats_;
};

} // namespace Bucks

#endif // BUCKS_COINS_H
