#include "u256.h"

#include <cstdlib>

namespace Bucks {

U256 U256::fromBytesBE(const Hash256& h) {
    U256 r;
    for (int i = 0; i < 32; ++i) {
        int pos = 31 - i;  // little-endian byte position of h[i]
        r.limbs_[pos / 4] |= uint32_t(h[i]) << (8 * (pos % 4));
    }
    return r;
}

Hash256 U256::toBytesBE() const {
    Hash256 h;
    for (int i = 0; i < 32; ++i) {
        int pos = 31 - i;
        h[i] = uint8_t(limbs_[pos / 4] >> (8 * (pos % 4)));
    }
    return h;
}

bool U256::isZero() const {
    for (uint32_t l : limbs_) if (l) return false;
    return true;
}

unsigned U256::bits() const {
    for (int i = 7; i >= 0; --i) {
        if (limbs_[i]) {
            unsigned b = 0;
            for (uint32_t v = limbs_[i]; v; v >>= 1) ++b;
            return unsigned(i) * 32 + b;
        }
    }
    return 0;
}

int U256::compare(const U256& a, const U256& b) {
    for (int i = 7; i >= 0; --i) {
        if (a.limbs_[i] < b.limbs_[i]) return -1;
        if (a.limbs_[i] > b.limbs_[i]) return 1;
    }
    return 0;
}

U256 U256::operator+(const U256& o) const {
    U256 r;
    uint64_t carry = 0;
    for (int i = 0; i < 8; ++i) {
        uint64_t s = uint64_t(limbs_[i]) + o.limbs_[i] + carry;
        r.limbs_[i] = uint32_t(s);
        carry = s >> 32;
    }
    return r;
}

U256 U256::operator-(const U256& o) const {
    U256 r;
    int64_t borrow = 0;
    for (int i = 0; i < 8; ++i) {
        int64_t d = int64_t(limbs_[i]) - int64_t(o.limbs_[i]) - borrow;
        borrow = d < 0 ? 1 : 0;
        r.limbs_[i] = uint32_t(d);  // wraps modulo 2^32 as intended
    }
    return r;
}

U256 U256::operator~() const {
    U256 r;
    for (int i = 0; i < 8; ++i) r.limbs_[i] = ~limbs_[i];
    return r;
}

U256& U256::operator*=(uint32_t m) {
    uint64_t carry = 0;
    for (int i = 0; i < 8; ++i) {
        uint64_t p = uint64_t(limbs_[i]) * m + carry;
        limbs_[i] = uint32_t(p);
        carry = p >> 32;
    }
    return *this;
}

U256 U256::operator<<(unsigned n) const {
    U256 r;
    if (n >= 256) return r;
    unsigned limb = n / 32, bit = n % 32;
    for (int i = 7; i >= int(limb); --i) {
        uint64_t v = uint64_t(limbs_[i - limb]) << bit;
        r.limbs_[i] |= uint32_t(v);
        if (i + 1 < 8) r.limbs_[i + 1] |= uint32_t(v >> 32);
    }
    return r;
}

U256 U256::operator>>(unsigned n) const {
    U256 r;
    if (n >= 256) return r;
    unsigned limb = n / 32, bit = n % 32;
    for (unsigned i = 0; i + limb < 8; ++i) {
        uint64_t v = uint64_t(limbs_[i + limb]) << 32;  // keeps the low part in the high half
        v >>= bit;
        r.limbs_[i] |= uint32_t(v >> 32);
        if (i > 0) r.limbs_[i - 1] |= uint32_t(v);
    }
    return r;
}

U256 U256::operator/(const U256& d) const {
    if (d.isZero()) std::abort();
    U256 quotient, rem;
    for (int i = int(bits()) - 1; i >= 0; --i) {
        // rem < d <= 2^256 - 1, so the shifted value can reach 257 bits; the
        // dropped top bit means it is certainly >= d (and the wrap in -= is exact).
        bool top = rem.bits() == 256;
        rem = rem << 1;
        if ((limbs_[i / 32] >> (i % 32)) & 1) rem.limbs_[0] |= 1;
        if (top || rem >= d) {
            rem -= d;
            quotient.limbs_[i / 32] |= uint32_t(1) << (i % 32);
        }
    }
    return quotient;
}

} // namespace Bucks
