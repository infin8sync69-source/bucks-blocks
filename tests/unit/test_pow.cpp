#include <gtest/gtest.h>

#include "pow.h"

using namespace Bucks;
using namespace Bucks::Pow;

namespace {
constexpr int64_t T = 1209600;
const U256 kLimit = decodeCompact(0x1d00ffff).target;

U256 hex64(uint64_t v) { return U256(v); }
}

TEST(Pow, CompactVectors) {
    struct V { uint32_t bits; uint64_t target; bool neg, ovf; uint32_t enc; };
    const V vecs[] = {
        {0x00000000, 0, false, false, 0x00000000},
        {0x01003456, 0, false, false, 0x00000000},
        {0x01123456, 0x12, false, false, 0x01120000},
        {0x02123456, 0x1234, false, false, 0x02123400},
        {0x03123456, 0x123456, false, false, 0x03123456},
        {0x04123456, 0x12345600, false, false, 0x04123456},
        {0x04923456, 0x12345600, true, false, 0},
        {0x05009234, 0x92340000, false, false, 0x05009234},
    };
    for (const auto& v : vecs) {
        Compact c = decodeCompact(v.bits);
        EXPECT_EQ(c.target, hex64(v.target)) << std::hex << v.bits;
        EXPECT_EQ(c.negative, v.neg) << std::hex << v.bits;
        EXPECT_EQ(c.overflow, v.ovf) << std::hex << v.bits;
        if (!v.neg) EXPECT_EQ(encodeCompact(c.target), v.enc) << std::hex << v.bits;
    }
    EXPECT_TRUE(decodeCompact(0xff123456).overflow);
    EXPECT_TRUE(decodeCompact(0x21010000).overflow);
    Compact c = decodeCompact(0x20123456);
    EXPECT_EQ(c.target, U256(0x123456) << (8 * 29));
    EXPECT_EQ(encodeCompact(c.target), 0x20123456u);
    EXPECT_EQ(decodeCompact(0x1d00ffff).target, U256(0xffff) << 208);
    EXPECT_EQ(encodeCompact(U256(0xffff) << 208), 0x1d00ffffu);
    EXPECT_EQ(decodeCompact(0x207fffff).target, U256(0x7fffff) << (8 * 29));
    EXPECT_EQ(encodeCompact(U256(0x7fffff) << (8 * 29)), 0x207fffffu);
}

TEST(Pow, BitsValid) {
    EXPECT_TRUE(bitsValid(0x1d00ffff, kLimit));
    EXPECT_FALSE(bitsValid(0x1d010000, kLimit));  // above powLimit
    EXPECT_FALSE(bitsValid(0x00000000, kLimit));  // zero target
    EXPECT_FALSE(bitsValid(0x04923456, kLimit));  // negative
    EXPECT_FALSE(bitsValid(0xff123456, kLimit));  // overflow
}

TEST(Pow, FullCompare) {
    U256 target = decodeCompact(0x1d00ffff).target;
    EXPECT_TRUE(checkProofOfWork(target.toBytesBE(), 0x1d00ffff, kLimit));
    EXPECT_FALSE(checkProofOfWork((target + U256(1)).toBytesBE(), 0x1d00ffff, kLimit));
    EXPECT_TRUE(checkProofOfWork((target - U256(1)).toBytesBE(), 0x1d00ffff, kLimit));
    // Bytes below the 3-byte mantissa matter (the old check ignored them).
    U256 t2 = decodeCompact(0x1c05a3f4).target;
    EXPECT_TRUE(checkProofOfWork(t2.toBytesBE(), 0x1c05a3f4, kLimit));
    EXPECT_FALSE(checkProofOfWork((t2 + U256(1)).toBytesBE(), 0x1c05a3f4, kLimit));
    // Invalid bits never pass, even for an all-zero hash.
    EXPECT_FALSE(checkProofOfWork(Hash256{}, 0x04923456, kLimit));
}

TEST(Pow, BlockProofVectors) {
    EXPECT_EQ(blockProof(0x1d00ffff), U256(0x100010001ull));
    EXPECT_EQ(blockProof(0x1e00ffff), U256(0x1000100ull));
    EXPECT_EQ(blockProof(0x207fffff), U256(2));
    EXPECT_EQ(blockProof(0x1c05a3f4), U256(0x2d62f2a3b3ull));
    EXPECT_TRUE(blockProof(0).isZero());
    EXPECT_TRUE(blockProof(0x04923456).isZero());
}

TEST(Pow, RetargetVectors) {
    struct V { uint32_t bits; int64_t first, last; uint32_t want; };
    const V vecs[] = {
        {0x1d00ffff, 1261130161, 1262152739, 0x1d00d86a},
        {0x1d00ffff, 1231006505, 1233061996, 0x1d00ffff},
        {0x1c05a3f4, 1279008237, 1279297671, 0x1c0168fd},
        {0x1c387f6f, 1263163443, 1269211443, 0x1d00e1fd},
        {0x1c05a3f4, 1000000, 1000000 + T, 0x1c05a3f4},
        {0x1c05a3f4, 1000000, 1000000 + T / 2, 0x1c02d1fa},
        {0x1c05a3f4, 1000000, 1000000 - 3600, 0x1c0168fd},  // negative span: harder, not easier
        {0x1d00ffff, 1000000, 1000000 + 10 * T, 0x1d00ffff},
    };
    for (const auto& v : vecs)
        EXPECT_EQ(calculateNextWorkRequired(v.bits, v.first, v.last, kLimit, T), v.want)
            << std::hex << v.bits << " " << std::dec << v.first;
}

TEST(Pow, OverflowGuard) {
    // The retarget product needs bits(powLimit) + bits(4T) <= 256; documents
    // why the launch limit is 0x1d00ffff and the old 0x1f00ffff was rejected.
    unsigned bitsT4 = U256(uint64_t(4 * T)).bits();
    EXPECT_LE(decodeCompact(0x1d00ffff).target.bits() + bitsT4, 256u);
    EXPECT_GT(decodeCompact(0x1f00ffff).target.bits() + bitsT4, 256u);
}
