#include <gtest/gtest.h>
#include "protocol_enc.h"
#include <vector>
#include <string>

TEST(ProtocolEncTest, DiffieHellmanExchangeAndEncryption) {
    // 1. Setup two parties
    Bucks::P2PSessionEncryption alice;
    Bucks::P2PSessionEncryption bob;

    // 2. Exchange public keys
    auto alicePub = alice.getEphemeralPubKey();
    auto bobPub = bob.getEphemeralPubKey();

    EXPECT_FALSE(alice.isEstablished());
    EXPECT_FALSE(bob.isEstablished());

    // 3. Establish shared secret
    bool aliceOk = alice.establishSharedSecret(bobPub);
    bool bobOk = bob.establishSharedSecret(alicePub);

    EXPECT_TRUE(aliceOk);
    EXPECT_TRUE(bobOk);
    EXPECT_TRUE(alice.isEstablished());
    EXPECT_TRUE(bob.isEstablished());

    // 4. Test Encryption: Alice -> Bob
    std::string plaintext = "Hello Blockchain World!";
    std::vector<uint8_t> plainBytes(plaintext.begin(), plaintext.end());

    std::vector<uint8_t> encrypted = alice.encrypt(plainBytes);
    EXPECT_FALSE(encrypted.empty());
    EXPECT_NE(encrypted, plainBytes); // Should differ

    std::vector<uint8_t> decrypted;
    bool decryptOk = bob.decrypt(encrypted, decrypted);
    
    EXPECT_TRUE(decryptOk);
    std::string decryptedText(decrypted.begin(), decrypted.end());
    EXPECT_EQ(plaintext, decryptedText);

    // 5. Test Decryption failure methods (optional, e.g.tampered data)
    std::vector<uint8_t> tampered = encrypted;
    if (!tampered.empty()) {
        tampered.back() ^= 0xFF; // Flip bits in last byte to break padding
        std::vector<uint8_t> badOutput;
        bool badDecrypt = bob.decrypt(tampered, badOutput);
        EXPECT_FALSE(badDecrypt);
    }
}
