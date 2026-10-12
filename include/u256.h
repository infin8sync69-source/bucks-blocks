#ifndef BUCKS_U256_H
#define BUCKS_U256_H

#include <array>
#include <cstdint>

#include "crypto.h"

namespace Bucks {

// Fixed-size 256-bit unsigned integer. Limb 0 is the least significant.
// Arithmetic wraps modulo 2^256; callers that must not wrap check bits().
class U256 {
public:
    U256() { limbs_.fill(0); }
    explicit U256(uint64_t v) {
        limbs_.fill(0);
        limbs_[0] = uint32_t(v);
        limbs_[1] = uint32_t(v >> 32);
    }

    // Byte 0 is the most significant byte (hash-as-number convention).
    static U256 fromBytesBE(const Hash256& h);
    Hash256 toBytesBE() const;

    bool isZero() const;
    uint64_t low64() const { return uint64_t(limbs_[0]) | (uint64_t(limbs_[1]) << 32); }
    // Position of the highest set bit + 1 (0 for zero).
    unsigned bits() const;

    friend bool operator==(const U256& a, const U256& b) { return a.limbs_ == b.limbs_; }
    friend bool operator!=(const U256& a, const U256& b) { return !(a == b); }
    friend bool operator<(const U256& a, const U256& b) { return compare(a, b) < 0; }
    friend bool operator>(const U256& a, const U256& b) { return compare(a, b) > 0; }
    friend bool operator<=(const U256& a, const U256& b) { return compare(a, b) <= 0; }
    friend bool operator>=(const U256& a, const U256& b) { return compare(a, b) >= 0; }

    U256 operator+(const U256& o) const;
    U256 operator-(const U256& o) const;
    U256 operator~() const;
    U256& operator+=(const U256& o) { return *this = *this + o; }
    U256& operator-=(const U256& o) { return *this = *this - o; }
    U256& operator*=(uint32_t m);
    // Long division; dividing by zero is a programming error (aborts).
    U256 operator/(const U256& d) const;
    // A shift by 256 or more yields 0.
    U256 operator<<(unsigned n) const;
    U256 operator>>(unsigned n) const;

private:
    static int compare(const U256& a, const U256& b);
    std::array<uint32_t, 8> limbs_;
};

} // namespace Bucks

#endif // BUCKS_U256_H
