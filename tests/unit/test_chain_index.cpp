#include <gtest/gtest.h>

#include <algorithm>
#include <random>

#include "chain.h"
#include "consensus.h"

using namespace Bucks;

namespace {

// Regtest-style limit: every block has proof 2.
constexpr uint32_t kBits = 0x207fffff;

struct Tree {
    BlockIndexMap map;
    uint32_t nextNonce = 1;

    BlockIndex* genesis() {
        BlockHeader h;
        h.timestamp = 1000;
        h.bits = kBits;
        return map.insert(h, nullptr);
    }
    BlockIndex* add(BlockIndex* parent, uint32_t time) {
        BlockHeader h;
        h.height = parent->height + 1;
        h.prevBlockHash = parent->hash;
        h.timestamp = time;
        h.bits = kBits;
        h.nonce = nextNonce++;   // distinct hashes for siblings
        return map.insert(h, parent);
    }
    BlockIndex* extend(BlockIndex* from, int n) {
        for (int i = 0; i < n; ++i) from = add(from, from->header.timestamp + 600);
        return from;
    }
};

const BlockIndex* naiveAncestor(const BlockIndex* i, uint32_t h) {
    while (i && i->height > h) i = i->prev;
    return (i && i->height == h) ? i : nullptr;
}

} // namespace

TEST(ChainIndex, HeightsAndChainWorkAccumulate) {
    // PAR-3 helper: genesis + 10 blocks at proof 2 each gives 22.
    Tree t;
    BlockIndex* g = t.genesis();
    EXPECT_EQ(g->height, 0u);
    EXPECT_EQ(g->chainWork, U256(2));
    EXPECT_EQ(g->status, uint32_t(HEADER_VALID));
    BlockIndex* tip = t.extend(g, 10);
    EXPECT_EQ(tip->height, 10u);
    EXPECT_EQ(tip->chainWork, U256(22));
    EXPECT_EQ(tip->prev->chainWork, U256(20));
}

TEST(ChainIndex, InsertIsKeyedByHash) {
    Tree t;
    BlockIndex* g = t.genesis();
    EXPECT_EQ(t.map.insert(g->header, nullptr), g);   // duplicate: same entry
    EXPECT_EQ(t.map.size(), 1u);
    BlockIndex* a = t.add(g, 1600);
    BlockIndex* b = t.add(g, 1600);                   // sibling, different hash
    EXPECT_NE(a, b);
    EXPECT_EQ(t.map.find(a->hash), a);
    EXPECT_EQ(t.map.find(b->hash), b);
    Hash256 unknown{};
    unknown.fill(9);
    EXPECT_EQ(t.map.find(unknown), nullptr);
}

TEST(ChainIndex, GetAncestorMatchesNaiveWalk) {
    Tree t;
    BlockIndex* g = t.genesis();
    BlockIndex* main = t.extend(g, 3000);

    // Side branches off a few heights, including deep ones.
    std::vector<BlockIndex*> tips = {main};
    for (uint32_t forkAt : {1u, 2u, 17u, 1000u, 2999u}) {
        tips.push_back(t.extend(main->getAncestor(forkAt), 40));
    }
    for (BlockIndex* tip : tips) {
        for (uint32_t h = 0; h <= tip->height; h += (h < 70 ? 1 : 37)) {
            ASSERT_EQ(tip->getAncestor(h), naiveAncestor(tip, h)) << tip->height << " " << h;
        }
        EXPECT_EQ(tip->getAncestor(tip->height), tip);
        EXPECT_EQ(tip->getAncestor(tip->height + 1), nullptr);
        EXPECT_EQ(tip->getAncestor(0), g);
    }
}

TEST(ChainIndex, SkipPointersAreSparse) {
    // A 100,000-block chain: ancestor lookups must be fast, not linear.
    Tree t;
    BlockIndex* tip = t.extend(t.genesis(), 100000);
    for (uint32_t h : {0u, 1u, 99u, 50000u, 99999u})
        EXPECT_EQ(tip->getAncestor(h)->height, h);
    uint64_t jumps = 0;
    for (const BlockIndex* i = tip; i->prev; ) {   // every skip goes strictly backwards
        if (i->skip) { EXPECT_LT(i->skip->height, i->height); ++jumps; }
        i = i->prev;
    }
    EXPECT_GT(jumps, 90000u);
}

TEST(ChainIndex, MedianTimePast) {
    Tree t;
    BlockIndex* g = t.genesis();   // time 1000
    // Near genesis: fewer than 11 blocks, median of what exists.
    EXPECT_EQ(g->medianTimePast(), 1000);
    BlockIndex* b1 = t.add(g, 1500);
    EXPECT_EQ(b1->medianTimePast(), 1500);            // {1000, 1500}: element [1]
    BlockIndex* b2 = t.add(b1, 1200);
    EXPECT_EQ(b2->medianTimePast(), 1200);            // {1000, 1200, 1500}

    // Eleven out-of-order times: the median ignores order.
    Tree t2;
    BlockIndex* i = t2.genesis();
    for (uint32_t time : {5000u, 1100u, 4000u, 1200u, 3000u, 1300u, 2000u, 1400u, 1900u, 1800u})
        i = t2.add(i, time);
    // times: 1000, 5000, 1100, 4000, 1200, 3000, 1300, 2000, 1400, 1900, 1800
    // sorted: 1000 1100 1200 1300 1400 1800 1900 2000 3000 4000 5000 -> [5] = 1800
    EXPECT_EQ(i->medianTimePast(), 1800);
    // A twelfth block drops the oldest (1000).
    BlockIndex* j = t2.add(i, 2500);
    // window: 5000 1100 4000 1200 3000 1300 2000 1400 1900 1800 2500
    // sorted: 1100 1200 1300 1400 1800 1900 2000 2500 3000 4000 5000 -> [5] = 1900
    EXPECT_EQ(j->medianTimePast(), 1900);
}

TEST(ChainIndex, MedianTimePastUsesTheBlocksOwnBranch) {
    // CON-9: a fork block must not see the active tip's times.
    Tree t;
    BlockIndex* g = t.genesis();
    BlockIndex* common = t.extend(g, 20);
    BlockIndex* tipA = common;
    for (int i = 0; i < 10; ++i) tipA = t.add(tipA, 100000 + i);   // far-future times on A
    BlockIndex* tipB = t.add(common, common->header.timestamp + 600);
    EXPECT_GT(tipA->medianTimePast(), 90000);
    EXPECT_LT(tipB->medianTimePast(), 20000);
}

TEST(ChainIndex, ActiveChainSetTip) {
    Tree t;
    BlockIndex* g = t.genesis();
    BlockIndex* a = t.extend(g, 10);
    BlockIndex* forkPoint = a->getAncestor(6);
    BlockIndex* b = t.extend(forkPoint, 8);

    Chain chain;
    EXPECT_EQ(chain.height(), -1);
    EXPECT_EQ(chain.tip(), nullptr);
    chain.setTip(a);
    EXPECT_EQ(chain.height(), 10);
    EXPECT_EQ(chain.genesis(), g);
    for (uint32_t h = 0; h <= 10; ++h) EXPECT_EQ(chain[h]->height, h);
    EXPECT_TRUE(chain.contains(a));
    EXPECT_FALSE(chain.contains(b));
    EXPECT_EQ(chain.next(g), chain[1]);
    EXPECT_EQ(chain.next(a), nullptr);
    EXPECT_EQ(chain.next(b), nullptr);   // not on the chain

    chain.setTip(b);                      // reorg: heights 7..14 now follow b
    EXPECT_EQ(chain.height(), 14);
    EXPECT_TRUE(chain.contains(b));
    EXPECT_FALSE(chain.contains(a));
    EXPECT_EQ(chain[6], forkPoint);
    for (int64_t h = 1; h <= 14; ++h) EXPECT_EQ(chain[h]->prev, chain[h - 1]);

    chain.setTip(forkPoint);              // shrink
    EXPECT_EQ(chain.height(), 6);
    EXPECT_EQ(chain[7], nullptr);
    chain.setTip(nullptr);
    EXPECT_EQ(chain.height(), -1);
}

TEST(ChainIndex, LocatorShape) {
    Tree t;
    BlockIndex* tip = t.extend(t.genesis(), 5000);
    Chain chain;
    chain.setTip(tip);

    std::vector<Hash256> loc = chain.locator();
    ASSERT_LE(loc.size(), 101u);
    EXPECT_EQ(loc.front(), tip->hash);
    EXPECT_EQ(loc.back(), chain.genesis()->hash);

    std::vector<uint32_t> heights;
    for (const Hash256& h : loc) heights.push_back(t.map.find(h)->height);
    // h, h-1 ... h-9, then h-11, h-15, h-23 ...
    for (size_t i = 0; i < 10; ++i) EXPECT_EQ(heights[i], 5000 - i);
    EXPECT_EQ(heights[10], 5000u - 11);
    EXPECT_EQ(heights[11], 5000u - 15);
    EXPECT_EQ(heights[12], 5000u - 23);
    EXPECT_TRUE(std::is_sorted(heights.rbegin(), heights.rend()));

    // Short chains: every block, ending at genesis.
    Tree t2;
    BlockIndex* small = t2.extend(t2.genesis(), 4);
    Chain c2;
    c2.setTip(small);
    EXPECT_EQ(c2.locator().size(), 5u);
    Chain c3;
    c3.setTip(c2.genesis());
    EXPECT_EQ(c3.locator().size(), 1u);
    EXPECT_TRUE(Chain().locator().empty());

    // From a fork block: still ends at genesis and walks its own branch.
    BlockIndex* side = t.extend(tip->getAncestor(4000), 50);
    std::vector<Hash256> sideLoc = chain.locator(side);
    EXPECT_EQ(sideLoc.front(), side->hash);
    EXPECT_EQ(sideLoc.back(), chain.genesis()->hash);
    EXPECT_LE(sideLoc.size(), 101u);
}

TEST(ChainIndex, LocatorIsAlwaysShort) {
    // Even for a million-high chain the locator stays well under 101 entries.
    Tree t;
    BlockIndex* tip = t.extend(t.genesis(), 20000);
    Chain chain;
    chain.setTip(tip);
    EXPECT_LT(chain.locator().size(), 40u);   // 10 + ~log2(20000)
}

TEST(ChainIndex, FindForkAndLocatorLookup) {
    Tree t;
    BlockIndex* g = t.genesis();
    BlockIndex* a = t.extend(g, 30);
    BlockIndex* forkPoint = a->getAncestor(12);
    BlockIndex* b = t.extend(forkPoint, 25);

    Chain chain;
    chain.setTip(a);
    EXPECT_EQ(chain.findFork(b), forkPoint);
    EXPECT_EQ(chain.findFork(a), a);
    EXPECT_EQ(chain.findFork(a->getAncestor(5)), a->getAncestor(5));
    EXPECT_EQ(chain.findFork(nullptr), nullptr);

    // A peer on a branch that left our chain 3 blocks ago: its dense locator
    // head contains the fork point.
    BlockIndex* nearFork = a->getAncestor(27);
    BlockIndex* nearBranch = t.extend(nearFork, 5);
    Chain peer;
    peer.setTip(nearBranch);
    EXPECT_EQ(findForkByLocator(t.map, chain, peer.locator()), nearFork);

    // A far fork can fall between the sparse locator entries: the answer is
    // then an older common block (here genesis), never a wrong one.
    Chain farPeer;
    farPeer.setTip(b);
    BlockIndex* found = findForkByLocator(t.map, chain, farPeer.locator());
    EXPECT_TRUE(chain.contains(found));
    EXPECT_LE(found->height, forkPoint->height);

    // A locator with only unknown hashes falls back to genesis.
    Hash256 unknown{};
    unknown.fill(0x77);
    EXPECT_EQ(findForkByLocator(t.map, chain, {unknown, unknown}), g);
    EXPECT_EQ(findForkByLocator(t.map, chain, {}), g);
}

TEST(ChainIndex, LastCommonAncestor) {
    Tree t;
    BlockIndex* g = t.genesis();
    BlockIndex* a = t.extend(g, 40);
    BlockIndex* f1 = a->getAncestor(15);
    BlockIndex* b = t.extend(f1, 30);
    BlockIndex* f2 = b->getAncestor(25);
    BlockIndex* c = t.extend(f2, 5);

    EXPECT_EQ(lastCommonAncestor(a, b), f1);
    EXPECT_EQ(lastCommonAncestor(b, a), f1);
    EXPECT_EQ(lastCommonAncestor(a, c), f1);
    EXPECT_EQ(lastCommonAncestor(b, c), f2);
    EXPECT_EQ(lastCommonAncestor(a, a), a);
    EXPECT_EQ(lastCommonAncestor(a, a->getAncestor(3)), a->getAncestor(3));
    EXPECT_EQ(lastCommonAncestor(g, c), g);
    EXPECT_EQ(lastCommonAncestor(nullptr, a), nullptr);

    // Two unrelated trees share nothing.
    Tree other;
    BlockIndex* og = other.genesis();
    og->header.timestamp = 99;   // irrelevant to the walk
    BlockIndex* ob = other.extend(og, 5);
    (void)ob;
    EXPECT_EQ(lastCommonAncestor(a, ob), nullptr);
}

TEST(ChainIndex, RandomTreeAgreement) {
    // Random tree: ancestor, lastCommonAncestor and findFork agree with naive walks.
    std::mt19937 rng(777);
    Tree t;
    std::vector<BlockIndex*> all = {t.genesis()};
    for (int i = 0; i < 3000; ++i) {
        BlockIndex* parent = all[rng() % 4 == 0 ? rng() % all.size() : all.size() - 1 - rng() % std::min<size_t>(all.size(), 3)];
        all.push_back(t.add(parent, parent->header.timestamp + 1 + rng() % 1000));
    }
    Chain chain;
    chain.setTip(all.back());
    for (int i = 0; i < 400; ++i) {
        BlockIndex* x = all[rng() % all.size()];
        BlockIndex* y = all[rng() % all.size()];
        const BlockIndex* lca = lastCommonAncestor(x, y);
        ASSERT_NE(lca, nullptr);
        // Naive: collect x's ancestors, walk y up until found.
        std::vector<const BlockIndex*> xs;
        for (const BlockIndex* p = x; p; p = p->prev) xs.push_back(p);
        const BlockIndex* naive = nullptr;
        for (const BlockIndex* p = y; p && !naive; p = p->prev)
            if (std::find(xs.begin(), xs.end(), p) != xs.end()) naive = p;
        ASSERT_EQ(lca, naive);

        const BlockIndex* fork = chain.findFork(x);
        ASSERT_EQ(fork, lastCommonAncestor(chain.tip(), x));
    }
}
