#include "block.h"
#include "merkle.h"
#include "crypto.h"

#include <ctime>
#include <stdexcept>
#include <iostream>

namespace Bucks {

// ------------------------------------------------------------
// BlockHeader implementation
// ------------------------------------------------------------

BlockHeader::BlockHeader()
    : version(1), height(0), timestamp(0), bits(0), nonce(0) {
    prevBlockHash.fill(0);
    merkleRoot.fill(0);
}

std::vector<uint8_t> BlockHeader::serialize() const {
    std::vector<uint8_t> data;
    data.reserve(84);
    
    // version (4)
    data.push_back(version & 0xFF);
    data.push_back((version >> 8) & 0xFF);
    data.push_back((version >> 16) & 0xFF);
    data.push_back((version >> 24) & 0xFF);
    
    // height (4)
    data.push_back(height & 0xFF);
    data.push_back((height >> 8) & 0xFF);
    data.push_back((height >> 16) & 0xFF);
    data.push_back((height >> 24) & 0xFF);
    
    // prevHash (32)
    data.insert(data.end(), prevBlockHash.begin(), prevBlockHash.end());
    
    // merkleRoot (32)
    data.insert(data.end(), merkleRoot.begin(), merkleRoot.end());
    
    // timestamp (4)
    data.push_back(timestamp & 0xFF);
    data.push_back((timestamp >> 8) & 0xFF);
    data.push_back((timestamp >> 16) & 0xFF);
    data.push_back((timestamp >> 24) & 0xFF);
    
    // bits (4)
    data.push_back(bits & 0xFF);
    data.push_back((bits >> 8) & 0xFF);
    data.push_back((bits >> 16) & 0xFF);
    data.push_back((bits >> 24) & 0xFF);
    
    // nonce (4)
    data.push_back(nonce & 0xFF);
    data.push_back((nonce >> 8) & 0xFF);
    data.push_back((nonce >> 16) & 0xFF);
    data.push_back((nonce >> 24) & 0xFF);
    
    return data;
}

Hash256 BlockHeader::hash() const {
    auto data = serialize();
    return doubleSha256(data);
}

// ------------------------------------------------------------
// Constructors
// ------------------------------------------------------------

Block::Block() {
    header = BlockHeader();
}

Block::Block(const BlockHeader& hdr, const std::vector<Transaction>& txs)
    : header(hdr), transactions(txs) {
}

Block::Block(uint32_t height, const Hash256& prevHash, const std::vector<Transaction>& txs)
    : transactions(txs) {
    header.version = 1;
    header.height = height;
    header.prevBlockHash = prevHash;
    header.timestamp = std::time(nullptr);
    header.bits = 0x1d00ffff; // default difficulty
    header.nonce = 0;
    header.merkleRoot = computeMerkleRoot();
}

// ------------------------------------------------------------
// Hashing
// ------------------------------------------------------------

Hash256 Block::getHash() const {
    return header.hash();
}

// ------------------------------------------------------------
// Merkle tree
// ------------------------------------------------------------

Hash256 Block::computeMerkleRoot() const {
    if (transactions.empty()) {
        return Hash256{};
    }

    std::vector<Hash256> txids;
    txids.reserve(transactions.size());

    for (const auto& tx : transactions) {
        txids.push_back(tx.txid());
    }

    MerkleTree tree(txids);
    return tree.getRoot();
}

// ------------------------------------------------------------
// Validation
// ------------------------------------------------------------

bool Block::isValid() const {
    // Check merkle root matches
    if (computeMerkleRoot() != header.merkleRoot)
        return false;

    // Check hash matches
    if (getHash() != header.hash())
        return false;

    // Transactions must not be empty
    if (transactions.empty())
        return false;

    // First transaction must be coinbase
    if (!transactions[0].isCoinbase())
        return false;

    // Validate all non-coinbase transactions
    for (size_t i = 1; i < transactions.size(); ++i) {
        if (!transactions[i].verify())
            return false;
    }

    return true;
}

// ------------------------------------------------------------
// Serialization
// ------------------------------------------------------------

std::vector<uint8_t> Block::serialize() const {
    std::vector<uint8_t> data;
    
    // Magic bytes (4)
    data.push_back(0xF9);
    data.push_back(0xBE);
    data.push_back(0xB4);
    data.push_back(0xD9);
    
    // Block header (serialize as bytes)
    auto headerData = header.serialize();
    std::cout << "DEBUG: Block::serialize - Header size: " << headerData.size() << " bytes" << std::endl;
    data.insert(data.end(), headerData.begin(), headerData.end());
    
    // Transaction count (4 bytes)
    uint32_t txCount = transactions.size();
    std::cout << "DEBUG: Block::serialize - TX count: " << txCount << std::endl;
    data.push_back((txCount) & 0xFF);
    data.push_back((txCount >> 8) & 0xFF);
    data.push_back((txCount >> 16) & 0xFF);
    data.push_back((txCount >> 24) & 0xFF);
    
    // Each transaction
    for (size_t i = 0; i < transactions.size(); ++i) {
        auto txData = transactions[i].serialize(false);
        // TX size (4 bytes)
        uint32_t txSize = txData.size();
        std::cout << "DEBUG: Block::serialize - TX " << i << " size: " << txSize << " bytes" << std::endl;
        data.push_back((txSize) & 0xFF);
        data.push_back((txSize >> 8) & 0xFF);
        data.push_back((txSize >> 16) & 0xFF);
        data.push_back((txSize >> 24) & 0xFF);
        // TX data
        data.insert(data.end(), txData.begin(), txData.end());
    }
    
    std::cout << "DEBUG: Block::serialize - Total size: " << data.size() << " bytes" << std::endl;
    return data;
}

Block Block::deserialize(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    
    // Check magic bytes
    if (data.size() < 4 || 
        data[0] != 0xF9 || data[1] != 0xBE || 
        data[2] != 0xB4 || data[3] != 0xD9) {
        throw std::runtime_error("Invalid block magic bytes");
    }
    offset += 4;
    
    // Deserialize header (84 bytes)
    if (data.size() < offset + 84) {
        throw std::runtime_error("Invalid block data: header too short");
    }
    
    BlockHeader header;
    header.version = data[offset] | (data[offset+1] << 8) | (data[offset+2] << 16) | (data[offset+3] << 24);
    offset += 4;
    header.height = data[offset] | (data[offset+1] << 8) | (data[offset+2] << 16) | (data[offset+3] << 24);
    offset += 4;
    std::copy(data.begin() + offset, data.begin() + offset + 32, header.prevBlockHash.begin());
    offset += 32;
    std::copy(data.begin() + offset, data.begin() + offset + 32, header.merkleRoot.begin());
    offset += 32;
    header.timestamp = data[offset] | (data[offset+1] << 8) | (data[offset+2] << 16) | (data[offset+3] << 24);
    offset += 4;
    header.bits = data[offset] | (data[offset+1] << 8) | (data[offset+2] << 16) | (data[offset+3] << 24);
    offset += 4;
    header.nonce = data[offset] | (data[offset+1] << 8) | (data[offset+2] << 16) | (data[offset+3] << 24);
    offset += 4;
    
    // Transaction count (4 bytes)
    if (data.size() < offset + 4) {
        throw std::runtime_error("Invalid block data: missing tx count");
    }
    
    uint32_t txCount = data[offset] | (data[offset+1] << 8) | 
                       (data[offset+2] << 16) | (data[offset+3] << 24);
    offset += 4;
    
    // Deserialize transactions
    std::vector<Transaction> transactions;
    transactions.reserve(txCount);
    
    for (uint32_t i = 0; i < txCount; ++i) {
        // TX size (4 bytes)
        if (data.size() < offset + 4) {
            throw std::runtime_error("Invalid block data: missing tx size");
        }
        
        uint32_t txSize = data[offset] | (data[offset+1] << 8) | 
                          (data[offset+2] << 16) | (data[offset+3] << 24);
        offset += 4;
        
        // TX data
        if (data.size() < offset + txSize) {
            throw std::runtime_error("Invalid block data: tx data truncated");
        }
        
        std::vector<uint8_t> txData(data.begin() + offset, data.begin() + offset + txSize);
        transactions.push_back(Transaction::deserialize(txData));
        offset += txSize;
    }
    
    return Block(header, transactions);
}

} // namespace Bucks
