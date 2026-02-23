#include <gtest/gtest.h>
#include "network.h"
#include <vector>
#include <random>
#include <iostream>

TEST(FuzzTest, NetMessageDeserializeRobustness) {
    // Basic fuzzing: feed random garbage to deserialize and ensure no crashes
    // We expect it might throw or return UNKNOWN, but NOT crash (segfault/abort)
    
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
            Bucks::NetMessage msg = Bucks::NetMessage::deserialize(junk);
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
