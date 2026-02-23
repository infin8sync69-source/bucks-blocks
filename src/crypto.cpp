#define OPENSSL_SUPPRESS_DEPRECATED
#include "crypto.h"
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>

#include <sstream>
#include <iomanip>
#include <cstring>

#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace Bucks {

// ... (existing hashing functions)

// ---------------- BIP-8192 Primitives ----------------

bool pbkdf2_hmac_sha512(
    const char* pass, size_t passLen,
    const char* salt, size_t saltLen,
    uint32_t iterations,
    uint8_t* out, size_t outLen
) {
    return PKCS5_PBKDF2_HMAC(
        pass, static_cast<int>(passLen),
        reinterpret_cast<const unsigned char*>(salt), static_cast<int>(saltLen),
        iterations, EVP_sha512(),
        static_cast<int>(outLen), out
    ) == 1;
}

std::vector<uint8_t> hmac_sha512(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& data
) {
    unsigned int len = 64;
    std::vector<uint8_t> out(len);
    HMAC(EVP_sha512(), key.data(), static_cast<int>(key.size()), 
         data.data(), data.size(), out.data(), &len);
    return out;
}

std::string unicode_nfkd(const std::string& input) {
    // Basic NFKD stub for Arabic: 
    // In a real project, use ICU. For this demo, we ensure basic normalization.
    // Strip common diacritics (harakat) if they exist.
    std::string result;
    for (size_t i = 0; i < input.size(); ++i) {
        uint8_t c = (uint8_t)input[i];
        // Simplified: UTF-8 Arabic diacritics range 0xd9 0x8b to 0xd9 0x92
        if (i + 1 < input.size() && (uint8_t)input[i] == 0xd9 && 
           ((uint8_t)input[i+1] >= 0x8b && (uint8_t)input[i+1] <= 0x92)) {
            i++; // skip diacritic
            continue;
        }
        result += input[i];
    }
    return result;
}

// ... (existing encoding and key functions)


// ---------------- Hashing ----------------

Hash256 sha256(const std::vector<uint8_t>& data) {
    Hash256 out;
    SHA256(data.data(), data.size(), out.data());
    return out;
}

Hash256 doubleSha256(const std::vector<uint8_t>& data) {
    Hash256 first = sha256(data);
    std::vector<uint8_t> tmp(first.begin(), first.end());
    return sha256(tmp);
}

Hash160 ripemd160(const std::vector<uint8_t>& data) {
    Hash160 out;
    RIPEMD160(data.data(), data.size(), out.data());
    return out;
}

Hash160 hash160(const std::vector<uint8_t>& data) {
    Hash256 h = sha256(data);
    std::vector<uint8_t> tmp(h.begin(), h.end());
    return ripemd160(tmp);
}

// ---------------- Encoding ----------------

std::string hexStr(const std::vector<uint8_t>& data) {
    std::ostringstream ss;
    for (auto b : data)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    return ss.str();
}

std::string base58CheckEncode(uint8_t version, const std::vector<uint8_t>& payload) {
    // Simple stub: return hex for now
    std::vector<uint8_t> data = {version};
    data.insert(data.end(), payload.begin(), payload.end());
    return hexStr(data);
}

std::vector<uint8_t> parseHex(const std::string& hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < hex.size(); i += 2)
        out.push_back(std::stoi(hex.substr(i, 2), nullptr, 16));
    return out;
}

// ---------------- Base58 ----------------
// (keep your existing implementation, but no checksum guessing)

// ---------------- Keys ----------------

bool generatePrivateKey(PrivateKey& out) {
    return RAND_bytes(out.data(), out.size()) == 1;
}

bool derivePublicKey(const PrivateKey& priv, PublicKey& pub) {
    EC_KEY* key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!key) return false;

    BIGNUM* bn = BN_bin2bn(priv.data(), priv.size(), nullptr);
    EC_KEY_set_private_key(key, bn);

    const EC_GROUP* group = EC_KEY_get0_group(key);
    EC_POINT* point = EC_POINT_new(group);

    EC_POINT_mul(group, point, bn, nullptr, nullptr, nullptr);
    EC_KEY_set_public_key(key, point);

    pub.resize(33);
    EC_POINT_point2oct(
        group,
        point,
        POINT_CONVERSION_COMPRESSED,
        pub.data(),
        pub.size(),
        nullptr
    );

    EC_POINT_free(point);
    BN_free(bn);
    EC_KEY_free(key);
    return true;
}

// ---------------- EC Arithmetic (for HD) ----------------

bool addScalar(const PrivateKey& p1, const PrivateKey& p2, PrivateKey& out) {
    EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if (!group) return false;
    BIGNUM* n = BN_new();
    EC_GROUP_get_order(group, n, nullptr);

    BIGNUM* b1 = BN_bin2bn(p1.data(), 32, nullptr);
    BIGNUM* b2 = BN_bin2bn(p2.data(), 32, nullptr);
    BIGNUM* res = BN_new();
    BN_CTX* ctx = BN_CTX_new();
    
    if (!b1 || !b2 || !res || !ctx) {
        if (b1) BN_free(b1);
        if (b2) BN_free(b2);
        if (res) BN_free(res);
        if (ctx) BN_CTX_free(ctx);
        BN_free(n); EC_GROUP_free(group);
        return false;
    }

    BN_mod_add(res, b1, b2, n, ctx);

    if (BN_is_zero(res)) {
        BN_CTX_free(ctx); BN_free(res); BN_free(b1); BN_free(b2); BN_free(n); EC_GROUP_free(group);
        return false;
    }

    BN_bn2binpad(res, out.data(), 32);

    BN_CTX_free(ctx); BN_free(res); BN_free(b1); BN_free(b2); BN_free(n); EC_GROUP_free(group);
    return true;
}

bool addPoint(const PublicKey& p1, const PrivateKey& p2, PublicKey& out) {
    EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    
    // Convert p1 to point
    EC_POINT* pt1 = EC_POINT_new(group);
    if (!EC_POINT_oct2point(group, pt1, p1.data(), p1.size(), nullptr)) {
        EC_POINT_free(pt1); EC_GROUP_free(group);
        return false;
    }

    // Convert p2 to scalar and multiply by G
    BIGNUM* b2 = BN_bin2bn(p2.data(), 32, nullptr);
    EC_POINT* pt2 = EC_POINT_new(group);
    EC_POINT_mul(group, pt2, b2, nullptr, nullptr, nullptr);

    // Sum pt1 + pt2
    EC_POINT* res = EC_POINT_new(group);
    EC_POINT_add(group, res, pt1, pt2, nullptr);

    if (EC_POINT_is_at_infinity(group, res)) {
        EC_POINT_free(res); EC_POINT_free(pt1); EC_POINT_free(pt2);
        BN_free(b2); EC_GROUP_free(group);
        return false;
    }

    out.resize(33);
    EC_POINT_point2oct(group, res, POINT_CONVERSION_COMPRESSED, out.data(), 33, nullptr);

    EC_POINT_free(res); EC_POINT_free(pt1); EC_POINT_free(pt2);
    BN_free(b2); EC_GROUP_free(group);
    return true;
}

// ---------------- ECDH (for P2P Encryption) ----------------

bool computeSharedSecret(const PrivateKey& priv, const PublicKey& otherPub, Hash256& secret) {
    EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if (!group) return false;

    EC_POINT* pubPt = EC_POINT_new(group);
    if (!EC_POINT_oct2point(group, pubPt, otherPub.data(), otherPub.size(), nullptr)) {
        EC_POINT_free(pubPt); EC_GROUP_free(group);
        return false;
    }

    BIGNUM* privBN = BN_bin2bn(priv.data(), 32, nullptr);
    EC_POINT* resPt = EC_POINT_new(group);

    // Compute resPt = priv * otherPub
    if (!EC_POINT_mul(group, resPt, nullptr, pubPt, privBN, nullptr)) {
        BN_free(privBN); EC_POINT_free(resPt); EC_POINT_free(pubPt); EC_GROUP_free(group);
        return false;
    }

    // Shared secret is the SHA256 of the x-coordinate
    BIGNUM* x = BN_new();
    EC_POINT_get_affine_coordinates_GFp(group, resPt, x, nullptr, nullptr);
    
    std::vector<uint8_t> xBytes(32);
    BN_bn2binpad(x, xBytes.data(), 32);
    
    secret = sha256(xBytes);

    BN_free(x); BN_free(privBN); EC_POINT_free(resPt); EC_POINT_free(pubPt); EC_GROUP_free(group);
    return true;
}

// ---------------- Signatures ----------------

bool signECDSA(
    const Hash256& msgHash,
    const PrivateKey& priv,
    std::vector<uint8_t>& signature
) {
    EC_KEY* key = EC_KEY_new_by_curve_name(NID_secp256k1);
    BIGNUM* bn = BN_bin2bn(priv.data(), priv.size(), nullptr);
    EC_KEY_set_private_key(key, bn);

    unsigned int sigLen = ECDSA_size(key);
    signature.resize(sigLen);

    int ok = ECDSA_sign(
        0,
        msgHash.data(),
        msgHash.size(),
        signature.data(),
        &sigLen,
        key
    );

    signature.resize(sigLen);
    BN_free(bn);
    EC_KEY_free(key);
    return ok == 1;
}

bool verifyECDSA(
    const Hash256& msgHash,
    const std::vector<uint8_t>& signature,
    const PublicKey& pub
) {
    EC_KEY* key = EC_KEY_new_by_curve_name(NID_secp256k1);
    const EC_GROUP* group = EC_KEY_get0_group(key);
    EC_POINT* point = EC_POINT_new(group);

    EC_POINT_oct2point(group, point, pub.data(), pub.size(), nullptr);
    EC_KEY_set_public_key(key, point);

    int ok = ECDSA_verify(
        0,
        msgHash.data(),
        msgHash.size(),
        signature.data(),
        signature.size(),
        key
    );

    EC_POINT_free(point);
    EC_KEY_free(key);
    return ok == 1;
}

// ---------------- Utilities ----------------

bool hashLessThan(const Hash256& a, const Hash256& b) {
    return std::memcmp(a.data(), b.data(), 32) < 0;
}

} // namespace Bucks
