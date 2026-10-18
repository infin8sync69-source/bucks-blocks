#include <gtest/gtest.h>

#include "chain.h"
#include "consensus.h"
#include "genesis.h"
#include "pow.h"
#include "validation_rules.h"

using namespace Bucks;
using namespace Bucks::Rules;

// Spec: docs/design/consensus-validation.md sections 6.5, 6.6, 7.3 (HDR-1 ... HDR-10).

namespace {

const ChainParams& regtest() { return chainParams(NetworkId::REGTEST); }

// Mainnet parameters with the Bitcoin retarget, the only rule with period time rules.
ChainParams bitcoinRetarget() {
    ChainParams p = chainParams(NetworkId::MAIN);
    p.powAlgorithm = PowAlgorithm::BITCOIN;
    return p;
}

BlockHeader childOf(const BlockIndex* parent, uint32_t time, uint32_t bits) {
    BlockHeader h;
    h.version = Consensus::BLOCK_VERSION;
    h.height = parent->height + 1;
    h.prevBlockHash = parent->hash;
    h.timestamp = time;
    h.bits = bits;
    h.nonce = 0;
    return h;
}

// Grinds the nonce until the header meets its own target (regtest is easy).
void mine(BlockHeader& h, const ChainParams& p) {
    for (uint32_t n = 0;; ++n) {
        h.nonce = n;
        if (Pow::checkProofOfWork(h.hash(), h.bits, p.powLimit)) return;
    }
}

// Index with the genesis of `p` at the root.
struct Fixture {
    BlockIndexMap map;
    BlockIndex* genesis = nullptr;

    explicit Fixture(const ChainParams& p) {
        Block g = Genesis::build(p);
        genesis = map.insert(g.header, nullptr);
    }
};

// Extends `from` with `count` blocks spaced by `spacing` seconds, starting at
// `start`. Headers carry the regtest bits and are not mined: stage B does not
// check proof of work.
BlockIndex* extend(BlockIndexMap& map, BlockIndex* from, int count, uint32_t start, uint32_t spacing) {
    BlockIndex* tip = from;
    for (int i = 0; i < count; ++i) {
        BlockHeader h = childOf(tip, start + uint32_t(i) * spacing, tip->header.bits);
        tip = map.insert(h, tip);
    }
    return tip;
}

} // namespace

// HDR-1: stage A rejects a header whose hash is above its target.
TEST(HeaderRules, BadPow) {
    BlockHeader h;
    h.version = Consensus::BLOCK_VERSION;
    h.bits = regtest().powLimitBits;
    for (uint32_t n = 0;; ++n) {
        h.nonce = n;
        if (!Pow::checkProofOfWork(h.hash(), h.bits, regtest().powLimit)) break;
    }
    ValidationState st;
    EXPECT_FALSE(CheckBlockHeader(h, regtest(), st));
    EXPECT_EQ(st.result, ValidationResult::INVALID);
    EXPECT_EQ(st.code, "high-hash");

    mine(h, regtest());
    ValidationState ok;
    EXPECT_TRUE(CheckBlockHeader(h, regtest(), ok)) << ok.code;
}

TEST(HeaderRules, StageABadVersionAndBits) {
    BlockHeader h;
    h.version = 1;
    h.bits = regtest().powLimitBits;
    ValidationState st;
    EXPECT_FALSE(CheckBlockHeader(h, regtest(), st));
    EXPECT_EQ(st.code, "bad-version");

    h.version = Consensus::BLOCK_VERSION;
    h.bits = 0x21010000;  // exponent 0x21 overflows 256 bits
    ValidationState st2;
    EXPECT_FALSE(CheckBlockHeader(h, regtest(), st2));
    EXPECT_EQ(st2.code, "bad-diffbits-encoding");
}

// HDR-2: bits must equal the value the retarget rule gives.
TEST(HeaderRules, WrongBits) {
    Fixture f(regtest());
    BlockHeader h = childOf(f.genesis, f.genesis->header.timestamp + 600, 0x1f7fffff);
    ValidationState st;
    EXPECT_FALSE(ContextualCheckBlockHeader(h, f.genesis, 0, regtest(), st));
    EXPECT_EQ(st.result, ValidationResult::INVALID);
    EXPECT_EQ(st.code, "bad-diffbits");
}

// HDR-3: height must be parent + 1.
TEST(HeaderRules, WrongHeight) {
    Fixture f(regtest());
    BlockHeader h = childOf(f.genesis, f.genesis->header.timestamp + 600, f.genesis->header.bits);
    h.height = f.genesis->height + 2;
    ValidationState st;
    EXPECT_FALSE(ContextualCheckBlockHeader(h, f.genesis, 0, regtest(), st));
    EXPECT_EQ(st.code, "bad-height");
}

// HDR-4: time equal to the median time past is refused, one more is accepted.
TEST(HeaderRules, MtpBoundary) {
    Fixture f(regtest());
    const int64_t mtp = f.genesis->medianTimePast();
    const int64_t now = mtp + 100;

    BlockHeader atMtp = childOf(f.genesis, uint32_t(mtp), f.genesis->header.bits);
    ValidationState st;
    EXPECT_FALSE(ContextualCheckBlockHeader(atMtp, f.genesis, now, regtest(), st));
    EXPECT_EQ(st.code, "time-too-old");

    BlockHeader justAbove = childOf(f.genesis, uint32_t(mtp + 1), f.genesis->header.bits);
    ValidationState ok;
    EXPECT_TRUE(ContextualCheckBlockHeader(justAbove, f.genesis, now, regtest(), ok)) << ok.code;
}

// HDR-5: a header 7,201 s ahead is not stored, and becomes valid as the clock moves.
TEST(HeaderRules, FutureNotCached) {
    Fixture f(regtest());
    const int64_t now = f.genesis->header.timestamp + 100;
    BlockHeader h = childOf(f.genesis, uint32_t(now + 7201), f.genesis->header.bits);

    ValidationState st;
    EXPECT_FALSE(ContextualCheckBlockHeader(h, f.genesis, now, regtest(), st));
    EXPECT_EQ(st.result, ValidationResult::TIME_TOO_NEW);
    EXPECT_EQ(st.code, "time-too-new");

    ValidationState later;
    EXPECT_TRUE(ContextualCheckBlockHeader(h, f.genesis, now + 2, regtest(), later)) << later.code;
}

// HDR-6: the first block of a retarget period may not be more than 600 s
// earlier than its parent. Bitcoin retarget, synthetic 2016 headers.
TEST(HeaderRules, TimewarpFirstBlock) {
    const ChainParams main = bitcoinRetarget();
    BlockIndexMap map;
    Block g = Genesis::build(main);
    BlockIndex* tip = map.insert(g.header, nullptr);
    const uint32_t base = 1'300'000'000u;
    tip = extend(map, tip, 2015, base + 1, 600);
    ASSERT_EQ(tip->height, 2015u);

    const uint32_t bits = GetNextWorkRequired(tip, main);
    const uint32_t now = tip->header.timestamp + 10'000;

    BlockHeader tooEarly = childOf(tip, tip->header.timestamp - 601, bits);
    ValidationState st;
    EXPECT_FALSE(ContextualCheckBlockHeader(tooEarly, tip, now, main, st));
    EXPECT_EQ(st.code, "time-timewarp-attack");

    BlockHeader boundary = childOf(tip, tip->header.timestamp - 600, bits);
    ValidationState ok;
    EXPECT_TRUE(ContextualCheckBlockHeader(boundary, tip, now, main, ok)) << ok.code;
}

// HDR-7: the last block of a period may not be earlier than the first block,
// even when it is above the median time past. Bitcoin retarget.
TEST(HeaderRules, PeriodNotNegative) {
    const ChainParams main = bitcoinRetarget();
    BlockIndexMap map;
    Block g = Genesis::build(main);
    const uint32_t base = 1'300'000'000u;
    // Block 0 is late; blocks 1..2014 are earlier than block 0 but all equal,
    // so the median time past of block 2014 is that earlier time.
    g.header.timestamp = base + 5000;
    BlockIndex* genesis = map.insert(g.header, nullptr);
    BlockIndex* tip = nullptr;
    {
        BlockIndex* prev = genesis;
        for (int i = 1; i <= 2014; ++i) {
            BlockHeader h = childOf(prev, base, main.powLimitBits);
            prev = map.insert(h, prev);
        }
        tip = prev;
    }
    ASSERT_EQ(tip->height, 2014u);
    const uint32_t now = base + 100'000;

    BlockHeader negative = childOf(tip, base + 2000, main.powLimitBits);  // > MTP, < block 0
    ValidationState st;
    EXPECT_FALSE(ContextualCheckBlockHeader(negative, tip, now, main, st));
    EXPECT_EQ(st.code, "time-period-negative");

    BlockHeader fine = childOf(tip, base + 5000, main.powLimitBits);     // equal to block 0
    ValidationState ok;
    EXPECT_TRUE(ContextualCheckBlockHeader(fine, tip, now, main, ok)) << ok.code;
}

// HDR-10: ASERT (main and test) has no retarget period, so the HDR-6 and HDR-7
// headers are valid there; H-MTP and H-FUTURE still bound every timestamp.
TEST(HeaderRules, AsertHasOnlyMtpAndFutureRules) {
    const ChainParams& main = chainParams(NetworkId::MAIN);
    ASSERT_EQ(main.powAlgorithm, PowAlgorithm::ASERT);
    const uint32_t base = 1'300'000'000u;
    {
        // HDR-6 layout: block 2016 is 601 s earlier than its parent.
        BlockIndexMap map;
        BlockIndex* tip = map.insert(Genesis::build(main).header, nullptr);
        tip = extend(map, tip, 2015, base + 1, 600);
        const uint32_t now = tip->header.timestamp + 10'000;
        BlockHeader early = childOf(tip, tip->header.timestamp - 601, GetNextWorkRequired(tip, main));
        ValidationState ok;
        EXPECT_TRUE(ContextualCheckBlockHeader(early, tip, now, main, ok)) << ok.code;

        BlockHeader atMtp = childOf(tip, uint32_t(tip->medianTimePast()), GetNextWorkRequired(tip, main));
        ValidationState old;
        EXPECT_FALSE(ContextualCheckBlockHeader(atMtp, tip, now, main, old));
        EXPECT_EQ(old.code, "time-too-old");
        BlockHeader future = childOf(tip, now + 7201, GetNextWorkRequired(tip, main));
        ValidationState late;
        EXPECT_FALSE(ContextualCheckBlockHeader(future, tip, now, main, late));
        EXPECT_EQ(late.code, "time-too-new");
    }
    {
        // HDR-7 layout: block 2015 is earlier than block 0 but above the MTP.
        BlockIndexMap map;
        Block g = Genesis::build(main);
        g.header.timestamp = base + 5000;
        BlockIndex* tip = map.insert(g.header, nullptr);
        for (int i = 1; i <= 2014; ++i) tip = map.insert(childOf(tip, base, main.powLimitBits), tip);
        ASSERT_EQ(tip->height, 2014u);
        BlockHeader negative = childOf(tip, base + 2000, GetNextWorkRequired(tip, main));
        ValidationState ok;
        EXPECT_TRUE(ContextualCheckBlockHeader(negative, tip, base + 100'000, main, ok)) << ok.code;
    }
}

// HDR-8: a checkpoint pins its hash, and forks below a checkpoint are refused.
TEST(HeaderRules, CheckpointFork) {
    ChainParams p = regtest();
    BlockIndexMap map;
    Block g = Genesis::build(p);
    BlockIndex* genesis = map.insert(g.header, nullptr);
    // Main chain 1..5 on regtest bits.
    BlockIndex* block5 = extend(map, genesis, 5, genesis->header.timestamp + 600, 600);
    ASSERT_EQ(block5->height, 5u);

    p.checkpoints = {Checkpoint{0, genesis->hash}, Checkpoint{5, block5->hash}};
    const uint32_t now = block5->header.timestamp + 1000;

    // Fork at height 3 (its parent is block 2 on the main chain).
    BlockIndex* block2 = block5->getAncestor(2);
    BlockHeader fork3 = childOf(block2, block2->header.timestamp + 601, block2->header.bits);
    ValidationState st;
    EXPECT_FALSE(ContextualCheckBlockHeader(fork3, block2, now, p, st, &map));
    EXPECT_EQ(st.code, "bad-fork-prior-to-checkpoint");

    // Competing block at the checkpoint height: wrong hash.
    BlockIndex* block4 = block5->getAncestor(4);
    BlockHeader rival5 = childOf(block4, block4->header.timestamp + 601, block4->header.bits);
    ValidationState st5;
    EXPECT_FALSE(ContextualCheckBlockHeader(rival5, block4, now, p, st5, &map));
    EXPECT_EQ(st5.code, "checkpoint-mismatch");

    // The real successor of the checkpoint is fine.
    BlockHeader next = childOf(block5, block5->header.timestamp + 600, block5->header.bits);
    ValidationState ok;
    EXPECT_TRUE(ContextualCheckBlockHeader(next, block5, now, p, ok, &map)) << ok.code;
}

// HDR-9: an unknown parent is not a penalty and nothing is stored.
TEST(HeaderRules, UnknownParentIsMissing) {
    Fixture f(regtest());
    BlockHeader h = childOf(f.genesis, f.genesis->header.timestamp + 600, f.genesis->header.bits);
    ValidationState st;
    EXPECT_FALSE(ContextualCheckBlockHeader(h, nullptr, 0, regtest(), st));
    EXPECT_EQ(st.result, ValidationResult::MISSING_PARENT);
    EXPECT_EQ(st.code, "prev-blk-not-found");
}

TEST(HeaderRules, ChildOfFailedParentRejected) {
    Fixture f(regtest());
    f.genesis->status |= FAILED_VALID;
    BlockHeader h = childOf(f.genesis, f.genesis->header.timestamp + 600, f.genesis->header.bits);
    ValidationState st;
    EXPECT_FALSE(ContextualCheckBlockHeader(h, f.genesis, 0, regtest(), st));
    EXPECT_EQ(st.code, "bad-prevblk");
}
