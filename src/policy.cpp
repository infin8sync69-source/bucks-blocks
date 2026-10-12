#include "policy.h"

#include "consensus.h"
#include "gold_rules.h"
#include "script.h"

namespace Bucks {
namespace Policy {

uint64_t FeeRate(uint64_t fee, uint32_t size) {
    if (size == 0) return 0;
    return fee * 1000 / size;   // fee <= MAX_MONEY, so fee * 1000 < 2^63
}

uint64_t FeeForSize(uint64_t rate, uint32_t size) {
    return (rate * size + 999) / 1000;
}

bool IsStandardTx(const Transaction& tx, ValidationState& state) {
    using R = ValidationResult;

    if (tx.version != Consensus::TX_VERSION) return state.invalid(R::TX_POLICY, "version");
    if (tx.serialize().size() > MAX_STANDARD_TX_SIZE) return state.invalid(R::TX_POLICY, "tx-size");

    for (const TransactionInput& in : tx.inputs) {
        if (in.sequence != STANDARD_SEQUENCE_A && in.sequence != STANDARD_SEQUENCE_B)
            return state.invalid(R::TX_POLICY, "non-standard-sequence");
    }

    uint32_t nullData = 0;
    for (const TransactionOutput& out : tx.outputs) {
        if (isP2PKH(out.scriptPubKey)) {
            if (out.amount < DUST_LIMIT) return state.invalid(R::TX_POLICY, "dust");
            continue;
        }
        std::vector<uint8_t> payload;
        if (!parseNullData(out.scriptPubKey, &payload))
            return state.invalid(R::TX_POLICY, "scriptpubkey");
        if (++nullData > MAX_STANDARD_NULLDATA_OUTPUTS)
            return state.invalid(R::TX_POLICY, "multi-op-return");
        // Gold events may carry their own, larger payload (up to the consensus limit).
        if (payload.size() > MAX_STANDARD_NULLDATA_PAYLOAD &&
            GoldRules::classify(payload).kind != GoldRules::Kind::EVENT)
            return state.invalid(R::TX_POLICY, "datacarrier");
    }
    return true;
}

} // namespace Policy
} // namespace Bucks
