#include <gtest/gtest.h>

#include "merkle.h"

using namespace Bucks;

namespace {
Hash256 leaf(const char* s) {
    std::string str(s);
    return doubleSha256(std::vector<uint8_t>(str.begin(), str.end()));
}
std::string hex(const Hash256& h) { return hexStr(std::vector<uint8_t>(h.begin(), h.end())); }
}

TEST(Merkle, Vectors) {
    // MRK-1: leaves are SHA256d("a"), SHA256d("b"), SHA256d("c").
    const Hash256 a = leaf("a"), b = leaf("b"), c = leaf("c");
    struct V { std::vector<Hash256> leaves; const char* root; bool mutated; };
    const V vecs[] = {
        {{a}, "bf5d3affb73efd2ec6c36ad3112dd933efed63c4e1cbffcfa88e2759c144f2d8", false},
        {{a, b}, "b767a3a12f5f8bb1949d163c51f9a42e6bda8dcd02d50353717f73d4338b1bf0", false},
        {{a, b, c}, "74449b8328cb6e97d305adb2fca5e90993fdf9c667fa40cb625f40508da40cbf", false},
        {{a, b, c, c}, "74449b8328cb6e97d305adb2fca5e90993fdf9c667fa40cb625f40508da40cbf", true},
        {{a, a}, "dd2b2a71c44ea394239c2ee2cd6541048fe397d6bc68554241dd195be71e3af2", true},
    };
    for (const auto& v : vecs) {
        bool mutated = !v.mutated;
        EXPECT_EQ(hex(merkleRoot(v.leaves, &mutated)), v.root);
        EXPECT_EQ(mutated, v.mutated);
        EXPECT_EQ(merkleRoot(v.leaves), merkleRoot(v.leaves, nullptr));
    }
}

TEST(Merkle, EmptyAndSingle) {
    bool mutated = true;
    EXPECT_EQ(merkleRoot({}, &mutated), Hash256{});
    EXPECT_FALSE(mutated);
    EXPECT_EQ(merkleRoot({leaf("x")}), leaf("x"));  // one leaf: the root is the txid
}

TEST(Merkle, DuplicateAtHigherLevelIsMutation) {
    // [a,b,a,b] has equal pairs only one level up: ab == ab.
    const Hash256 a = leaf("a"), b = leaf("b");
    bool mutated = false;
    Hash256 root = merkleRoot({a, b, a, b}, &mutated);
    EXPECT_TRUE(mutated);
    EXPECT_NE(root, merkleRoot({a, b}));
}
