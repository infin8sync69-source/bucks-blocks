#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <vector>
#include <array>
#include <cstdint>
#include <ctime>

#include "crypto.h"
#include "script.h"

namespace Bucks {

constexpr size_t MAX_TX_INPUTS = 1000;
constexpr size_t MAX_TX_OUTPUTS = 1000;
constexpr size_t MAX_TX_SIZE = 100000; // 100KB

// ------------------------------------------------------------
// Transaction Input
// ------------------------------------------------------------
struct TransactionInput {
    Hash256 prevTxid;              // 32-byte txid
    uint32_t prevIndex;            // output index
    std::vector<uint8_t> scriptSig;  // unlocking script
    std::vector<uint8_t> pubKey;     // pubkey (filled when signed)
    uint32_t sequence;

    TransactionInput();
    TransactionInput(const Hash256& prevTx, uint32_t idx);
    bool isCoinbase() const;
};

// ------------------------------------------------------------
// Transaction Output
// ------------------------------------------------------------
struct TransactionOutput {
    uint64_t amount;                  // amount in awraq
    Script scriptPubKey;              // locking script

    TransactionOutput();
    TransactionOutput(uint64_t amt, const Script& spk);
    TransactionOutput(uint64_t amt, const std::string& scriptStr); // convenience for tests
};

// Witness (SegWit)
struct Witness {
    std::vector<std::vector<uint8_t>> stack;
};

// ------------------------------------------------------------
// Transaction
// ------------------------------------------------------------
class Transaction {
public:
    uint32_t version;
    std::vector<TransactionInput> inputs;
    std::vector<TransactionOutput> outputs;
    uint32_t lockTime;
    std::time_t timestamp;
    std::vector<Witness> witnesses; // one per input (optional)
    uint64_t fee_amount = 0;

    Transaction();
    Transaction(const std::vector<TransactionInput>& in, const std::vector<TransactionOutput>& out);

    // Serialization: includeWitness controls segwit serialization
    std::vector<uint8_t> serialize(bool includeWitness = false) const;
    static Transaction deserialize(const std::vector<uint8_t>& data);

    // TXID / WTXID
    Hash256 txid() const;   // legacy hash (NO witness)
    Hash256 wtxid() const;  // witness hash (WITH witness)

    // Signature hashing and signing helpers
    Hash256 legacySignatureHash(size_t inputIndex) const;
    bool signInput(size_t inputIndex, const PrivateKey& privKey, const PublicKey& pubKey);
    bool verifyInput(size_t inputIndex) const;

    // Helpers
    bool isCoinbase() const;
    uint64_t totalOutput() const;
    bool verify() const; // verify all inputs

    // Fee helpers (policy fields)
    void setFee(uint64_t f) { fee_amount = f; }
    uint64_t fee() const { return fee_amount; }
};

} // namespace Bucks

#endif // TRANSACTION_H
