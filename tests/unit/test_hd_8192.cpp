#include <gtest/gtest.h>
#include "hd_8192.h"
#include "crypto.h"
#include <algorithm>
#include <string>
#include <vector>

TEST(HD8192Test, MasterKeyCreation) {
    // 512-bit seed
    std::vector<uint8_t> seed(64, 0x01);
    
    // Check if we can create a master key without crashing
    // Note: Since ExtendedKey is internal, we mostly check public interface or successful return
    // Assuming createMaster returns an ExtendedKey
    Bucks::ExtendedKey master = Bucks::HD8192::createMaster(seed);
    
    // Basic validity checks (depth 0 for master)
    EXPECT_EQ(master.depth, 0);
    EXPECT_FALSE(master.key.empty());
    EXPECT_FALSE(master.chainCode.empty());
}

TEST(HD8192Test, ChildDerivation) {
    std::vector<uint8_t> seed(64, 0xab);
    Bucks::ExtendedKey master = Bucks::HD8192::createMaster(seed);
    
    // Normal derivation (index 0)
    Bucks::ExtendedKey child0 = Bucks::HD8192::deriveChild(master, 0);
    EXPECT_EQ(child0.depth, 1);
    EXPECT_EQ(child0.childNumber, 0);
    EXPECT_EQ(child0.key.size(), 32); // Private key size

    // Hardened derivation (index 0 | 0x80000000)
    Bucks::ExtendedKey childHardened = Bucks::HD8192::deriveChild(master, 0x80000000);
    EXPECT_EQ(childHardened.depth, 1);
    EXPECT_GE(childHardened.childNumber, 0x80000000);
}

TEST(HD8192Test, DeriveChildRejectsInvalidParentKey) {
    // CRY-12: a zero private key used to "derive" children because addScalar
    // never range-checked its base key, and the fingerprint was computed from
    // derivePublicKey(0)'s all-zero pubkey.
    Bucks::ExtendedKey bad;
    bad.isPrivate = true;
    bad.key.assign(32, 0x00);
    bad.chainCode.fill(0x42);

    EXPECT_TRUE(Bucks::HD8192::deriveChild(bad, 0x80000000).key.empty());
    EXPECT_TRUE(Bucks::HD8192::deriveChild(bad, 0).key.empty());
    EXPECT_TRUE(Bucks::HD8192::derivePath(bad, "m/0'/1").key.empty());

    // Malformed key sizes must not be copied into fixed-size key buffers.
    Bucks::ExtendedKey wrongPriv = bad;
    wrongPriv.key.assign(33, 0x01);
    EXPECT_TRUE(Bucks::HD8192::deriveChild(wrongPriv, 0x80000000).key.empty());
    Bucks::ExtendedKey wrongPub;
    wrongPub.isPrivate = false;
    wrongPub.key.assign(32, 0x02);
    EXPECT_TRUE(Bucks::HD8192::deriveChild(wrongPub, 0).key.empty());

    // A failed step stops the whole path instead of deriving from an empty key.
    Bucks::ExtendedKey empty;
    EXPECT_TRUE(Bucks::HD8192::derivePath(empty, "m/0/1").key.empty());
}

TEST(HD8192Test, PublicDerivationMatchesPrivate) {
    // CKDpub(neuter(parent), i) == neuter(CKDpriv(parent, i)) for non-hardened i.
    std::vector<uint8_t> seed(64, 0x5c);
    Bucks::ExtendedKey master = Bucks::HD8192::createMaster(seed);
    ASSERT_EQ(master.key.size(), 32u);

    Bucks::PrivateKey masterPriv;
    std::copy(master.key.begin(), master.key.end(), masterPriv.begin());
    Bucks::ExtendedKey neutered = master;
    neutered.isPrivate = false;
    ASSERT_TRUE(Bucks::derivePublicKey(masterPriv, neutered.key));

    for (uint32_t i : {0u, 1u, 7u, 0x7fffffffu}) {
        Bucks::ExtendedKey privChild = Bucks::HD8192::deriveChild(master, i);
        Bucks::ExtendedKey pubChild = Bucks::HD8192::deriveChild(neutered, i);
        ASSERT_EQ(privChild.key.size(), 32u);
        ASSERT_EQ(pubChild.key.size(), 33u);

        Bucks::PrivateKey childPriv;
        std::copy(privChild.key.begin(), privChild.key.end(), childPriv.begin());
        Bucks::PublicKey expected;
        ASSERT_TRUE(Bucks::derivePublicKey(childPriv, expected));
        EXPECT_EQ(pubChild.key, expected) << i;
        EXPECT_EQ(pubChild.chainCode, privChild.chainCode) << i;
    }

    // Path parsing and single steps agree.
    Bucks::ExtendedKey viaPath = Bucks::HD8192::derivePath(master, "m/0'/1");
    Bucks::ExtendedKey viaSteps = Bucks::HD8192::deriveChild(
        Bucks::HD8192::deriveChild(master, 0x80000000), 1);
    EXPECT_EQ(viaPath.key, viaSteps.key);
    EXPECT_FALSE(viaPath.key.empty());
}

// ---------------------------------------------------------------------------
// (h) BIP-32 test vector 1. Expected values were computed independently in
//     Python (hmac/hashlib + a pure-Python secp256k1) and agree with the
//     published vector; they also prove the master key is "Bitcoin seed".
// ---------------------------------------------------------------------------

static std::vector<uint8_t> hdFromHex(const std::string& hex) {
    return Bucks::parseHex(hex);
}

TEST(HD8192Test, Bip32TestVector1) {
    std::vector<uint8_t> seed = hdFromHex("000102030405060708090a0b0c0d0e0f");
    Bucks::ExtendedKey master = Bucks::HD8192::createMaster(seed);
    ASSERT_EQ(master.key.size(), 32u);
    EXPECT_TRUE(master.isPrivate);
    EXPECT_EQ(Bucks::hexStr(master.key),
              "e8f32e723decf4051aefac8e2c93c9c5b214313817cdb01a1494b917c8436b35");
    EXPECT_EQ(Bucks::hexStr(std::vector<uint8_t>(master.chainCode.begin(), master.chainCode.end())),
              "873dff81c02f525623fd1fe5167eac3a55a049de3d314bb42ee227ffed37d508");
    EXPECT_EQ(Bucks::HD8192::getFingerprint(master), 0x3442193eu);

    Bucks::PrivateKey mpriv;
    std::copy(master.key.begin(), master.key.end(), mpriv.begin());
    Bucks::PublicKey mpub;
    ASSERT_TRUE(Bucks::derivePublicKey(mpriv, mpub));
    EXPECT_EQ(Bucks::hexStr(mpub), "0339a36013301597daef41fbe593a02cc513d0b55527ec2df1050e2e8ff49c85c2");

    // m/0'
    Bucks::ExtendedKey child = Bucks::HD8192::deriveChild(master, 0x80000000u);
    ASSERT_EQ(child.key.size(), 32u);
    EXPECT_EQ(child.depth, 1u);
    EXPECT_EQ(child.childNumber, 0x80000000u);
    EXPECT_EQ(child.parentFingerprint, 0x3442193eu);
    EXPECT_EQ(Bucks::hexStr(child.key),
              "edb2e14f9ee77d26dd93b4ecede8d16ed408ce149b6cd80b0715a2d911a0afea");
    EXPECT_EQ(Bucks::hexStr(std::vector<uint8_t>(child.chainCode.begin(), child.chainCode.end())),
              "47fdacbd0f1097043b78c63c20c34ef4ed9a111d980047ad16282c7ae6236141");
    Bucks::PrivateKey cpriv;
    std::copy(child.key.begin(), child.key.end(), cpriv.begin());
    Bucks::PublicKey cpub;
    ASSERT_TRUE(Bucks::derivePublicKey(cpriv, cpub));
    EXPECT_EQ(Bucks::hexStr(cpub), "035a784662a4a20a65bf6aab9ae98a6c068a81c52e4b032c0fb5400c706cfccc56");

    // The same step through the path parser, with both hardened spellings.
    for (const char* path : {"m/0'", "m/0h", "m/0H"}) {
        Bucks::ExtendedKey viaPath = Bucks::HD8192::derivePath(master, path);
        EXPECT_EQ(viaPath.key, child.key) << path;
        EXPECT_EQ(viaPath.chainCode, child.chainCode) << path;
    }
    // "m" is the master itself.
    Bucks::ExtendedKey m = Bucks::HD8192::derivePath(master, "m");
    EXPECT_EQ(m.key, master.key);
    EXPECT_EQ(m.depth, 0u);

    // Empty seed is refused.
    EXPECT_TRUE(Bucks::HD8192::createMaster({}).key.empty());
}

// (i) Strict path parsing
TEST(HD8192Test, ParsePathAcceptsWellFormedPaths) {
    std::vector<uint32_t> idx;
    ASSERT_TRUE(Bucks::HD8192::parsePath("m", idx));
    EXPECT_TRUE(idx.empty());
    ASSERT_TRUE(Bucks::HD8192::parsePath("m/0", idx));
    EXPECT_EQ(idx, std::vector<uint32_t>({0}));
    ASSERT_TRUE(Bucks::HD8192::parsePath("m/44'/1'/0'/0/0", idx));
    EXPECT_EQ(idx, std::vector<uint32_t>({44 | 0x80000000u, 1 | 0x80000000u, 0x80000000u, 0, 0}));
    ASSERT_TRUE(Bucks::HD8192::parsePath("m/2147483647'", idx));
    EXPECT_EQ(idx, std::vector<uint32_t>({0xffffffffu}));
    ASSERT_TRUE(Bucks::HD8192::parsePath("m/2147483647", idx));
    EXPECT_EQ(idx, std::vector<uint32_t>({0x7fffffffu}));
    ASSERT_TRUE(Bucks::HD8192::parsePath("m/5h/6H", idx));
    EXPECT_EQ(idx, std::vector<uint32_t>({5 | 0x80000000u, 6 | 0x80000000u}));
    EXPECT_EQ(Bucks::defaultReceivePath(), "m/44'/" + std::to_string(Bucks::BUCKS_COIN_TYPE) + "'/0'/0/0");
    ASSERT_TRUE(Bucks::HD8192::parsePath(Bucks::defaultReceivePath(), idx));
    EXPECT_EQ(idx.size(), 5u);
}

TEST(HD8192Test, ParsePathRejectsMalformedPaths) {
    const char* bad[] = {
        "", "m/", "/", "/0", "m//0", "m/0/", "m/0'/", "m0", "M/0", "n/0", " m/0", "m/0 ",
        "m/ 0", "m/a", "m/0x", "m/0'x", "m/0''", "m/-1", "m/+1", "m/01", "m/00",
        "m/2147483648", "m/4294967295", "m/4294967296", "m/99999999999", "m/18446744073709551616",
        "m/0/1/2/", "m/0'h", "m/'", "m/h", "m\\0", "m/0\n",
    };
    std::vector<uint32_t> idx;
    for (const char* p : bad)
        EXPECT_FALSE(Bucks::HD8192::parsePath(p, idx)) << "accepted: '" << p << "'";

    // derivePath fails closed on every one of them, even with a good master.
    std::vector<uint8_t> seed(64, 0x11);
    Bucks::ExtendedKey master = Bucks::HD8192::createMaster(seed);
    ASSERT_FALSE(master.key.empty());
    for (const char* p : bad)
        EXPECT_TRUE(Bucks::HD8192::derivePath(master, p).key.empty()) << "derived: '" << p << "'";
}
