#ifndef BIP_8192_H
#define BIP_8192_H

#include <string>
#include <vector>
#include <cstdint>

namespace Bucks {

/**
 * Errors returned by BIP-8192 operations
 */
enum class Bip8192Error {
    OK = 0,
    INVALID_ENTROPY_SIZE,
    WORDLIST_LOAD_FAILED,
    INVALID_WORD_COUNT,
    INVALID_WORD,
    CHECKSUM_MISMATCH
};

/**
 * Generate a BIP-8192 mnemonic from entropy.
 *
 * Entropy rules:
 *  - 128 to 256 bits
 *  - Multiple of 32 bits
 */
std::string bip8192MnemonicFromEntropy(
    const std::vector<uint8_t>& entropy,
    const std::string& wordlistPath,
    Bip8192Error* err = nullptr
);

/**
 * Validate a BIP-8192 mnemonic against a wordlist.
 */
Bip8192Error bip8192ValidateMnemonic(
    const std::string& mnemonic,
    const std::string& wordlistPath
);

/**
 * Derive a 64-byte seed from mnemonic + passphrase
 * using PBKDF2-HMAC-SHA512 (2048 rounds).
 */
std::vector<uint8_t> bip8192MnemonicToSeed(
    const std::string& mnemonic,
    const std::string& passphrase
);

} // namespace Bucks

#endif // BIP_8192_H
