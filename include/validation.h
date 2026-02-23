#ifndef VALIDATION_H
#define VALIDATION_H

#include <cstdint>
#include <vector>
#include <string>

#include "block.h"
#include "blockheader.h"
#include "transaction.h"
#include "utxodb.h"
#include "consensus.h"

namespace Bucks {
class BlockDB;
namespace Validation {

/**
 * ============================================================
 *  VALIDATION (CONSENSUS-CRITICAL)
 *  ------------------------------------------------------------
 *  This module decides what is VALID or INVALID.
 *  Any change here is a HARD FORK.
 * ============================================================
 */


/* ============================================================
 *  Error codes (deterministic, debuggable)
 * ============================================================
 */
enum class ValidationError {
    NONE = 0,

    // ---- Block header ----
    INVALID_VERSION,
    INVALID_PREV_BLOCK,
    INVALID_TIMESTAMP,
    INVALID_POW,
    INVALID_MERKLE_ROOT,

    // ---- Block structure ----
    EMPTY_BLOCK,
    BLOCK_TOO_LARGE,
    INVALID_COINBASE,
    MULTIPLE_COINBASE,

    // ---- Transaction ----
    TX_EMPTY_INPUTS,
    TX_EMPTY_OUTPUTS,
    TX_TOO_LARGE,
    TX_DUPLICATE_INPUT,
    TX_INVALID_OUTPUT_VALUE,
    TX_OUTPUT_OVERFLOW,

    // ---- UTXO / Spending ----
    TX_MISSING_UTXO,
    TX_ALREADY_SPENT,
    TX_COINBASE_MATURITY,
    TX_INPUT_VALUE_LESS_THAN_OUTPUT,

    // ---- Script / Signature ----
    SCRIPT_EVAL_FALSE,
    INVALID_SIGNATURE,

    // ---- Internal ----
    UNKNOWN_ERROR
};


/* ============================================================
 *  Stateless Validation (no UTXO access)
 * ============================================================
 */

/**
 * Validate block header fields except PoW.
 */
bool checkBlockHeader(
    const BlockHeader& header,
    uint32_t currentTime,
    uint32_t medianTimePast,
    ValidationError& error
);

/**
 * Validate Proof-of-Work.
 */
bool checkBlockProofOfWork(
    const BlockHeader& header,
    ValidationError& error
);

/**
 * Validate Merkle root against transactions.
 */
bool checkMerkleRoot(
    const Block& block,
    ValidationError& error
);

/**
 * Stateless transaction checks:
 * - structure
 * - size limits
 * - value ranges
 */
bool checkTransactionBasic(
    const Transaction& tx,
    ValidationError& error
);


/* ============================================================
 *  Stateful Validation (UTXO-dependent)
 * ============================================================
 */

/**
 * Validate a transaction against the UTXO set.
 *
 * Rules enforced:
 * - Inputs exist
 * - Inputs unspent
 * - Signatures valid
 * - Input sum >= output sum
 * - Coinbase maturity
 */
bool checkTransactionInputs(
    const Transaction& tx,
    const UTXODB& utxoDB,
    uint64_t currentHeight,
    ValidationError& error
);


/* ============================================================
 *  Block Validation
 * ============================================================
 */

/**
 * Full block validation (stateless + stateful).
 *
 * DOES NOT modify UTXO set.
 */
bool checkBlock(
    const Block& block,
    const BlockHeader& prevHeader,
    const UTXODB& utxoDB,
    uint64_t currentHeight,
    uint32_t medianTimePast,
    uint32_t expectedBits,
    ValidationError& error
);


/* ============================================================
 *  UTXO Application (MUST BE ATOMIC)
 * ============================================================
 */

/**
 * Apply block transactions to the UTXO set.
 * Assumes block is already validated.
 */
bool connectBlock(
    const Block& block,
    UTXODB& utxoDB,
    BlockDB* blockDB,
    ValidationError& error
);

/**
 * Disconnect a block (UNDO state)
 */
bool disconnectBlock(
    const Block& block,
    UTXODB& utxoDB,
    BlockDB& blockDB,
    ValidationError& error
);

} // namespace Validation
} // namespace Bucks

#endif // VALIDATION_H
