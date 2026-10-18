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
// sha256 and doubleSha256 throw std::runtime_error if OpenSSL fails (never
// return an unset hash). ripemd160 is in-tree and cannot fail.

Hash256 sha256(const std::vector<uint8_t>& data);
Hash256 doubleSha256(const std::vector<uint8_t>& data);
Hash160 ripemd160(const std::vector<uint8_t>& data);
Hash160 hash160(const std::vector<uint8_t>& data);

// ---------------- Mnemonic / HD Primitives ----------------

bool pbkdf2_hmac_sha512(
    const char* pass, size_t passLen,
    const char* salt, size_t saltLen,
    uint32_t iterations,
    uint8_t* out, size_t outLen
);

// Returns the 64-byte MAC, or an empty vector if OpenSSL fails.
std::vector<uint8_t> hmac_sha512(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& data
);

// Note: the old unicode_nfkd() was removed. Mnemonic seeds are now derived
// from raw entropy (see bip_8192.h), so no text normalisation feeds a key.

// ---------------- Encoding ----------------

std::string hexStr(const std::vector<uint8_t>& data);
std::vector<uint8_t> parseHex(const std::string& hex);

std::string base58Encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> base58Decode(const std::string& str);

// Base58Check encode: SHA256d checksum appended; Bucks P2PKH uses version 0x00
std::string base58CheckEncode(uint8_t version, const std::vector<uint8_t>& payload);

// Base58Check decode: verifies checksum; returns (version || payload) or empty on failure
std::vector<uint8_t> base58CheckDecode(const std::string& encoded);

// ---------------- Keys ----------------
// All key functions fail closed: any invalid input or OpenSSL error returns
// false (and leaves no usable key behind).

/**
 * True if priv is a usable secp256k1 secret key: 1 <= priv < n.
 */
bool isValidPrivateKey(const PrivateKey& priv);

/**
 * Strict public key check: exactly 33 bytes, 0x02/0x03 prefix, canonical
 * x < p, on the curve and not the point at infinity. Uncompressed (0x04)
 * and hybrid (0x06/0x07) encodings are rejected.
 */
bool isValidPublicKey(const PublicKey& pub);

/**
 * Random key in [1, n-1]. False (and out zeroed) if the RNG fails.
 */
bool generatePrivateKey(PrivateKey& out);

/**
 * Compressed pubkey for priv. False, with pub cleared, unless 1 <= priv < n.
 */
bool derivePublicKey(const PrivateKey& priv, PublicKey& pub);

// ---------------- EC Arithmetic (for HD) ----------------

/**
 * Add two scalars (private keys) mod n
 * Fails if p1 is not a valid key, p2 >= n, or the sum is 0.
 */
bool addScalar(const PrivateKey& p1, const PrivateKey& p2, PrivateKey& out);

/**
 * Add a scalar (p2) to a point (p1)
 * point = p1 + p2*G
 * Fails if p1 is not a valid key, p2 >= n, or the sum is infinity.
 */
bool addPoint(const PublicKey& p1, const PrivateKey& p2, PublicKey& out);

// ---------------- ECDH (for P2P Encryption) ----------------

/**
 * Compute shared secret using ECDH (secp256k1)
 * secret = SHA256(x(priv * otherPub)); fails on an invalid key on either side.
 */
bool computeSharedSecret(const PrivateKey& priv, const PublicKey& otherPub, Hash256& secret);

// ---------------- Signatures ----------------

/**
 * DER-encoded ECDSA signature, always low-S (s <= n/2).
 */
bool signECDSA(
    const Hash256& msgHash,
    const PrivateKey& priv,
    std::vector<uint8_t>& signature
);

/**
 * True only for a valid key (isValidPublicKey), a canonical DER signature
 * with r, s in [1, n-1] and, per Consensus::REQUIRE_LOW_S, s <= n/2, that
 * verifies. Any parse or OpenSSL failure returns false.
 */
bool verifyECDSA(
    const Hash256& msgHash,
    const std::vector<uint8_t>& signature,
    const PublicKey& pub
);

// ---------------- Utilities ----------------

bool hashLessThan(const Hash256& a, const Hash256& b);

/**
 * Startup self-test of the consensus primitives: SHA-256, RIPEMD-160 and
 * hash160 against published vectors, one secp256k1 key derivation and a
 * sign/verify round trip. False (err names the check) means this build or
 * the OpenSSL it runs on cannot be trusted with the chain.
 */
bool cryptoSelfTest(std::string& err);

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
