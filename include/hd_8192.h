#ifndef HD_8192_H
#define HD_8192_H

#include <vector>
#include <array>
#include <string>
#include <cstdint>
#include "crypto.h"

namespace Bucks {

/**
 * SLIP-44 coin type used in the default derivation path. 1 is the SLIP-44
 * "testnet (all coins)" value: register a Bucks coin type with SLIP-44 before
 * mainnet and replace this placeholder.
 */
constexpr uint32_t BUCKS_COIN_TYPE = 1;

constexpr uint32_t HD_HARDENED_BIT = 0x80000000u;

/**
 * Default receive path for new and restored wallets:
 * m/44'/BUCKS_COIN_TYPE'/0'/0/0 (BIP-44 layout).
 */
std::string defaultReceivePath();

/**
 * BIP-32 Extended Key
 *
 * Contains a 32-byte private or 33-byte compressed public key and a 32-byte
 * chain code. Used for hierarchical deterministic derivation.
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

/**
 * Standard BIP-32 derivation over secp256k1 (master HMAC key "Bitcoin seed").
 * Every function fails closed: an invalid input or a failed step returns an
 * ExtendedKey with an empty `key`, and callers must check for that.
 */
class HD8192 {
public:
    /**
     * Create Master Extended Key from a seed (usually 64 bytes)
     */
    static ExtendedKey createMaster(const std::vector<uint8_t>& seed);

    /**
     * Derive a private child key from a private parent key (CKDpriv)
     * Hardened derivation if childIndex >= HD_HARDENED_BIT
     */
    static ExtendedKey deriveChild(const ExtendedKey& parent, uint32_t childIndex);

    /**
     * Strict path parser: "m" or "m/<i>[']/<j>[']/..." where each index is a
     * decimal number < 2^31 without leading zeros, optionally followed by
     * ' (or h/H) for hardened. Anything else (empty components, trailing
     * slashes, non-digits, overflow, whitespace) is rejected.
     */
    static bool parsePath(const std::string& path, std::vector<uint32_t>& indices);

    /**
     * Derive a key from a path like "m/0'/1/2". Returns an empty key for a
     * malformed path or a failed derivation step.
     */
    static ExtendedKey derivePath(const ExtendedKey& master, const std::string& path);

    /**
     * Utility to calculate fingerprint of a key
     */
    static uint32_t getFingerprint(const ExtendedKey& key);
};

} // namespace Bucks

#endif // HD_8192_H
