#ifndef BLOCKHEADER_H
#define BLOCKHEADER_H

#include <array>
#include <vector>
#include <cstdint>

#include "crypto.h"
#include "serialize.h"

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
     * Serialize header into canonical byte form (exactly 84 bytes).
     * Used for hashing and network transmission.
     */
    std::vector<uint8_t> serialize() const;

    /** Parse exactly 84 bytes; throws ParseError otherwise. */
    static BlockHeader deserialize(const std::vector<uint8_t>& data);
    static BlockHeader parse(Reader& r);

    /**
     * Double-SHA256 hash of the serialized header.
     * This is the block hash.
     */
    Hash256 hash() const;
};

} // namespace Bucks

#endif // BLOCKHEADER_H
