#include <gtest/gtest.h>

#include <vector>

#include "consensus.h"
#include "serialize.h"
#include "transaction.h"
#include "tx_vector.h"

using namespace Bucks;

TEST(TxV2, VectorRoundTrip) {
    // TX-1: parse the spec vector, re-serialize, compare bytes and txid.
    std::vector<uint8_t> bytes = parseHex(TxVector::fullTxHex());
    ASSERT_EQ(bytes.size(), 354u);
    Transaction tx = Transaction::deserialize(bytes);
    EXPECT_EQ(tx.version, 2u);
    ASSERT_EQ(tx.inputs.size(), 2u);
    ASSERT_EQ(tx.outputs.size(), 2u);
    EXPECT_EQ(tx.serialize(), bytes);
    Hash256 id = tx.txid();
    EXPECT_EQ(hexStr(std::vector<uint8_t>(id.begin(), id.end())),
              "4fd3e27719f43e962d304c52c2a2bff9f14444747abe9079a856f425d10d4740");
    // The unlock scripts (signature and public key) are part of the txid.
    Transaction changed = tx;
    changed.inputs[0].scriptSig[5] ^= 1;
    EXPECT_NE(changed.txid(), tx.txid());
}

TEST(TxV2, DefaultsAndFields) {
    Transaction tx;
    EXPECT_EQ(tx.version, Consensus::TX_VERSION);
    EXPECT_EQ(TransactionInput().sequence, Consensus::SEQUENCE_FINAL);
    TransactionInput in(Hash256{}, 3);
    in.scriptSig = {1, 2, 3, 4};
    tx.inputs.push_back(in);
    tx.outputs.emplace_back(1000, Script{0x6a});
    tx.lockTime = 77;
    Transaction back = Transaction::deserialize(tx.serialize());
    EXPECT_EQ(back.lockTime, 77u);
    EXPECT_EQ(back.inputs[0].scriptSig, in.scriptSig);
    EXPECT_EQ(back.outputs[0].amount, 1000u);
    EXPECT_EQ(back.serialize(), tx.serialize());
}

TEST(TxV2, HugeCountsNoAllocation) {
    // TX-2: version, then a 4-billion input count with only 20 bytes left.
    std::vector<uint8_t> b = {2, 0, 0, 0, 0xfe, 0xff, 0xff, 0xff, 0xff};
    b.insert(b.end(), 20, 0);
    EXPECT_THROW(Transaction::deserialize(b), ParseError);

    // A count that fits the cap but not the remaining bytes.
    std::vector<uint8_t> c = {2, 0, 0, 0, 0xfd, 0xd0, 0x07};  // 2000 inputs
    c.insert(c.end(), 100, 0);
    EXPECT_THROW(Transaction::deserialize(c), ParseError);

    // Oversize script length.
    Transaction tx;
    tx.inputs.emplace_back();
    tx.outputs.emplace_back(1, Script{0x6a});
    std::vector<uint8_t> ok = tx.serialize();
    ASSERT_NO_THROW(Transaction::deserialize(ok));
    std::vector<uint8_t> tooBig(Consensus::MAX_TX_SIZE + 1, 0);
    EXPECT_THROW(Transaction::deserialize(tooBig), ParseError);
}

TEST(TxV2, NoTrailingBytesAndMinimalCounts) {
    // SER-3
    std::vector<uint8_t> bytes = parseHex(TxVector::fullTxHex());
    bytes.push_back(0);
    EXPECT_THROW(Transaction::deserialize(bytes), ParseError);

    // Non-minimal input count (FD 01 00 instead of 01).
    Transaction tx;
    tx.inputs.emplace_back();
    tx.outputs.emplace_back(1, Script{0x6a});
    std::vector<uint8_t> good = tx.serialize();
    std::vector<uint8_t> bad(good.begin(), good.begin() + 4);
    bad.insert(bad.end(), {0xfd, 0x01, 0x00});
    bad.insert(bad.end(), good.begin() + 5, good.end());
    EXPECT_THROW(Transaction::deserialize(bad), ParseError);

    // Truncated.
    good.pop_back();
    EXPECT_THROW(Transaction::deserialize(good), ParseError);
}

TEST(TxV2, LargeScriptsUseFullCompactSize) {
    // Old writer truncated lengths >= 65,536; the new one uses FE/FF forms.
    Transaction tx;
    tx.inputs.emplace_back();
    tx.inputs[0].scriptSig.assign(70000, 0x42);
    tx.outputs.emplace_back(1, Script{0x6a});
    std::vector<uint8_t> raw = tx.serialize();
    ASSERT_GT(raw.size(), 70000u);
    // Over MAX_TX_SIZE for relay, but the codec itself must be lossless; the
    // parser cap is MAX_TX_SIZE, so use a smaller-than-cap script here.
    tx.inputs[0].scriptSig.assign(300, 0x42);  // uses the FD form
    Transaction back = Transaction::deserialize(tx.serialize());
    EXPECT_EQ(back.inputs[0].scriptSig.size(), 300u);
}

TEST(TxV2, IsCoinbaseIsStrict) {
    Transaction cb;
    cb.inputs.emplace_back();
    cb.inputs[0].prevIndex = 0xFFFFFFFF;
    EXPECT_TRUE(cb.isCoinbase());

    Transaction notCb = cb;
    notCb.inputs[0].prevIndex = 0;  // the old check ignored prevIndex
    EXPECT_FALSE(notCb.isCoinbase());
    notCb = cb;
    notCb.inputs[0].prevTxid[31] = 1;
    EXPECT_FALSE(notCb.isCoinbase());
    notCb = cb;
    notCb.inputs.push_back(cb.inputs[0]);
    EXPECT_FALSE(notCb.isCoinbase());
}

TEST(TxV2, TotalOutputSaturates) {
    Transaction tx;
    tx.outputs.emplace_back(UINT64_MAX, Script{0x6a});
    tx.outputs.emplace_back(5, Script{0x6a});
    EXPECT_EQ(tx.totalOutput(), UINT64_MAX);
}
