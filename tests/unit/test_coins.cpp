#include <gtest/gtest.h>

#include <random>

#include "coins.h"
#include "consensus.h"

using namespace Bucks;

namespace {

OutPoint op(uint8_t fill, uint32_t index) {
    OutPoint o;
    o.txid.fill(fill);
    o.index = index;
    return o;
}

Coin coin(uint64_t amount, uint8_t keyFill = 0x22, uint32_t height = 7, bool cb = false) {
    Hash160 h;
    h.fill(keyFill);
    Coin c;
    c.amount = amount;
    c.scriptPubKey = makeP2PKH(h);
    c.height = height;
    c.isCoinbase = cb;
    return c;
}

std::string hex(const U256& v) {
    Hash256 h = v.toBytesBE();
    return hexStr(std::vector<uint8_t>(h.begin(), h.end()));
}

} // namespace

TEST(Coins, DigestVector) {
    // Independent Python computation of SHA256(txid || LE32(index) || LE32(height)
    // || u8(isCoinbase) || LE64(amount) || CompactSize(len) || script).
    EXPECT_EQ(hex(coinDigest(op(0x11, 2), coin(5000000000ull, 0x22, 7, true))),
              "b3b8d1a2db3cad4e53c5e3e45f0f507daca97b361060805d279857ff9de415c2");
    // Every field matters.
    U256 base = coinDigest(op(0x11, 2), coin(100));
    EXPECT_NE(base, coinDigest(op(0x12, 2), coin(100)));
    EXPECT_NE(base, coinDigest(op(0x11, 3), coin(100)));
    EXPECT_NE(base, coinDigest(op(0x11, 2), coin(101)));
    EXPECT_NE(base, coinDigest(op(0x11, 2), coin(100, 0x23)));
    EXPECT_NE(base, coinDigest(op(0x11, 2), coin(100, 0x22, 8)));
    EXPECT_NE(base, coinDigest(op(0x11, 2), coin(100, 0x22, 7, true)));
}

TEST(Coins, CacheAddSpendGet) {
    MemoryCoinsView base;
    base.insertForTest(op(1, 0), coin(1000));
    CoinsViewCache view(&base);

    Coin c;
    ASSERT_TRUE(view.getCoin(op(1, 0), c));
    EXPECT_EQ(c.amount, 1000u);
    EXPECT_FALSE(view.haveCoin(op(9, 9)));

    // Plain insert: an unspent outpoint cannot be added again.
    EXPECT_FALSE(view.addCoin(op(1, 0), coin(5)));
    EXPECT_TRUE(view.addCoin(op(2, 0), coin(2000)));
    EXPECT_FALSE(view.addCoin(op(2, 0), coin(2000)));

    Coin spent;
    ASSERT_TRUE(view.spendCoin(op(1, 0), &spent));
    EXPECT_EQ(spent, coin(1000));
    EXPECT_FALSE(view.haveCoin(op(1, 0)));
    EXPECT_FALSE(view.spendCoin(op(1, 0)));   // already spent
    EXPECT_FALSE(view.spendCoin(op(7, 7)));   // never existed
    EXPECT_TRUE(base.haveCoin(op(1, 0)));     // the base is untouched
    EXPECT_FALSE(base.haveCoin(op(2, 0)));
}

TEST(Coins, ChangesAreTheNetEffect) {
    MemoryCoinsView base;
    base.insertForTest(op(1, 0), coin(1000));
    base.insertForTest(op(1, 1), coin(500));
    CoinsViewCache view(&base);

    ASSERT_TRUE(view.addCoin(op(2, 0), coin(70)));
    ASSERT_TRUE(view.spendCoin(op(2, 0)));            // created and spent in the same view
    ASSERT_TRUE(view.spendCoin(op(1, 1)));
    ASSERT_TRUE(view.addCoin(op(1, 1), coin(500)));   // restored exactly
    ASSERT_TRUE(view.spendCoin(op(1, 0)));
    ASSERT_TRUE(view.addCoin(op(1, 0), coin(999)));   // overwritten with a different coin
    ASSERT_TRUE(view.addCoin(op(3, 0), coin(30)));
    Hash256 best{};
    best.fill(0x5a);
    view.setBestBlock(best);
    EXPECT_EQ(view.bestBlock(), best);

    CoinChanges ch = view.changes();
    ASSERT_EQ(ch.removed.size(), 1u);
    EXPECT_EQ(ch.removed[0].first.index, 0u);
    EXPECT_EQ(ch.removed[0].second, coin(1000));
    ASSERT_EQ(ch.added.size(), 2u);
    EXPECT_EQ(ch.added[0].second, coin(999));   // ordered by outpoint
    EXPECT_EQ(ch.added[1].second, coin(30));
    ASSERT_TRUE(ch.bestBlock.has_value());

    UtxoStats before = base.stats();
    ASSERT_TRUE(base.apply(ch));
    EXPECT_EQ(base.stats().count, before.count + 1);
    EXPECT_EQ(base.stats().totalAmount, before.totalAmount - 1000 + 999 + 30);
    EXPECT_EQ(base.stats(), base.recomputeStats());
    EXPECT_EQ(base.bestBlock(), best);
}

TEST(Coins, StackedCaches) {
    MemoryCoinsView base;
    base.insertForTest(op(1, 0), coin(10));
    CoinsViewCache outer(&base);
    ASSERT_TRUE(outer.addCoin(op(2, 0), coin(20)));
    CoinsViewCache inner(&outer);
    EXPECT_TRUE(inner.haveCoin(op(2, 0)));
    ASSERT_TRUE(inner.spendCoin(op(2, 0)));
    EXPECT_TRUE(outer.haveCoin(op(2, 0)));   // inner changes stay in inner
    EXPECT_EQ(inner.changes().removed.size(), 1u);
}

TEST(Coins, ApplyIsAllOrNothing) {
    MemoryCoinsView base;
    base.insertForTest(op(1, 0), coin(1000));
    UtxoStats before = base.stats();

    CoinChanges missing;                      // removes something that is not there
    missing.removed.emplace_back(op(9, 9), coin(1));
    EXPECT_FALSE(base.apply(missing));

    CoinChanges wrongContent;                 // removes with different content
    wrongContent.removed.emplace_back(op(1, 0), coin(1001));
    EXPECT_FALSE(base.apply(wrongContent));

    CoinChanges collision;                    // adds an outpoint that is unspent
    collision.added.emplace_back(op(1, 0), coin(5));
    EXPECT_FALSE(base.apply(collision));

    CoinChanges overflow;                     // would exceed MAX_MONEY
    overflow.added.emplace_back(op(4, 0), coin(Consensus::MAX_MONEY));
    EXPECT_FALSE(base.apply(overflow));

    CoinChanges partial;                      // valid add followed by an invalid remove
    partial.added.emplace_back(op(5, 0), coin(5));
    partial.removed.emplace_back(op(9, 9), coin(1));
    EXPECT_FALSE(base.apply(partial));

    EXPECT_EQ(base.stats(), before);
    EXPECT_EQ(base.size(), 1u);
    EXPECT_FALSE(base.haveCoin(op(5, 0)));
}

TEST(Coins, StatsApplyRejectsUnderflow) {
    UtxoStats s;
    CoinChanges ch;
    ch.removed.emplace_back(op(1, 0), coin(1));
    EXPECT_FALSE(s.apply(ch));
    EXPECT_EQ(s, UtxoStats());
}

TEST(Coins, IncrementalStatsMatchFullScan) {
    // Random blocks of adds and spends through a cache; after every commit the
    // incrementally maintained statistics equal a scan of the whole set.
    std::mt19937 rng(12345);
    MemoryCoinsView base;
    std::vector<OutPoint> live;
    uint32_t nextTx = 1;

    for (int block = 0; block < 200; ++block) {
        CoinsViewCache view(&base);
        std::vector<OutPoint> liveNow = live;
        int ops = 1 + rng() % 12;
        for (int i = 0; i < ops; ++i) {
            if (!liveNow.empty() && rng() % 3 == 0) {
                size_t k = rng() % liveNow.size();
                ASSERT_TRUE(view.spendCoin(liveNow[k]));
                liveNow.erase(liveNow.begin() + k);
            } else {
                OutPoint o;
                o.txid.fill(0);
                for (int b = 0; b < 4; ++b) o.txid[b] = uint8_t(nextTx >> (8 * b));
                o.index = rng() % 3;
                nextTx++;
                ASSERT_TRUE(view.addCoin(o, coin(1 + rng() % 100000, uint8_t(rng()), block, rng() % 5 == 0)));
                liveNow.push_back(o);
            }
        }
        ASSERT_TRUE(base.apply(view.changes())) << block;
        live = liveNow;
        ASSERT_EQ(base.stats(), base.recomputeStats()) << block;
        ASSERT_EQ(base.stats().count, live.size());
    }
}
