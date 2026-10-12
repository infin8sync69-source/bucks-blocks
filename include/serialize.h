#ifndef BUCKS_SERIALIZE_H
#define BUCKS_SERIALIZE_H

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "crypto.h"

namespace Bucks {

// Thrown by Reader on any structural problem. It never crosses a trust
// boundary: callers catch it and map it to MALFORMED (peer/API data) or
// INTERNAL (own database).
struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Hard upper bound for any CompactSize accepted by Reader.
constexpr uint64_t MAX_COMPACT_SIZE = 33554432;

// Bounds-checked little-endian reader. Every read checks remaining() first.
class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    explicit Reader(const std::vector<uint8_t>& v) : data_(v.data()), size_(v.size()) {}

    uint8_t  u8();
    uint16_t u16();
    uint32_t u32();
    uint64_t u64();
    Hash256  hash256();
    // Minimal encoding only; throws if the value exceeds maxValue (which is
    // itself capped at MAX_COMPACT_SIZE).
    uint64_t compactSize(uint64_t maxValue);
    // Length <= maxLen and <= remaining().
    std::vector<uint8_t> varBytes(size_t maxLen);
    void bytes(uint8_t* out, size_t n);

    size_t remaining() const { return size_ - pos_; }
    bool atEnd() const { return pos_ == size_; }
    size_t position() const { return pos_; }
    // Throws ParseError("trailing bytes") unless every byte was consumed.
    void requireEnd() const;

private:
    void need(size_t n) const;
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
};

// Appends little-endian encodings to a byte vector.
class Writer {
public:
    Writer() = default;

    Writer& u8(uint8_t v);
    Writer& u16(uint16_t v);
    Writer& u32(uint32_t v);
    Writer& u64(uint64_t v);
    Writer& hash256(const Hash256& h);
    Writer& compactSize(uint64_t v);   // always the shortest form
    Writer& varBytes(const std::vector<uint8_t>& v);
    Writer& bytes(const uint8_t* p, size_t n);

    const std::vector<uint8_t>& data() const { return buf_; }
    std::vector<uint8_t> take() { return std::move(buf_); }

private:
    std::vector<uint8_t> buf_;
};

// Number of bytes compactSize(v) occupies.
size_t compactSizeLen(uint64_t v);

} // namespace Bucks

#endif // BUCKS_SERIALIZE_H
