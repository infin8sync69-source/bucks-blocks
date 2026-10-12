#ifndef BUCKS_GOLD_RULES_H
#define BUCKS_GOLD_RULES_H

#include <cstdint>
#include <vector>

#include "crypto.h"
#include "transaction.h"
#include "validation_rules.h"

namespace Bucks {

// Gold hook (docs/design/consensus-validation.md section 7.8). The gold spec
// (docs/design/gold-reserve-module.md) will replace this stub. Until then
// every NULL_DATA payload is NOT_GOLD, the gold state stays empty, and the
// undo record for gold is empty.
namespace GoldRules {

enum class Kind : uint8_t { NOT_GOLD, EVENT, MALFORMED };

struct Classification {
    Kind kind = Kind::NOT_GOLD;
    uint32_t sigOps = 0;
};

// Consensus gold state. Empty until the gold spec is implemented.
struct GoldState {
    friend bool operator==(const GoldState&, const GoldState&) { return true; }
    friend bool operator!=(const GoldState& a, const GoldState& b) { return !(a == b); }
};

// Context-free classification of a NULL_DATA payload. Used by the sigop
// count of the block rules, policy and the template builder.
Classification classify(const std::vector<uint8_t>& payload);

// Test-only: replaces the classifier so tests can give payloads sigop costs.
// Pass nullptr to restore the stub.
using ClassifyFn = Classification (*)(const std::vector<uint8_t>&);
void setClassifierForTesting(ClassifyFn fn);

// Undo bytes of one block for the gold state. Empty in the stub.
class GoldUndoBuilder {
public:
    std::vector<uint8_t> finish() const { return {}; }
};

GoldUndoBuilder beginBlock(const GoldState& stateBeforeBlock);

// Contextual check of one NULL_DATA output. The stub accepts only NOT_GOLD
// payloads, so it returns true without changing the state.
bool connectOutput(const Transaction& tx, uint32_t txIndex, uint32_t vout, const Hash256& txid,
                   uint32_t height, GoldState& state, GoldUndoBuilder& undo, ValidationState& st);

// Exact inverse of the undo bytes. The stub accepts only an empty record.
bool disconnectBlock(GoldState& state, const std::vector<uint8_t>& goldUndo);

} // namespace GoldRules
} // namespace Bucks

#endif // BUCKS_GOLD_RULES_H
