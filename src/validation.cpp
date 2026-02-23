#include "validation.h"
#include "blockdb.h"
#include <iostream>

namespace Bucks {
namespace Validation {

bool checkTransactionBasic(
    const Transaction& tx,
    ValidationError& error
) {
    if (tx.inputs.empty()) {
        error = ValidationError::TX_EMPTY_INPUTS;
        return false;
    }
    if (tx.outputs.empty()) {
        error = ValidationError::TX_EMPTY_OUTPUTS;
        return false;
    }

    if (tx.inputs.size() > MAX_TX_INPUTS) {
        std::cerr << "DEBUG: Transaction has too many inputs: " << tx.inputs.size() << std::endl;
        error = ValidationError::TX_TOO_LARGE;
        return false;
    }
    if (tx.outputs.size() > MAX_TX_OUTPUTS) {
        std::cerr << "DEBUG: Transaction has too many outputs: " << tx.outputs.size() << std::endl;
        error = ValidationError::TX_TOO_LARGE;
        return false;
    }

    // Round 6: Malleability & Standardness checks
    for (const auto& in : tx.inputs) {
        if (in.scriptSig.size() > 1650) {
            std::cerr << "DEBUG: scriptSig too large: " << in.scriptSig.size() << " bytes" << std::endl;
            error = ValidationError::TX_TOO_LARGE;
            return false;
        }
    }
    
    for (const auto& w : tx.witnesses) {
        if (w.stack.size() > 100) {
            std::cerr << "DEBUG: witness stack too deep: " << w.stack.size() << " items" << std::endl;
            error = ValidationError::TX_TOO_LARGE;
            return false;
        }
        for (const auto& item : w.stack) {
            if (item.size() > 520) {
                std::cerr << "DEBUG: witness item too large: " << item.size() << " bytes" << std::endl;
                error = ValidationError::TX_TOO_LARGE;
                return false;
            }
        }
    }

    size_t size = tx.serialize(true).size();
    if (size > MAX_TX_SIZE) {
        std::cerr << "DEBUG: Transaction is too large: " << size << " bytes" << std::endl;
        error = ValidationError::TX_TOO_LARGE;
        return false;
    }

    error = ValidationError::NONE;
    return true;
}

bool checkBlockProofOfWork(
    const BlockHeader& header,
    ValidationError& error
) {
    Hash256 hash = header.hash();
    
    uint32_t exponent = header.bits >> 24;
    uint32_t mantissa = header.bits & 0x00ffffff;

    if (exponent > 0x20 || mantissa > 0x00ffffff) {
        error = ValidationError::INVALID_POW;
        return false;
    }

    // Target = mantissa * 256^(exponent - 3)
    // 1. Check zero prefix
    uint32_t zeroBytes = 0x20 - exponent;
    for (uint32_t i = 0; i < zeroBytes; ++i) {
        if (hash[i] != 0) {
            error = ValidationError::INVALID_POW;
            return false;
        }
    }

    // 2. Check value at transition
    if (zeroBytes < 32) {
        uint32_t hashVal = 0;
        if (zeroBytes <= 29) {
            hashVal = (hash[zeroBytes] << 16) | (hash[zeroBytes+1] << 8) | hash[zeroBytes+2];
        } else if (zeroBytes == 30) {
            hashVal = (hash[30] << 16) | (hash[31] << 8);
        } else if (zeroBytes == 31) {
            hashVal = (hash[31] << 16);
        }

        if (hashVal > mantissa) {
            error = ValidationError::INVALID_POW;
            return false;
        }
    }
    
    return true; 
}

bool checkBlockHeader(
    const BlockHeader& header,
    uint32_t currentTime,
    uint32_t medianTimePast,
    ValidationError& error
) {
    if (header.version == 0) {
        error = ValidationError::INVALID_VERSION;
        return false;
    }
    // BIP-113: Median Time Past
    if (header.timestamp <= medianTimePast) {
        error = ValidationError::INVALID_TIMESTAMP;
        return false;
    }
    // Future time limit (2 hours)
    if (header.timestamp > currentTime + 7200) {
        error = ValidationError::INVALID_TIMESTAMP;
        return false;
    }
    return true;
}

bool checkMerkleRoot(
    const Block& block,
    ValidationError& error
) {
    if (block.computeMerkleRoot() != block.header.merkleRoot) {
        error = ValidationError::INVALID_MERKLE_ROOT;
        return false;
    }
    return true;
}

bool checkTransactionInputs(
    const Transaction& tx,
    const UTXODB& utxoDB,
    uint64_t currentHeight,
    ValidationError& error
) {
    if (tx.isCoinbase()) return true;

    uint64_t totalIn = 0;
    for (size_t i = 0; i < tx.inputs.size(); ++i) {
        const auto& in = tx.inputs[i];
        OutPoint op{in.prevTxid, in.prevIndex};
        UTXO utxo;
        if (!utxoDB.get(op, utxo)) {
            error = ValidationError::TX_MISSING_UTXO;
            return false;
        }

        // 1. Ownership Check (P2PKH)
        // scriptPubKey: DUP HASH160 <20-byte-hash> EQUALVERIFY CHECKSIG
        if (utxo.scriptPubKey.size() == 25 && 
            utxo.scriptPubKey[0] == 0x76 && utxo.scriptPubKey[1] == 0xa9 &&
            utxo.scriptPubKey[2] == 0x14 && utxo.scriptPubKey[23] == 0x88 &&
            utxo.scriptPubKey[24] == 0xac) 
        {
            Hash160 providedPubkeyHash = hash160(in.pubKey);
            bool match = true;
            for (int k = 0; k < 20; ++k) {
                if (providedPubkeyHash[k] != utxo.scriptPubKey[3 + k]) {
                    match = false;
                    break;
                }
            }
            if (!match) {
                std::cerr << "DEBUG: Ownership check FAILED! Signer is not the owner of UTXO " << i << std::endl;
                error = ValidationError::INVALID_SIGNATURE; // Or a specific OWNERSHIP_ERROR
                return false;
            }
        }

        // 2. Coinbase Maturity Check
        if (utxo.isCoinbase) {
            if (currentHeight < utxo.height + Consensus::COINBASE_MATURITY) {
                std::cerr << "DEBUG: Coinbase maturity check FAILED! Height: " << currentHeight << " required: " << utxo.height + 100 << std::endl;
                error = ValidationError::TX_COINBASE_MATURITY;
                return false;
            }
        }

        // 3. Signature Check
        if (!tx.verifyInput(i)) {
            error = ValidationError::INVALID_SIGNATURE;
            return false;
        }

        totalIn += utxo.amount;
    }

    // 3. Value Integrity
    uint64_t totalOut = tx.totalOutput();
    if (totalIn < totalOut) {
        error = ValidationError::TX_INPUT_VALUE_LESS_THAN_OUTPUT;
        return false;
    }

    return true;
}

bool checkBlock(
    const Block& block,
    const BlockHeader& prevHeader,
    const UTXODB& utxoDB,
    uint64_t currentHeight,
    uint32_t medianTimePast,
    uint32_t expectedBits,
    ValidationError& error
) {
    // 1. Header checks
    if (!checkBlockHeader(block.header, std::time(nullptr), medianTimePast, error)) return false;
    if (!checkBlockProofOfWork(block.header, error)) return false;
    if (block.header.prevBlockHash != prevHeader.hash()) {
        error = ValidationError::INVALID_PREV_BLOCK;
        return false;
    }
    if (block.header.height != prevHeader.height + 1) {
        error = ValidationError::INVALID_VERSION; // height mismatch
        return false;
    }
    
    // Round 10: Difficulty must match expected
    if (block.header.bits != expectedBits) {
        std::cerr << "DEBUG: Difficulty mismatch! Expected " << expectedBits << " got " << block.header.bits << std::endl;
        error = ValidationError::INVALID_POW;
        return false;
    }

    // 2. Structural checks
    if (!checkMerkleRoot(block, error)) return false;
    if (block.transactions.empty()) {
        error = ValidationError::EMPTY_BLOCK;
        return false;
    }
    if (!block.transactions[0].isCoinbase()) {
        error = ValidationError::INVALID_COINBASE;
        return false;
    }
    for (size_t i = 1; i < block.transactions.size(); ++i) {
        if (block.transactions[i].isCoinbase()) {
            error = ValidationError::MULTIPLE_COINBASE;
            return false;
        }
    }

    // 3. Stateful transaction checks & Round 10: SigOps
    size_t totalBlockSize = 0;
    uint32_t totalSigOps = 0;

    // First pass: Basic sanity + SigOps (Cheap)
    for (const auto& tx : block.transactions) {
        if (!checkTransactionBasic(tx, error)) return false;
        totalBlockSize += tx.serialize(true).size();
        
        // Count SigOps
        for (const auto& out : tx.outputs) {
            for (uint8_t op : out.scriptPubKey) if (op == 0xac) totalSigOps++;
        }
        for (const auto& in : tx.inputs) {
            for (uint8_t op : in.scriptSig) if (op == 0xac) totalSigOps++;
        }
    }

    if (totalBlockSize > Consensus::MAX_BLOCK_SIZE) {
        error = ValidationError::BLOCK_TOO_LARGE;
        return false;
    }
    if (totalSigOps > Consensus::MAX_SIGOPS_PER_BLOCK) {
        std::cerr << "DEBUG: SigOp limit exceeded: " << totalSigOps << std::endl;
        error = ValidationError::TX_TOO_LARGE; 
        return false;
    }

    // Second pass: Stateful checks (Expensive - Signatures)
    for (const auto& tx : block.transactions) {
        if (!checkTransactionInputs(tx, utxoDB, currentHeight, error)) return false;
    }

    return true;
}

// ============================================================================
// Connect block (apply state)
// ============================================================================

bool connectBlock(
    const Block& block,
    UTXODB& utxoDB,
    BlockDB* blockDB,
    ValidationError& error
) {
    // Basic structural checks
    if (block.transactions.empty()) {
        error = ValidationError::EMPTY_BLOCK;
        return false;
    }

    // Begin transaction
    std::cout << "DEBUG: Validation::connectBlock - Starting transaction at height " << block.header.height << "..." << std::endl;
    if (!utxoDB.beginTransaction()) {
        std::cerr << "DEBUG: Failed to begin transaction!" << std::endl;
        return false;
    }

    std::vector<UTXO> undoData;

    // Process transactions
    for (size_t txIndex = 0; txIndex < block.transactions.size(); ++txIndex) {
        const Transaction& tx = block.transactions[txIndex];

        // Stateful validation (CRITICAL: Rounds 1-8)
        if (!checkTransactionBasic(tx, error)) {
            utxoDB.rollbackTransaction();
            return false;
        }
        if (!checkTransactionInputs(tx, utxoDB, block.header.height, error)) {
            std::cerr << "DEBUG: Transaction " << txIndex << " failed stateful validation (Error: " << (int)error << ")" << std::endl;
            utxoDB.rollbackTransaction();
            return false;
        }

        if (!(txIndex == 0 && tx.isCoinbase())) {
            // Regular tx: spend inputs
            for (const auto& in : tx.inputs) {
                OutPoint op{in.prevTxid, in.prevIndex};
                UTXO prev;
                if (!utxoDB.get(op, prev)) {
                    // This should theoretically not happen if checkTransactionInputs passed, 
                    // but we check for extra safety.
                    utxoDB.rollbackTransaction();
                    error = ValidationError::TX_MISSING_UTXO;
                    return false;
                }
                // Save to undo data before erasing
                undoData.push_back(prev);
                if (!utxoDB.erase(op)) {
                    utxoDB.rollbackTransaction();
                    return false;
                }
            }
        }

        // Create new outputs (for both coinbase and regular tx)
        for (size_t i = 0; i < tx.outputs.size(); ++i) {
            OutPoint op{tx.txid(), static_cast<uint32_t>(i)};
            UTXO utxo;
            utxo.outpoint = op;
            utxo.amount = tx.outputs[i].amount;
            utxo.scriptPubKey = tx.outputs[i].scriptPubKey;
            utxo.isCoinbase = (txIndex == 0);
            utxo.height = block.header.height;
            if (!utxoDB.put(op, utxo)) {
                utxoDB.rollbackTransaction();
                return false;
            }
        }
    }

    // Save undo data
    if (blockDB && !blockDB->saveUndoData(block.header.height, undoData)) {
        std::cerr << "DEBUG: Failed to save undo data for block " << block.header.height << std::endl;
        utxoDB.rollbackTransaction();
        return false;
    }

    // Commit
    if (!utxoDB.commitTransaction()) {
        std::cerr << "DEBUG: Failed to commit transaction!" << std::endl;
        return false;
    }
    std::cout << "DEBUG: Block validation & connection SUCCESS!" << std::endl;

    return true;
}

// ============================================================================
// Disconnect block (reorg support)
// ============================================================================

bool disconnectBlock(
    const Block& block,
    UTXODB& utxoDB,
    BlockDB& blockDB,
    ValidationError& error
) {
    std::cout << "DEBUG: Disconnecting block " << block.header.height << "..." << std::endl;
    
    if (!utxoDB.beginTransaction()) return false;

    // 1. Remove outputs created by this block
    for (const auto& tx : block.transactions) {
        for (size_t i = 0; i < tx.outputs.size(); ++i) {
            OutPoint op{tx.txid(), static_cast<uint32_t>(i)};
            if (!utxoDB.erase(op)) {
                utxoDB.rollbackTransaction();
                return false;
            }
        }
    }

    // 2. Restore spent inputs from undo data
    std::vector<UTXO> undoData;
    if (!blockDB.loadUndoData(block.header.height, undoData)) {
        std::cerr << "DEBUG: Failed to load undo data for rollback of height " << block.header.height << std::endl;
        utxoDB.rollbackTransaction();
        return false;
    }

    for (const auto& utxo : undoData) {
        if (!utxoDB.put(utxo.outpoint, utxo)) {
            utxoDB.rollbackTransaction();
            return false;
        }
    }

    if (!utxoDB.commitTransaction()) return false;
    
    std::cout << "DEBUG: Successfully disconnected block " << block.header.height << std::endl;
    return true;
}

} // namespace Validation
} // namespace Bucks
