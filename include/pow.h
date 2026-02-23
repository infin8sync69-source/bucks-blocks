#ifndef POW_H
#define POW_H

#include <cstdint>

#include "crypto.h"
#include "blockheader.h"

namespace Bucks {
namespace Pow {

/**
 * ============================================================
 *  PROOF OF WORK (CONSENSUS-CRITICAL)
 *  ------------------------------------------------------------
 *  Any change here is a HARD FORK
 * ============================================================
 */

/**
 * Decode compact nBits representation into full 256-bit target.
 *
 * Bitcoin compact format:
 *   bits = [exponent (1 byte)] [mantissa (3 bytes)]
 */
Hash256 targetFromBits(uint32_t bits);

/**
 * Encode full 256-bit target into compact nBits format.
 */
uint32_t bitsFromTarget(const Hash256& target);

/**
 * Check Proof-of-Work:
 *   hash(header) <= target(bits)
 */
bool checkProofOfWork(const BlockHeader& header);

/**
 * Calculate the expected difficulty target for the next block.
 *
 * Implements Bitcoin-style retargeting:
 *   - Adjust every DIFFICULTY_ADJUSTMENT_INTERVAL blocks
 *   - Clamp adjustment factor to 4x / 0.25x
 */
uint32_t calculateNextBits(
    uint32_t prevBits,
    uint32_t firstBlockTime,
    uint32_t lastBlockTime
);

} // namespace Pow
} // namespace Bucks

#endif // POW_H
