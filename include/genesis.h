#ifndef GENESIS_H
#define GENESIS_H

#include <string>

#include "block.h"
#include "chainparams.h"

namespace Bucks {
namespace Genesis {

/**
 * Rebuild the genesis block of a network from its ChainParams
 * (docs/design/consensus-validation.md section 3.5). Pure function: no
 * mining, no clock. The coinbase creates no coins (amount 0, NULL_DATA
 * output); its scriptSig is LE32(0) followed by the inscription.
 */
Block build(const ChainParams& params);

/**
 * Startup check. Returns false and fills `err` if the genesis is not
 * configured (all-zero hash), if the rebuilt merkle root or hash differs from
 * the hardcoded values, or if the header does not satisfy its own proof of
 * work. The node exits with code 3 on failure.
 */
bool check(const ChainParams& params, std::string& err);

} // namespace Genesis
} // namespace Bucks

#endif // GENESIS_H
