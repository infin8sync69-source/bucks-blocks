#ifndef HD_8192_H
#define HD_8192_H

#include <vector>
#include <array>
#include <string>
#include <cstdint>
#include "crypto.h"

namespace Bucks {

/**
 * BIP-8192 Extended Key
 * 
 * Contains a 32-byte private or public key and a 32-byte chain code.
 * Used for hierarchical deterministic derivation.
 */
struct ExtendedKey {
    bool isPrivate;
    uint32_t depth;
    uint32_t parentFingerprint;
    uint32_t childNumber;
    Hash256 chainCode;
    std::vector<uint8_t> key; // 32 bytes for private, 33 for public

    ExtendedKey() : isPrivate(false), depth(0), parentFingerprint(0), childNumber(0) {}
};

class HD8192 {
public:
    /**
     * Create Master Extended Key from a seed (usually 64-1024 bytes)
     */
    static ExtendedKey createMaster(const std::vector<uint8_t>& seed);

    /**
     * Derive a private child key from a private parent key (CKDpriv)
     * Hardened derivation if childIndex >= 0x80000000
     */
    static ExtendedKey deriveChild(const ExtendedKey& parent, uint32_t childIndex);

    /**
     * Derive a key from a path like "m/0'/1/2"
     */
    static ExtendedKey derivePath(const ExtendedKey& master, const std::string& path);

    /**
     * Utility to calculate fingerprint of a key
     */
    static uint32_t getFingerprint(const ExtendedKey& key);
};

} // namespace Bucks

#endif // HD_8192_H
