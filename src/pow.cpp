#include "pow.h"

namespace Bucks {
namespace Pow {

Compact decodeCompact(uint32_t c) {
    Compact r;
    unsigned size = c >> 24;
    uint32_t word = c & 0x007FFFFF;
    if (size <= 3) {
        r.target = U256(word >> (8 * (3 - size)));
    } else {
        r.target = U256(word) << (8 * (size - 3));
    }
    r.negative = word != 0 && (c & 0x00800000) != 0;
    r.overflow = word != 0 && (size > 34 ||
                               (word > 0xFF && size > 33) ||
                               (word > 0xFFFF && size > 32));
    return r;
}

uint32_t encodeCompact(const U256& target) {
    unsigned size = (target.bits() + 7) / 8;
    uint64_t c;
    if (size <= 3) {
        c = target.low64() << (8 * (3 - size));
    } else {
        c = (target >> (8 * (size - 3))).low64();
    }
    if (c & 0x00800000) {
        c >>= 8;
        size += 1;
    }
    return uint32_t(c) | (uint32_t(size) << 24);
}

bool bitsValid(uint32_t bits, const U256& powLimit, U256* targetOut) {
    Compact c = decodeCompact(bits);
    if (c.negative || c.overflow || c.target.isZero() || c.target > powLimit) return false;
    if (targetOut) *targetOut = c.target;
    return true;
}

bool checkProofOfWork(const Hash256& hash, uint32_t bits, const U256& powLimit) {
    U256 target;
    if (!bitsValid(bits, powLimit, &target)) return false;
    return U256::fromBytesBE(hash) <= target;
}

U256 blockProof(uint32_t bits) {
    Compact c = decodeCompact(bits);
    if (c.negative || c.overflow || c.target.isZero()) return U256();
    // 2^256 / (target + 1) == (~target / (target + 1)) + 1
    return (~c.target / (c.target + U256(1))) + U256(1);
}

uint32_t calculateNextWorkRequired(uint32_t lastBits,
                                   int64_t firstTime,
                                   int64_t lastTime,
                                   const U256& powLimit,
                                   int64_t T) {
    int64_t actual = lastTime - firstTime;
    if (actual < T / 4) actual = T / 4;
    if (actual > 4 * T) actual = 4 * T;

    U256 target = decodeCompact(lastBits).target;
    target *= uint32_t(actual);
    target = target / U256(uint64_t(T));
    if (target > powLimit) target = powLimit;
    return encodeCompact(target);
}

// The specification needs arithmetic (flooring) right shifts of negative
// numbers; C++17 leaves them implementation-defined.
static_assert((int64_t(-1) >> 1) == int64_t(-1), "ASERT needs arithmetic right shifts");

U256 calculateASERT(const U256& refTarget,
                    int64_t spacing,
                    int64_t timeDiff,
                    int64_t heightDiff,
                    const U256& powLimit,
                    int64_t halfLife) {
    // Exponent in 16-bit fixed point. Division truncates toward zero, as the
    // specification requires (its note 3); flooring here changes ~75% of the
    // official vectors.
    const int64_t exponent = ((timeDiff - spacing * (heightDiff + 1)) * 65536) / halfLife;

    // exponent = shifts * 2^16 + frac with 0 <= frac < 2^16 (the shift floors).
    int64_t shifts = exponent >> 16;
    const uint64_t frac = uint64_t(exponent - shifts * 65536);

    // 65536 * 2^(frac / 65536) by the cubic approximation; the sum fits in
    // uint64 (largest at frac = 65535: 2^64 - 1.8e14).
    const uint32_t factor = 65536 + uint32_t((195766423245049ull * frac +
                                              971821376ull * frac * frac +
                                              5127ull * frac * frac * frac +
                                              (1ull << 47)) >> 48);
    U256 next = refTarget;
    next *= factor;  // < 2^256 by the startup assertion on powLimit

    // Multiply by 2^shifts and drop the 2^16 scale of the factor.
    shifts -= 16;
    if (shifts <= 0) {
        next = -shifts >= 256 ? U256() : next >> unsigned(-shifts);
    } else if (shifts >= 256 || ((next << unsigned(shifts)) >> unsigned(shifts)) != next) {
        // The shift would lose high bits: the exact value is at least 2^256,
        // so above powLimit as well (the specification's note 6).
        next = powLimit;
    } else {
        next = next << unsigned(shifts);
    }

    if (next.isZero()) return U256(1);  // 0 is not a valid target, 1 is
    if (next > powLimit) return powLimit;
    return next;
}

} // namespace Pow
} // namespace Bucks
