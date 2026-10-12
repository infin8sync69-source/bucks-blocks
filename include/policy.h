#ifndef BUCKS_POLICY_H
#define BUCKS_POLICY_H

#include <cstdint>

#include "transaction.h"
#include "validation_rules.h"

namespace Bucks {

// Local node policy (docs/design/consensus-validation.md section 3.2 and 9.3).
// None of this is consensus: other nodes may differ.
namespace Policy {

constexpr uint32_t MAX_STANDARD_TX_SIZE = 100'000;
constexpr uint64_t MIN_RELAY_FEERATE = 1'000;          // awraq per 1,000 bytes
constexpr uint64_t INCREMENTAL_RELAY_FEERATE = 1'000;  // awraq per 1,000 bytes
constexpr uint64_t DUST_LIMIT = 546;                   // every P2PKH output, awraq
constexpr uint32_t MAX_STANDARD_NULLDATA_PAYLOAD = 80;
constexpr uint32_t MAX_STANDARD_NULLDATA_OUTPUTS = 1;
constexpr uint32_t MAX_STANDARD_TX_SIGOPS = 4'000;
constexpr uint32_t STANDARD_SEQUENCE_A = 0xFFFFFFFE;
constexpr uint32_t STANDARD_SEQUENCE_B = 0xFFFFFFFF;

constexpr uint64_t MAX_MEMPOOL_BYTES = 100'000'000;
constexpr int64_t MEMPOOL_EXPIRY = 1'209'600;          // 14 days
constexpr int64_t ROLLING_FEE_HALFLIFE = 43'200;       // 12 hours
constexpr size_t MAX_ORPHAN_TXS = 100;
constexpr size_t MAX_ORPHAN_TXS_PER_PEER = 25;
constexpr int64_t ORPHAN_TX_EXPIRY = 1'200;            // 20 minutes
constexpr uint32_t MAX_ORPHAN_TX_SIZE = 100'000;

// Template space kept for the header, the transaction count and the coinbase.
constexpr uint32_t COINBASE_RESERVED_SIZE = 1'000;

// fee * 1000 / size, rounded down (awraq per 1,000 bytes).
uint64_t FeeRate(uint64_t fee, uint32_t size);
// ceil(rate * size / 1000): the fee a transaction of `size` bytes must pay.
uint64_t FeeForSize(uint64_t rate, uint32_t size);

// P-VERSION, P-SIZE, P-SEQUENCE, P-DUST, P-NULLDATA. Failures are TX_POLICY.
// Assumes CheckTransaction already passed.
bool IsStandardTx(const Transaction& tx, ValidationState& state);

} // namespace Policy
} // namespace Bucks

#endif // BUCKS_POLICY_H
