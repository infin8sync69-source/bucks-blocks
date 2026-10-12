#include <gtest/gtest.h>

#include "consensus.h"
#include "script.h"
#include "validation_rules.h"

using namespace Bucks;
using namespace Bucks::Rules;

// Spec: docs/design/consensus-validation.md section 15.3 (TXR-1 ... TXR-6).

namespace {

Hash256 hashWith(uint8_t first) {
    Hash256 h{};
    h[0] = first;
    return h;
}

CompressedPubKey pubKey(uint8_t seed) {
    CompressedPubKey k{};
    k[0] = 0x02;
    k[1] = seed;
    return k;
}

// A transaction that passes every rule except the one a test changes:
// one P2PKH input with a well-formed unlock, one P2PKH output.
Transaction validSpend() {
    Transaction tx;
    tx.version = Consensus::TX_VERSION;
    tx.lockTime = 0;

    TransactionInput in(hashWith(0x11), 0);
    in.sequence = Consensus::SEQUENCE_FINAL;
    std::vector<uint8_t> der(8, 0x30);
    makeP2PKHUnlock(der, pubKey(1), in.scriptSig);
    tx.inputs.push_back(in);

    Hash160 keyHash{};
    keyHash[0] = 0x42;
    tx.outputs.emplace_back(1000, makeP2PKH(keyHash));
    return tx;
}

// A coinbase with the given scriptSig. Default is valid for height 1.
Transaction coinbase(const std::vector<uint8_t>& scriptSig) {
    Transaction tx;
    tx.version = Consensus::TX_VERSION;
    tx.lockTime = 0;
    TransactionInput in;
    in.prevTxid = Hash256{};
    in.prevIndex = 0xFFFFFFFF;
    in.scriptSig = scriptSig;
    in.sequence = Consensus::SEQUENCE_FINAL;
    tx.inputs.push_back(in);
    Hash160 keyHash{};
    keyHash[0] = 0x43;
    tx.outputs.emplace_back(50 * Consensus::COIN, makeP2PKH(keyHash));
    return tx;
}

std::vector<uint8_t> cbScript(uint32_t height) {
    std::vector<uint8_t> s = {uint8_t(height), uint8_t(height >> 8), uint8_t(height >> 16),
                              uint8_t(height >> 24)};
    s.push_back('m');
    return s;
}

void expectReject(const Transaction& tx, const char* code) {
    ValidationState st;
    EXPECT_FALSE(CheckTransaction(tx, st)) << "expected " << code;
    EXPECT_EQ(st.code, code);
    EXPECT_EQ(st.result, ValidationResult::INVALID);
}

Script nullDataRaw(std::initializer_list<uint8_t> bytes) { return Script(bytes); }

} // namespace

// TXR-1: only P2PKH and canonical NULL_DATA outputs exist.
TEST(TxRules, OnlyTwoOutputTypes) {
    Script p2pk = {0x21};
    p2pk.insert(p2pk.end(), 33, 0x02);
    p2pk.push_back(0xac);

    Script p2sh = {0xa9, 0x14};
    p2sh.insert(p2sh.end(), 20, 0x00);
    p2sh.push_back(0x87);

    const std::vector<Script> bad = {
        p2pk,                                                 // P2PK
        Script{0x51},                                         // OP_TRUE
        Script{0x51, 0x21, 0x02, 0x51, 0xae},                 // bare multisig (shape only)
        p2sh,                                                 // P2SH
        nullDataRaw({0x6a, 0x4c, 0x01, 0x07}),                 // non-minimal push (PUSHDATA1)
    };
    for (const Script& s : bad) {
        Transaction tx = validSpend();
        tx.outputs[0] = TransactionOutput(0, s);
        // Amount 0 keeps the failure on the output type, not the value rule.
        expectReject(tx, "bad-txns-output-type");
    }

    // NULL_DATA payload of 1,025 bytes: a PUSHDATA2 that is over the limit.
    Script over = {0x6a, 0x4d, 0x01, 0x04};
    over.insert(over.end(), 1025, 0x55);
    Transaction tx = validSpend();
    tx.outputs[0] = TransactionOutput(0, over);
    expectReject(tx, "bad-txns-output-type");
}

// TXR-2: NULL_DATA outputs carry no value, and a zero-value one is valid.
TEST(TxRules, NullDataZeroValue) {
    Script marker;
    ASSERT_TRUE(makeNullData({'B', 'V', 'L', 'T'}, marker));

    Transaction ok = validSpend();
    ok.outputs.emplace_back(0, marker);
    ValidationState st;
    EXPECT_TRUE(CheckTransaction(ok, st)) << st.code;

    Transaction burns = validSpend();
    burns.outputs.emplace_back(1, marker);
    expectReject(burns, "bad-txns-nulldata-value");
}

// TXR-3: MoneyRange on outputs.
TEST(TxRules, MoneyRange) {
    Transaction tooBig = validSpend();
    tooBig.outputs[0].amount = Consensus::MAX_MONEY + 1;
    expectReject(tooBig, "bad-txns-vout-toolarge");

    Transaction sumTooBig = validSpend();
    Hash160 k{};
    k[0] = 0x44;
    sumTooBig.outputs[0].amount = Consensus::MAX_MONEY;
    sumTooBig.outputs.emplace_back(1, makeP2PKH(k));
    expectReject(sumTooBig, "bad-txns-txouttotal-toolarge");

    Transaction atLimit = validSpend();
    atLimit.outputs[0].amount = Consensus::MAX_MONEY;
    ValidationState st;
    EXPECT_TRUE(CheckTransaction(atLimit, st)) << st.code;
}

// TXR-4 (transaction part): no outpoint twice in one transaction.
TEST(TxRules, DuplicateInputs) {
    Transaction tx = validSpend();
    tx.inputs.push_back(tx.inputs[0]);
    expectReject(tx, "bad-txns-inputs-duplicate");
}

// TXR-5: coinbase scriptSig length, sequence and lock time.
TEST(TxRules, CoinbaseRules) {
    ValidationState st;
    EXPECT_TRUE(CheckTransaction(coinbase(cbScript(1)), st)) << st.code;

    expectReject(coinbase(std::vector<uint8_t>(3, 1)), "bad-cb-length");
    expectReject(coinbase(std::vector<uint8_t>(101, 1)), "bad-cb-length");

    Transaction seq = coinbase(cbScript(1));
    seq.inputs[0].sequence = 0;
    expectReject(seq, "bad-cb-sequence");

    Transaction lock = coinbase(cbScript(1));
    lock.lockTime = 1;
    expectReject(lock, "bad-cb-locktime");
}

// A non-coinbase with a null prevout is not a coinbase, so it is refused.
TEST(TxRules, NullPrevoutRejected) {
    Transaction tx = validSpend();
    tx.inputs[0].prevTxid = Hash256{};
    expectReject(tx, "bad-txns-prevout-null");
}

// Bad unlock layouts (SIG-4 covers the DER variants in the signature tests).
TEST(TxRules, UnlockLayout) {
    Transaction tx = validSpend();
    tx.inputs[0].scriptSig.push_back(0x00);  // one extra byte
    expectReject(tx, "bad-scriptsig-format");
}

// TXR-6: BIP113 finality for block and mempool.
TEST(TxRules, Finality) {
    Transaction tx = validSpend();
    tx.lockTime = 101;  // height lock
    tx.inputs[0].sequence = 0xFFFFFFFE;
    EXPECT_FALSE(IsFinal(tx, 100, 0));   // lockTime above height: not final
    EXPECT_FALSE(IsFinal(tx, 101, 0));   // lockTime == height: still not final (strict <)
    EXPECT_TRUE(IsFinal(tx, 102, 0));    // lockTime < height: final

    tx.lockTime = 101;
    tx.inputs[0].sequence = Consensus::SEQUENCE_FINAL;
    EXPECT_TRUE(IsFinal(tx, 100, 0));    // all inputs final: always final

    // Time lock: compared with the median time past, not the wall clock.
    Transaction timed = validSpend();
    timed.lockTime = 500'000'001;
    timed.inputs[0].sequence = 0xFFFFFFFE;
    EXPECT_FALSE(IsFinal(timed, 1000, 500'000'001));  // equal is not below
    EXPECT_TRUE(IsFinal(timed, 1000, 500'000'002));
}

// Block-level stateless checks on top of the transaction rules.
TEST(BlockRules, CoinbaseFirstAndUnique) {
    Block block;
    block.header.version = Consensus::BLOCK_VERSION;
    block.transactions.push_back(validSpend());  // not a coinbase
    ValidationState st;
    block.header.merkleRoot = merkleRoot({block.transactions[0].txid()});
    EXPECT_FALSE(CheckBlock(block, chainParams(NetworkId::REGTEST), st));
    EXPECT_EQ(st.code, "bad-cb-missing");
}

TEST(BlockRules, MerkleMismatchIsMutated) {
    Block block;
    block.header.version = Consensus::BLOCK_VERSION;
    block.transactions.push_back(coinbase(cbScript(1)));
    block.header.merkleRoot = hashWith(0x99);
    ValidationState st;
    EXPECT_FALSE(CheckBlock(block, chainParams(NetworkId::REGTEST), st));
    EXPECT_EQ(st.result, ValidationResult::MUTATED);
    EXPECT_EQ(st.code, "bad-txnmrklroot");
}

TEST(BlockRules, DuplicatedTransactionIsMutated) {
    Block block;
    block.header.version = Consensus::BLOCK_VERSION;
    Transaction cb = coinbase(cbScript(1));
    block.transactions = {cb, cb};
    bool mutated = false;
    std::vector<Hash256> ids = {cb.txid(), cb.txid()};
    block.header.merkleRoot = merkleRoot(ids, &mutated);
    ASSERT_TRUE(mutated);
    ValidationState st;
    EXPECT_FALSE(CheckBlock(block, chainParams(NetworkId::REGTEST), st));
    EXPECT_EQ(st.result, ValidationResult::MUTATED);
    EXPECT_EQ(st.code, "bad-txns-duplicate");
}

// Stage D: the coinbase must commit to the height.
TEST(BlockRules, CoinbaseHeightCommitment) {
    Block block;
    block.transactions.push_back(coinbase(cbScript(7)));
    ValidationState st;
    EXPECT_TRUE(ContextualCheckBlock(block, 7, 0, st)) << st.code;
    EXPECT_FALSE(ContextualCheckBlock(block, 8, 0, st));
    EXPECT_EQ(st.code, "bad-cb-height");
}

TEST(BlockRules, NonFinalTransactionInBlock) {
    Block block;
    Transaction cb = coinbase(cbScript(5));
    Transaction late = validSpend();
    late.lockTime = 10;
    late.inputs[0].sequence = 0xFFFFFFFE;
    block.transactions = {cb, late};
    ValidationState st;
    EXPECT_FALSE(ContextualCheckBlock(block, 5, 0, st));
    EXPECT_EQ(st.code, "bad-txns-nonfinal");
}

// Subsidy vectors of section 7.4.
TEST(Subsidy, MainVectors) {
    const ChainParams& p = chainParams(NetworkId::MAIN);
    EXPECT_EQ(Subsidy(1, p), 5'000'000'000ull);
    EXPECT_EQ(Subsidy(209'999, p), 5'000'000'000ull);
    EXPECT_EQ(Subsidy(210'000, p), 2'500'000'000ull);
    EXPECT_EQ(Subsidy(420'000, p), 1'250'000'000ull);
    EXPECT_EQ(Subsidy(6'720'000, p), 1ull);
    EXPECT_EQ(Subsidy(6'930'000, p), 0ull);
    EXPECT_EQ(Subsidy(13'440'000, p), 0ull);
    EXPECT_EQ(Subsidy(4'294'967'295u, p), 0ull);  // no undefined shift
}
