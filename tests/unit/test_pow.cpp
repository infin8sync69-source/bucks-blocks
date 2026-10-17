#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

    // ASERT multiplies by a factor below 2^17: 0x1e7fffff (239 bits) is the
    // largest limit that fits, testnet's 0x1e00ffff has 232 bits.
    EXPECT_LE(decodeCompact(0x1e7fffff).target.bits() + ASERT_FACTOR_BITS, 256u);
    EXPECT_GT(decodeCompact(0x1f00ffff).target.bits() + ASERT_FACTOR_BITS, 256u);
    U256 product = decodeCompact(0x1e7fffff).target;
    product *= 131071u;  // the largest factor does not wrap
    EXPECT_EQ(product / U256(131071), decodeCompact(0x1e7fffff).target);
}

// POW-6: the official aserti3-2d vectors (Bitcoin Cash Node qa-assets, see
// tests/unit/data/aserti3-2d/README.md): 12 runs, 14,000 blocks, powLimit
// 0x1d00ffff, spacing 600 s, half-life 172,800 s.
TEST(Pow, AsertVectors) {
    size_t checked = 0;
    for (int run = 1; run <= 12; ++run) {
        char name[8];
        std::snprintf(name, sizeof name, "run%02d", run);
        std::ifstream in(std::string(BUCKS_SOURCE_DIR) + "/tests/unit/data/aserti3-2d/" + name);
        ASSERT_TRUE(in.good()) << name;

        // run10 goes past the largest int64 height: heights are unsigned, only
        // their difference (at most 10) is used.
        uint64_t anchorHeight = 0;
        int64_t anchorParentTime = -1, iterations = -1;
        uint32_t anchorBits = 0;
        size_t rows = 0;
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("##", 0) == 0) {
                const size_t colon = line.find(':');
                if (colon == std::string::npos) continue;
                std::string key = line.substr(2, colon - 2);
                key.erase(0, key.find_first_not_of(' '));
                const std::string value = line.substr(colon + 1);
                if (key == "anchor height") anchorHeight = std::stoull(value);
                else if (key == "anchor parent time") anchorParentTime = std::stoll(value);
                else if (key == "anchor nBits") anchorBits = uint32_t(std::stoul(value, nullptr, 16));
                else if (key == "iterations") iterations = std::stoll(value);
                continue;
            }
            if (line.empty() || line[0] == '#') continue;
            ASSERT_NE(anchorBits, 0u) << name;
            std::istringstream row(line);
            uint64_t iteration = 0, height = 0;
            int64_t time = 0;
            std::string want;
            ASSERT_TRUE(row >> iteration >> height >> time >> want) << name << ": " << line;
            const uint32_t got = encodeCompact(calculateASERT(decodeCompact(anchorBits).target, 600,
                                                              time - anchorParentTime,
                                                              int64_t(height - anchorHeight), kLimit, 172800));
            ++rows;
            if (got != uint32_t(std::stoul(want, nullptr, 16))) {
                ADD_FAILURE() << name << " iteration " << iteration << ": got " << std::hex << got
                              << ", want " << want;
                break;
            }
        }
        EXPECT_EQ(int64_t(rows), iterations) << name;
        checked += rows;
    }
    EXPECT_EQ(checked, 14000u);
}

// POW-7: what the official vectors do not reach (all of them use 0x1d00ffff):
// testnet's limit 0x1e00ffff, the largest admitted limit 0x1e7fffff, shifts of
// 256 bits or more both ways, the target-1 floor, a negative exponent that
// truncates to 0 (600599 s at height 1000) and the largest polynomial (773399 s).
// Generated by the independent reference: python3 -I tools/aserti3_2d.py edge
TEST(Pow, AsertEdgeVectors) {
    struct V { uint32_t refBits; int64_t timeDiff, heightDiff; uint32_t limitBits, want; };
    const V vecs[] = {
        {0x01010000, 600, 0, 0x1d00ffff, 0x01010000},
        {0x01010000, -7200, 0, 0x1d00ffff, 0x01010000},
        {0x01010000, 7800, 0, 0x1d00ffff, 0x01010000},
        {0x01010000, 1200000, 10, 0x1d00ffff, 0x01770000},
        {0x01010000, 773399, 1000, 0x1d00ffff, 0x01010000},
        {0x01010000, 600599, 1000, 0x1d00ffff, 0x01010000},
        {0x01010000, 4294967296, 0, 0x1d00ffff, 0x1d00ffff},
        {0x01010000, -4294967296, 4294967295, 0x1d00ffff, 0x01010000},
        {0x01010000, 51840000, 0, 0x1d00ffff, 0x1d00ffff},
        {0x01010000, -51840000, 0, 0x1d00ffff, 0x01010000},
        {0x01010000, 1202, 1, 0x1d00ffff, 0x01010000},
        {0x1e00ffff, 600, 0, 0x1e00ffff, 0x1e00ffff},
        {0x1e00ffff, -7200, 0, 0x1e00ffff, 0x1e00f820},
        {0x1e00ffff, 7800, 0, 0x1e00ffff, 0x1e00ffff},
        {0x1e00ffff, 1200000, 10, 0x1e00ffff, 0x1e00ffff},
        {0x1e00ffff, 773399, 1000, 0x1e00ffff, 0x1e00ffff},
        {0x1e00ffff, 600599, 1000, 0x1e00ffff, 0x1e00ffff},
        {0x1e00ffff, 4294967296, 0, 0x1e00ffff, 0x1e00ffff},
        {0x1e00ffff, -4294967296, 4294967295, 0x1e00ffff, 0x01010000},
        {0x1e00ffff, 51840000, 0, 0x1e00ffff, 0x1e00ffff},
        {0x1e00ffff, -51840000, 0, 0x1e00ffff, 0x01010000},
        {0x1e00ffff, 1202, 1, 0x1e00ffff, 0x1e00ffff},
        {0x1c0ffff0, 600, 0, 0x1e00ffff, 0x1c0ffff0},
        {0x1c0ffff0, -7200, 0, 0x1e00ffff, 0x1c0f8208},
        {0x1c0ffff0, 7800, 0, 0x1e00ffff, 0x1c10783f},
        {0x1c0ffff0, 1200000, 10, 0x1e00ffff, 0x1d077f50},
        {0x1c0ffff0, 773399, 1000, 0x1e00ffff, 0x1c1fffd0},
        {0x1c0ffff0, 600599, 1000, 0x1e00ffff, 0x1c0ffff0},
        {0x1c0ffff0, 4294967296, 0, 0x1e00ffff, 0x1e00ffff},
        {0x1c0ffff0, -4294967296, 4294967295, 0x1e00ffff, 0x01010000},
        {0x1c0ffff0, 51840000, 0, 0x1e00ffff, 0x1e00ffff},
        {0x1c0ffff0, -51840000, 0, 0x1e00ffff, 0x01010000},
        {0x1c0ffff0, 1202, 1, 0x1e00ffff, 0x1c0ffff0},
        {0x1e7fffff, 600, 0, 0x1e7fffff, 0x1e7fffff},
        {0x1e7fffff, -7200, 0, 0x1e7fffff, 0x1e7c10bf},
        {0x1e7fffff, 7800, 0, 0x1e7fffff, 0x1e7fffff},
        {0x1e7fffff, 1200000, 10, 0x1e7fffff, 0x1e7fffff},
        {0x1e7fffff, 773399, 1000, 0x1e7fffff, 0x1e7fffff},
        {0x1e7fffff, 600599, 1000, 0x1e7fffff, 0x1e7fffff},
        {0x1e7fffff, 4294967296, 0, 0x1e7fffff, 0x1e7fffff},
        {0x1e7fffff, -4294967296, 4294967295, 0x1e7fffff, 0x01010000},
        {0x1e7fffff, 51840000, 0, 0x1e7fffff, 0x1e7fffff},
        {0x1e7fffff, -51840000, 0, 0x1e7fffff, 0x01010000},
        {0x1e7fffff, 1202, 1, 0x1e7fffff, 0x1e7fffff},
        {0x01010000, 600, 0, 0x1e7fffff, 0x01010000},
        {0x01010000, -7200, 0, 0x1e7fffff, 0x01010000},
        {0x01010000, 7800, 0, 0x1e7fffff, 0x01010000},
        {0x01010000, 1200000, 10, 0x1e7fffff, 0x01770000},
        {0x01010000, 773399, 1000, 0x1e7fffff, 0x01010000},
        {0x01010000, 600599, 1000, 0x1e7fffff, 0x01010000},
        {0x01010000, 4294967296, 0, 0x1e7fffff, 0x1e7fffff},
        {0x01010000, -4294967296, 4294967295, 0x1e7fffff, 0x01010000},
        {0x01010000, 51840000, 0, 0x1e7fffff, 0x1e7fffff},
        {0x01010000, -51840000, 0, 0x1e7fffff, 0x01010000},
        {0x01010000, 1202, 1, 0x1e7fffff, 0x01010000},
    };
    for (const auto& v : vecs) {
        const U256 limit = decodeCompact(v.limitBits).target;
        EXPECT_EQ(encodeCompact(calculateASERT(decodeCompact(v.refBits).target, 600, v.timeDiff,
                                               v.heightDiff, limit, 172800)),
                  v.want)
            << std::hex << v.refBits << " " << std::dec << v.timeDiff << " " << v.heightDiff << " "
            << std::hex << v.limitBits;
    }
}

TEST(Pow, AsertExactDoublingAndHalving) {
    // A whole number of half-lives has fraction 0: factor 65536, exact powers of two.
    const U256 ref = decodeCompact(0x1c0ffff0).target;
    const U256 limit = decodeCompact(0x1e00ffff).target;
    EXPECT_EQ(calculateASERT(ref, 600, 600, 0, limit, 172800), ref);
    EXPECT_EQ(calculateASERT(ref, 600, 600 + 172800, 0, limit, 172800), ref << 1);
    EXPECT_EQ(calculateASERT(ref, 600, 600 - 3 * 172800, 0, limit, 172800), ref >> 3);
    // The same in blocks: 288 blocks ahead of schedule is one half-life.
    EXPECT_EQ(calculateASERT(ref, 600, 600 * 1001 - 172800, 1000, limit, 172800), ref >> 1);
}
