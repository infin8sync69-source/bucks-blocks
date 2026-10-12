#ifndef BUCKS_CONNECT_BLOCK_H
#define BUCKS_CONNECT_BLOCK_H

#include <vector>

#include "block.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "gold_rules.h"
#include "undo.h"
#include "validation_rules.h"

namespace Bucks {

// Stage E (docs/design/consensus-validation.md sections 7.6, 7.7).
// Both functions change only `view` and `gold`. The caller commits them, or
// discards them after a failure, and never reuses them after a failure.

// Precondition: stages C and D passed for `block`, `txids` are its txids, and
// view.bestBlock() == index.prev->hash. `index` is not genesis.
// Returns false and sets `state` on an invalid block. A read error is INTERNAL,
// which cannot happen with the in-memory view used here.
bool ConnectBlock(const Block& block, const std::vector<Hash256>& txids, const BlockIndex& index,
                  CoinsViewCache& view, GoldRules::GoldState& gold, const ChainParams& params,
                  BlockUndo& undoOut, ValidationState& state);

enum class DisconnectResult : uint8_t {
    OK,
    CORRUPT   // the undo data or the coin set does not match the block: database corruption
};

// Exact inverse of ConnectBlock. `undo` must already have passed
// BlockUndo::deserialize; this function checks the rest of section 7.7.
DisconnectResult DisconnectBlock(const Block& block, const std::vector<Hash256>& txids,
                                 const BlockIndex& index, CoinsViewCache& view,
                                 GoldRules::GoldState& gold, const BlockUndo& undo);

} // namespace Bucks

#endif // BUCKS_CONNECT_BLOCK_H
