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

} // namespace Pow
} // namespace Bucks
