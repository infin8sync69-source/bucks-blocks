// Regression tests for the fail-closed secp256k1 code in src/crypto.cpp
// (audit findings CRY-2, GLD-1, CON-5, CRY-5, CRY-12).
//
// The curve math used to build test vectors (forgeries, high-S variants,
// uncompressed encodings) is done here directly with OpenSSL BIGNUM/EC_POINT,
// independently of the Bucks code under test.

#include <gtest/gtest.h>
#include "crypto.h"
#include "consensus.h"
#include "transaction.h"
#include "sighash.h"

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

#include <memory>
#include <string>
#include <vector>

using Bucks::Hash256;
using Bucks::PrivateKey;
using Bucks::PublicKey;
using Bytes = std::vector<uint8_t>;

namespace {

struct BnFree    { void operator()(BIGNUM* p)   const { BN_free(p); } };
struct CtxFree   { void operator()(BN_CTX* p)   const { BN_CTX_free(p); } };
struct GroupFree { void operator()(EC_GROUP* p) const { EC_GROUP_free(p); } };
struct PointFree { void operator()(EC_POINT* p) const { EC_POINT_free(p); } };
using Bn    = std::unique_ptr<BIGNUM, BnFree>;
using Ctx   = std::unique_ptr<BN_CTX, CtxFree>;
using Group = std::unique_ptr<EC_GROUP, GroupFree>;
using Point = std::unique_ptr<EC_POINT, PointFree>;

struct Curve {
    Group group{EC_GROUP_new_by_curve_name(NID_secp256k1)};
    Ctx ctx{BN_CTX_new()};
    const BIGNUM* n() const { return EC_GROUP_get0_order(group.get()); }
};

Bn bnFrom(const Bytes& b) {
    return Bn(BN_bin2bn(b.data(), static_cast<int>(b.size()), nullptr));
}

Bytes bytes32(const BIGNUM* v) {
    Bytes out(32);
    EXPECT_EQ(BN_bn2binpad(v, out.data(), 32), 32);
    return out;
}

PrivateKey toPriv(const Bytes& b) {
    PrivateKey k{};
    std::copy(b.begin(), b.end(), k.begin());
    return k;
}

Bytes orderBytes() {
    Curve c;
    return bytes32(c.n());
}

// v + delta (delta may be negative), as 32 big-endian bytes.
Bytes addSmall(const Bytes& v, long delta) {
    Bn b = bnFrom(v);
    Bn d(BN_new());
    BN_set_word(d.get(), static_cast<BN_ULONG>(delta < 0 ? -delta : delta));
    if (delta < 0) BN_sub(b.get(), b.get(), d.get());
    else           BN_add(b.get(), b.get(), d.get());
    return bytes32(b.get());
}

// n - v
Bytes negateScalar(const Bytes& v) {
    Curve c;
    Bn r(BN_new());
    BN_sub(r.get(), c.n(), bnFrom(v).get());
    return bytes32(r.get());
}

Bytes halfOrderBytes() {
    Curve c;
    Bn h(BN_new());
    BN_rshift1(h.get(), c.n());
    return bytes32(h.get());
}

// Numeric a > b for equal-length big-endian values.
bool greater(const Bytes& a, const Bytes& b) {
    return std::lexicographical_compare(b.begin(), b.end(), a.begin(), a.end());
}

// Canonical DER INTEGER for a non-negative big-endian magnitude.
Bytes derInteger(Bytes v) {
    while (v.size() > 1 && v[0] == 0) v.erase(v.begin());
    if (v[0] & 0x80) v.insert(v.begin(), 0x00);
    Bytes out{0x02, static_cast<uint8_t>(v.size())};
    out.insert(out.end(), v.begin(), v.end());
    return out;
}

Bytes derSignature(const Bytes& r, const Bytes& s) {
    Bytes body = derInteger(r);
    Bytes si = derInteger(s);
    body.insert(body.end(), si.begin(), si.end());
    Bytes out{0x30, static_cast<uint8_t>(body.size())};
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

// Split a DER signature into 32-byte r and s.
bool splitSignature(const Bytes& sig, Bytes& r, Bytes& s) {
    if (sig.size() < 8 || sig[0] != 0x30 || sig[1] != sig.size() - 2) return false;
    size_t pos = 2;
    auto readInt = [&](Bytes& out) {
        if (pos + 2 > sig.size() || sig[pos] != 0x02) return false;
        size_t len = sig[pos + 1];
        pos += 2;
        if (len == 0 || pos + len > sig.size()) return false;
        Bytes v(sig.begin() + pos, sig.begin() + pos + len);
        pos += len;
        while (v.size() > 32 && v[0] == 0) v.erase(v.begin());
        if (v.size() > 32) return false;
        out.assign(32 - v.size(), 0);
        out.insert(out.end(), v.begin(), v.end());
        return true;
    };
    return readInt(r) && readInt(s) && pos == sig.size();
}

// Uncompressed (0x04) or hybrid (0x06/0x07) encoding of a compressed key.
Bytes reencode(const PublicKey& compressed, bool hybrid) {
    Curve c;
    Point p(EC_POINT_new(c.group.get()));
    EXPECT_EQ(EC_POINT_oct2point(c.group.get(), p.get(), compressed.data(),
                                 compressed.size(), c.ctx.get()), 1);
    Bytes out(65);
    EXPECT_EQ(EC_POINT_point2oct(c.group.get(), p.get(), POINT_CONVERSION_UNCOMPRESSED,
                                 out.data(), out.size(), c.ctx.get()), 65u);
    if (hybrid) out[0] = static_cast<uint8_t>(0x06 | (out[64] & 1));
    return out;
}

// The audit's keyless forgery (CRY-2 / GLD-1). When the public key failed to
// parse, the old verifyECDSA left Q at infinity, so u1*G + u2*Q = u1*G.
// With r = x(kG) mod n and s = e/k mod n, u1 = k and the check passes for
// any message without a private key. s is folded to low-S (x(-kG) = x(kG)),
// so the only thing left to reject it is the public key check.
Bytes forgeKeylessSignature(const Hash256& msg, unsigned long k) {
    Curve c;
    Bn kb(BN_new());
    BN_set_word(kb.get(), k);
    Point R(EC_POINT_new(c.group.get()));
    EXPECT_EQ(EC_POINT_mul(c.group.get(), R.get(), kb.get(), nullptr, nullptr, c.ctx.get()), 1);
    Bn x(BN_new());
    EXPECT_EQ(EC_POINT_get_affine_coordinates(c.group.get(), R.get(), x.get(), nullptr, c.ctx.get()), 1);
    Bn r(BN_new());
    BN_nnmod(r.get(), x.get(), c.n(), c.ctx.get());

    Bn e = bnFrom(Bytes(msg.begin(), msg.end()));
    Bn kinv(BN_mod_inverse(nullptr, kb.get(), c.n(), c.ctx.get()));
    Bn s(BN_new());
    BN_mod_mul(s.get(), e.get(), kinv.get(), c.n(), c.ctx.get());

    Bytes sB = bytes32(s.get());
    if (greater(sB, halfOrderBytes())) sB = negateScalar(sB);
    return derSignature(bytes32(r.get()), sB);
}

// Independent proof that a forged signature passes ECDSA against Q = infinity:
// x(u1*G) mod n == r, with u1 = e/s.
bool acceptedByInfinityKey(const Hash256& msg, const Bytes& sig) {
    Bytes rB, sB;
    if (!splitSignature(sig, rB, sB)) return false;
    Curve c;
    Bn e = bnFrom(Bytes(msg.begin(), msg.end()));
    Bn sinv(BN_mod_inverse(nullptr, bnFrom(sB).get(), c.n(), c.ctx.get()));
    Bn u1(BN_new());
    BN_mod_mul(u1.get(), e.get(), sinv.get(), c.n(), c.ctx.get());
    Point P(EC_POINT_new(c.group.get()));
    EC_POINT_mul(c.group.get(), P.get(), u1.get(), nullptr, nullptr, c.ctx.get());
    Bn x(BN_new());
    EC_POINT_get_affine_coordinates(c.group.get(), P.get(), x.get(), nullptr, c.ctx.get());
    BN_nnmod(x.get(), x.get(), c.n(), c.ctx.get());
    return bytes32(x.get()) == rB;
}

Hash256 hashOf(const std::string& s) {
    return Bucks::sha256(Bytes(s.begin(), s.end()));
}

// Deterministic, valid test key #i.
PrivateKey testKey(uint32_t i) {
    Hash256 h = hashOf("bucks-ecdsa-test-key-" + std::to_string(i));
    PrivateKey k = toPriv(Bytes(h.begin(), h.end()));
    EXPECT_TRUE(Bucks::isValidPrivateKey(k));
    return k;
}

PublicKey pubOf(const PrivateKey& k) {
    PublicKey pub;
    EXPECT_TRUE(Bucks::derivePublicKey(k, pub));
    return pub;
}

// The vault message from validateVaultMultisig: sha256(marker_LE4 || amount_LE8).
Hash256 vaultMessage(uint32_t marker, uint64_t amount) {
    Bytes msg(12);
    for (int i = 0; i < 4; ++i) msg[i]     = (marker >> (i * 8)) & 0xFF;
    for (int i = 0; i < 8; ++i) msg[4 + i] = (amount >> (i * 8)) & 0xFF;
    return Bucks::sha256(msg);
}

const Bytes G_X = Bucks::parseHex(
    "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");

// 0x02 || x, padded to 33 bytes.
PublicKey compressedWithX(const Bytes& x) {
    PublicKey p{0x02};
    p.insert(p.end(), x.begin(), x.end());
    return p;
}

} // namespace

// ---------------------------------------------------------------------------
// (a) Keyless forgery against invalid / placeholder public keys
// ---------------------------------------------------------------------------

TEST(EcdsaForgery, KeylessForgeryAgainstInvalidPubkeysIsRejected) {
    const Hash256 msg = hashOf("pay the attacker");

    std::vector<std::pair<std::string, PublicKey>> badKeys = {
        {"empty", {}},
        {"one zero byte", {0x00}},
        {"33 zero bytes (old derivePublicKey(0) output)", PublicKey(33, 0x00)},
        {"33 bytes, x=5 not on curve", compressedWithX(Bucks::parseHex(
            "0000000000000000000000000000000000000000000000000000000000000005"))},
        {"33 bytes, x=p", compressedWithX(Bucks::parseHex(
            "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f"))},
        {"33 bytes of garbage", PublicKey(33, 0xff)},
    };

    for (unsigned long k : {1ul, 2ul, 7ul, 0x1234567ul}) {
        Bytes sig = forgeKeylessSignature(msg, k);
        // The forgery is a genuine ECDSA solution for Q = infinity ...
        ASSERT_TRUE(acceptedByInfinityKey(msg, sig)) << "k=" << k;
        // ... and canonical low-S DER, so only the key check can stop it.
        for (const auto& [name, key] : badKeys) {
            EXPECT_FALSE(Bucks::isValidPublicKey(key)) << name;
            EXPECT_FALSE(Bucks::verifyECDSA(msg, sig, key)) << name << " k=" << k;
        }
    }
}

TEST(EcdsaForgery, VaultPlaceholderKeysCannotAuthorizeGoldEvents) {
    // GLD-1: 3 copies of one forged signature under key indices 0-2 used to
    // pass the 3-of-5 vault check for any GOLD/REDM amount.
    for (uint32_t marker : {Bucks::Consensus::GOLD_MARKER, Bucks::Consensus::REDEEM_MARKER}) {
        const Hash256 msg = vaultMessage(marker, 1000000000000000ull);
        for (unsigned long k : {1ul, 3ul, 0xdeadbeeful}) {
            Bytes sig = forgeKeylessSignature(msg, k);
            ASSERT_TRUE(acceptedByInfinityKey(msg, sig));
            for (int i = 0; i < Bucks::Consensus::VAULT_MULTISIG_TOTAL; ++i) {
                PublicKey key = Bucks::parseHex(Bucks::Consensus::VAULT_AUTHORITY_PUBKEYS[i]);
                EXPECT_FALSE(Bucks::isValidPublicKey(key)) << "vault key " << i;
                EXPECT_FALSE(Bucks::verifyECDSA(msg, sig, key)) << "vault key " << i << " k=" << k;
            }
        }
    }
}

TEST(EcdsaForgery, ForgeryAgainstValidKeyIsRejected) {
    // Control: the same construction does not work against a real key.
    const Hash256 msg = hashOf("control");
    PublicKey pub = pubOf(testKey(1));
    Bytes sig = forgeKeylessSignature(msg, 5);
    EXPECT_FALSE(Bucks::verifyECDSA(msg, sig, pub));
}

// ---------------------------------------------------------------------------
// (b) Public key parsing
// ---------------------------------------------------------------------------

TEST(EcdsaPubKey, StrictCompressedKeysOnly) {
    PrivateKey priv = testKey(2);
    PublicKey pub = pubOf(priv);
    ASSERT_EQ(pub.size(), 33u);
    EXPECT_TRUE(Bucks::isValidPublicKey(pub));

    Bytes uncompressed = reencode(pub, false);
    Bytes hybrid = reencode(pub, true);
    ASSERT_EQ(uncompressed[0], 0x04);
    ASSERT_TRUE(hybrid[0] == 0x06 || hybrid[0] == 0x07);

    PublicKey trailing = pub;
    trailing.push_back(0x00);
    PublicKey flippedParity = pub;
    flippedParity[0] ^= 0x01;              // the other point with this x, still valid
    PublicKey prefix04 = pub;
    prefix04[0] = 0x04;                    // 33 bytes with an uncompressed prefix
    PublicKey prefix00 = pub;
    prefix00[0] = 0x00;
    PublicKey prefix05 = pub;
    prefix05[0] = 0x05;

    std::vector<std::pair<std::string, PublicKey>> bad = {
        {"empty", {}},
        {"1 byte", {0x02}},
        {"32 bytes", PublicKey(pub.begin(), pub.end() - 1)},
        {"34 bytes", trailing},
        {"0x04 uncompressed (65 bytes)", uncompressed},
        {"0x06/0x07 hybrid (65 bytes)", hybrid},
        {"0x04 prefix, 33 bytes", prefix04},
        {"0x00 prefix", prefix00},
        {"0x05 prefix", prefix05},
        {"off-curve x=0", compressedWithX(Bytes(32, 0x00))},
        {"off-curve x=5", compressedWithX(Bucks::parseHex(
            "0000000000000000000000000000000000000000000000000000000000000005"))},
        {"x = p", compressedWithX(Bucks::parseHex(
            "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f"))},
        {"x = 2^256-1", compressedWithX(Bytes(32, 0xff))},
    };
    for (const auto& [name, key] : bad)
        EXPECT_FALSE(Bucks::isValidPublicKey(key)) << name;

    EXPECT_TRUE(Bucks::isValidPublicKey(flippedParity));
    EXPECT_TRUE(Bucks::isValidPublicKey(compressedWithX(G_X)));

    // A valid signature must not verify under another encoding of the same key.
    const Hash256 msg = hashOf("encoding");
    Bytes sig;
    ASSERT_TRUE(Bucks::signECDSA(msg, priv, sig));
    EXPECT_TRUE(Bucks::verifyECDSA(msg, sig, pub));
    EXPECT_FALSE(Bucks::verifyECDSA(msg, sig, uncompressed));
    EXPECT_FALSE(Bucks::verifyECDSA(msg, sig, hybrid));
    EXPECT_FALSE(Bucks::verifyECDSA(msg, sig, trailing));
    EXPECT_FALSE(Bucks::verifyECDSA(msg, sig, flippedParity));
}

// ---------------------------------------------------------------------------
// (c) / (d) Low-S
// ---------------------------------------------------------------------------

TEST(EcdsaLowS, HighSVariantIsRejected) {
    ASSERT_TRUE(Bucks::Consensus::REQUIRE_LOW_S);
    for (uint32_t i = 0; i < 16; ++i) {
        PrivateKey priv = testKey(100 + i);
        PublicKey pub = pubOf(priv);
        const Hash256 msg = hashOf("high-s " + std::to_string(i));

        Bytes sig;
        ASSERT_TRUE(Bucks::signECDSA(msg, priv, sig));
        ASSERT_TRUE(Bucks::verifyECDSA(msg, sig, pub));

        Bytes r, s;
        ASSERT_TRUE(splitSignature(sig, r, s));
        Bytes highS = derSignature(r, negateScalar(s));   // (r, n - s): same math, other txid
        EXPECT_FALSE(Bucks::verifyECDSA(msg, highS, pub)) << i;
    }
}

TEST(EcdsaLowS, SignAlwaysProducesLowS) {
    const Bytes half = halfOrderBytes();
    for (uint32_t i = 0; i < 300; ++i) {
        PrivateKey priv = testKey(1000 + i);
        PublicKey pub = pubOf(priv);
        const Hash256 msg = hashOf("low-s " + std::to_string(i));

        Bytes sig;
        ASSERT_TRUE(Bucks::signECDSA(msg, priv, sig)) << i;
        Bytes r, s;
        ASSERT_TRUE(splitSignature(sig, r, s)) << i;
        EXPECT_FALSE(greater(s, half)) << "high-S signature at iteration " << i;
        EXPECT_EQ(sig, derSignature(r, s)) << "non-canonical DER at iteration " << i;
        EXPECT_TRUE(Bucks::verifyECDSA(msg, sig, pub)) << i;
    }
}

// ---------------------------------------------------------------------------
// (e) Strict DER
// ---------------------------------------------------------------------------

TEST(EcdsaDer, NonCanonicalEncodingsAreRejected) {
    ASSERT_TRUE(Bucks::Consensus::REQUIRE_STRICT_DER);
    PrivateKey priv = testKey(3);
    PublicKey pub = pubOf(priv);
    const Hash256 msg = hashOf("strict der");
    Bytes sig;
    ASSERT_TRUE(Bucks::signECDSA(msg, priv, sig));
    ASSERT_TRUE(Bucks::verifyECDSA(msg, sig, pub));
    Bytes r, s;
    ASSERT_TRUE(splitSignature(sig, r, s));

    // Trailing byte after the SEQUENCE (with and without fixing the length).
    Bytes trailing = sig;
    trailing.push_back(0x01);
    EXPECT_FALSE(Bucks::verifyECDSA(msg, trailing, pub));
    Bytes trailingInside = sig;
    trailingInside.push_back(0x00);
    trailingInside[1] += 1;
    EXPECT_FALSE(Bucks::verifyECDSA(msg, trailingInside, pub));

    // Integers padded with an unnecessary leading 0x00.
    auto padded = [](const Bytes& v) {
        Bytes body = derInteger(v);
        body.insert(body.begin() + 2, 0x00);
        body[1] += 1;
        return body;
    };
    auto seq = [](const Bytes& a, const Bytes& b) {
        Bytes body = a;
        body.insert(body.end(), b.begin(), b.end());
        Bytes out{0x30, static_cast<uint8_t>(body.size())};
        out.insert(out.end(), body.begin(), body.end());
        return out;
    };
    EXPECT_FALSE(Bucks::verifyECDSA(msg, seq(padded(r), derInteger(s)), pub));
    EXPECT_FALSE(Bucks::verifyECDSA(msg, seq(derInteger(r), padded(s)), pub));

    // Long-form length for the SEQUENCE (0x81 len) instead of the short form.
    Bytes longForm{0x30, 0x81, sig[1]};
    longForm.insert(longForm.end(), sig.begin() + 2, sig.end());
    EXPECT_FALSE(Bucks::verifyECDSA(msg, longForm, pub));

    // Wrong outer length.
    Bytes badLen = sig;
    badLen[1] -= 1;
    EXPECT_FALSE(Bucks::verifyECDSA(msg, badLen, pub));

    // Negative r (drop the 0x00 that keeps the high bit positive), when possible.
    if (r[0] & 0x80) {
        Bytes negR{0x02, 0x20};
        negR.insert(negR.end(), r.begin(), r.end());
        EXPECT_FALSE(Bucks::verifyECDSA(msg, seq(negR, derInteger(s)), pub));
    }

    // Out-of-range scalars, canonically encoded.
    const Bytes zero(32, 0x00);
    const Bytes n = orderBytes();
    EXPECT_FALSE(Bucks::verifyECDSA(msg, derSignature(zero, s), pub));
    EXPECT_FALSE(Bucks::verifyECDSA(msg, derSignature(r, zero), pub));
    EXPECT_FALSE(Bucks::verifyECDSA(msg, derSignature(n, s), pub));
    EXPECT_FALSE(Bucks::verifyECDSA(msg, derSignature(r, n), pub));

    // Truncated, empty and oversized inputs.
    EXPECT_FALSE(Bucks::verifyECDSA(msg, {}, pub));
    EXPECT_FALSE(Bucks::verifyECDSA(msg, Bytes(sig.begin(), sig.end() - 1), pub));
    EXPECT_FALSE(Bucks::verifyECDSA(msg, Bytes(80, 0x30), pub));
}

// ---------------------------------------------------------------------------
// (f) Round trip
// ---------------------------------------------------------------------------

TEST(EcdsaRoundTrip, SignVerify) {
    PrivateKey priv = testKey(4);
    PublicKey pub = pubOf(priv);
    PublicKey otherPub = pubOf(testKey(5));
    const Hash256 msg = hashOf("round trip");

    Bytes sig;
    ASSERT_TRUE(Bucks::signECDSA(msg, priv, sig));
    EXPECT_GE(sig.size(), 8u);
    EXPECT_LE(sig.size(), 72u);
    EXPECT_TRUE(Bucks::verifyECDSA(msg, sig, pub));

    EXPECT_FALSE(Bucks::verifyECDSA(hashOf("other message"), sig, pub));
    EXPECT_FALSE(Bucks::verifyECDSA(msg, sig, otherPub));

    Bytes flipped = sig;
    flipped[flipped.size() - 1] ^= 0x01;   // last byte of s
    EXPECT_FALSE(Bucks::verifyECDSA(msg, flipped, pub));
}

TEST(EcdsaRoundTrip, SignRejectsInvalidPrivateKeys) {
    const Hash256 msg = hashOf("bad signer");
    for (const Bytes& k : {Bytes(32, 0x00), orderBytes(), addSmall(orderBytes(), 1), Bytes(32, 0xff)}) {
        Bytes sig{0xaa};
        EXPECT_FALSE(Bucks::signECDSA(msg, toPriv(k), sig));
        EXPECT_TRUE(sig.empty());
    }
}

// ---------------------------------------------------------------------------
// (g) Private key range
// ---------------------------------------------------------------------------

TEST(EcdsaKeys, PrivateKeyRange) {
    const Bytes n = orderBytes();
    // Pins the hard-coded order in crypto.cpp against OpenSSL's.
    EXPECT_FALSE(Bucks::isValidPrivateKey(toPriv(Bytes(32, 0x00))));
    EXPECT_TRUE(Bucks::isValidPrivateKey(toPriv(addSmall(Bytes(32, 0x00), 1))));
    EXPECT_TRUE(Bucks::isValidPrivateKey(toPriv(addSmall(n, -1))));
    EXPECT_FALSE(Bucks::isValidPrivateKey(toPriv(n)));
    EXPECT_FALSE(Bucks::isValidPrivateKey(toPriv(addSmall(n, 1))));
    EXPECT_FALSE(Bucks::isValidPrivateKey(toPriv(Bytes(32, 0xff))));
}

TEST(EcdsaKeys, DerivePublicKeyRejectsZeroAndOrder) {
    const Bytes n = orderBytes();
    for (const Bytes& k : {Bytes(32, 0x00), n, addSmall(n, 1), Bytes(32, 0xff)}) {
        PublicKey pub{0x02, 0x03};
        EXPECT_FALSE(Bucks::derivePublicKey(toPriv(k), pub)) << Bucks::hexStr(k);
        EXPECT_TRUE(pub.empty());
    }

    // 1 -> G (even y), n-1 -> -G (odd y)
    PublicKey one, minusOne;
    ASSERT_TRUE(Bucks::derivePublicKey(toPriv(addSmall(Bytes(32, 0x00), 1)), one));
    ASSERT_TRUE(Bucks::derivePublicKey(toPriv(addSmall(n, -1)), minusOne));
    PublicKey expectG = compressedWithX(G_X);
    PublicKey expectMinusG = expectG;
    expectMinusG[0] = 0x03;
    EXPECT_EQ(one, expectG);
    EXPECT_EQ(minusOne, expectMinusG);
}

TEST(EcdsaKeys, GeneratePrivateKeyIsValid) {
    for (int i = 0; i < 32; ++i) {
        PrivateKey k;
        ASSERT_TRUE(Bucks::generatePrivateKey(k));
        EXPECT_TRUE(Bucks::isValidPrivateKey(k));
    }
}

// ---------------------------------------------------------------------------
// HD tweaks and ECDH
// ---------------------------------------------------------------------------

TEST(EcdsaTweak, AddScalarRangeChecks) {
    const Bytes n = orderBytes();
    PrivateKey k = testKey(6);
    PrivateKey out{};

    // Tweak >= n is invalid (BIP-32), not silently reduced mod n.
    EXPECT_FALSE(Bucks::addScalar(k, toPriv(n), out));
    EXPECT_FALSE(Bucks::addScalar(k, toPriv(Bytes(32, 0xff)), out));
    // Invalid base key.
    EXPECT_FALSE(Bucks::addScalar(toPriv(Bytes(32, 0x00)), k, out));
    EXPECT_FALSE(Bucks::addScalar(toPriv(n), k, out));
    // k + (n - k) = 0.
    EXPECT_FALSE(Bucks::addScalar(k, toPriv(negateScalar(Bytes(k.begin(), k.end()))), out));

    // Zero tweak is allowed and is the identity.
    ASSERT_TRUE(Bucks::addScalar(k, toPriv(Bytes(32, 0x00)), out));
    EXPECT_EQ(out, k);
    // (n-1) + 2 = 1 (mod n)
    ASSERT_TRUE(Bucks::addScalar(toPriv(addSmall(n, -1)), toPriv(addSmall(Bytes(32, 0), 2)), out));
    EXPECT_EQ(Bytes(out.begin(), out.end()), addSmall(Bytes(32, 0), 1));
}

TEST(EcdsaTweak, AddPointMatchesAddScalarAndRangeChecks) {
    const Bytes n = orderBytes();
    PrivateKey k = testKey(8);
    PublicKey P = pubOf(k);

    for (uint32_t i = 0; i < 8; ++i) {
        PrivateKey t = testKey(200 + i);
        PrivateKey sum{};
        PublicKey tweaked;
        ASSERT_TRUE(Bucks::addScalar(k, t, sum));
        ASSERT_TRUE(Bucks::addPoint(P, t, tweaked));
        EXPECT_EQ(tweaked, pubOf(sum)) << i;
    }

    PublicKey out;
    EXPECT_FALSE(Bucks::addPoint(P, toPriv(n), out));                       // tweak >= n
    EXPECT_FALSE(Bucks::addPoint(PublicKey(33, 0x00), testKey(9), out));    // invalid point
    EXPECT_FALSE(Bucks::addPoint(reencode(P, false), testKey(9), out));     // uncompressed
    EXPECT_FALSE(Bucks::addPoint(P, toPriv(negateScalar(Bytes(k.begin(), k.end()))), out)); // infinity

    PublicKey same;
    ASSERT_TRUE(Bucks::addPoint(P, toPriv(Bytes(32, 0x00)), same));
    EXPECT_EQ(same, P);
}

TEST(EcdsaEcdh, SharedSecretIsSymmetricAndFailsClosed) {
    PrivateKey a = testKey(10), b = testKey(11);
    PublicKey A = pubOf(a), B = pubOf(b);
    Hash256 s1{}, s2{};
    ASSERT_TRUE(Bucks::computeSharedSecret(a, B, s1));
    ASSERT_TRUE(Bucks::computeSharedSecret(b, A, s2));
    EXPECT_EQ(s1, s2);

    Hash256 untouched{};
    untouched.fill(0x5a);
    Hash256 s = untouched;
    EXPECT_FALSE(Bucks::computeSharedSecret(a, PublicKey(33, 0x00), s));
    EXPECT_FALSE(Bucks::computeSharedSecret(a, compressedWithX(Bytes(32, 0x00)), s));
    EXPECT_FALSE(Bucks::computeSharedSecret(a, reencode(B, false), s));
    EXPECT_FALSE(Bucks::computeSharedSecret(toPriv(Bytes(32, 0x00)), B, s));
    EXPECT_FALSE(Bucks::computeSharedSecret(toPriv(orderBytes()), B, s));
    EXPECT_EQ(s, untouched);
}

// ---------------------------------------------------------------------------
// Transaction-level malleability (CRY-5)
// ---------------------------------------------------------------------------

TEST(EcdsaTransaction, HighSScriptSigIsRejected) {
    PrivateKey priv = testKey(12);
    PublicKey pub = pubOf(priv);

    Hash256 prev{};
    prev.fill(0x11);
    Hash256 genesis{};
    genesis.fill(0x77);
    std::vector<Bucks::SpentOutput> spent = {
        {5000, Bucks::makeP2PKH(Bucks::hash160(pub))}};
    Bucks::Transaction tx({Bucks::TransactionInput(prev, 0)},
                          {Bucks::TransactionOutput(1000, Bucks::Script{0x6a})});
    ASSERT_TRUE(Bucks::signP2PKHInput(tx, 0, spent, priv, genesis));
    Bucks::SigHashCache cache(tx, spent);
    ASSERT_EQ(Bucks::verifyP2PKHInput(tx, 0, cache, spent[0], genesis), Bucks::SpendError::OK);

    // Swap s for n - s inside the scriptSig: a different txid, same signer.
    const Bucks::Script& ss = tx.inputs[0].scriptSig;
    Bytes der(ss.begin() + 1, ss.begin() + ss[0]);
    Bytes r, s;
    ASSERT_TRUE(splitSignature(der, r, s));
    Bucks::Transaction mutated = tx;
    Bucks::CompressedPubKey cpk;
    std::copy(pub.begin(), pub.end(), cpk.begin());
    Bytes highS = derSignature(r, negateScalar(s));
    if (highS.size() >= 8 && highS.size() <= 72)
        ASSERT_TRUE(Bucks::makeP2PKHUnlock(highS, cpk, mutated.inputs[0].scriptSig));
    EXPECT_NE(mutated.txid(), tx.txid());
    Bucks::SigHashCache cache2(mutated, spent);
    EXPECT_EQ(Bucks::verifyP2PKHInput(mutated, 0, cache2, spent[0], genesis),
              Bucks::SpendError::BAD_SIGNATURE);
}
