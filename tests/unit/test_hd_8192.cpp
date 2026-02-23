#include <gtest/gtest.h>
#include "hd_8192.h"
#include "crypto.h"

TEST(HD8192Test, MasterKeyCreation) {
    // 512-bit seed
    std::vector<uint8_t> seed(64, 0x01);
    
    // Check if we can create a master key without crashing
    // Note: Since ExtendedKey is internal, we mostly check public interface or successful return
    // Assuming createMaster returns an ExtendedKey
    Bucks::ExtendedKey master = Bucks::HD8192::createMaster(seed);
    
    // Basic validity checks (depth 0 for master)
    EXPECT_EQ(master.depth, 0);
    EXPECT_FALSE(master.key.empty());
    EXPECT_FALSE(master.chainCode.empty());
}

TEST(HD8192Test, ChildDerivation) {
    std::vector<uint8_t> seed(64, 0xab);
    Bucks::ExtendedKey master = Bucks::HD8192::createMaster(seed);
    
    // Normal derivation (index 0)
    Bucks::ExtendedKey child0 = Bucks::HD8192::deriveChild(master, 0);
    EXPECT_EQ(child0.depth, 1);
    EXPECT_EQ(child0.childNumber, 0);
    EXPECT_EQ(child0.key.size(), 32); // Private key size

    // Hardened derivation (index 0 | 0x80000000)
    Bucks::ExtendedKey childHardened = Bucks::HD8192::deriveChild(master, 0x80000000);
    EXPECT_EQ(childHardened.depth, 1);
    EXPECT_GE(childHardened.childNumber, 0x80000000);
}
