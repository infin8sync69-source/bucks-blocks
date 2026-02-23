#ifndef BLOCKHEADER_H
#define BLOCKHEADER_H

#include <array>
#include <vector>
#include <cstdint>

#include "crypto.h"

namespace Bucks {

/**
 * BlockHeader (Consensus-Critical)
 *
 * This structure defines the exact data that is hashed
 * for Proof-of-Work and chain selection.
 *
 * Any change here is a HARD FORK.
 */
struct BlockHeader {
    uint32_t version;          // block version
    uint32_t height;           // block height in chain
    Hash256 prevBlockHash;     // hash of previous block header
    Hash256 merkleRoot;        // merkle root of transactions
    uint32_t timestamp;        // UNIX epoch time
    uint32_t bits;             // compact PoW target (nBits)
    uint32_t nonce;            // PoW nonce

    BlockHeader();

    /**
     * Serialize header into canonical byte form (80 bytes)
     * Used for hashing and network transmission.
     */
    std::vector<uint8_t> serialize() const;

    /**
     * Double-SHA256 hash of the serialized header.
     * This is the block hash.
     */
    Hash256 hash() const;
};

} // namespace Bucks

#endif // BLOCKHEADER_H
