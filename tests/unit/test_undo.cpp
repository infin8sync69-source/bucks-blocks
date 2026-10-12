#include <gtest/gtest.h>

#include "consensus.h"
#include "serialize.h"
#include "undo.h"

using namespace Bucks;

namespace {

Coin coin(uint64_t amount, uint32_t height, bool cb) {
    Hash160 h;
    h.fill(0x33);
    Coin c;
    c.amount = amount;
    c.scriptPubKey = makeP2PKH(h);
    c.height = height;
    c.isCoinbase = cb;
    return c;
}

BlockUndo sample() {
    BlockUndo u;
    u.blockHash.fill(0xAB);
    u.spent = {coin(5000000000ull, 3, true), coin(12345, 100, false)};
    u.goldUndo = {1, 2, 3};
    return u;
}

// Rebuild a valid record around a (possibly corrupt) body, so only the
// intended field is wrong and the checksum cannot be what rejects it.
std::vector<uint8_t> withChecksum(std::vector<uint8_t> body) {
    Hash256 c = doubleSha256(body);
    body.insert(body.end(), c.begin(), c.end());
    return body;
}

std::vector<uint8_t> bodyOf(const std::vector<uint8_t>& rec) {
    return std::vector<uint8_t>(rec.begin(), rec.end() - 32);
}

} // namespace

TEST(Undo, RoundTrip) {
    BlockUndo u = sample();
    std::vector<uint8_t> raw = u.serialize();
    BlockUndo back = BlockUndo::deserialize(raw);
    EXPECT_EQ(back.blockHash, u.blockHash);
    EXPECT_EQ(back.spent, u.spent);
    EXPECT_EQ(back.goldUndo, u.goldUndo);
    EXPECT_EQ(back.serialize(), raw);
}

TEST(Undo, SizeExample) {
    // One spent input, no gold events: 1 + 32 + 1 + 39 + 1 + 32 = 106 bytes.
    BlockUndo u;
    u.spent = {coin(1000, 5, false)};
    EXPECT_EQ(u.serialize().size(), 106u);
    BlockUndo empty;
    EXPECT_EQ(empty.serialize().size(), 1u + 32 + 1 + 1 + 32);
    EXPECT_NO_THROW(BlockUndo::deserialize(empty.serialize()));
}

TEST(Undo, CorruptionDetected) {
    // CB-9: any single flipped byte, any truncation, trailing data.
    std::vector<uint8_t> raw = sample().serialize();
    for (size_t i = 0; i < raw.size(); ++i) {
        std::vector<uint8_t> bad = raw;
        bad[i] ^= 0x01;
        EXPECT_THROW(BlockUndo::deserialize(bad), ParseError) << "flip " << i;
    }
    for (size_t n = 0; n < raw.size(); ++n) {
        std::vector<uint8_t> cut(raw.begin(), raw.begin() + n);
        EXPECT_THROW(BlockUndo::deserialize(cut), ParseError) << "truncate " << n;
    }
    std::vector<uint8_t> extra = raw;
    extra.push_back(0);
    EXPECT_THROW(BlockUndo::deserialize(extra), ParseError);
}

TEST(Undo, StructuralErrorsWithValidChecksum) {
    const std::vector<uint8_t> body = bodyOf(sample().serialize());
    // Offsets: 0 version, 1..32 hash, 33 count, 34.. first coin record.
    auto mutate = [&](auto f) {
        std::vector<uint8_t> b = body;
        f(b);
        return withChecksum(b);
    };
    EXPECT_THROW(BlockUndo::deserialize(mutate([](auto& b) { b[0] = 2; })), ParseError);       // version
    EXPECT_THROW(BlockUndo::deserialize(mutate([](auto& b) { b[34 + 4] = 0x02; })), ParseError); // flags
    EXPECT_THROW(BlockUndo::deserialize(mutate([](auto& b) { b[33] = 3; })), ParseError);       // count + 1
    EXPECT_THROW(BlockUndo::deserialize(mutate([](auto& b) { b[33] = 1; })), ParseError);       // count - 1 leaves data
    EXPECT_THROW(BlockUndo::deserialize(mutate([](auto& b) {                                    // amount 0
                     for (int i = 0; i < 8; ++i) b[34 + 5 + i] = 0;
                 })), ParseError);
    EXPECT_THROW(BlockUndo::deserialize(mutate([](auto& b) {                                    // amount > MAX_MONEY
                     for (int i = 0; i < 8; ++i) b[34 + 5 + i] = 0xFF;
                 })), ParseError);
    EXPECT_THROW(BlockUndo::deserialize(mutate([](auto& b) { b[34 + 13 + 1] = 0x75; })), ParseError);  // not P2PKH
    EXPECT_THROW(BlockUndo::deserialize(mutate([](auto& b) { b[34 + 13] = 24; })), ParseError);        // script length
}

TEST(Undo, HugeCountsAndGoldLimit) {
    // A count far larger than the data must fail before allocating.
    Writer w;
    w.u8(1).hash256(Hash256{}).compactSize(30000000).compactSize(0);
    EXPECT_THROW(BlockUndo::deserialize(withChecksum(w.take())), ParseError);

    BlockUndo big;
    big.goldUndo.assign(MAX_GOLD_UNDO, 7);
    EXPECT_NO_THROW(BlockUndo::deserialize(big.serialize()));
    big.goldUndo.push_back(7);
    EXPECT_THROW(BlockUndo::deserialize(big.serialize()), ParseError);
}
