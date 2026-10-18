#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <random>

#include "chain.h"
#include "consensus.h"
#include "genesis.h"
#include "pow.h"
#include "validation_rules.h"

using namespace Bucks;
using namespace Bucks::Rules;

// Spec: docs/design/consensus-validation.md section 6.5 (DIFF-1 ... DIFF-6).
// ASERT anchored at genesis on the block index, and simulations of the hashrate
// swings of audit NEW-2 against the real GetNextWorkRequired.

namespace {

constexpr uint32_t kGenesisTime = 1'800'000'000u;
constexpr int64_t kHalfLife = 172800;

// Expected hashes for one block at `bits`, as a double (simulation only).
double expectedHashes(uint32_t bits) {
    const U256 proof = Pow::blockProof(bits);
    double d = 0;
    for (int limb = 3; limb >= 0; --limb) d = d * 18446744073709551616.0 + double((proof >> (64 * limb)).low64());
    return d;
}

// A header-only chain under mainnet rules (or the rule given). Every block
// carries the bits GetNextWorkRequired asks for; times are free.
struct Net {
    ChainParams params;
    BlockIndexMap map;
    BlockIndex* tip = nullptr;
    int64_t time = kGenesisTime;   // the tip's time, kept as int64 so a stall can be measured

    explicit Net(PowAlgorithm algo = PowAlgorithm::ASERT, uint32_t genesisBits = 0) {
        params = chainParams(NetworkId::MAIN);
        params.powAlgorithm = algo;
        params.genesisTime = kGenesisTime;
        Block g = Genesis::build(params);
        if (genesisBits) g.header.bits = genesisBits;   // an anchor away from the limit
        tip = map.insert(g.header, nullptr);
    }

    uint32_t nextBits() const { return GetNextWorkRequired(tip, params); }

    // Appends a block `dt` seconds after the tip.
    void add(int64_t dt) {
        BlockHeader h;
        h.version = Consensus::BLOCK_VERSION;
        h.height = tip->height + 1;
        h.prevBlockHash = tip->hash;
        time += dt;
        h.timestamp = uint32_t(time);
        h.bits = nextBits();
        tip = map.insert(h, tip);
    }

    // Seconds `hashrate` needs for the next block: the expectation, or one
    // exponential draw when `rng` is given.
    int64_t solveTime(double hashrate, std::mt19937_64* rng = nullptr) const {
        double mean = expectedHashes(nextBits()) / hashrate;
        if (rng) mean *= -std::log(1.0 - double((*rng)() >> 11) * 0x1.0p-53);
        return std::max<int64_t>(1, std::llround(mean));
    }

    int64_t mine(double hashrate, std::mt19937_64* rng = nullptr) {
        const int64_t dt = solveTime(hashrate, rng);
        add(dt);
        return dt;
    }

    double difficulty() const { return expectedHashes(tip->header.bits) / expectedHashes(params.powLimitBits); }
};

} // namespace

// DIFF-1: the anchor is genesis with a virtual parent 600 s before it: block 1
// keeps the genesis bits, a chain on schedule keeps them, and each half-life
// behind (ahead of) schedule doubles (halves) the target exactly.
TEST(Difficulty, AsertAnchorIsGenesis) {
    const uint32_t anchor = 0x1c0ffff0;   // harder than the limit, so no clamping
    const U256 target = Pow::decodeCompact(anchor).target;
    Net n(PowAlgorithm::ASERT, anchor);
    EXPECT_EQ(n.nextBits(), anchor);
    for (int i = 0; i < 300; ++i) {
        n.add(600);
        ASSERT_EQ(n.nextBits(), anchor) << "height " << n.tip->height;
    }
    n.add(600 + kHalfLife);
    EXPECT_EQ(n.nextBits(), Pow::encodeCompact(target << 1));
    n.add(600 - 3 * kHalfLife);   // two half-lives ahead of schedule overall
    EXPECT_EQ(n.nextBits(), Pow::encodeCompact(target >> 2));
    // At the limit a chain behind schedule stays at the limit.
    Net slow;
    slow.add(100'000);
    EXPECT_EQ(slow.nextBits(), slow.params.powLimitBits);
}

// DIFF-2: the anchor convention equals the specification's own form (anchor
// height must be > 0 there): anchor = block 1, whose bits are the genesis bits,
// anchor parent time = genesis time. Random chains, including timestamps that
// go backwards.
TEST(Difficulty, AsertMatchesSpecificationForm) {
    std::mt19937_64 rng(20261017);
    for (uint32_t anchor : {0x1d00ffffu, 0x1c0ffff0u, 0x1b0404cbu}) {
        Net n(PowAlgorithm::ASERT, anchor);
        n.add(int64_t(rng() % 5000));
        for (int i = 0; i < 2000; ++i) {
            const uint64_t r = rng();
            n.add(int64_t(r % 3000) - (r % 7 == 0 ? 2500 : 0));
            const U256 spec = Pow::calculateASERT(Pow::decodeCompact(anchor).target, 600,
                                                  int64_t(n.tip->header.timestamp) - kGenesisTime,
                                                  int64_t(n.tip->height) - 1, n.params.powLimit, kHalfLife);
            ASSERT_EQ(n.nextBits(), Pow::encodeCompact(spec)) << "height " << n.tip->height;
        }
    }
}

// DIFF-3: absolute scheduling: the bits depend on the parent's height and time
// only, not on the path that led there (no accumulated rounding).
TEST(Difficulty, AsertIgnoresThePath) {
    Net steady, erratic;
    for (int i = 0; i < 1000; ++i) steady.add(600);
    std::mt19937_64 rng(7);
    for (int i = 0; i < 999; ++i) erratic.add(int64_t(rng() % 1200));
    erratic.add(steady.time - erratic.time);
    ASSERT_EQ(erratic.tip->height, steady.tip->height);
    ASSERT_EQ(erratic.tip->header.timestamp, steady.tip->header.timestamp);
    EXPECT_EQ(erratic.nextBits(), steady.nextBits());
}

// DIFF-4: steady hashrate keeps ~600 s blocks. 50 MH/s needs about 7 times the
// minimum difficulty. Expected solve times converge to 600 s; random
// (exponential) solve times average 600 s over 10,000 blocks.
TEST(Difficulty, SteadyHashrateKeeps600Seconds) {
    Net n;
    for (int i = 0; i < 6000; ++i) n.mine(50e6);
    int64_t sum = 0;
    for (int i = 0; i < 2000; ++i) sum += n.mine(50e6);
    EXPECT_NEAR(double(sum) / 2000, 600.0, 3.0);
    EXPECT_NEAR(n.difficulty(), 50e6 * 600 / 4295032833.0, 0.1);

    std::mt19937_64 rng(42);
    Net r;
    for (int i = 0; i < 3000; ++i) r.mine(50e6, &rng);
    const int64_t start = r.time;
    for (int i = 0; i < 10000; ++i) r.mine(50e6, &rng);
    EXPECT_NEAR(double(r.time - start) / 10000, 600.0, 15.0);
}

// DIFF-5 (audit NEW-2): 1 GH/s mines next to the 0.5 MH/s node miner for 22
// days, then leaves. Under ASERT the first block after it leaves still needs the
// old difficulty (about 14 days for 0.5 MH/s; no rule that uses only the parent
// can avoid that), and that one late timestamp brings the difficulty back to
// the limit: the chain recovers in one block. Under the Bitcoin retarget the
// next 2016-block period alone takes about 28,000 days.
TEST(Difficulty, HashrateLeavesRecoversInOneBlock) {
    const double big = 1e9 + 0.5e6, node = 0.5e6;
    const double atLimit = 4295032833.0 / node;   // 8,590 s: one node block at the limit

    Net asert;
    while (asert.time - kGenesisTime < 22 * 86400) asert.mine(big);
    EXPECT_GT(asert.difficulty(), 100.0);
    const int64_t first = asert.mine(node);
    EXPECT_GT(first, 10 * 86400);
    const int64_t second = asert.mine(node);
    const int64_t third = asert.mine(node);
    EXPECT_LT(second, 1.2 * atLimit);
    EXPECT_LT(third, 1.2 * atLimit);

    // The audit's replay: periods 1-5 (about 22.5 days), then the next period
    // at the difficulty the big miner left, mined by the node alone.
    Net bitcoin(PowAlgorithm::BITCOIN);
    while (bitcoin.tip->height + 1 < 5 * Consensus::RETARGET_INTERVAL) bitcoin.mine(big);
    EXPECT_GT(expectedHashes(bitcoin.nextBits()) / expectedHashes(bitcoin.params.powLimitBits), 100.0);
    const double stallDays = Consensus::RETARGET_INTERVAL * expectedHashes(bitcoin.nextBits()) / node / 86400;
    EXPECT_GT(stallDays, 10000.0);   // about 28,000
}

// DIFF-6 (documents a property, not a defect): while the hashrate is below the
// minimum difficulty's 7.2 MH/s, blocks come slower than 600 s at the limit and
// the chain falls behind its schedule. A large miner who arrives later mines at
// the limit until the chain is back on schedule: about (delay / 600) blocks.
TEST(Difficulty, SlowStartLetsANewMinerCatchUpAtTheLimit) {
    Net n;
    while (n.time - kGenesisTime < 30 * 86400) n.mine(0.5e6);
    const int64_t behind = (n.time - kGenesisTime) - 600 * int64_t(n.tip->height);
    EXPECT_GT(behind, 2'000'000);
    int atLimit = 0;
    while (n.nextBits() == n.params.powLimitBits) {
        n.mine(1e9);
        ++atLimit;
    }
    EXPECT_NEAR(atLimit, double(behind) / 596, 0.01 * behind / 596);
}
