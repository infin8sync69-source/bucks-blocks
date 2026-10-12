#include "serialize.h"

#include <algorithm>

namespace Bucks {

void Reader::need(size_t n) const {
    // Subtraction form: pos_ <= size_ always holds, so this cannot overflow.
    if (size_ - pos_ < n) throw ParseError("unexpected end of data");
}

uint8_t Reader::u8() {
    need(1);
    return data_[pos_++];
}

uint16_t Reader::u16() {
    need(2);
    uint16_t v = uint16_t(data_[pos_]) | (uint16_t(data_[pos_ + 1]) << 8);
    pos_ += 2;
    return v;
}

uint32_t Reader::u32() {
    need(4);
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= uint32_t(data_[pos_ + i]) << (8 * i);
    pos_ += 4;
    return v;
}

uint64_t Reader::u64() {
    need(8);
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= uint64_t(data_[pos_ + i]) << (8 * i);
    pos_ += 8;
    return v;
}

Hash256 Reader::hash256() {
    Hash256 h;
    bytes(h.data(), h.size());
    return h;
}

uint64_t Reader::compactSize(uint64_t maxValue) {
    const uint64_t cap = std::min<uint64_t>(maxValue, MAX_COMPACT_SIZE);
    uint8_t tag = u8();
    uint64_t v;
    if (tag < 0xFD) {
        v = tag;
    } else if (tag == 0xFD) {
        v = u16();
        if (v < 0xFD) throw ParseError("non-minimal compact size");
    } else if (tag == 0xFE) {
        v = u32();
        if (v <= 0xFFFF) throw ParseError("non-minimal compact size");
    } else {
        v = u64();
        if (v <= 0xFFFFFFFFull) throw ParseError("non-minimal compact size");
    }
    if (v > cap) throw ParseError("compact size too large");
    return v;
}

std::vector<uint8_t> Reader::varBytes(size_t maxLen) {
    uint64_t len = compactSize(maxLen);
    if (len > remaining()) throw ParseError("length exceeds remaining data");
    std::vector<uint8_t> out(static_cast<size_t>(len));
    bytes(out.data(), out.size());
    return out;
}

void Reader::bytes(uint8_t* out, size_t n) {
    need(n);
    if (n) std::copy(data_ + pos_, data_ + pos_ + n, out);
    pos_ += n;
}

void Reader::requireEnd() const {
    if (!atEnd()) throw ParseError("trailing bytes");
}

Writer& Writer::u8(uint8_t v) {
    buf_.push_back(v);
    return *this;
}

Writer& Writer::u16(uint16_t v) {
    for (int i = 0; i < 2; ++i) buf_.push_back(uint8_t(v >> (8 * i)));
    return *this;
}

Writer& Writer::u32(uint32_t v) {
    for (int i = 0; i < 4; ++i) buf_.push_back(uint8_t(v >> (8 * i)));
    return *this;
}

Writer& Writer::u64(uint64_t v) {
    for (int i = 0; i < 8; ++i) buf_.push_back(uint8_t(v >> (8 * i)));
    return *this;
}

Writer& Writer::hash256(const Hash256& h) {
    buf_.insert(buf_.end(), h.begin(), h.end());
    return *this;
}

Writer& Writer::compactSize(uint64_t v) {
    if (v < 0xFD) {
        u8(uint8_t(v));
    } else if (v <= 0xFFFF) {
        u8(0xFD);
        u16(uint16_t(v));
    } else if (v <= 0xFFFFFFFFull) {
        u8(0xFE);
        u32(uint32_t(v));
    } else {
        u8(0xFF);
        u64(v);
    }
    return *this;
}

Writer& Writer::varBytes(const std::vector<uint8_t>& v) {
    compactSize(v.size());
    buf_.insert(buf_.end(), v.begin(), v.end());
    return *this;
}

Writer& Writer::bytes(const uint8_t* p, size_t n) {
    if (n) buf_.insert(buf_.end(), p, p + n);
    return *this;
}

size_t compactSizeLen(uint64_t v) {
    if (v < 0xFD) return 1;
    if (v <= 0xFFFF) return 3;
    if (v <= 0xFFFFFFFFull) return 5;
    return 9;
}

} // namespace Bucks
