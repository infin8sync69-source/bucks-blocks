#include "chain.h"

#include <algorithm>

#include "pow.h"

namespace Bucks {

namespace {

// Bitcoin's skip-list heights: every block points far back so that
// getAncestor needs O(log n) steps.
uint32_t invertLowestOne(uint32_t n) { return n & (n - 1); }

uint32_t skipHeight(uint32_t height) {
    if (height < 2) return 0;
    return (height & 1) ? invertLowestOne(invertLowestOne(height - 1)) + 1
                        : invertLowestOne(height);
}

} // namespace

const BlockIndex* BlockIndex::getAncestor(uint32_t h) const {
    if (h > height) return nullptr;
    const BlockIndex* walk = this;
    uint32_t heightWalk = height;
    while (heightWalk > h) {
        uint32_t heightSkip = skipHeight(heightWalk);
        uint32_t heightSkipPrev = skipHeight(heightWalk - 1);
        if (walk->skip &&
            (heightSkip == h ||
             (heightSkip > h && !(heightSkipPrev + 2 < heightSkip && heightSkipPrev >= h)))) {
            walk = walk->skip;
            heightWalk = heightSkip;
        } else {
            walk = walk->prev;
            --heightWalk;
        }
    }
    return walk;
}

BlockIndex* BlockIndex::getAncestor(uint32_t h) {
    return const_cast<BlockIndex*>(static_cast<const BlockIndex*>(this)->getAncestor(h));
}

int64_t BlockIndex::medianTimePast() const {
    int64_t times[MTP_SPAN];
    size_t n = 0;
    for (const BlockIndex* i = this; i && n < MTP_SPAN; i = i->prev)
        times[n++] = int64_t(i->header.timestamp);
    std::sort(times, times + n);
    return times[n / 2];
}

BlockIndex* BlockIndexMap::insert(const BlockHeader& header, BlockIndex* parent) {
    Hash256 hash = header.hash();
    auto it = map_.find(hash);
    if (it != map_.end()) return it->second.get();

    auto e = std::make_unique<BlockIndex>();
    e->hash = hash;
    e->header = header;
    e->prev = parent;
    e->height = parent ? parent->height + 1 : 0;
    e->chainWork = (parent ? parent->chainWork : U256()) + Pow::blockProof(header.bits);
    e->status = HEADER_VALID;
    if (parent) e->skip = parent->getAncestor(skipHeight(e->height));

    BlockIndex* raw = e.get();
    map_.emplace(hash, std::move(e));
    return raw;
}

BlockIndex* BlockIndexMap::find(const Hash256& hash) {
    auto it = map_.find(hash);
    return it == map_.end() ? nullptr : it->second.get();
}

const BlockIndex* BlockIndexMap::find(const Hash256& hash) const {
    auto it = map_.find(hash);
    return it == map_.end() ? nullptr : it->second.get();
}

void Chain::setTip(BlockIndex* idx) {
    if (!idx) {
        v_.clear();
        return;
    }
    v_.resize(size_t(idx->height) + 1);
    while (idx && v_[idx->height] != idx) {
        v_[idx->height] = idx;
        idx = idx->prev;
    }
}

std::vector<Hash256> Chain::locator(const BlockIndex* from) const {
    std::vector<Hash256> have;
    if (!from) from = tip();
    int64_t step = 1;
    while (from) {
        have.push_back(from->hash);
        if (from->height == 0) break;
        if (have.size() >= 10) step *= 2;   // h .. h-9, then h-11, h-15, h-23 ...
        int64_t h = std::max<int64_t>(int64_t(from->height) - step, 0);
        from = contains(from) ? (*this)[h] : from->getAncestor(uint32_t(h));
    }
    return have;
}

BlockIndex* Chain::findFork(const BlockIndex* idx) const {
    if (!idx) return nullptr;
    if (int64_t(idx->height) > height()) idx = idx->getAncestor(uint32_t(height()));
    while (idx && !contains(idx)) idx = idx->prev;
    return const_cast<BlockIndex*>(idx);
}

BlockIndex* findForkByLocator(const BlockIndexMap& map, const Chain& chain,
                              const std::vector<Hash256>& locator) {
    for (const Hash256& h : locator) {
        const BlockIndex* e = map.find(h);
        if (e && chain.contains(e)) return const_cast<BlockIndex*>(e);
    }
    return chain.genesis();
}

const BlockIndex* lastCommonAncestor(const BlockIndex* a, const BlockIndex* b) {
    if (!a || !b) return nullptr;
    if (a->height > b->height) a = a->getAncestor(b->height);
    else if (b->height > a->height) b = b->getAncestor(a->height);
    while (a != b) {
        if (!a || !b) return nullptr;
        a = a->prev;
        b = b->prev;
    }
    return a;
}

} // namespace Bucks
