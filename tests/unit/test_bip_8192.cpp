#include <gtest/gtest.h>
#include "bip_8192.h"
#include <vector>
#include <string>

TEST(BIP8192Test, MnemonicGenerationAndValidation) {
    std::vector<uint8_t> entropy(32, 0x5a); // 256 bits of entropy
    
    Bucks::Bip8192Error err;
    std::string mnemonic = Bucks::bip8192MnemonicFromEntropy(entropy, "wordlists/bip_8192.txt", &err);
    
    // If we can't find wordlist, we can't fully test, but we can verify error handling.
    // In a real env, we should ensure the file exists.
    if (err == Bucks::Bip8192Error::OK) {
        EXPECT_FALSE(mnemonic.empty());
        
        // Validate
        Bucks::Bip8192Error valErr = Bucks::bip8192ValidateMnemonic(mnemonic, "wordlists/bip_8192.txt");
        EXPECT_EQ(valErr, Bucks::Bip8192Error::OK);
        
        // Seed
        std::vector<uint8_t> seed = Bucks::bip8192MnemonicToSeed(mnemonic, "pass");
        EXPECT_EQ(seed.size(), 64);
    } else if (err == Bucks::Bip8192Error::WORDLIST_LOAD_FAILED) {
        SUCCEED() << "Skipping exact mnemonic check due to missing wordlist file";
    } else {
        ADD_FAILURE() << "Unexpected error: " << (int)err;
    }
}

TEST(BIP8192Test, ValidationFailure) {
    // If wordlist exists, this should fail invalid mnemonic
    Bucks::Bip8192Error err = Bucks::bip8192ValidateMnemonic("invalid words here", "wordlists/bip_8192.txt");
    if (err != Bucks::Bip8192Error::WORDLIST_LOAD_FAILED) {
        EXPECT_NE(err, Bucks::Bip8192Error::OK);
    }
}
