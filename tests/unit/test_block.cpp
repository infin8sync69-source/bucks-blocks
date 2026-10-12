#include <gtest/gtest.h>

#include "block.h"
#include "consensus.h"
#include "serialize.h"
#include "tx_vector.h"

using namespace Bucks;

namespace {

std::string hex(const std::vector<uint8_t>& v) { return hexStr(v); }
std::string hex(const Hash256& h) { return hexStr(std::vector<uint8_t>(h.begin(), h.end())); }

// Regtest genesis from docs/design/consensus-validation.md section 3.5.
Block regtestGenesis() {
    Transaction cb;
    TransactionInput in;
    in.prevIndex = 0xFFFFFFFF;
    in.scriptSig = parseHex("00000000" "4275636b7320726567746573742067656e65736973");
    cb.inputs.push_back(in);
    cb.outputs.emplace_back(0, Script{0x6a});

    BlockHeader h;
    h.height = 0;
    h.timestamp = 1767225600;
    h.bits = 0x207fffff;
    h.nonce = 0;
    Block b(h, {cb});
    b.header.merkleRoot = b.computeMerkleRoot();
    return b;
}

Block sampleBlock(size_t txs) {
    Block b = regtestGenesis();
    for (size_t i = 1; i < txs; ++i) {
        Transaction t = Transaction::deserialize(parseHex(TxVector::fullTxHex()));
        t.lockTime = uint32_t(i);
        b.transactions.push_back(t);
    }
    b.header.merkleRoot = b.computeMerkleRoot();
    return b;
}

} // namespace

TEST(BlockV2, RegtestGenesisVector) {
    Block g = regtestGenesis();
    EXPECT_EQ(hex(g.transactions[0].serialize()).size(), 86u * 2);
    EXPECT_EQ(hex(g.transactions[0].txid()),
              "e3cc1903035c668a5d3b09ad4873b414adaaaa5894a93dbd1b13a759b33f36d5");
    EXPECT_EQ(hex(g.header.merkleRoot), hex(g.transactions[0].txid()));

    std::vector<uint8_t> hdr = g.header.serialize();
    ASSERT_EQ(hdr.size(), 84u);
    EXPECT_EQ(hex(hdr),
              "02000000" "00000000" + std::string(64, '0') +
              "e3cc1903035c668a5d3b09ad4873b414adaaaa5894a93dbd1b13a759b33f36d5" +
              "00b95569" "ffff7f20" "00000000");
    EXPECT_EQ(hex(g.getHash()),
              "3491115920c29000d16f7d776bbab4a3399e5065abeeaf73fa23a273406bf520");
}

TEST(BlockV2, SerializationIsHeaderCountTransactions) {
    Block b = sampleBlock(3);
    std::vector<uint8_t> raw = b.serialize();
    // No magic bytes and no per-transaction length prefixes.
    EXPECT_EQ(std::vector<uint8_t>(raw.begin(), raw.begin() + 84), b.header.serialize());
    EXPECT_EQ(raw[84], 3);
    size_t expected = 84 + 1;
    for (const auto& t : b.transactions) expected += t.serialize().size();
    EXPECT_EQ(raw.size(), expected);

    Block back = Block::deserialize(raw);
    EXPECT_EQ(back.getHash(), b.getHash());
    ASSERT_EQ(back.transactions.size(), 3u);
    for (size_t i = 0; i < 3; ++i) EXPECT_EQ(back.transactions[i].txid(), b.transactions[i].txid());
    EXPECT_EQ(back.serialize(), raw);
}

TEST(BlockV2, HeaderIsExactly84Bytes) {
    BlockHeader h;
    h.timestamp = 1234567890;
    h.bits = 0x1d00ffff;
    h.nonce = 12345;
    std::vector<uint8_t> raw = h.serialize();
    ASSERT_EQ(raw.size(), 84u);
    BlockHeader back = BlockHeader::deserialize(raw);
    EXPECT_EQ(back.hash(), h.hash());
    EXPECT_EQ(back.version, Consensus::BLOCK_VERSION);
    raw.push_back(0);
    EXPECT_THROW(BlockHeader::deserialize(raw), ParseError);
    raw.resize(83);
    EXPECT_THROW(BlockHeader::deserialize(raw), ParseError);
}

TEST(BlockV2, HugeTxCount) {
    // BLK-2: header followed by a 4-billion transaction count.
    std::vector<uint8_t> raw = BlockHeader().serialize();
    raw.insert(raw.end(), {0xfe, 0xff, 0xff, 0xff, 0xff});
    raw.insert(raw.end(), 100, 0);
    EXPECT_THROW(Block::deserialize(raw), ParseError);

    // A count within the cap but larger than the data could hold.
    raw = BlockHeader().serialize();
    raw.insert(raw.end(), {0xfd, 0xe8, 0x03});  // 1000 transactions
    raw.insert(raw.end(), 100, 0);
    EXPECT_THROW(Block::deserialize(raw), ParseError);
}

TEST(BlockV2, MalformedBlocksRejected) {
    std::vector<uint8_t> raw = sampleBlock(2).serialize();

    std::vector<uint8_t> trailing = raw;
    trailing.push_back(0);
    EXPECT_THROW(Block::deserialize(trailing), ParseError);

    std::vector<uint8_t> truncated(raw.begin(), raw.end() - 1);
    EXPECT_THROW(Block::deserialize(truncated), ParseError);

    // Over MAX_BLOCK_SIZE is refused before anything is parsed.
    std::vector<uint8_t> huge(Consensus::MAX_BLOCK_SIZE + 1, 0);
    EXPECT_THROW(Block::deserialize(huge), ParseError);

    // The old format (magic F9BEB4D9, 4-byte count) is not accepted.
    std::vector<uint8_t> old = {0xF9, 0xBE, 0xB4, 0xD9};
    std::vector<uint8_t> hdr = BlockHeader().serialize();
    old.insert(old.end(), hdr.begin(), hdr.end());
    old.insert(old.end(), {0, 0, 0, 0});
    EXPECT_THROW(Block::deserialize(old), ParseError);

    EXPECT_THROW(Block::deserialize({}), ParseError);
}

TEST(BlockV2, EmptyBlockParsesButHasNoTransactions) {
    // The parser checks structure only; "at least one transaction" is a rule.
    Block b;
    Block back = Block::deserialize(b.serialize());
    EXPECT_TRUE(back.transactions.empty());
}
