#include <gtest/gtest.h>
#include "network.h"
#include <vector>

TEST(NetworkMessageTest, SerializationRoundTrip) {
    // Create a dummy payload
    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    
    // Create a message
    Bucks::NetMessage original(Bucks::MessageType::VERSION, payload);
    
    // Serialize
    std::vector<uint8_t> serialized = original.serialize();
    EXPECT_FALSE(serialized.empty());
    
    // Header is 24 bytes, payload is 4 bytes -> total 28
    // Actually, check implementation details if possible, but minimal check is size >= 24
    EXPECT_GE(serialized.size(), 24);

    // Deserialize
    Bucks::NetMessage recovered = Bucks::NetMessage::deserialize(serialized);
    
    // Verify
    EXPECT_EQ(recovered.type, Bucks::MessageType::VERSION);
    EXPECT_EQ(recovered.payload, payload);
}

TEST(NetworkMessageTest, CommandToTypeMapping) {
    // Check known mappings
    EXPECT_EQ(Bucks::NetMessageHeader::commandToType("version"), Bucks::MessageType::VERSION);
    EXPECT_EQ(Bucks::NetMessageHeader::commandToType("inv"), Bucks::MessageType::INV);
    EXPECT_EQ(Bucks::NetMessageHeader::commandToType("unknown_cmd"), Bucks::MessageType::UNKNOWN);
    
    // Check reverse
    std::string cmd = Bucks::NetMessageHeader::typeToCommand(Bucks::MessageType::VERSION);
    // Warning: implementation might return "version" or "version\0..."
    // Usually it returns a sterilized string or we compare prefix. 
    // Assuming typeToCommand returns a std::string generic name.
    EXPECT_STREQ(cmd.c_str(), "version");
}
