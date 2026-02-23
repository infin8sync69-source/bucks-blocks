#include <gtest/gtest.h>
#include "block.h"

TEST(BlockSerialization, RoundTrip) {
    // Create a simple block
    Bucks::Block original;
    
    // Set header fields
    original.header.version = 1;
    original.header.prevBlockHash.fill(0x00);
    original.header.merkleRoot.fill(0x01);
    original.header.timestamp = 1234567890;
    original.header.bits = 0x1d00ffff;
    original.header.nonce = 12345;

    // Serialize to bytes
    std::vector<uint8_t> serialized = original.serialize();

    // Deserialize back
    Bucks::Block recovered = Bucks::Block::deserialize(serialized);

    // Verify fields match (via header members)
    EXPECT_EQ(recovered.header.version, original.header.version);
    EXPECT_EQ(recovered.header.prevBlockHash, original.header.prevBlockHash);
    EXPECT_EQ(recovered.header.merkleRoot, original.header.merkleRoot);
    EXPECT_EQ(recovered.header.timestamp, original.header.timestamp);
    EXPECT_EQ(recovered.header.bits, original.header.bits);
    EXPECT_EQ(recovered.header.nonce, original.header.nonce);
}
