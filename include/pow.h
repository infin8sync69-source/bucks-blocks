#ifndef POW_H
#define POW_H

#include <cstdint>

#include "crypto.h"
#include "u256.h"

namespace Bucks {
namespace Pow {

/**
 * PROOF OF WORK (CONSENSUS-CRITICAL; any change is a hard fork).
 * Specified in docs/design/consensus-validation.md section 6.
 */

struct Compact {
    U256 target;
    bool negative = false;
    bool overflow = false;
};

Compact decodeCompact(uint32_t bits);
uint32_t encodeCompact(const U256& target);

// H-BITS: not negative, no overflow, target != 0 and target <= powLimit.
// On success the decoded target is stored in *targetOut (if given).
bool bitsValid(uint32_t bits, const U256& powLimit, U256* targetOut = nullptr);

// H-POW: bitsValid AND hash (read big-endian) <= target. Full 256-bit compare.
bool checkProofOfWork(const Hash256& hash, uint32_t bits, const U256& powLimit);

// Expected work for one block: floor(2^256 / (target + 1)).
// Returns 0 if bits are invalid (negative, overflow or zero target).
U256 blockProof(uint32_t bits);

// Retarget formula; times are signed 64-bit, the timespan is clamped to
// [T/4, 4T] and the result is clamped to powLimit. The caller guarantees
// bits(powLimit) + bits(4T) <= 256 (startup assertion), so no overflow.
uint32_t calculateNextWorkRequired(uint32_t lastBits,
                                   int64_t firstTime,
                                   int64_t lastTime,
                                   const U256& powLimit,
                                   int64_t targetTimespan);

} // namespace Pow
} // namespace Bucks

#endif // POW_H
