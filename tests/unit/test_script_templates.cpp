#include <gtest/gtest.h>

#include "consensus.h"
#include "script.h"

using namespace Bucks;

namespace {
std::vector<uint8_t> payloadOf(size_t n) { return std::vector<uint8_t>(n, 0x5A); }
CompressedPubKey key(uint8_t prefix = 0x02) {
    CompressedPubKey k{};
    k[0] = prefix;
    for (size_t i = 1; i < 33; ++i) k[i] = uint8_t(i);
    return k;
}
}

TEST(ScriptTemplates, P2PKH) {
    Hash160 h;
    for (size_t i = 0; i < 20; ++i) h[i] = uint8_t(i + 1);
    Script s = makeP2PKH(h);
    ASSERT_EQ(s.size(), 25u);
    EXPECT_EQ(s[0], 0x76);
    EXPECT_EQ(s[24], 0xac);
    Hash160 back{};
    EXPECT_TRUE(isP2PKH(s, &back));
    EXPECT_EQ(back, h);
    Script bad = s;
    bad[2] = 0x15;
    EXPECT_FALSE(isP2PKH(bad));
    bad = s;
    bad.push_back(0);
    EXPECT_FALSE(isP2PKH(bad));
    EXPECT_FALSE(isP2PKH({}));
}

TEST(ScriptTemplates, NullDataCanonicalForms) {
    struct C { size_t len; size_t total; };
    for (C c : {C{0, 1}, C{1, 3}, C{75, 77}, C{76, 79}, C{255, 258}, C{256, 260}, C{1024, 1028}}) {
        Script s;
        ASSERT_TRUE(makeNullData(payloadOf(c.len), s)) << c.len;
        EXPECT_EQ(s.size(), c.total) << c.len;
        std::vector<uint8_t> out;
        ASSERT_TRUE(parseNullData(s, &out)) << c.len;
        EXPECT_EQ(out, payloadOf(c.len));
        EXPECT_TRUE(isStandardOutputScript(s));
    }
    Script s;
    EXPECT_FALSE(makeNullData(payloadOf(1025), s));
}

TEST(ScriptTemplates, OnlyTwoOutputTypes) {
    // TXR-1 template part: everything else is rejected.
    Script p2pk = {0x21};
    p2pk.insert(p2pk.end(), 33, 0x02);
    p2pk.push_back(0xac);
    Script multisig = {0x51, 0x21};
    multisig.insert(multisig.end(), 33, 0x02);
    multisig.insert(multisig.end(), {0x51, 0xae});
    Script p2sh = {0xa9, 0x14};
    p2sh.insert(p2sh.end(), 20, 0);
    p2sh.push_back(0x87);
    for (const Script& s : {Script{}, Script{0x51}, p2pk, multisig, p2sh})
        EXPECT_FALSE(isStandardOutputScript(s));

    // Non-minimal pushes: 5 bytes via PUSHDATA1, 100 via PUSHDATA2.
    Script nm1 = {0x6a, 0x4c, 0x05};
    nm1.insert(nm1.end(), 5, 1);
    Script nm2 = {0x6a, 0x4d, 100, 0};
    nm2.insert(nm2.end(), 100, 1);
    // Wrong length: push says 5 but 4 follow; trailing byte; zero-length push.
    Script shortPush = {0x6a, 0x05, 1, 2, 3, 4};
    Script trailing = {0x6a, 0x01, 7, 0};
    Script zeroPush = {0x6a, 0x00};
    Script tooBig = {0x6a, 0x4d, 0x01, 0x04};  // 1025
    tooBig.insert(tooBig.end(), 1025, 1);
    for (const Script& s : {nm1, nm2, shortPush, trailing, zeroPush, tooBig})
        EXPECT_FALSE(isStandardOutputScript(s));
}

TEST(ScriptTemplates, UnlockLayout) {
    for (size_t derLen : {size_t(8), size_t(70), size_t(72)}) {
        std::vector<uint8_t> der(derLen, 0x30);
        Script s;
        ASSERT_TRUE(makeP2PKHUnlock(der, key(0x03), s));
        EXPECT_EQ(s.size(), derLen + 1 + 35);  // L + 35
        std::vector<uint8_t> derBack;
        CompressedPubKey kBack{};
        ASSERT_TRUE(parseP2PKHUnlock(s, derBack, kBack));
        EXPECT_EQ(derBack, der);
        EXPECT_EQ(kBack, key(0x03));
    }
    Script s;
    EXPECT_FALSE(makeP2PKHUnlock(std::vector<uint8_t>(7, 0x30), key(), s));
    EXPECT_FALSE(makeP2PKHUnlock(std::vector<uint8_t>(73, 0x30), key(), s));

    ASSERT_TRUE(makeP2PKHUnlock(std::vector<uint8_t>(70, 0x30), key(), s));
    std::vector<uint8_t> d;
    CompressedPubKey k;
    auto mutated = [&](auto f) { Script m = s; f(m); return parseP2PKHUnlock(m, d, k); };
    EXPECT_FALSE(mutated([](Script& m) { m.push_back(0); }));             // length
    EXPECT_FALSE(mutated([](Script& m) { m.pop_back(); }));
    EXPECT_FALSE(mutated([](Script& m) { m[m.size() - 35] = 0x81; }));    // sighash type
    EXPECT_FALSE(mutated([](Script& m) { m[m.size() - 34] = 0x20; }));    // key push
    EXPECT_FALSE(mutated([](Script& m) { m[m.size() - 33] = 0x04; }));    // key prefix
    EXPECT_FALSE(mutated([](Script& m) { m[0] = 0x08; }));                // L too small
    EXPECT_FALSE(mutated([](Script& m) { m[0] = 0x4a; }));                // L too big
    EXPECT_FALSE(parseP2PKHUnlock({}, d, k));
}
