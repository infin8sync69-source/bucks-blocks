#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "crypto.h"
#include "script.h"
#include "serialize.h"

namespace Bucks {

// Transaction v2 (docs/design/consensus-validation.md section 4.3).
// Everything validation needs is inside the serialized bytes: there is no
// witness, no separate pubkey, no timestamp and no fee field.

struct TransactionInput {
    Hash256 prevTxid;                // 32-byte txid
    uint32_t prevIndex;              // output index
    std::vector<uint8_t> scriptSig;  // coinbase data or P2PKH unlock script
    uint32_t sequence;

    TransactionInput();
    TransactionInput(const Hash256& prevTx, uint32_t idx);
};

struct TransactionOutput {
    uint64_t amount;                 // amount in awraq
    Script scriptPubKey;             // P2PKH or NULL_DATA

    TransactionOutput();
    TransactionOutput(uint64_t amt, const Script& spk);
    TransactionOutput(uint64_t amt, const std::string& scriptStr); // convenience for tests
};

class Transaction {
public:
    uint32_t version;
    std::vector<TransactionInput> inputs;
    std::vector<TransactionOutput> outputs;
    uint32_t lockTime;

    Transaction();
    Transaction(const std::vector<TransactionInput>& in, const std::vector<TransactionOutput>& out);

    std::vector<uint8_t> serialize() const;
    // Throws ParseError on any structural problem, including trailing bytes.
    static Transaction deserialize(const std::vector<uint8_t>& data);
    // Reads one transaction from the stream (used inside blocks); the
    // transaction itself is capped at MAX_TX_SIZE bytes.
    static Transaction parse(Reader& r);

    // txid = SHA256d(serialization); the unlock script is part of it.
    Hash256 txid() const;

    // Exactly one input, prevTxid all zero and prevIndex == 0xFFFFFFFF.
    bool isCoinbase() const;
    // Sum of output amounts; saturates at UINT64_MAX instead of wrapping.
    uint64_t totalOutput() const;
};

} // namespace Bucks

#endif // TRANSACTION_H
