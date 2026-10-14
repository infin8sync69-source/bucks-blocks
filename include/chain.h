#ifndef BUCKS_CHAIN_H
#define BUCKS_CHAIN_H

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "blockheader.h"
#include "crypto.h"
#include "u256.h"

namespace Bucks {

// Block index, active chain and fork helpers
// (docs/design/consensus-validation.md section 8.1 and 8.9).

// Canonical status bits; the storage layer persists this integer unchanged.
enum BlockStatus : uint32_t {
    HEADER_VALID    = 0x01,  // passed stages A and B
    HAVE_DATA       = 0x02,  // block bytes stored
    DATA_CHECKED    = 0x04,  // passed stages C and D
    CONNECTED_VALID = 0x08,  // ConnectBlock succeeded at least once
    HAVE_UNDO       = 0x10,  // undo record stored
    IN_MAIN_CHAIN   = 0x20,  // in the active chain (persisted mirror of the vector)
    FAILED_VALID    = 0x40,  // the block itself is invalid
    FAILED_CHILD    = 0x80,  // an ancestor is invalid
    FAILED_MASK     = FAILED_VALID | FAILED_CHILD
};

constexpr size_t MTP_SPAN = 11;  // Consensus::MTP_WINDOW

struct BlockIndex {
    Hash256     hash{};
    BlockIndex* prev = nullptr;
    BlockIndex* skip = nullptr;         // skip-list pointer for O(log n) getAncestor
    BlockHeader header;
    uint32_t    height = 0;
    U256        chainWork;              // cumulative, including this block
    uint32_t    status = 0;
    uint32_t    txCount = 0;           // 0 until block data is stored
    bool        haveChainData = false; // this block and all ancestors have data
    uint64_t    sequenceId = 0;        // order in which block data arrived
    int64_t     firstSeen = 0;         // when this node first stored the header (not persisted)

    BlockIndex* getAncestor(uint32_t h);
    const BlockIndex* getAncestor(uint32_t h) const;
    // Median of this block's time and up to 10 ancestors (section 6.6),
    // computed on this block's own branch.
    int64_t medianTimePast() const;
    bool failed() const { return (status & FAILED_MASK) != 0; }
};

struct Hash256Hasher {
    size_t operator()(const Hash256& h) const {
        size_t v = 0;
        for (size_t i = 0; i < sizeof(v); ++i) v |= size_t(h[i]) << (8 * i);
        return v;
    }
};

// Owns every index entry, keyed by block hash (never by height).
class BlockIndexMap {
public:
    // Adds the header under `parent` (null only for genesis). Height and chain
    // work are derived from the parent; the skip pointer is built. Returns the
    // existing entry if the hash is already present. The caller has already
    // validated the header (stages A and B).
    BlockIndex* insert(const BlockHeader& header, BlockIndex* parent);

    BlockIndex* find(const Hash256& hash);
    const BlockIndex* find(const Hash256& hash) const;
    size_t size() const { return map_.size(); }

    template <class F>
    void forEach(F&& f) const {
        for (const auto& kv : map_) f(*kv.second);
    }

    // Visits every entry in unspecified order (candidate and descendant scans).
    template <class F>
    void forEach(F&& f) {
        for (auto& kv : map_) f(*kv.second);
    }

private:
    std::unordered_map<Hash256, std::unique_ptr<BlockIndex>, Hash256Hasher> map_;
};

// The active chain: chain[h]->height == h.
class Chain {
public:
    BlockIndex* genesis() const { return v_.empty() ? nullptr : v_.front(); }
    BlockIndex* tip() const { return v_.empty() ? nullptr : v_.back(); }
    BlockIndex* operator[](int64_t h) const {
        return (h < 0 || h >= int64_t(v_.size())) ? nullptr : v_[size_t(h)];
    }
    bool contains(const BlockIndex* idx) const { return idx && (*this)[idx->height] == idx; }
    BlockIndex* next(const BlockIndex* idx) const { return contains(idx) ? (*this)[int64_t(idx->height) + 1] : nullptr; }
    int64_t height() const { return int64_t(v_.size()) - 1; }  // -1 when empty

    // Makes `idx` the tip, rewriting only the part that differs. nullptr empties it.
    void setTip(BlockIndex* idx);

    // Newest-first hashes: the entry and its ancestors at heights h, h-1 ... h-9,
    // then steps that double (h-11, h-15, h-23 ...), always ending with genesis.
    // At most 101 hashes. `from` defaults to the tip.
    std::vector<Hash256> locator(const BlockIndex* from = nullptr) const;

    // Last entry of this chain that is an ancestor of (or equal to) `idx`.
    BlockIndex* findFork(const BlockIndex* idx) const;

private:
    std::vector<BlockIndex*> v_;
};

// First locator hash that is in the active chain; genesis if none.
BlockIndex* findForkByLocator(const BlockIndexMap& map, const Chain& chain,
                              const std::vector<Hash256>& locator);

// Highest block that is an ancestor of both (nullptr if the trees differ).
const BlockIndex* lastCommonAncestor(const BlockIndex* a, const BlockIndex* b);

} // namespace Bucks

#endif // BUCKS_CHAIN_H
