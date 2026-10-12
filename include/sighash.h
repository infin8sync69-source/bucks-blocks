#ifndef BUCKS_SIGHASH_H
#define BUCKS_SIGHASH_H

#include <cstdint>
#include <vector>

#include "crypto.h"
#include "transaction.h"

namespace Bucks {

// Signature hash v1 and P2PKH spend verification
// (docs/design/consensus-validation.md section 5).

struct SpentOutput {
    uint64_t amount;
    std::vector<uint8_t> scriptPubKey;
};

// TaggedHash(tag, msg) = SHA256(SHA256(tag) || SHA256(tag) || msg)
Hash256 taggedHash(const std::string& tag, const std::vector<uint8_t>& msg);

// The five per-transaction sha* values, built once and reused for every input.
class SigHashCache {
public:
    // `spent` must hold one entry per input, in input order (throws otherwise).
    SigHashCache(const Transaction& tx, const std::vector<SpentOutput>& spent);
    Hash256 sigHash(uint32_t inputIndex, const Hash256& genesisHash) const;

private:
    uint32_t version_, lockTime_;
    size_t inputCount_;
    Hash256 shaPrevouts_, shaAmounts_, shaScriptPubKeys_, shaSequences_, shaOutputs_;
};

// Result of checking one input (rule C-SIG). code() gives the reject string.
enum class SpendError {
    OK,
    BAD_SPENT_SCRIPT,   // spent output is not P2PKH (database corruption)
    BAD_UNLOCK_FORMAT,  // bad-scriptsig-format
    BAD_PUBKEY,         // bad-pubkey
    BAD_PUBKEY_HASH,    // bad-pubkey-hash
    BAD_SIGNATURE       // bad-sig
};
const char* spendErrorCode(SpendError e);

// Sets tx.inputs[i].scriptSig. Fails (false) if the key does not own spent[i].
bool signP2PKHInput(Transaction& tx, uint32_t i, const std::vector<SpentOutput>& spent,
                    const PrivateKey& key, const Hash256& genesisHash);

SpendError verifyP2PKHInput(const Transaction& tx, uint32_t i, const SigHashCache& cache,
                            const SpentOutput& spent, const Hash256& genesisHash);

} // namespace Bucks

#endif // BUCKS_SIGHASH_H
