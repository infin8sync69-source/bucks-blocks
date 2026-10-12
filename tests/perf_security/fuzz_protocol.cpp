#include <gtest/gtest.h>
#include "chainparams.h"
#include "network.h"
#include <vector>
#include <random>
#include <iostream>

TEST(FuzzTest, NetMessageDeserializeRobustness) {
    // Basic fuzzing: feed random garbage to deserialize and ensure no crashes
    // We expect it might throw or return UNKNOWN, but NOT crash (segfault/abort)
    
    const auto magic = Bucks::chainParams(Bucks::NetworkId::REGTEST).netMagic;
    std::mt19937 rng(42); // Deterministic seed
    std::uniform_int_distribution<uint8_t> byteDist(0, 255);
    std::uniform_int_distribution<size_t> lenDist(0, 1024); // Up to 1KB

    const int ITERATIONS = 1000;
    int caughtExceptions = 0;

    for (int i = 0; i < ITERATIONS; ++i) {
        size_t len = lenDist(rng);
        std::vector<uint8_t> junk(len);
        for (auto& b : junk) b = byteDist(rng);

        try {
            Bucks::NetMessage msg = Bucks::NetMessage::deserialize(junk, magic);
            // If it returns, it should be valid or UNKNOWN
        } catch (...) {
            // Exceptions are acceptable for garbage input (depending on implementation style)
            // But usually deserialize should handle gracefully. 
            // If checking robustness against crashes, catching is fine.
            caughtExceptions++;
        }
    }

    std::cout << "[ FUZZ ] survive " << ITERATIONS << " random inputs. Caught exceptions: " << caughtExceptions << std::endl;
}

// Mutated valid frames reach the later checks (command, length, checksum); pure noise
// stops at the magic. Every outcome is a message or one of the two frame exceptions.
TEST(FuzzTest, MutatedFramesOnlyFailInTheDocumentedWays) {
    const auto magic = Bucks::chainParams(Bucks::NetworkId::REGTEST).netMagic;
    std::mt19937 rng(4242);
    const Bucks::MessageType types[] = {Bucks::MessageType::VERSION, Bucks::MessageType::INV,
                                        Bucks::MessageType::GETHEADERS, Bucks::MessageType::HEADERS,
                                        Bucks::MessageType::BLOCK, Bucks::MessageType::TX,
                                        Bucks::MessageType::PING, Bucks::MessageType::ADDR};
    int accepted = 0, malformed = 0, incomplete = 0;
    for (int i = 0; i < 20000; ++i) {
        std::vector<uint8_t> payload(rng() % 200);
        for (auto& b : payload) b = uint8_t(rng());
        std::vector<uint8_t> frame = Bucks::NetMessage(types[rng() % 8], payload).serialize(magic);
        const int flips = rng() % 4;
        for (int k = 0; k < flips; ++k) frame[rng() % frame.size()] ^= uint8_t(1u << (rng() % 8));
        if (rng() % 5 == 0) frame.resize(rng() % (frame.size() + 1));
        try {
            Bucks::NetMessage m = Bucks::NetMessage::deserialize(frame, magic);
            ASSERT_LE(m.payload.size(), Bucks::MAX_MESSAGE_PAYLOAD);
            ++accepted;
        } catch (const Bucks::NetMalformed&) {
            ++malformed;
        } catch (const Bucks::NetIncomplete&) {
            ++incomplete;
        }
    }
    std::cout << "[ FUZZ ] frames: " << accepted << " accepted, " << malformed << " malformed, "
              << incomplete << " incomplete" << std::endl;
    EXPECT_GT(accepted, 1000);    // unmutated frames get through
    EXPECT_GT(malformed, 1000);
}
