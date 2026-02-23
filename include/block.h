#ifndef BLOCK_H
#define BLOCK_H

#include <vector>
#include <cstdint>
#include "blockheader.h"
#include "transaction.h"

namespace Bucks {

/**
 * Block (CONSENSUS-CRITICAL)
 *
 * Contains a block header and list of transactions.
 * The header is hashed for Proof-of-Work.
 */
class Block {
public:
    BlockHeader header;                    // Block header (consensus-critical)
    std::vector<Transaction> transactions; // Transactions in block

    Block();
    Block(const BlockHeader& hdr, const std::vector<Transaction>& txs);
    Block(uint32_t height, const Hash256& prevHash, const std::vector<Transaction>& txs); // convenience

    // ---- Hashing ----
    /**
     * Get the block hash (double-SHA256 of serialized header)
     */
    Hash256 getHash() const;

    // ---- Merkle tree ----
    /**
     * Compute merkle root from transactions
     */
    Hash256 computeMerkleRoot() const;

    // ---- Validation ----
    bool isValid() const;

    // ---- Accessors ----
    uint32_t getHeight() const { return header.height; }
    const Hash256& getPrevHash() const { return header.prevBlockHash; }
    uint32_t getNonce() const { return header.nonce; }
    uint32_t getTimestamp() const { return header.timestamp; }
    uint32_t getBits() const { return header.bits; }
    size_t getTxCount() const { return transactions.size(); }

    // ---- Serialization ----
    /**
     * Serialize block to binary format for storage
     */
    std::vector<uint8_t> serialize() const;
    
    /**
     * Deserialize block from binary format
     */
    static Block deserialize(const std::vector<uint8_t>& data);

    // Public fields for compatibility
    Hash256 hash;
    uint32_t difficulty;
};

} // namespace Bucks

#endif // BLOCK_H
