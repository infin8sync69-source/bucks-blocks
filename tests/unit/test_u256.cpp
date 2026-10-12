#include <gtest/gtest.h>

#include <string>

#include "u256.h"

using namespace Bucks;

namespace {

U256 fromHex(const std::string& s) {
    std::string h(64 - s.size(), '0');
    h += s;
    Hash256 b;
    for (int i = 0; i < 32; ++i) b[i] = uint8_t(std::stoi(h.substr(2 * i, 2), nullptr, 16));
    return U256::fromBytesBE(b);
}

} // namespace

TEST(U256, BytesRoundTripAndBits) {
    Hash256 h{};
    for (int i = 0; i < 32; ++i) h[i] = uint8_t(i + 1);
    EXPECT_EQ(U256::fromBytesBE(h).toBytesBE(), h);
    EXPECT_EQ(U256().bits(), 0u);
    EXPECT_EQ(U256(1).bits(), 1u);
    EXPECT_EQ((U256(1) << 255).bits(), 256u);
    EXPECT_TRUE((U256(1) << 256).isZero());
    EXPECT_EQ(U256(0x1234).low64(), 0x1234u);
    Hash256 one{};
    one[31] = 1;
    EXPECT_EQ(U256::fromBytesBE(one), U256(1));  // byte 0 is most significant
}

TEST(U256, ShiftsAddSub) {
    U256 a = fromHex("0123456789abcdef0123456789abcdef");
    for (unsigned n : {0u, 1u, 7u, 31u, 32u, 33u, 64u, 100u}) {
        EXPECT_EQ((a << n) >> n, a) << n;
    }
    EXPECT_EQ(U256(5) - U256(7) + U256(7), U256(5));  // wraps modulo 2^256
    EXPECT_EQ(~U256() + U256(1), U256());
    EXPECT_EQ(fromHex("ffffffff") + U256(1), fromHex("100000000"));
    EXPECT_TRUE(U256(3) < U256(4) && U256(4) >= U256(4) && !(U256(4) > U256(4)));
    U256 m = fromHex("ffffffffffffffff");
    m *= 3;
    EXPECT_EQ(m, fromHex("2fffffffffffffffd"));
}

TEST(U256, DivisionVectors) {
    // Reference values computed with Python big integers.
    struct V { const char *a, *b, *q; };
    const V vecs[] = {
        {"1e2feb89414c343c1027c4d1c386bbc4cd613e30d8f16adf91b7584a2265b1f5",
         "1a6cecc1b78e510617311d8a3c2ce6f44", "1247120fd930abbf3397714a8f156be3"},
        {"63ca828dd5f4b3b2e4b06ce60741c7a87ce42c8218072e8c35bf992dc9e9c616",
         "2008a05a6c4647159c324c9859b810e76", "31d7d18e97e82d25dfa7b2bcf2e59e43"},
        {"1a2b8f1ff1fd42a29755d4c13a902931cd447e35b8b6d8fe442e3d437204e52d",
         "2a648a7dd06839eb905b6e6e307d4bedc", "9e0918d9650a93c7083b6e626c8143a"},
        {"6c0fd4f5f8130c4237730edfafbd67f9619699cfe1988ad9f06c144a025b413f",
         "46c41a457eed8d14f06d3fef701966a0c381e88f38c0c8fd8712b8bc076f3787", "1"},
    };
    for (const auto& v : vecs) EXPECT_EQ(fromHex(v.a) / fromHex(v.b), fromHex(v.q));
    // Divisors above 2^255 exercise the dropped-top-bit path.
    U256 big = ~U256();
    EXPECT_EQ(big / big, U256(1));
    EXPECT_EQ(big / (big - U256(1)), U256(1));
    EXPECT_EQ((big - U256(1)) / big, U256());
    EXPECT_EQ(U256(100) / U256(7), U256(14));
}
