#include "pow.h"

#include <algorithm>
#include <cstring>

namespace Bucks {

// -----------------------------------------------------------------------------
// Compact bits → target
// -----------------------------------------------------------------------------

Hash256 bitsToTarget(uint32_t bits) {
    Hash256 target{};
    target.fill(0);

    uint32_t exponent = bits >> 24;
    uint32_t mantissa = bits & 0x007fffff;

    if (bits & 0x00800000) {
        // Negative target (invalid)
        return target;
    }

    if (exponent <= 3) {
        mantissa >>= 8 * (3 - exponent);
        for (int i = 0; i < 4; ++i) {
            target[31 - i] = (mantissa >> (8 * i)) & 0xff;
        }
    } else {
        int offset = exponent - 3;
        for (int i = 0; i < 3; ++i) {
            target[31 - (i + offset)] =
                (mantissa >> (8 * i)) & 0xff;
        }
    }

    return target;
}

// -----------------------------------------------------------------------------
// Target → compact bits
// -----------------------------------------------------------------------------

uint32_t targetToBits(const Hash256& target) {
    int first = 0;
    while (first < 32 && target[first] == 0)
        ++first;

    int size = 32 - first;
    uint32_t mantissa = 0;

    if (size >= 3) {
        mantissa =
            (target[first] << 16) |
            (target[first + 1] << 8) |
            (target[first + 2]);
    } else if (size == 2) {
        mantissa =
            (target[first] << 16) |
            (target[first + 1] << 8);
    } else if (size == 1) {
        mantissa = (target[first] << 16);
    }

    if (mantissa & 0x00800000) {
        mantissa >>= 8;
        size++;
    }

    return (size << 24) | (mantissa & 0x007fffff);
}

// -----------------------------------------------------------------------------
// Hash <= target
// -----------------------------------------------------------------------------

bool checkProofOfWork(const Hash256& hash, uint32_t bits) {
    Hash256 target = bitsToTarget(bits);

    // Big-endian comparison
    for (size_t i = 0; i < 32; ++i) {
        if (hash[i] < target[i])
            return true;
        if (hash[i] > target[i])
            return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Difficulty retarget (Phase-1: Bitcoin-style)
// -----------------------------------------------------------------------------

uint32_t calculateNextWorkRequired(
    const BlockHeader& lastHeader,
    const BlockHeader& firstHeader,
    const ConsensusParams& params
) {
    int64_t actualTimespan =
        lastHeader.timestamp - firstHeader.timestamp;

    if (actualTimespan < params.targetTimespan / 4)
        actualTimespan = params.targetTimespan / 4;

    if (actualTimespan > params.targetTimespan * 4)
        actualTimespan = params.targetTimespan * 4;

    Hash256 oldTarget = bitsToTarget(lastHeader.bits);
    Hash256 newTarget = oldTarget;

    // Multiply target by actualTimespan / targetTimespan
    multiplyTarget(
        newTarget,
        actualTimespan,
        params.targetTimespan
    );

    if (newTarget > params.powLimit)
        newTarget = params.powLimit;

    return targetToBits(newTarget);
}

// -----------------------------------------------------------------------------
// Target math helper (big integer)
// -----------------------------------------------------------------------------

void multiplyTarget(
    Hash256& target,
    uint64_t numerator,
    uint64_t denominator
) {
    __uint128_t carry = 0;

    for (int i = 31; i >= 0; --i) {
        __uint128_t value =
            (__uint128_t)target[i] * numerator + carry;

        target[i] = (uint8_t)(value / denominator);
        carry = value % denominator;
    }
}

} // namespace Bucks
