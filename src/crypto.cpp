#define OPENSSL_SUPPRESS_DEPRECATED
#include "crypto.h"
#include "consensus.h"
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>

#include <sstream>
#include <iomanip>
#include <cstring>
#include <climits>
#include <algorithm>
#include <memory>
#include <stdexcept>

#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace Bucks {

// -----------------------------------------------------------------------------
// FIX: fail-closed secp256k1 helpers — Bucks v1 security patch (CRY-2, CRY-12)
// Every OpenSSL object is owned by a unique_ptr so each early return frees it,
// and every OpenSSL return value is checked. Any failure means "invalid".
// -----------------------------------------------------------------------------

namespace {

struct BnFree    { void operator()(BIGNUM* p)    const { BN_clear_free(p); } };
struct BnCtxFree { void operator()(BN_CTX* p)    const { BN_CTX_free(p); } };
struct GroupFree { void operator()(EC_GROUP* p)  const { EC_GROUP_free(p); } };
struct PointFree { void operator()(EC_POINT* p)  const { EC_POINT_clear_free(p); } };
struct KeyFree   { void operator()(EC_KEY* p)    const { EC_KEY_free(p); } };
struct SigFree   { void operator()(ECDSA_SIG* p) const { ECDSA_SIG_free(p); } };

using BnPtr    = std::unique_ptr<BIGNUM, BnFree>;
using BnCtxPtr = std::unique_ptr<BN_CTX, BnCtxFree>;
using GroupPtr = std::unique_ptr<EC_GROUP, GroupFree>;
using PointPtr = std::unique_ptr<EC_POINT, PointFree>;
using KeyPtr   = std::unique_ptr<EC_KEY, KeyFree>;
using SigPtr   = std::unique_ptr<ECDSA_SIG, SigFree>;

// secp256k1 group order n, big-endian.
const uint8_t SECP256K1_ORDER[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
    0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41
};

// Compressed public key: prefix byte + 32-byte x coordinate.
constexpr size_t COMPRESSED_PUBKEY_SIZE = 33;

// DER ECDSA signature bounds for a 256-bit curve (BIP66 limits without the
// sighash byte): SEQUENCE{INTEGER r, INTEGER s}, each 1..33 bytes.
constexpr size_t MIN_DER_SIG_SIZE = 8;
constexpr size_t MAX_DER_SIG_SIZE = 72;

// k < n, computed as the borrow out of k - n. Runs over every byte so the
// timing does not depend on the (secret) value.
bool scalarBelowOrder(const uint8_t* k) {
    int borrow = 0;
    for (int i = 31; i >= 0; --i) {
        int diff = static_cast<int>(k[i]) - static_cast<int>(SECP256K1_ORDER[i]) - borrow;
        borrow = diff < 0 ? 1 : 0;
    }
    return borrow == 1;
}

bool scalarIsZero(const uint8_t* k) {
    uint8_t acc = 0;
    for (int i = 0; i < 32; ++i) acc |= k[i];
    return acc == 0;
}

GroupPtr newSecp256k1Group() {
    return GroupPtr(EC_GROUP_new_by_curve_name(NID_secp256k1));
}

// Strict decode of a 33-byte compressed key. Returns null for any other
// length or prefix, x >= p, a point not on the curve, or infinity.
PointPtr decodePublicKey(const EC_GROUP* group, const PublicKey& pub, BN_CTX* ctx) {
    if (pub.size() != COMPRESSED_PUBKEY_SIZE) return nullptr;
    if (pub[0] != 0x02 && pub[0] != 0x03) return nullptr;

    PointPtr pt(EC_POINT_new(group));
    if (!pt) return nullptr;
    if (EC_POINT_oct2point(group, pt.get(), pub.data(), pub.size(), ctx) != 1) return nullptr;
    if (EC_POINT_is_at_infinity(group, pt.get())) return nullptr;
    if (EC_POINT_is_on_curve(group, pt.get(), ctx) != 1) return nullptr;

    // Canonical encoding only: re-encoding must give back the same bytes.
    uint8_t check[COMPRESSED_PUBKEY_SIZE];
    if (EC_POINT_point2oct(group, pt.get(), POINT_CONVERSION_COMPRESSED,
                           check, sizeof(check), ctx) != sizeof(check)) return nullptr;
    if (std::memcmp(check, pub.data(), sizeof(check)) != 0) return nullptr;
    return pt;
}

// Encode a point as a 33-byte compressed key. Fails for infinity.
bool encodePublicKey(const EC_GROUP* group, const EC_POINT* pt, BN_CTX* ctx, PublicKey& out) {
    if (EC_POINT_is_at_infinity(group, pt)) return false;
    PublicKey enc(COMPRESSED_PUBKEY_SIZE);
    if (EC_POINT_point2oct(group, pt, POINT_CONVERSION_COMPRESSED,
                           enc.data(), enc.size(), ctx) != enc.size()) return false;
    out = std::move(enc);
    return true;
}

} // namespace

// ... (existing hashing functions)

// ---------------- BIP-8192 Primitives ----------------

bool pbkdf2_hmac_sha512(
    const char* pass, size_t passLen,
    const char* salt, size_t saltLen,
    uint32_t iterations,
    uint8_t* out, size_t outLen
) {
    // OpenSSL takes int lengths; refuse anything that would be truncated.
    if (passLen > INT_MAX || saltLen > INT_MAX || outLen > INT_MAX) return false;
    if (iterations == 0 || iterations > static_cast<uint32_t>(INT_MAX)) return false;
    return PKCS5_PBKDF2_HMAC(
        pass, static_cast<int>(passLen),
        reinterpret_cast<const unsigned char*>(salt), static_cast<int>(saltLen),
        static_cast<int>(iterations), EVP_sha512(),
        static_cast<int>(outLen), out
    ) == 1;
}

std::vector<uint8_t> hmac_sha512(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& data
) {
    // Returns an empty vector on failure; callers must check the size.
    if (key.size() > INT_MAX) return {};
    // OpenSSL 3 rejects a NULL pointer even with length 0 (the old code then
    // silently returned 64 zero bytes), so never pass NULL for empty input.
    static const uint8_t EMPTY = 0;
    const uint8_t* keyPtr  = key.empty()  ? &EMPTY : key.data();
    const uint8_t* dataPtr = data.empty() ? &EMPTY : data.data();
    unsigned int len = 0;
    std::vector<uint8_t> out(64);
    if (!HMAC(EVP_sha512(), keyPtr, static_cast<int>(key.size()),
              dataPtr, data.size(), out.data(), &len) || len != out.size()) {
        OPENSSL_cleanse(out.data(), out.size());
        return {};
    }
    return out;
}

// ---------------- Hashing ----------------

// The OpenSSL 3 one-shot digests go through EVP and can fail (e.g. a missing
// provider). There is no error channel in the return type, so throw rather
// than hand back an uninitialised hash.
Hash256 sha256(const std::vector<uint8_t>& data) {
    Hash256 out{};
    if (!SHA256(data.data(), data.size(), out.data()))
        throw std::runtime_error("SHA-256 failed");
    return out;
}

Hash256 doubleSha256(const std::vector<uint8_t>& data) {
    Hash256 first = sha256(data);
    std::vector<uint8_t> tmp(first.begin(), first.end());
    return sha256(tmp);
}

Hash160 ripemd160(const std::vector<uint8_t>& data) {
    // RIPEMD-160 is only in the default provider from OpenSSL 3.0.7 on.
    Hash160 out{};
    if (!RIPEMD160(data.data(), data.size(), out.data()))
        throw std::runtime_error("RIPEMD-160 failed");
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

std::vector<uint8_t> parseHex(const std::string& hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < hex.size(); i += 2)
        out.push_back(std::stoi(hex.substr(i, 2), nullptr, 16));
    return out;
}

// ---------------- Base58 ----------------
// Bitcoin Base58 alphabet — excludes visually ambiguous characters 0, O, I, l
static const char BASE58_CHARS[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

std::string base58Encode(const std::vector<uint8_t>& data) {
    // Count leading zero bytes (each becomes a leading '1')
    int leadingZeros = 0;
    for (auto b : data) {
        if (b != 0) break;
        ++leadingZeros;
    }

    // Big-number division: convert bytes to base-58 digits
    // We accumulate digits in reverse order (least significant first)
    std::vector<uint8_t> digits;
    digits.reserve(data.size() * 138 / 100 + 1); // log(256)/log(58) ≈ 1.38

    for (uint8_t b : data) {
        uint32_t carry = b;
        for (auto& d : digits) {
            carry += (uint32_t)d << 8;
            d = (uint8_t)(carry % 58);
            carry /= 58;
        }
        while (carry) {
            digits.push_back((uint8_t)(carry % 58));
            carry /= 58;
        }
    }

    // Build result: leading '1's, then digits in reverse (most significant first)
    std::string result(leadingZeros, '1');
    for (auto it = digits.rbegin(); it != digits.rend(); ++it)
        result += BASE58_CHARS[*it];

    return result;
}

std::vector<uint8_t> base58Decode(const std::string& str) {
    // Count leading '1' characters (each decodes to a leading 0x00 byte)
    int leadingOnes = 0;
    for (char c : str) {
        if (c != '1') break;
        ++leadingOnes;
    }

    // Convert each Base58 character to its index value, accumulate as big-endian bytes
    std::vector<uint8_t> bytes;
    bytes.reserve(str.size() * 733 / 1000 + 1); // log(58)/log(256) ≈ 0.733

    for (char c : str) {
        const char* p = std::strchr(BASE58_CHARS, c);
        if (!p) return {}; // invalid character — return empty

        uint32_t carry = (uint32_t)(p - BASE58_CHARS);
        for (auto& b : bytes) {
            carry += (uint32_t)b * 58;
            b = (uint8_t)(carry & 0xff);
            carry >>= 8;
        }
        while (carry) {
            bytes.push_back((uint8_t)(carry & 0xff));
            carry >>= 8;
        }
    }

    // Reverse to big-endian and prepend leading zero bytes
    std::reverse(bytes.begin(), bytes.end());
    std::vector<uint8_t> result(leadingOnes, 0x00);
    result.insert(result.end(), bytes.begin(), bytes.end());

    return result;
}

// ---------------- Base58Check ----------------
// Bucks mainnet P2PKH address version byte is 0x00 (same as Bitcoin),
// so addresses begin with '1'.  Version byte 0x00 + 20-byte hash160 payload
// encodes deterministically as a 34-character Base58Check string.

std::string base58CheckEncode(uint8_t version, const std::vector<uint8_t>& payload) {
    // 1. Prepend version byte
    std::vector<uint8_t> vPayload;
    vPayload.reserve(1 + payload.size() + 4);
    vPayload.push_back(version);
    vPayload.insert(vPayload.end(), payload.begin(), payload.end());

    // 2. Compute 4-byte checksum: SHA256(SHA256(version || payload))[0..3]
    Hash256 checkHash = doubleSha256(vPayload);
    vPayload.push_back(checkHash[0]);
    vPayload.push_back(checkHash[1]);
    vPayload.push_back(checkHash[2]);
    vPayload.push_back(checkHash[3]);

    // 3. Base58-encode the whole thing (version + payload + checksum)
    return base58Encode(vPayload);
}

std::vector<uint8_t> base58CheckDecode(const std::string& encoded) {
    // 1. Base58-decode
    std::vector<uint8_t> decoded = base58Decode(encoded);

    // 2. Must be at least 5 bytes (1 version + at least 0 payload + 4 checksum)
    if (decoded.size() < 5) return {};

    // 3. Split: everything except the last 4 bytes is (version || payload)
    std::vector<uint8_t> vPayload(decoded.begin(), decoded.end() - 4);
    std::array<uint8_t, 4> checksum;
    checksum[0] = decoded[decoded.size() - 4];
    checksum[1] = decoded[decoded.size() - 3];
    checksum[2] = decoded[decoded.size() - 2];
    checksum[3] = decoded[decoded.size() - 1];

    // 4. Verify checksum
    Hash256 expected = doubleSha256(vPayload);
    if (checksum[0] != expected[0] || checksum[1] != expected[1] ||
        checksum[2] != expected[2] || checksum[3] != expected[3]) {
        return {}; // checksum mismatch — corrupted or wrong network
    }

    // 5. Return (version || payload); caller strips version byte [0]
    return vPayload;
}

// ---------------- Keys ----------------

bool isValidPrivateKey(const PrivateKey& priv) {
    return !scalarIsZero(priv.data()) && scalarBelowOrder(priv.data());
}

bool isValidPublicKey(const PublicKey& pub) {
    GroupPtr group = newSecp256k1Group();
    BnCtxPtr ctx(BN_CTX_new());
    if (!group || !ctx) return false;
    return decodePublicKey(group.get(), pub, ctx.get()) != nullptr;
}

bool generatePrivateKey(PrivateKey& out) {
    // Rejection sampling: a random 256-bit value is >= n with probability
    // ~2^-128, so this loop almost never runs twice.
    for (int attempt = 0; attempt < 128; ++attempt) {
        if (RAND_bytes(out.data(), static_cast<int>(out.size())) != 1) break;
        if (isValidPrivateKey(out)) return true;
    }
    OPENSSL_cleanse(out.data(), out.size());
    return false;
}

bool derivePublicKey(const PrivateKey& priv, PublicKey& pub) {
    pub.clear();
    // 0 and n would both give the point at infinity
    if (!isValidPrivateKey(priv)) return false;

    GroupPtr group = newSecp256k1Group();
    BnCtxPtr ctx(BN_CTX_new());
    if (!group || !ctx) return false;

    BnPtr d(BN_bin2bn(priv.data(), static_cast<int>(priv.size()), nullptr));
    PointPtr point(EC_POINT_new(group.get()));
    if (!d || !point) return false;

    if (EC_POINT_mul(group.get(), point.get(), d.get(), nullptr, nullptr, ctx.get()) != 1)
        return false;

    return encodePublicKey(group.get(), point.get(), ctx.get(), pub);
}

// ---------------- EC Arithmetic (for HD) ----------------

bool addScalar(const PrivateKey& p1, const PrivateKey& p2, PrivateKey& out) {
    // p1 must be a valid key; the tweak p2 must be < n (BIP-32: IL >= n is invalid).
    if (!isValidPrivateKey(p1) || !scalarBelowOrder(p2.data())) return false;

    GroupPtr group = newSecp256k1Group();
    BnCtxPtr ctx(BN_CTX_new());
    if (!group || !ctx) return false;
    const BIGNUM* n = EC_GROUP_get0_order(group.get());
    if (!n) return false;

    BnPtr b1(BN_bin2bn(p1.data(), 32, nullptr));
    BnPtr b2(BN_bin2bn(p2.data(), 32, nullptr));
    BnPtr res(BN_new());
    if (!b1 || !b2 || !res) return false;

    if (BN_mod_add(res.get(), b1.get(), b2.get(), n, ctx.get()) != 1) return false;
    if (BN_is_zero(res.get())) return false;

    PrivateKey sum;
    if (BN_bn2binpad(res.get(), sum.data(), static_cast<int>(sum.size())) != 32) {
        OPENSSL_cleanse(sum.data(), sum.size());
        return false;
    }
    out = sum;
    OPENSSL_cleanse(sum.data(), sum.size());
    return true;
}

bool addPoint(const PublicKey& p1, const PrivateKey& p2, PublicKey& out) {
    // The tweak must be < n (BIP-32); p1 must be a valid compressed key.
    if (!scalarBelowOrder(p2.data())) return false;

    GroupPtr group = newSecp256k1Group();
    BnCtxPtr ctx(BN_CTX_new());
    if (!group || !ctx) return false;

    PointPtr pt1 = decodePublicKey(group.get(), p1, ctx.get());
    if (!pt1) return false;

    // pt2 = p2*G
    BnPtr b2(BN_bin2bn(p2.data(), 32, nullptr));
    PointPtr pt2(EC_POINT_new(group.get()));
    PointPtr res(EC_POINT_new(group.get()));
    if (!b2 || !pt2 || !res) return false;
    if (EC_POINT_mul(group.get(), pt2.get(), b2.get(), nullptr, nullptr, ctx.get()) != 1)
        return false;

    // res = p1 + p2*G; infinity is rejected by encodePublicKey
    if (EC_POINT_add(group.get(), res.get(), pt1.get(), pt2.get(), ctx.get()) != 1)
        return false;

    return encodePublicKey(group.get(), res.get(), ctx.get(), out);
}

// ---------------- ECDH (for P2P Encryption) ----------------

bool computeSharedSecret(const PrivateKey& priv, const PublicKey& otherPub, Hash256& secret) {
    if (!isValidPrivateKey(priv)) return false;

    GroupPtr group = newSecp256k1Group();
    BnCtxPtr ctx(BN_CTX_new());
    if (!group || !ctx) return false;

    PointPtr pubPt = decodePublicKey(group.get(), otherPub, ctx.get());
    if (!pubPt) return false;

    BnPtr privBN(BN_bin2bn(priv.data(), 32, nullptr));
    PointPtr resPt(EC_POINT_new(group.get()));
    BnPtr x(BN_new());
    if (!privBN || !resPt || !x) return false;

    // Compute resPt = priv * otherPub
    if (EC_POINT_mul(group.get(), resPt.get(), nullptr, pubPt.get(), privBN.get(), ctx.get()) != 1)
        return false;
    if (EC_POINT_is_at_infinity(group.get(), resPt.get())) return false;

    // Shared secret is the SHA256 of the x-coordinate
    if (EC_POINT_get_affine_coordinates(group.get(), resPt.get(), x.get(), nullptr, ctx.get()) != 1)
        return false;

    std::vector<uint8_t> xBytes(32);
    if (BN_bn2binpad(x.get(), xBytes.data(), 32) != 32) {
        OPENSSL_cleanse(xBytes.data(), xBytes.size());
        return false;
    }

    secret = sha256(xBytes);
    OPENSSL_cleanse(xBytes.data(), xBytes.size());
    return true;
}

// ---------------- Signatures ----------------

bool signECDSA(
    const Hash256& msgHash,
    const PrivateKey& priv,
    std::vector<uint8_t>& signature
) {
    signature.clear();

    // Also rejects priv == 0 and priv >= n.
    PublicKey pub;
    if (!derivePublicKey(priv, pub)) return false;

    KeyPtr key(EC_KEY_new_by_curve_name(NID_secp256k1));
    if (!key) return false;
    const EC_GROUP* group = EC_KEY_get0_group(key.get());
    const BIGNUM* n = group ? EC_GROUP_get0_order(group) : nullptr;
    if (!n) return false;

    BnPtr d(BN_bin2bn(priv.data(), static_cast<int>(priv.size()), nullptr));
    if (!d || EC_KEY_set_private_key(key.get(), d.get()) != 1) return false;

    SigPtr sig(ECDSA_do_sign(msgHash.data(), static_cast<int>(msgHash.size()), key.get()));
    if (!sig) return false;

    // FIX: low-S normalisation — Bucks v1 security patch (CRY-5)
    // (r, s) and (r, n - s) are both valid ECDSA signatures; consensus only
    // accepts s <= n/2, so always emit that form.
    const BIGNUM* r = nullptr;
    const BIGNUM* s = nullptr;
    ECDSA_SIG_get0(sig.get(), &r, &s);
    BnPtr halfN(BN_new());
    if (!r || !s || !halfN || BN_rshift1(halfN.get(), n) != 1) return false;
    if (BN_cmp(s, halfN.get()) > 0) {
        BnPtr newR(BN_dup(r));
        BnPtr newS(BN_new());
        if (!newR || !newS || BN_sub(newS.get(), n, s) != 1) return false;
        if (ECDSA_SIG_set0(sig.get(), newR.get(), newS.get()) != 1) return false;
        newR.release();   // now owned by sig
        newS.release();
    }

    int derLen = i2d_ECDSA_SIG(sig.get(), nullptr);
    if (derLen <= 0) return false;
    std::vector<uint8_t> der(static_cast<size_t>(derLen));
    unsigned char* p = der.data();
    if (i2d_ECDSA_SIG(sig.get(), &p) != derLen) return false;

    // Never hand out a signature our own consensus check would reject.
    if (!verifyECDSA(msgHash, der, pub)) return false;

    signature = std::move(der);
    return true;
}

// FIX: verifyECDSA fails closed — Bucks v1 security patch (CRY-2, GLD-1, CON-5, CRY-5)
// The old code ignored EC_POINT_oct2point's result, so an unparseable key was
// left at infinity and OpenSSL accepted a keyless forgery (r = x(kG),
// s = e/k). Now anything other than a fully valid key and a canonical,
// low-S signature that verifies returns false.
bool verifyECDSA(
    const Hash256& msgHash,
    const std::vector<uint8_t>& signature,
    const PublicKey& pub
) {
    if (signature.size() < MIN_DER_SIG_SIZE || signature.size() > MAX_DER_SIG_SIZE)
        return false;

    KeyPtr key(EC_KEY_new_by_curve_name(NID_secp256k1));
    BnCtxPtr ctx(BN_CTX_new());
    if (!key || !ctx) return false;
    const EC_GROUP* group = EC_KEY_get0_group(key.get());
    const BIGNUM* n = group ? EC_GROUP_get0_order(group) : nullptr;
    if (!n) return false;

    PointPtr point = decodePublicKey(group, pub, ctx.get());
    if (!point || EC_KEY_set_public_key(key.get(), point.get()) != 1) return false;

    // Parse DER; the whole buffer must be consumed.
    const unsigned char* p = signature.data();
    SigPtr sig(d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(signature.size())));
    if (!sig || p != signature.data() + signature.size()) return false;

    // Strict DER (BIP66-style): only the canonical encoding is accepted, so
    // consensus does not depend on how leniently OpenSSL parses.
    if (Consensus::REQUIRE_STRICT_DER) {
        unsigned char* der = nullptr;
        int derLen = i2d_ECDSA_SIG(sig.get(), &der);
        bool canonical = derLen > 0 &&
                         static_cast<size_t>(derLen) == signature.size() &&
                         std::memcmp(der, signature.data(), signature.size()) == 0;
        OPENSSL_free(der);
        if (!canonical) return false;
    }

    // r and s must both be in [1, n-1].
    const BIGNUM* r = nullptr;
    const BIGNUM* s = nullptr;
    ECDSA_SIG_get0(sig.get(), &r, &s);
    if (!r || !s) return false;
    if (BN_is_zero(r) || BN_is_negative(r) || BN_cmp(r, n) >= 0) return false;
    if (BN_is_zero(s) || BN_is_negative(s) || BN_cmp(s, n) >= 0) return false;

    // Low-S: reject the malleated (r, n - s) form.
    if (Consensus::REQUIRE_LOW_S) {
        BnPtr halfN(BN_new());
        if (!halfN || BN_rshift1(halfN.get(), n) != 1) return false;
        if (BN_cmp(s, halfN.get()) > 0) return false;
    }

    // 1 = valid, 0 = invalid, -1 = error
    return ECDSA_do_verify(msgHash.data(), static_cast<int>(msgHash.size()),
                           sig.get(), key.get()) == 1;
}

// ---------------- Utilities ----------------

bool hashLessThan(const Hash256& a, const Hash256& b) {
    return std::memcmp(a.data(), b.data(), 32) < 0;
}

} // namespace Bucks
