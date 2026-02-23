#include <gtest/gtest.h>
#include "crypto.h"
#include <chrono>
#include <vector>
#include <iostream>

TEST(Performance, SHA256_Benchmark) {
    const size_t ITERATIONS = 100000;
    std::string data = "Performance testing for blockchain hash function payload of typical size 512 bytes..."; 
    // Pad to 512 bytes
    while (data.size() < 512) data += "padding";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < ITERATIONS; ++i) {
        std::vector<uint8_t> bytes(data.begin(), data.end());
        auto hash = Bucks::sha256(bytes);
        // Prevent optimization (use result)
        if (hash[0] == 0xFF) volatile int x = 0;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "[ PERF ] SHA256 512B x " << ITERATIONS << ": " << duration << " ms" << std::endl;
    
    // Assert reasonable performance (e.g., < 1000ms for 100k hashes implies > 100k op/s)
    // This depends heavily on hardware, but it catches massive regressions.
    // 100,000 hashes in 1s is 10us per hash.
    // OpenSSL SHA256 is very fast. 
    EXPECT_LE(duration, 5000) << "Hashing performance is too slow!";
}
