#ifndef CRYPTO_H
#define CRYPTO_H

#include <array>
#include <vector>
#include <cstdint>
#include <string>
#include <functional>

namespace Bucks {

// ---------------- Hash Types ----------------

using Hash256 = std::array<uint8_t, 32>;
using Hash160 = std::array<uint8_t, 20>;
using PrivateKey = std::array<uint8_t, 32>;
using PublicKey = std::vector<uint8_t>; // compressed (33 bytes)

// ---------------- Hashing ----------------

Hash256 sha256(const std::vector<uint8_t>& data);
Hash256 doubleSha256(const std::vector<uint8_t>& data);
Hash160 ripemd160(const std::vector<uint8_t>& data);
Hash160 hash160(const std::vector<uint8_t>& data);

// ---------------- BIP-8192 Primitives ----------------

bool pbkdf2_hmac_sha512(
    const char* pass, size_t passLen,
    const char* salt, size_t saltLen,
    uint32_t iterations,
    uint8_t* out, size_t outLen
);

std::vector<uint8_t> hmac_sha512(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& data
);

std::string unicode_nfkd(const std::string& input);

// ---------------- Encoding ----------------

std::string hexStr(const std::vector<uint8_t>& data);
std::vector<uint8_t> parseHex(const std::string& hex);

std::string base58Encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> base58Decode(const std::string& data);

std::string base58CheckEncode(uint8_t version, const std::vector<uint8_t>& payload);

// ---------------- Keys ----------------

bool generatePrivateKey(PrivateKey& out);
bool derivePublicKey(const PrivateKey& priv, PublicKey& pub);

// ---------------- EC Arithmetic (for HD) ----------------

/**
 * Add two scalars (private keys) mod n
 */
bool addScalar(const PrivateKey& p1, const PrivateKey& p2, PrivateKey& out);

/**
 * Add a scalar (p2) to a point (p1)
 * point = p1 + p2*G
 */
bool addPoint(const PublicKey& p1, const PrivateKey& p2, PublicKey& out);

// ---------------- ECDH (for P2P Encryption) ----------------

/**
 * Compute shared secret using ECDH (secp256k1)
 */
bool computeSharedSecret(const PrivateKey& priv, const PublicKey& otherPub, Hash256& secret);

// ---------------- Signatures ----------------

bool signECDSA(
    const Hash256& msgHash,
    const PrivateKey& priv,
    std::vector<uint8_t>& signature
);

bool verifyECDSA(
    const Hash256& msgHash,
    const std::vector<uint8_t>& signature,
    const PublicKey& pub
);

// ---------------- Utilities ----------------

bool hashLessThan(const Hash256& a, const Hash256& b);

} // namespace Bucks

namespace std {
template<>
struct hash<Bucks::Hash256> {
    size_t operator()(const Bucks::Hash256& h) const noexcept {
        // FNV-1a 64-bit hash folded to size_t
        uint64_t hash = 14695981039346656037ULL;
        for (uint8_t b : h) {
            hash ^= static_cast<uint64_t>(b);
            hash *= 1099511628211ULL;
        }
        return static_cast<size_t>(hash);
    }
};
} // namespace std

#endif // CRYPTO_H
