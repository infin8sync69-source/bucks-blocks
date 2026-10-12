#include <gtest/gtest.h>

#include "consensus.h"
#include "mempool.h"
#include "policy.h"
#include "test_chain.h"

using namespace Bucks;
using namespace Bucks::TestUtil;

// Spec: docs/design/consensus-validation.md section 15.7 (MP-1 ... MP-10).

namespace {

constexpr uint64_t kSubsidy = 50 * Consensus::COIN;   // regtest, below the first halving

// A regtest chain with mature coinbases (all paid to key 3) and a mempool
// wired to the chain's step hook.
class MempoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        chain = std::make_unique<TestChain>();
        ASSERT_TRUE(chain->ok());
        mempool = std::make_unique<Mempool>(chain->params(), [this] { return clock; });
        chain->state().setStepHook([this](const std::vector<Block>& d, const std::vector<Block>& c) {
            mempool->updateForStep(d, c, chain->state());
        });
        clock = 1'000'000;
        for (int i = 0; i < 120; ++i) coinbases.push_back(chain->extend().transactions[0].txid());
        // coinbases[i] was created at height i + 1; tip is 120, next block 121:
        // heights 1..21 are mature.
    }

    OutPoint coin(int n) const { return OutPoint{coinbases.at(n), 0}; }   // n-th mature coinbase

    Transaction spend(int n, uint64_t fee, int dest = 5) const {
        return spendTx(coin(n), kSubsidy, 3, kSubsidy - fee, dest, chain->params());
    }

    ValidationState accept(const Transaction& tx, AcceptOptions opt = {}) {
        return mempool->accept(tx, chain->state(), opt);
    }

    std::unique_ptr<TestChain> chain;
    std::unique_ptr<Mempool> mempool;
    std::vector<Hash256> coinbases;
    int64_t clock = 0;
};

} // namespace

TEST_F(MempoolTest, AcceptsAValidSpend) {
    Transaction tx = spend(0, 10'000);
    ValidationState st = accept(tx);
    EXPECT_TRUE(st.valid()) << st.code;
    ASSERT_TRUE(mempool->contains(tx.txid()));
    const MempoolEntry* e = mempool->find(tx.txid());
    EXPECT_EQ(e->fee, 10'000u);
    EXPECT_EQ(e->size, tx.serialize().size());
    EXPECT_EQ(e->feeRate, Policy::FeeRate(10'000, e->size));
    EXPECT_EQ(e->entryHeight, 120u);
    EXPECT_EQ(e->sigOps, 1u);
    EXPECT_EQ(mempool->totalBytes(), e->size);
    EXPECT_TRUE(mempool->spends(coin(0)));

    // Same transaction again.
    EXPECT_EQ(accept(tx).result, ValidationResult::DUPLICATE);
}

// MP-1
TEST_F(MempoolTest, FullValidation) {
    // Bad signature: output changed after signing.
    Transaction tampered = spend(0, 10'000);
    tampered.outputs[0].amount -= 1;
    ValidationState st = accept(tampered);
    EXPECT_EQ(st.result, ValidationResult::INVALID);
    EXPECT_EQ(st.code, "bad-sig");

    // Wrong owner: key 4 signs for a coin owned by key 3.
    Transaction wrongOwner = spend(1, 10'000);
    {
        const std::vector<SpentOutput> spent = {SpentOutput{kSubsidy, ownedBy(3)}};
        SigHashCache cache(wrongOwner, spent);
        std::vector<uint8_t> der;
        ASSERT_TRUE(signECDSA(cache.sigHash(0, chain->params().genesisHash), privKey(4), der));
        CompressedPubKey k4;
        PublicKey p4 = pubKey(4);
        std::copy(p4.begin(), p4.end(), k4.begin());
        ASSERT_TRUE(makeP2PKHUnlock(der, k4, wrongOwner.inputs[0].scriptSig));
    }
    st = accept(wrongOwner);
    EXPECT_EQ(st.result, ValidationResult::INVALID);
    EXPECT_EQ(st.code, "bad-pubkey-hash");

    // Output type that is neither P2PKH nor NULL_DATA (anyone-can-spend OP_TRUE).
    Transaction badType = spend(2, 10'000);
    badType.outputs[0].scriptPubKey = Script{0x51};
    st = accept(badType);
    EXPECT_EQ(st.result, ValidationResult::INVALID);
    EXPECT_EQ(st.code, "bad-txns-output-type");

    // Coinbase transactions never enter the pool.
    EXPECT_EQ(accept(coinbaseTx(121, kSubsidy)).code, "coinbase");

    // Spending more than the inputs hold.
    Transaction overspend = spendTx(coin(3), kSubsidy, 3, kSubsidy + 1, 5, chain->params());
    st = accept(overspend);
    EXPECT_EQ(st.result, ValidationResult::INVALID);
    EXPECT_EQ(st.code, "bad-txns-in-belowout");

    EXPECT_EQ(mempool->size(), 0u);
}

// MP-2
TEST_F(MempoolTest, FirstSeenWins) {
    Transaction tx1 = spend(0, 10'000, 5);
    Transaction tx2 = spend(0, 20'000, 6);   // same coin, different payee and fee
    EXPECT_TRUE(accept(tx1).valid());
    ValidationState st = accept(tx2);
    EXPECT_EQ(st.result, ValidationResult::TX_CONFLICT);
    EXPECT_EQ(st.code, "txn-mempool-conflict");
    EXPECT_TRUE(mempool->contains(tx1.txid()));
    EXPECT_FALSE(mempool->contains(tx2.txid()));
}

// MP-3
TEST_F(MempoolTest, ConflictRemovedOnBlock) {
    Transaction tx1 = spend(0, 10'000, 5);
    Transaction tx2 = spend(0, 20'000, 6);
    Transaction other = spend(1, 10'000, 5);
    ASSERT_TRUE(accept(tx1).valid());
    ASSERT_TRUE(accept(other).valid());

    // A block confirms tx2 instead of tx1.
    chain->extend({tx2});
    EXPECT_EQ(chain->tip()->height, 121u);
    EXPECT_FALSE(mempool->contains(tx1.txid()));    // conflicting entry gone
    EXPECT_TRUE(mempool->contains(other.txid()));   // unrelated entry stays
    EXPECT_EQ(mempool->size(), 1u);
    EXPECT_FALSE(mempool->spends(coin(0)));

    // Confirmed transactions leave the pool.
    chain->extend({other});
    EXPECT_EQ(mempool->size(), 0u);
    EXPECT_EQ(mempool->totalBytes(), 0u);
}

// MP-4
TEST_F(MempoolTest, ReorgRemovesInvalidAndReAddsDisconnected) {
    const BlockIndex* fork = chain->tip();

    // Branch A: confirms X (spends coinbase 0, pays key 5), then another block.
    Transaction x = spend(0, 10'000, 5);
    chain->extend({x});                       // A1
    chain->extend();                          // A2
    ASSERT_EQ(chain->tip()->height, 122u);

    // Y spends X's output: a confirmed coin that exists only on branch A.
    Transaction y = spendTx(OutPoint{x.txid(), 0}, kSubsidy - 10'000, 5, kSubsidy - 30'000, 6, chain->params());
    ASSERT_TRUE(accept(y).valid());
    // Z spends an unrelated coin: must survive the reorg.
    Transaction z = spend(1, 10'000, 5);
    ASSERT_TRUE(accept(z).valid());

    // Branch B from the fork: three blocks, no X.
    Block b1 = chain->build(fork, {}, 1);
    ASSERT_TRUE(chain->submit(b1).valid());
    Block b2 = chain->build(chain->state().find(b1.getHash()), {}, 1);
    ASSERT_TRUE(chain->submit(b2).valid());
    EXPECT_EQ(chain->tip()->height, 122u);    // equal work: first seen stays
    Block b3 = chain->build(chain->state().find(b2.getHash()), {}, 1);
    ASSERT_TRUE(chain->submit(b3).valid());
    EXPECT_EQ(chain->tip()->hash, b3.getHash());

    EXPECT_FALSE(mempool->contains(y.txid()));   // its parent coin vanished
    EXPECT_TRUE(mempool->contains(x.txid()));    // X came back from the disconnected block
    EXPECT_TRUE(mempool->contains(z.txid()));    // unaffected
}

// MP-5
TEST_F(MempoolTest, EvictionByFeeRate) {
    // DER signatures differ in length, so sizes differ by a byte or two: use the real ones.
    Transaction t1 = spend(0, 1'000), t2 = spend(1, 2'000), t3 = spend(2, 3'000);
    const uint32_t s1 = t1.serialize().size(), s2 = t2.serialize().size(), s3 = t3.serialize().size();
    mempool = std::make_unique<Mempool>(chain->params(), [this] { return clock; }, s1 + s2 + s3 + 10);

    ASSERT_TRUE(accept(t1).valid());
    ASSERT_TRUE(accept(t2).valid());
    ASSERT_TRUE(accept(t3).valid());
    EXPECT_EQ(mempool->minFeeRate(), 0u);
    const uint64_t rate1 = mempool->find(t1.txid())->feeRate;

    // A higher fee rate evicts the cheapest entry and raises the rolling floor.
    Transaction high = spend(3, 5'000);
    ASSERT_TRUE(accept(high).valid());
    EXPECT_FALSE(mempool->contains(t1.txid()));
    EXPECT_TRUE(mempool->contains(high.txid()));
    const uint64_t floorAfter = rate1 + Policy::INCREMENTAL_RELAY_FEERATE;
    EXPECT_EQ(mempool->minFeeRate(), floorAfter);

    // Below the floor: refused on the fee floor.
    ValidationState st = accept(spend(4, 1'100));
    EXPECT_EQ(st.code, "min-fee-not-met");

    // Above the floor but cheaper than everything in a full pool: nothing is evicted.
    Transaction cheap = spend(5, 1'500);
    ASSERT_GT(Policy::FeeRate(1'500, cheap.serialize().size()), floorAfter);
    st = accept(cheap);
    EXPECT_EQ(st.result, ValidationResult::TX_POLICY);
    EXPECT_EQ(st.code, "mempool-full");
    EXPECT_EQ(mempool->size(), 3u);
    EXPECT_TRUE(mempool->contains(t2.txid()));

    // The floor halves every 12 hours and drops to zero.
    clock += Policy::ROLLING_FEE_HALFLIFE;
    EXPECT_LT(mempool->minFeeRate(), floorAfter);
    clock += 20 * Policy::ROLLING_FEE_HALFLIFE;
    EXPECT_EQ(mempool->minFeeRate(), 0u);
}

// MP-6
TEST_F(MempoolTest, DustAndMinFee) {
    Transaction dust = spendTx(coin(0), kSubsidy, 3, 545, 5, chain->params());
    ValidationState st = accept(dust);
    EXPECT_EQ(st.result, ValidationResult::TX_POLICY);
    EXPECT_EQ(st.code, "dust");

    // A one-input, one-output transaction is 185-193 bytes depending on the
    // signature length, so its floor is 185-193 awraq at 1,000 per 1,000 bytes.
    ValidationState low = accept(spend(1, 150));
    EXPECT_EQ(low.result, ValidationResult::TX_POLICY);
    EXPECT_EQ(low.code, "min-fee-not-met");
    st = accept(spend(2, 0));               // zero-fee spam
    EXPECT_EQ(st.code, "min-fee-not-met");
    EXPECT_TRUE(accept(spend(3, 200)).valid());   // above the floor for any size
    EXPECT_EQ(Policy::FeeForSize(Policy::MIN_RELAY_FEERATE, 193), 193u);
}

// MP-7
TEST_F(MempoolTest, OrphanLimits) {
    auto orphan = [&](uint32_t salt) {
        OutPoint missing;
        missing.txid = hashWith(0xEE);
        missing.txid[1] = uint8_t(salt);
        missing.txid[2] = uint8_t(salt >> 8);
        missing.index = 0;
        return spendTx(missing, kSubsidy, 3, kSubsidy - 5'000, 5, chain->params());
    };
    AcceptOptions opt;
    for (uint32_t i = 0; i < 101; ++i) {
        opt.peerId = 100 + (i % 10);
        EXPECT_EQ(accept(orphan(i), opt).result, ValidationResult::TX_MISSING_INPUTS);
    }
    EXPECT_LE(mempool->orphanCount(), Policy::MAX_ORPHAN_TXS);
    EXPECT_EQ(mempool->orphanCount(), Policy::MAX_ORPHAN_TXS);

    // One peer sending 26 more never holds more than 25 (the pool is full, so
    // random eviction may also take some of its own).
    opt.peerId = 999;
    for (uint32_t i = 0; i < 26; ++i) accept(orphan(1000 + i), opt);
    EXPECT_LE(mempool->orphanCount(), Policy::MAX_ORPHAN_TXS);
    EXPECT_LE(mempool->orphanCountForPeer(999), Policy::MAX_ORPHAN_TXS_PER_PEER);

    // On an otherwise empty pool the per-peer cap is exactly 25.
    mempool = std::make_unique<Mempool>(chain->params(), [this] { return clock; });
    for (uint32_t i = 0; i < 30; ++i) accept(orphan(5000 + i), opt);
    EXPECT_EQ(mempool->orphanCountForPeer(999), Policy::MAX_ORPHAN_TXS_PER_PEER);
    EXPECT_EQ(mempool->orphanCount(), Policy::MAX_ORPHAN_TXS_PER_PEER);

    // Twenty minutes later they have expired.
    clock += Policy::ORPHAN_TX_EXPIRY + 1;
    mempool->updateForStep({}, {}, chain->state());
    EXPECT_EQ(mempool->orphanCount(), 0u);
}

// MP-8
TEST_F(MempoolTest, OrphanAfterConfirmation) {
    Transaction parent = spend(0, 10'000, 5);
    ASSERT_TRUE(accept(parent).valid());
    Transaction child = spendTx(OutPoint{parent.txid(), 0}, kSubsidy - 10'000, 5, kSubsidy - 30'000, 6, chain->params());

    ValidationState st = accept(child);
    EXPECT_EQ(st.result, ValidationResult::TX_MISSING_INPUTS);
    EXPECT_EQ(st.code, "mempool-parent");   // unconfirmed parents are not supported in v1
    EXPECT_TRUE(mempool->hasOrphan(child.txid()));
    EXPECT_FALSE(mempool->contains(child.txid()));

    chain->extend({parent});                // the parent confirms
    EXPECT_TRUE(mempool->contains(child.txid()));
    EXPECT_FALSE(mempool->hasOrphan(child.txid()));
}

// MP-9
TEST_F(MempoolTest, ReloadValidated) {
    Transaction good = spend(0, 10'000);
    std::vector<uint8_t> tampered = spend(1, 10'000).serialize();
    tampered[60] ^= 0x01;                                  // inside the signature
    std::vector<uint8_t> garbage = {1, 2, 3};
    std::vector<uint8_t> trailing = spend(2, 10'000).serialize();
    trailing.push_back(0);

    std::vector<size_t> dropped;
    size_t loaded = mempool->reloadValidated({good.serialize(), tampered, garbage, trailing}, chain->state(), dropped);
    EXPECT_EQ(loaded, 1u);
    EXPECT_EQ(dropped, (std::vector<size_t>{1, 2, 3}));
    EXPECT_TRUE(mempool->contains(good.txid()));
    EXPECT_EQ(mempool->size(), 1u);
}

// MP-10
TEST_F(MempoolTest, NonStandardSequence) {
    Transaction tx = spend(0, 10'000);
    tx.inputs[0].sequence = 0x12345678;
    ValidationState st = accept(tx);
    EXPECT_EQ(st.result, ValidationResult::TX_POLICY);
    EXPECT_EQ(st.code, "non-standard-sequence");
}

TEST_F(MempoolTest, PrematureAndNonFinal) {
    // The most recent coinbase is still immature.
    Transaction young = spendTx(OutPoint{coinbases.back(), 0}, kSubsidy, 3, kSubsidy - 10'000, 5, chain->params());
    ValidationState st = accept(young);
    EXPECT_EQ(st.result, ValidationResult::TX_PREMATURE);
    EXPECT_EQ(st.code, "premature-spend-of-coinbase");

    // lockTime in the future with a non-final sequence.
    Transaction locked = spend(0, 10'000);
    locked.lockTime = 500;                         // a height
    locked.inputs[0].sequence = Policy::STANDARD_SEQUENCE_A;
    st = accept(locked);
    EXPECT_EQ(st.result, ValidationResult::TX_PREMATURE);
    EXPECT_EQ(st.code, "non-final");
}

TEST_F(MempoolTest, InitialBlockDownloadRefusesPeerTransactions) {
    AcceptOptions opt;
    opt.fromPeer = true;
    opt.inInitialBlockDownload = true;
    ValidationState st = accept(spend(0, 10'000), opt);
    EXPECT_EQ(st.result, ValidationResult::TX_POLICY);
    EXPECT_EQ(st.code, "initial-block-download");
    opt.fromPeer = false;   // our own wallet is still allowed
    EXPECT_TRUE(accept(spend(0, 10'000), opt).valid());
}

TEST_F(MempoolTest, ExpiryAfterFourteenDays) {
    Transaction tx = spend(0, 10'000);
    ASSERT_TRUE(accept(tx).valid());
    clock += Policy::MEMPOOL_EXPIRY - 1;
    mempool->updateForStep({}, {}, chain->state());
    EXPECT_TRUE(mempool->contains(tx.txid()));
    clock += 2;
    mempool->updateForStep({}, {}, chain->state());
    EXPECT_FALSE(mempool->contains(tx.txid()));
    EXPECT_FALSE(mempool->spends(coin(0)));
}

TEST_F(MempoolTest, FeeRateOrderForTemplates) {
    Transaction low = spend(0, 2'000), mid = spend(1, 5'000), high = spend(2, 9'000);
    ASSERT_TRUE(accept(mid).valid());
    ASSERT_TRUE(accept(low).valid());
    ASSERT_TRUE(accept(high).valid());
    std::vector<MempoolEntry> sorted = mempool->byFeeRateDescending();
    ASSERT_EQ(sorted.size(), 3u);
    EXPECT_EQ(sorted[0].txid, high.txid());
    EXPECT_EQ(sorted[1].txid, mid.txid());
    EXPECT_EQ(sorted[2].txid, low.txid());
}

TEST(PolicyRules, StandardnessOfNullData) {
    ValidationState st;
    Transaction tx;
    tx.inputs.emplace_back();
    tx.outputs.emplace_back(1000, ownedBy(5));
    Script data;
    ASSERT_TRUE(makeNullData(std::vector<uint8_t>(80, 1), data));
    tx.outputs.emplace_back(0, data);
    EXPECT_TRUE(Policy::IsStandardTx(tx, st)) << st.code;

    ASSERT_TRUE(makeNullData(std::vector<uint8_t>(81, 1), data));
    tx.outputs[1].scriptPubKey = data;
    EXPECT_FALSE(Policy::IsStandardTx(tx, st));
    EXPECT_EQ(st.code, "datacarrier");

    ASSERT_TRUE(makeNullData(std::vector<uint8_t>(10, 1), data));
    tx.outputs[1].scriptPubKey = data;
    tx.outputs.emplace_back(0, data);
    EXPECT_FALSE(Policy::IsStandardTx(tx, st));
    EXPECT_EQ(st.code, "multi-op-return");

    EXPECT_EQ(Policy::FeeRate(1000, 250), 4000u);
    EXPECT_EQ(Policy::FeeForSize(1000, 250), 250u);
    EXPECT_EQ(Policy::FeeForSize(1001, 250), 251u);   // rounds up
}
