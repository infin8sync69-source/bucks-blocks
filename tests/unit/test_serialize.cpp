#include <gtest/gtest.h>

#include <string>

#include "serialize.h"

using namespace Bucks;

namespace {

std::string hex(const std::vector<uint8_t>& v) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t b : v) { s += d[b >> 4]; s += d[b & 15]; }
    return s;
}

std::vector<uint8_t> unhex(const std::string& s) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < s.size(); i += 2)
        out.push_back(uint8_t(std::stoi(s.substr(i, 2), nullptr, 16)));
    return out;
}

} // namespace

TEST(Serialize, CompactSizeVectors) {
    struct V { uint64_t n; const char* h; };
    const V vecs[] = {
        {0, "00"}, {252, "fc"}, {253, "fdfd00"}, {65535, "fdffff"},
        {65536, "fe00000100"}, {100000, "fea0860100"},
        {4294967295ull, "feffffffff"},
    };
    for (const auto& v : vecs) {
        Writer w;
        w.compactSize(v.n);
        EXPECT_EQ(hex(w.data()), v.h) << v.n;
        EXPECT_EQ(compactSizeLen(v.n), w.data().size());
    }
    // 33,554,432 is the global cap; larger values are written but never read.
    for (const auto& v : vecs) {
        if (v.n > MAX_COMPACT_SIZE) continue;
        auto b = unhex(v.h);
        Reader r(b);
        EXPECT_EQ(r.compactSize(MAX_COMPACT_SIZE), v.n);
        EXPECT_TRUE(r.atEnd());
    }
    // The FF form is accepted when the value is large enough and within max.
    {
        Writer w;
        w.u8(0xFF).u64(0x100000000ull);
        Reader r(w.data());
        EXPECT_THROW(r.compactSize(MAX_COMPACT_SIZE), ParseError);  // over the cap
    }
    for (const char* bad : {"fdfc00", "fe00010000", "feffff0000"}) {
        auto b = unhex(bad);
        Reader r(b);
        EXPECT_THROW(r.compactSize(MAX_COMPACT_SIZE), ParseError) << bad;
    }
    // Caller maximum.
    auto b = unhex("fdfd00");
    Reader r(b);
    EXPECT_THROW(r.compactSize(252), ParseError);
}

TEST(Serialize, ReaderBounds) {
    std::vector<uint8_t> buf(10, 0xAB);
    Reader r(buf);
    EXPECT_NO_THROW(r.u64());
    EXPECT_THROW(r.u64(), ParseError);
    EXPECT_EQ(r.remaining(), 2u);

    // varBytes with a huge declared length must throw, not allocate.
    auto big = unhex("feffffffff00");
    Reader r2(big);
    EXPECT_THROW(r2.varBytes(1u << 20), ParseError);

    // Declared length larger than remaining data.
    auto short_ = unhex("0a0102");
    Reader r3(short_);
    EXPECT_THROW(r3.varBytes(100), ParseError);

    Reader r4(nullptr, 0);
    EXPECT_THROW(r4.u8(), ParseError);
}

TEST(Serialize, FixedWidthRoundTrip) {
    Writer w;
    Hash256 h{};
    for (size_t i = 0; i < h.size(); ++i) h[i] = uint8_t(i);
    w.u8(1).u16(0x0203).u32(0x04050607).u64(0x08090a0b0c0d0e0full).hash256(h)
        .varBytes({9, 8, 7});
    EXPECT_EQ(hex(w.data()).substr(0, 2 + 4 + 8 + 16),
              "01" "0302" "07060504" "0f0e0d0c0b0a0908");
    Reader r(w.data());
    EXPECT_EQ(r.u8(), 1);
    EXPECT_EQ(r.u16(), 0x0203);
    EXPECT_EQ(r.u32(), 0x04050607u);
    EXPECT_EQ(r.u64(), 0x08090a0b0c0d0e0full);
    EXPECT_EQ(r.hash256(), h);
    EXPECT_EQ(r.varBytes(10), (std::vector<uint8_t>{9, 8, 7}));
    EXPECT_NO_THROW(r.requireEnd());
}

TEST(Serialize, NoTrailingBytes) {
    std::vector<uint8_t> b = {1, 2};
    Reader r(b);
    r.u8();
    try {
        r.requireEnd();
        FAIL() << "expected ParseError";
    } catch (const ParseError& e) {
        EXPECT_STREQ(e.what(), "trailing bytes");
    }
}
