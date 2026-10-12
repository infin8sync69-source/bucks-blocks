#ifndef BUCKS_VALIDATION_RULES_H
#define BUCKS_VALIDATION_RULES_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "block.h"
#include "chain.h"
#include "chainparams.h"
#include "transaction.h"

namespace Bucks {

// Pure consensus rule functions (docs/design/consensus-validation.md
// sections 6, 7.1-7.5). Stages A-D live here; ConnectBlock is stage E.
// The functions take no locks and touch no global state.

enum class ValidationResult : uint8_t {
    VALID,
    INVALID,          // breaks a consensus rule; permanent
    MUTATED,          // block data does not match its own header (merkle); this copy only
    MALFORMED,        // bytes cannot be parsed
    MISSING_PARENT,   // parent header unknown: cannot validate yet
    TIME_TOO_NEW,     // header time > now + 2 h: cannot validate yet
    DUPLICATE,        // already known
    INTERNAL,         // local failure: I/O error, out of memory, database corruption
    // mempool only:
    TX_POLICY, TX_MISSING_INPUTS, TX_CONFLICT, TX_PREMATURE
};

struct ValidationState {
    ValidationResult result = ValidationResult::VALID;
    std::string code;     // reject code, e.g. "bad-txns-BIP30"
    std::string detail;   // free text for logs only

    bool valid() const { return result == ValidationResult::VALID; }

    bool invalid(ValidationResult r, const char* rejectCode, std::string msg = {}) {
        result = r;
        code = rejectCode;
        detail = std::move(msg);
        return false;
    }
};

namespace Rules {

// ---- Stage A: context-free header (H-VER, H-BITS, H-POW) ----
bool CheckBlockHeader(const BlockHeader& header, const ChainParams& params, ValidationState& state);

// ---- Stage B: header against its parent (H-PARENT ... H-FUTURE) ----
// `index` is optional; when given, rule H-CKPT(b) also looks for checkpoints
// that are already present in the block index.
bool ContextualCheckBlockHeader(const BlockHeader& header, const BlockIndex* parent,
                                int64_t now, const ChainParams& params, ValidationState& state,
                                const BlockIndexMap* index = nullptr);

// ---- Difficulty (H-DIFF) ----
// bits the child of `parent` must carry. The window ancestor is found through
// the block index of parent's own branch.
uint32_t GetNextWorkRequired(const BlockIndex* parent, const ChainParams& params);

// Retarget formula with a signed timespan clamped to [T/4, 4T].
uint32_t CalculateNextWorkRequired(uint32_t lastBits, int64_t firstTime, int64_t lastTime,
                                   const ChainParams& params);

// ---- Stateless transaction rules (T-VERSION ... T-UNLOCK) ----
bool CheckTransaction(const Transaction& tx, ValidationState& state);

// BIP113 finality. `mtp` is the median time past used for time locks.
bool IsFinal(const Transaction& tx, uint32_t height, int64_t mtp);

// Block subsidy in awraq, 0 from 64 halvings on.
uint64_t Subsidy(uint32_t height, const ChainParams& params);

// Signature operations of one transaction: one per input of a non-coinbase
// transaction, plus the gold hook's count for every NULL_DATA output.
uint64_t TxSigOps(const Transaction& tx);

// ---- Stage C: context-free block body (B-PARSE is done by Block::deserialize) ----
// On success *txids (if given) receives the txids, computed once.
bool CheckBlock(const Block& block, const ChainParams& params, ValidationState& state,
                std::vector<Hash256>* txids = nullptr);

// ---- Stage D: block against its parent (B-CB-HEIGHT, B-FINAL) ----
bool ContextualCheckBlock(const Block& block, uint32_t height, int64_t parentMtp,
                          ValidationState& state);

} // namespace Rules
} // namespace Bucks

#endif // BUCKS_VALIDATION_RULES_H
