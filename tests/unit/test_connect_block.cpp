#include <gtest/gtest.h>

#include "chain.h"
#include "coins.h"
#include "connect_block.h"
#include "consensus.h"
#include "genesis.h"
#include "script.h"
#include "sighash.h"
#include "tx_vector.h"
#include "validation_rules.h"

using namespace Bucks;
using namespace TxVector;

// Spec: docs/design/consensus-validation.md sections 7.6, 7.7, 15.3 (CB-1 ... CB-10).

namespace {

const ChainParams& regtest() { return chainParams(NetworkId::REGTEST); }

Hash256 hashWith(uint8_t first) {
    Hash256 h{};
    h[0] = first;
    return h;
}

Script ownedBy(int key) { return makeP2PKH(hash160(pubKey(key))); }

Coin coinOf(uint64_t amount, uint32_t height, bool isCoinbase, int key = 1) {
    Coin c;
    c.amount = amount;
    c.scriptPubKey = ownedBy(key);
    c.height = height;
    c.isCoinbase = isCoinbase;
    return c;
}

// Coinbase paying `amount` to key 3, with the height committed in its scriptSig.
Transaction coinbaseTx(uint32_t height, uint64_t amount) {
    Transaction tx;
    tx.version = Consensus::TX_VERSION;
    TransactionInput in;
    in.prevTxid = Hash256{};
    in.prevIndex = 0xFFFFFFFF;
    in.sequence = Consensus::SEQUENCE_FINAL;
    in.scriptSig = {uint8_t(height), uint8_t(height >> 8), uint8_t(height >> 16), uint8_t(height >> 24), 'c'};
    tx.inputs.push_back(in);
    tx.outputs.emplace_back(amount, ownedBy(3));
    return tx;
}

// Spends `prev` (owned by `ownerKey`) into one output owned by `destKey`.
// Signed after the outputs are fixed, because the signature commits to them.
Transaction spendTx(const OutPoint& prev, uint64_t prevAmount, int ownerKey, uint64_t toAmount,
                    int destKey) {
    Transaction tx;
    tx.version = Consensus::TX_VERSION;
    TransactionInput in(prev.txid, prev.index);
    in.sequence = Consensus::SEQUENCE_FINAL;
    tx.inputs.push_back(in);
    tx.outputs.emplace_back(toAmount, ownedBy(destKey));
    const std::vector<SpentOutput> spent = {SpentOutput{prevAmount, ownedBy(ownerKey)}};
    EXPECT_TRUE(signP2PKHInput(tx, 0, spent, privKey(ownerKey), regtest().genesisHash));
    return tx;
}

std::vector<Hash256> idsOf(const std::vector<Transaction>& txs) {
    std::vector<Hash256> ids;
    for (const Transaction& t : txs) ids.push_back(t.txid());
    return ids;
}

// Header-only chain on regtest. Connect does not check headers, so the
// index is enough.
struct Env {
    BlockIndexMap map;
    BlockIndex* genesis = nullptr;

    Env() {
        Block g = Genesis::build(regtest());
        genesis = map.insert(g.header, nullptr);
    }

    BlockIndex* indexAt(uint32_t height) {
        BlockIndex* tip = genesis;
        while (tip->height < height) {
            BlockHeader h;
            h.version = Consensus::BLOCK_VERSION;
            h.height = tip->height + 1;
            h.prevBlockHash = tip->hash;
            h.timestamp = tip->header.timestamp + 600;
            h.bits = tip->header.bits;
            tip = map.insert(h, tip);
        }
        return tip;
    }
};

// Sets the view's best block to `hash`, and optionally adds coins (BIP30 / maturity tests).
void prepare(MemoryCoinsView& base, const Hash256& bestHash,
             const std::vector<std::pair<OutPoint, Coin>>& coins = {}) {
    CoinsViewCache c(&base);
    for (const auto& [op, coin] : coins) EXPECT_TRUE(c.addCoin(op, coin));
    c.setBestBlock(bestHash);
    EXPECT_TRUE(base.apply(c.changes()));
}

// Connects one block on top of index.prev. Returns the ok flag and fills the state.
bool connect(const MemoryCoinsView& base, BlockIndex* index, const std::vector<Transaction>& txs,
             ValidationState& st, BlockUndo* undoOut = nullptr) {
    CoinsViewCache view(&base);
    Block block;
    block.transactions = txs;
    GoldRules::GoldState gold;
    BlockUndo undo;
    return ConnectBlock(block, idsOf(txs), *index, view, gold, regtest(), undoOut ? *undoOut : undo, st);
}

} // namespace

// CB-1: the coinbase may not exceed subsidy + fees.
TEST(ConnectBlock, CoinbaseLimit) {
    Env env;
    BlockIndex* idx = env.indexAt(1);
    MemoryCoinsView base;
    prepare(base, env.genesis->hash);

    const uint64_t sub = Rules::Subsidy(1, regtest());
    ValidationState over;
    EXPECT_FALSE(connect(base, idx, {coinbaseTx(1, sub + 1)}, over));
    EXPECT_EQ(over.code, "bad-cb-amount");

    ValidationState ok;
    EXPECT_TRUE(connect(base, idx, {coinbaseTx(1, sub)}, ok)) << ok.code;
}

// CB-2: a block may claim its fees in the coinbase, and no more.
TEST(ConnectBlock, FeesClaimable) {
    Env env;
    BlockIndex* idx = env.indexAt(1);
    MemoryCoinsView base;
    const OutPoint prev{hashWith(0x51), 0};
    prepare(base, env.genesis->hash, {{prev, coinOf(1000, 0, false)}});

    Transaction spend = spendTx(prev, 1000, 1, 900, 2);  // fee 100
    const uint64_t sub = Rules::Subsidy(1, regtest());

    ValidationState ok;
    EXPECT_TRUE(connect(base, idx, {coinbaseTx(1, sub + 100), spend}, ok)) << ok.code;

    ValidationState greedy;
    EXPECT_FALSE(connect(base, idx, {coinbaseTx(1, sub + 101), spend}, greedy));
    EXPECT_EQ(greedy.code, "bad-cb-amount");
}

// CB-3: the subsidy halves at the regtest interval (150 blocks).
TEST(ConnectBlock, Halving) {
    Env env;
    BlockIndex* idx = env.indexAt(150);
    MemoryCoinsView base;
    prepare(base, idx->prev->hash);

    ValidationState st;
    EXPECT_FALSE(connect(base, idx, {coinbaseTx(150, 50 * Consensus::COIN)}, st));
    EXPECT_EQ(st.code, "bad-cb-amount");

    ValidationState ok;
    EXPECT_TRUE(connect(base, idx, {coinbaseTx(150, 25 * Consensus::COIN)}, ok)) << ok.code;
}

// CB-4: a new output may not overwrite an unspent one (BIP30).
TEST(ConnectBlock, BIP30) {
    Env env;
    BlockIndex* idx = env.indexAt(1);
    const Transaction cb = coinbaseTx(1, Rules::Subsidy(1, regtest()));
    MemoryCoinsView base;
    prepare(base, env.genesis->hash, {{OutPoint{cb.txid(), 0}, coinOf(1, 0, false)}});

    ValidationState st;
    EXPECT_FALSE(connect(base, idx, {cb}, st));
    EXPECT_EQ(st.code, "bad-txns-BIP30");
}

// CB-6: a coinbase output becomes spendable exactly 100 blocks later.
TEST(ConnectBlock, Maturity) {
    Env env;
    const OutPoint cbOp{hashWith(0x61), 0};
    const uint64_t prevAmount = 1000;

    for (uint32_t spendHeight : {109u, 110u}) {
        BlockIndex* idx = env.indexAt(spendHeight);
        MemoryCoinsView base;
        prepare(base, idx->prev->hash, {{cbOp, coinOf(prevAmount, 10, true)}});
        Transaction spend = spendTx(cbOp, prevAmount, 1, 900, 2);
        ValidationState st;
        const bool ok = connect(base, idx, {coinbaseTx(spendHeight, Rules::Subsidy(spendHeight, regtest()) + 100), spend}, st);
        if (spendHeight == 109) {
            EXPECT_FALSE(ok);
            EXPECT_EQ(st.code, "bad-txns-premature-spend-of-coinbase");
        } else {
            EXPECT_TRUE(ok) << st.code;
        }
    }
}

// CB-7: a later transaction may spend an output of an earlier one in the same
// block; disconnecting restores the view exactly.
TEST(ConnectBlock, InBlockChainSpend) {
    Env env;
    BlockIndex* idx = env.indexAt(1);
    const OutPoint funded{hashWith(0x71), 0};
    MemoryCoinsView base;
    prepare(base, env.genesis->hash, {{funded, coinOf(1000, 0, false)}});

    Transaction a = spendTx(funded, 1000, 1, 900, 2);  // key 2 receives 900
    Transaction b = spendTx(OutPoint{a.txid(), 0}, 900, 2, 800, 1);
    const uint64_t sub = Rules::Subsidy(1, regtest());

    CoinsViewCache view(&base);
    Block block;
    block.transactions = {coinbaseTx(1, sub + 200), a, b};
    GoldRules::GoldState gold;
    BlockUndo undo;
    ValidationState st;
    const auto txids = idsOf(block.transactions);
    ASSERT_TRUE(ConnectBlock(block, txids, *idx, view, gold, regtest(), undo, st)) << st.code;
    EXPECT_FALSE(view.haveCoin(funded));
    EXPECT_FALSE(view.haveCoin(OutPoint{a.txid(), 0}));
    EXPECT_TRUE(view.haveCoin(OutPoint{b.txid(), 0}));

    ASSERT_EQ(DisconnectBlock(block, txids, *idx, view, gold, undo), DisconnectResult::OK);
    const CoinChanges after = view.changes();
    EXPECT_TRUE(after.added.empty());
    EXPECT_TRUE(after.removed.empty());
    Coin restored;
    EXPECT_TRUE(view.getCoin(funded, restored));
    EXPECT_EQ(restored.amount, 1000u);
}

// CB-8: the undo record restores the coin's height and coinbase flag.
TEST(ConnectBlock, UndoRestoresHeight) {
    Env env;
    BlockIndex* idx = env.indexAt(150);
    const OutPoint cbOp{hashWith(0x81), 0};
    MemoryCoinsView base;
    prepare(base, idx->prev->hash, {{cbOp, coinOf(1000, 5, true)}});

    Transaction spend = spendTx(cbOp, 1000, 1, 900, 2);
    CoinsViewCache view(&base);
    Block block;
    block.transactions = {coinbaseTx(150, Rules::Subsidy(150, regtest()) + 100), spend};
    GoldRules::GoldState gold;
    BlockUndo undo;
    ValidationState st;
    const auto txids = idsOf(block.transactions);
    ASSERT_TRUE(ConnectBlock(block, txids, *idx, view, gold, regtest(), undo, st)) << st.code;
    ASSERT_EQ(DisconnectBlock(block, txids, *idx, view, gold, undo), DisconnectResult::OK);

    Coin back;
    ASSERT_TRUE(view.getCoin(cbOp, back));
    EXPECT_EQ(back.height, 5u);
    EXPECT_TRUE(back.isCoinbase);
}

// CB-9: undo data that does not match the block is corruption, not success.
TEST(ConnectBlock, UndoCorruptionDetected) {
    Env env;
    BlockIndex* idx = env.indexAt(1);
    const OutPoint funded{hashWith(0x91), 0};
    MemoryCoinsView base;
    prepare(base, env.genesis->hash, {{funded, coinOf(1000, 0, false)}});

    Transaction spend = spendTx(funded, 1000, 1, 900, 2);
    CoinsViewCache view(&base);
    Block block;
    block.transactions = {coinbaseTx(1, Rules::Subsidy(1, regtest()) + 100), spend};
    GoldRules::GoldState gold;
    BlockUndo undo;
    ValidationState st;
    const auto txids = idsOf(block.transactions);
    ASSERT_TRUE(ConnectBlock(block, txids, *idx, view, gold, regtest(), undo, st)) << st.code;

    BlockUndo wrongHash = undo;
    wrongHash.blockHash = hashWith(0x01);
    EXPECT_EQ(DisconnectBlock(block, txids, *idx, view, gold, wrongHash), DisconnectResult::CORRUPT);

    BlockUndo missing = undo;
    missing.spent.pop_back();
    EXPECT_EQ(DisconnectBlock(block, txids, *idx, view, gold, missing), DisconnectResult::CORRUPT);

    // A changed restored coin is caught by BlockUndo::deserialize's checksum,
    // not here; DisconnectBlock checks what it can see (hash and count).
}

// A signature by the wrong key or for a changed output is refused.
TEST(ConnectBlock, TamperedSignatureRejected) {
    Env env;
    BlockIndex* idx = env.indexAt(1);
    const OutPoint funded{hashWith(0xA1), 0};
    MemoryCoinsView base;
    prepare(base, env.genesis->hash, {{funded, coinOf(1000, 0, false)}});

    Transaction spend = spendTx(funded, 1000, 1, 900, 2);
    spend.outputs[0].amount = 901;  // changes the sighash after signing
    ValidationState st;
    EXPECT_FALSE(connect(base, idx, {coinbaseTx(1, Rules::Subsidy(1, regtest()) + 99), spend}, st));
    EXPECT_EQ(st.code, "bad-sig");
}

// Stub payload classifier: every NULL_DATA output costs one sigop.
namespace {
Bucks::GoldRules::Classification oneSigop(const std::vector<uint8_t>&) {
    return Bucks::GoldRules::Classification{Bucks::GoldRules::Kind::EVENT, 1};
}
struct ClassifierGuard {
    explicit ClassifierGuard(GoldRules::ClassifyFn fn) { GoldRules::setClassifierForTesting(fn); }
    ~ClassifierGuard() { GoldRules::setClassifierForTesting(nullptr); }
};
} // namespace

// CB-10: the block sigop limit counts gold verification checks.
TEST(ConnectBlock, SigopsLimit) {
    Script marker;
    ASSERT_TRUE(makeNullData({0x01}, marker));

    auto buildBlock = [&](size_t txCount) {
        Block block;
        block.header.version = Consensus::BLOCK_VERSION;
        block.transactions.push_back(coinbaseTx(1, 1));
        for (size_t i = 0; i < txCount; ++i) {
            Transaction tx;
            tx.version = Consensus::TX_VERSION;
            TransactionInput in(hashWith(uint8_t(0xB0 + i)), 0);
            in.sequence = Consensus::SEQUENCE_FINAL;
            in.scriptSig.clear();
            std::vector<uint8_t> der(8, 0x30);
            CompressedPubKey key{};
            key[0] = 0x02;
            makeP2PKHUnlock(der, key, in.scriptSig);
            tx.inputs.push_back(in);
            for (int j = 0; j < 2999; ++j) tx.outputs.emplace_back(0, marker);
            block.transactions.push_back(tx);
        }
        return block;
    };

    Block heavy = buildBlock(7);  // 7 inputs + 7 * 2999 gold sigops = 21,000
    {
        ClassifierGuard g(oneSigop);
        ValidationState st;
        std::vector<Hash256> ids = idsOf(heavy.transactions);
        bool mutated = false;
        heavy.header.merkleRoot = merkleRoot(ids, &mutated);
        EXPECT_FALSE(Rules::CheckBlock(heavy, regtest(), st));
        EXPECT_EQ(st.code, "bad-blk-sigops");
    }
    {
        ClassifierGuard g(nullptr);  // stub: markers cost nothing
        ValidationState st;
        std::vector<Hash256> ids = idsOf(heavy.transactions);
        bool mutated = false;
        heavy.header.merkleRoot = merkleRoot(ids, &mutated);
        EXPECT_TRUE(Rules::CheckBlock(heavy, regtest(), st)) << st.code;
    }
}
