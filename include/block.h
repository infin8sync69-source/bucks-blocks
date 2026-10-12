#ifndef BLOCK_H
#define BLOCK_H

#include <vector>
#include <cstdint>
#include "blockheader.h"
#include "transaction.h"
#include "merkle.h"

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

    // ---- Hashing ----
    /**
     * Get the block hash (double-SHA256 of serialized header)
     */
    Hash256 getHash() const;

    // ---- Merkle tree ----
    /**
     * Compute merkle root from transactions. *mutated (if given) is set when
     * the transaction list contains a duplicated pair (section 4.6).
     */
    Hash256 computeMerkleRoot(bool* mutated = nullptr) const;

    // ---- Accessors ----
    uint32_t getHeight() const { return header.height; }
    const Hash256& getPrevHash() const { return header.prevBlockHash; }
    uint32_t getNonce() const { return header.nonce; }
    uint32_t getTimestamp() const { return header.timestamp; }
    uint32_t getBits() const { return header.bits; }
    size_t getTxCount() const { return transactions.size(); }

    // ---- Serialization ----
    /**
     * Wire and disk format (section 4.5): 84-byte header, CompactSize
     * transaction count, then the transactions back to back. No magic bytes,
     * no length prefixes.
     */
    std::vector<uint8_t> serialize() const;

    /**
     * Throws ParseError. The data length is checked against MAX_BLOCK_SIZE
     * before anything is parsed; trailing bytes are rejected.
     */
    static Block deserialize(const std::vector<uint8_t>& data);
};

} // namespace Bucks

#endif // BLOCK_H
