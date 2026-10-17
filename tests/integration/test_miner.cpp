#include <gtest/gtest.h>

#include <atomic>

#include "consensus.h"
#include "genesis.h"
#include "mempool.h"
#include "miner.h"
#include "pow.h"
#include "test_chain.h"
#include "validation_rules.h"

using namespace Bucks;
using namespace Bucks::TestUtil;

// Spec: docs/design/consensus-validation.md section 15.8 (MIN-1 ... MIN-7).
// MIN-6 (mining through the API) belongs to the server switch, plan step 16.

namespace {

constexpr uint64_t kSubsidy = 50 * Consensus::COIN;

uint32_t readLE32(const std::vector<uint8_t>& v, size_t at = 0) {
    return uint32_t(v[at]) | (uint32_t(v[at + 1]) << 8) | (uint32_t(v[at + 2]) << 16) | (uint32_t(v[at + 3]) << 24);
}

uint64_t readLE64(const std::vector<uint8_t>& v, size_t at) {
    uint64_t r = 0;
    for (int i = 0; i < 8; ++i) r |= uint64_t(v[at + i]) << (8 * i);
    return r;
}

// `p` with another proof-of-work limit and its genesis re-mined to match.
ChainParams withLimit(ChainParams p, uint32_t limitBits) {
    p.powLimitBits = limitBits;
    p.powLimit = Pow::decodeCompact(p.powLimitBits).target;
    for (p.genesisNonce = 0;; ++p.genesisNonce) {
        p.genesisMerkleRoot = Genesis::build(p).header.merkleRoot;
        p.genesisHash = Genesis::build(p).getHash();
        if (Pow::checkProofOfWork(p.genesisHash, p.powLimitBits, p.powLimit)) break;
    }
    p.checkpoints = {Checkpoint{0, p.genesisHash}};
    return p;
}

// A chain with a mempool, an assembler and a miner, all on one clock.
struct Rig {
    explicit Rig(const ChainParams& params = chainParams(NetworkId::REGTEST))
        : chain(params),
          mempool(chain.params(), [this] { return chain.now(); }),
          assembler(chain.params(), chain.state(), mempool, [this] { return chain.now(); }),
          miner(chain.state(), assembler, [this] { return chain.now(); }) {
        chain.state().setStepHook([this](const std::vector<Block>& d, const std::vector<Block>& c) {
            mempool.updateForStep(d, c, chain.state());
        });
    }

    // Mines `n` blocks, remembering each coinbase txid.
    void grow(int n) {
        for (int i = 0; i < n; ++i) coinbases.push_back(chain.extend().transactions[0].txid());
    }

    Transaction spend(int n, uint64_t fee, int dest = 5) const {
        return spendTx(OutPoint{coinbases.at(n), 0}, kSubsidy, 3, kSubsidy - fee, dest, chain.params());
    }

    MineResult mine(std::string& err, const AssemblerOptions& o = {}) {
        return miner.mineOne(ownedBy(7), o, stop, err);
    }

    TestChain chain;
    Mempool mempool;
    BlockAssembler assembler;
    Miner miner;
    std::vector<Hash256> coinbases;
    std::atomic<bool> stop{false};
};

// One input spending a whole coinbase, many small outputs: close to the
// 100,000 byte standardness limit.
Transaction bigTx(const Rig& r, int n, uint64_t fee, size_t outputs = 2900) {
    Transaction tx;
    tx.version = Consensus::TX_VERSION;
    TransactionInput in(r.coinbases.at(n), 0);
    tx.inputs.push_back(in);
    const Script payee = ownedBy(5);   // one EC multiplication, not one per output
    uint64_t rest = kSubsidy - fee - 1000 * (outputs - 1);
    tx.outputs.emplace_back(rest, payee);
    for (size_t i = 1; i < outputs; ++i) tx.outputs.emplace_back(1000, payee);
    const std::vector<SpentOutput> spent = {SpentOutput{kSubsidy, ownedBy(3)}};
    signP2PKHInput(tx, 0, spent, privKey(3), r.chain.params().genesisHash);
    return tx;
}

} // namespace

// MIN-1
TEST(Miner, TemplateRespectsLimits) {
    Rig r;
    r.grow(125);   // coinbases at heights 1..26 are mature for the next block

    // 22 transactions of about 98.8 kB: more than 2 MB in the pool, with distinct fee rates.
    std::vector<Hash256> ids;
    uint64_t bytes = 0;
    for (int i = 0; i < 22; ++i) {
        Transaction tx = bigTx(r, i, 200'000 + 50'000 * uint64_t(i));
        const ValidationState st = r.mempool.accept(tx, r.chain.state());
        ASSERT_TRUE(st.valid()) << i << " " << st.code;
        ids.push_back(tx.txid());
        bytes += tx.serialize().size();
    }
    ASSERT_GT(bytes, 2'000'000u);

    BlockTemplate t;
    std::string err;
    ASSERT_TRUE(r.assembler.createNewBlock(ownedBy(7), {}, t, err)) << err;
    EXPECT_LE(t.block.serialize().size(), Consensus::MAX_BLOCK_SIZE);

    // Only the best fee rates fit: 10 transactions of ~98.8 kB, highest fees first.
    ASSERT_EQ(t.block.transactions.size(), 1u + 10);
    for (size_t k = 0; k < 10; ++k)
        EXPECT_EQ(t.block.transactions[1 + k].txid(), ids[21 - k]) << k;
    uint64_t fees = 0;
    for (int i = 12; i < 22; ++i) fees += 200'000 + 50'000 * uint64_t(i);
    EXPECT_EQ(t.fees, fees);
    EXPECT_EQ(t.block.transactions[0].outputs[0].amount, Rules::Subsidy(126, r.chain.params()) + fees);

    // The node accepts it.
    std::string mineErr;
    ASSERT_EQ(r.mine(mineErr), MineResult::FOUND) << mineErr;
    EXPECT_EQ(r.chain.tip()->height, 126u);
    EXPECT_EQ(r.chain.tip()->hash, r.miner.lastBlockHash());
    EXPECT_EQ(r.mempool.size(), 12u);   // the other twelve wait for the next block
}

// MIN-2
TEST(Miner, TemplateAtHalving) {
    Rig r;
    r.grow(149);   // regtest halves every 150 blocks
    Transaction tx = r.spend(0, 12'345);
    ASSERT_TRUE(r.mempool.accept(tx, r.chain.state()).valid());

    BlockTemplate t;
    std::string err;
    ASSERT_TRUE(r.assembler.createNewBlock(ownedBy(7), {}, t, err)) << err;
    EXPECT_EQ(t.block.header.height, 150u);
    EXPECT_EQ(t.block.transactions[0].outputs[0].amount, 25 * Consensus::COIN + 12'345);
    EXPECT_EQ(t.block.transactions[0].outputs[0].scriptPubKey, ownedBy(7));
}

// MIN-3: the Bitcoin retarget's period rules (H-TW1) move the template time.
TEST(Miner, TimewarpAwareTime) {
    ChainParams p = chainParams(NetworkId::REGTEST);
    p.powAlgorithm = PowAlgorithm::BITCOIN;
    Rig r(p);
    r.grow(2014);
    const int64_t now = r.chain.now();
    // Block 2015 is dated 3,000 s ahead of the clock (allowed up to 7,200).
    Block b = r.chain.build(r.chain.tip(), {}, 0, uint32_t(now + 3000 - r.chain.tip()->header.timestamp));
    ASSERT_TRUE(r.chain.submit(b).valid());
    ASSERT_EQ(r.chain.tip()->height, 2015u);

    BlockTemplate t;
    std::string err;
    ASSERT_TRUE(r.assembler.createNewBlock(ownedBy(7), {}, t, err)) << err;
    EXPECT_EQ(t.block.header.height, 2016u);
    EXPECT_GE(int64_t(t.block.header.timestamp), int64_t(r.chain.tip()->header.timestamp) - 600);
    EXPECT_EQ(int64_t(t.block.header.timestamp), now + 2400);   // tip time - 600, above the clock
    std::string mineErr;
    EXPECT_EQ(r.mine(mineErr), MineResult::FOUND) << mineErr;
    EXPECT_EQ(r.chain.tip()->height, 2016u);
}

// MIN-3b: ASERT (regtest's rule set, as on main and test) has no period rules,
// so the same template is dated by the clock, 3,000 s before its parent.
TEST(Miner, AsertTimeIgnoresPeriods) {
    Rig r;
    ASSERT_EQ(r.chain.params().powAlgorithm, PowAlgorithm::ASERT);
    r.grow(2014);
    const int64_t now = r.chain.now();
    Block b = r.chain.build(r.chain.tip(), {}, 0, uint32_t(now + 3000 - r.chain.tip()->header.timestamp));
    ASSERT_TRUE(r.chain.submit(b).valid());

    BlockTemplate t;
    std::string err;
    ASSERT_TRUE(r.assembler.createNewBlock(ownedBy(7), {}, t, err)) << err;
    EXPECT_EQ(t.block.header.height, 2016u);
    EXPECT_EQ(int64_t(t.block.header.timestamp), now);
    std::string mineErr;
    EXPECT_EQ(r.mine(mineErr), MineResult::FOUND) << mineErr;
    EXPECT_EQ(r.chain.tip()->height, 2016u);
}

// MIN-8: ASERT with retargeting on, end to end. Blocks one second apart are
// ahead of schedule, so every block is harder than its parent; the template
// carries the ASERT bits, the parent's bits are refused, a mined block is
// accepted. Limit 0x1e7fffff (the largest ASERT admits): about 2^17 hashes per block.
TEST(Miner, AsertBitsEndToEnd) {
    ChainParams p = withLimit(chainParams(NetworkId::REGTEST), 0x1e7fffff);
    p.powNoRetargeting = false;
    Rig r(p);
    ASSERT_TRUE(r.chain.ok()) << r.chain.error();
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(r.chain.submit(r.chain.build(r.chain.tip(), {}, 0, 1)).valid());
    const uint32_t want = Rules::GetNextWorkRequired(r.chain.tip(), r.chain.params());
    EXPECT_LT(Pow::decodeCompact(want).target, Pow::decodeCompact(r.chain.tip()->header.bits).target);

    BlockTemplate t;
    std::string err;
    ASSERT_TRUE(r.assembler.createNewBlock(ownedBy(7), {}, t, err)) << err;
    EXPECT_EQ(t.block.header.bits, want);

    Block easier = r.chain.build(r.chain.tip(), {}, 1, 1);
    easier.header.bits = r.chain.tip()->header.bits;
    for (easier.header.nonce = 0;; ++easier.header.nonce)
        if (Pow::checkProofOfWork(easier.header.hash(), easier.header.bits, p.powLimit)) break;
    EXPECT_EQ(r.chain.submit(easier).code, "bad-diffbits");

    std::string mineErr;
    ASSERT_EQ(r.mine(mineErr), MineResult::FOUND) << mineErr;
    EXPECT_EQ(r.chain.tip()->height, 4u);
    EXPECT_EQ(r.chain.tip()->header.bits, want);
}

// MIN-4
TEST(Miner, ExtraNonceRollover) {
    // A harder limit (about 2^16 hashes per block) and a nonce space of 10,
    // so the miner must roll the extra nonce many times.
    ChainParams p = withLimit(chainParams(NetworkId::REGTEST), 0x1f00ffff);
    Rig r(p);
    ASSERT_TRUE(r.chain.ok()) << r.chain.error();
    r.miner.setNonceSpaceForTesting(10);
    std::string err;
    ASSERT_EQ(r.mine(err), MineResult::FOUND) << err;
    EXPECT_GE(r.miner.extraNonceRollovers(), 1u);
    EXPECT_EQ(r.miner.templatesBuilt(), 1u);

    // The coinbase of the block carries the extra nonce that finally worked.
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(r.chain.store().getBlock(r.chain.tip()->hash, bytes));
    const Block mined = Block::deserialize(bytes);
    const std::vector<uint8_t>& sig = mined.transactions[0].inputs[0].scriptSig;
    EXPECT_EQ(readLE32(sig, 0), 1u);
    EXPECT_EQ(readLE64(sig, 4), r.miner.extraNonceRollovers());
}

// MIN-5
TEST(Miner, StaleTemplateAbandoned) {
    Rig r;
    r.grow(3);
    const uint32_t start = r.chain.tip()->height;
    r.miner.setCheckInterval(1);

    bool fired = false;
    Hash256 foreign{};
    r.miner.setBatchHookForTesting([&] {
        if (fired) return;
        fired = true;
        foreign = r.chain.extend({}, 9).getHash();   // another miner finds a block first
    });
    std::string err;
    ASSERT_EQ(r.mine(err), MineResult::FOUND) << err;

    EXPECT_EQ(r.miner.staleRestarts(), 1u);
    EXPECT_EQ(r.miner.templatesBuilt(), 2u);
    EXPECT_EQ(r.chain.tip()->height, start + 2);
    ASSERT_NE(r.chain.tip()->prev, nullptr);
    EXPECT_EQ(r.chain.tip()->prev->hash, foreign);   // built on the new tip, nothing stale submitted
}

// MIN-7
TEST(Miner, SyncRule) {
    ChainParams p = chainParams(NetworkId::REGTEST);
    p.id = NetworkId::TEST;   // the rule applies to every network but regtest
    Rig r(p);
    ASSERT_TRUE(r.chain.ok());
    BlockTemplate t;
    std::string err;

    // (a) The tip (genesis) is years older than the clock and nothing better is known: mining works.
    EXPECT_TRUE(r.assembler.createNewBlock(ownedBy(7), {}, t, err)) << err;

    // (b) A header with more work is known but its block is not connected.
    Block better = r.chain.build(r.chain.tip());
    ASSERT_TRUE(r.chain.state().ProcessNewBlockHeaders({better.header}).valid());
    EXPECT_FALSE(r.assembler.createNewBlock(ownedBy(7), {}, t, err));
    EXPECT_EQ(err, "node is syncing");
    EXPECT_EQ(r.chain.state().miningBlockedReason(), "node is syncing");

    // The block arrives: the tip catches up and mining is allowed again.
    ASSERT_TRUE(r.chain.submit(better).valid());
    EXPECT_TRUE(r.assembler.createNewBlock(ownedBy(7), {}, t, err)) << err;
    EXPECT_EQ(t.block.header.height, 2u);

    // Regtest has no restriction, even with a better unconnected header.
    Rig reg;
    Block ahead = reg.chain.build(reg.chain.tip());
    ASSERT_TRUE(reg.chain.state().ProcessNewBlockHeaders({ahead.header}).valid());
    EXPECT_TRUE(reg.assembler.createNewBlock(ownedBy(7), {}, t, err)) << err;
}

TEST(Miner, MinimumChainWorkBlocksMining) {
    ChainParams p = chainParams(NetworkId::REGTEST);
    p.id = NetworkId::TEST;
    p.minimumChainWork = U256(1000);
    Rig r(p);
    BlockTemplate t;
    std::string err;
    EXPECT_FALSE(r.assembler.createNewBlock(ownedBy(7), {}, t, err));
    EXPECT_EQ(err, "node is syncing");
}

TEST(Miner, CoinbaseShapeAndOptions) {
    Rig r;
    r.grow(2);
    AssemblerOptions o;
    o.minerTag = "Bucks test";
    o.extraPayload = {'C', 'I', 'D', ':', '1'};
    BlockTemplate t;
    std::string err;
    ASSERT_TRUE(r.assembler.createNewBlock(ownedBy(7), o, t, err)) << err;

    const Transaction& cb = t.block.transactions[0];
    EXPECT_EQ(cb.version, 2u);
    EXPECT_EQ(cb.lockTime, 0u);
    const std::vector<uint8_t>& sig = cb.inputs[0].scriptSig;
    ASSERT_EQ(sig.size(), 12u + o.minerTag.size());
    EXPECT_EQ(readLE32(sig, 0), 3u);
    EXPECT_EQ(readLE64(sig, 4), 0u);
    EXPECT_EQ(std::string(sig.begin() + 12, sig.end()), "Bucks test");
    ASSERT_EQ(cb.outputs.size(), 2u);
    EXPECT_EQ(cb.outputs[0].amount, kSubsidy);
    std::vector<uint8_t> payload;
    ASSERT_TRUE(parseNullData(cb.outputs[1].scriptPubKey, &payload));
    EXPECT_EQ(payload, o.extraPayload);
    EXPECT_EQ(cb.outputs[1].amount, 0u);

    // setExtraNonce changes the coinbase and the merkle root, and stays valid.
    const Hash256 rootBefore = t.block.header.merkleRoot;
    BlockAssembler::setExtraNonce(t, 77);
    EXPECT_NE(t.block.header.merkleRoot, rootBefore);
    EXPECT_EQ(readLE64(t.block.transactions[0].inputs[0].scriptSig, 4), 77u);
    ValidationState st;
    EXPECT_TRUE(r.assembler.testBlockValidity(t.block, st)) << st.code;

    // Option limits.
    AssemblerOptions bad;
    bad.minerTag = std::string(89, 'x');
    EXPECT_FALSE(r.assembler.createNewBlock(ownedBy(7), bad, t, err));
    bad = AssemblerOptions{};
    bad.extraPayload.assign(81, 1);
    EXPECT_FALSE(r.assembler.createNewBlock(ownedBy(7), bad, t, err));
    EXPECT_FALSE(r.assembler.createNewBlock(Script{0x51}, {}, t, err));
    EXPECT_EQ(err, "miner script must be P2PKH");
}

TEST(Miner, EveryCoinbaseIsUnique) {
    Rig r;
    r.grow(1);
    BlockTemplate a, b;
    std::string err;
    ASSERT_TRUE(r.assembler.createNewBlock(ownedBy(7), {}, a, err));
    BlockAssembler::setExtraNonce(a, 1);
    ASSERT_TRUE(r.assembler.createNewBlock(ownedBy(7), {}, b, err));
    EXPECT_NE(a.block.transactions[0].txid(), b.block.transactions[0].txid());
    // Next height, same miner: different again because of the height prefix.
    r.grow(1);
    BlockTemplate c;
    ASSERT_TRUE(r.assembler.createNewBlock(ownedBy(7), {}, c, err));
    EXPECT_NE(b.block.transactions[0].txid(), c.block.transactions[0].txid());
}

TEST(Miner, ZeroRewardUsesNullDataCoinbase) {
    // Halving every block: the subsidy is 0 from height 33 on.
    ChainParams p = chainParams(NetworkId::REGTEST);
    p.halvingInterval = 1;
    Rig r(p);
    r.grow(32);
    ASSERT_EQ(Rules::Subsidy(33, p), 0u);

    BlockTemplate t;
    std::string err;
    ASSERT_TRUE(r.assembler.createNewBlock(ownedBy(7), {}, t, err)) << err;
    const Transaction& cb = t.block.transactions[0];
    ASSERT_EQ(cb.outputs.size(), 1u);
    EXPECT_EQ(cb.outputs[0].amount, 0u);
    EXPECT_TRUE(parseNullData(cb.outputs[0].scriptPubKey));

    std::string mineErr;
    EXPECT_EQ(r.mine(mineErr), MineResult::FOUND) << mineErr;   // consensus accepts it
    EXPECT_EQ(r.chain.tip()->height, 33u);
}

TEST(Miner, StopRequestEndsTheLoop) {
    Rig r;
    r.stop = true;
    std::string err;
    EXPECT_EQ(r.mine(err), MineResult::STOPPED);
    EXPECT_EQ(r.chain.tip()->height, 0u);
}

// Audit CON-1: a header whose block never arrives must not hold mining forever.
TEST(Miner, HeaderWithoutABlockStopsGatingAfterTenMinutes) {
    ChainParams p = chainParams(NetworkId::REGTEST);
    p.id = NetworkId::TEST;
    Rig r(p);
    ASSERT_TRUE(r.chain.ok());
    BlockTemplate t;
    std::string err;

    Block phantom = r.chain.build(r.chain.tip());
    ASSERT_TRUE(r.chain.state().ProcessNewBlockHeaders({phantom.header}).valid());   // its body never comes
    EXPECT_EQ(r.chain.state().miningBlockedReason(), "node is syncing");

    r.chain.now() += 599;
    EXPECT_EQ(r.chain.state().miningBlockedReason(), "node is syncing");
    r.chain.now() += 2;   // 601 s after the header was first stored
    EXPECT_EQ(r.chain.state().miningBlockedReason(), "");
    EXPECT_TRUE(r.assembler.createNewBlock(ownedBy(7), {}, t, err)) << err;
}
