#include "block.h"
#include "merkle.h"
#include "crypto.h"
#include "consensus.h"
#include "serialize.h"


namespace Bucks {

// ------------------------------------------------------------
// BlockHeader implementation
// ------------------------------------------------------------

BlockHeader::BlockHeader()
    : version(Consensus::BLOCK_VERSION), height(0), timestamp(0), bits(0), nonce(0) {
    prevBlockHash.fill(0);
    merkleRoot.fill(0);
}

std::vector<uint8_t> BlockHeader::serialize() const {
    Writer w;
    w.u32(version).u32(height).hash256(prevBlockHash).hash256(merkleRoot)
        .u32(timestamp).u32(bits).u32(nonce);
    return w.take();  // 84 bytes
}

BlockHeader BlockHeader::parse(Reader& r) {
    BlockHeader h;
    h.version = r.u32();
    h.height = r.u32();
    h.prevBlockHash = r.hash256();
    h.merkleRoot = r.hash256();
    h.timestamp = r.u32();
    h.bits = r.u32();
    h.nonce = r.u32();
    return h;
}

BlockHeader BlockHeader::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() != Consensus::BLOCK_HEADER_SIZE) throw ParseError("bad header size");
    Reader r(data);
    BlockHeader h = parse(r);
    r.requireEnd();
    return h;
}

Hash256 BlockHeader::hash() const {
    return doubleSha256(serialize());
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

// ------------------------------------------------------------
// Hashing
// ------------------------------------------------------------

Hash256 Block::getHash() const {
    return header.hash();
}

// ------------------------------------------------------------
// Merkle tree
// ------------------------------------------------------------

Hash256 Block::computeMerkleRoot(bool* mutated) const {
    std::vector<Hash256> txids;
    txids.reserve(transactions.size());
    for (const auto& tx : transactions) txids.push_back(tx.txid());
    return merkleRoot(txids, mutated);
}

// ------------------------------------------------------------
// Serialization (section 4.5)
// ------------------------------------------------------------

std::vector<uint8_t> Block::serialize() const {
    Writer w;
    w.bytes(header.serialize().data(), Consensus::BLOCK_HEADER_SIZE);
    w.compactSize(transactions.size());
    for (const auto& tx : transactions) {
        std::vector<uint8_t> raw = tx.serialize();
        w.bytes(raw.data(), raw.size());
    }
    return w.take();
}

Block Block::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() > Consensus::MAX_BLOCK_SIZE) throw ParseError("block too large");
    Reader r(data);
    BlockHeader header = BlockHeader::parse(r);

    // Smallest possible transaction is 60 bytes (spec section 11.1, rule R2).
    constexpr size_t MIN_TX_SIZE = 60;
    uint64_t txCount = r.compactSize(Consensus::MAX_BLOCK_TXS_PARSE);
    if (txCount > r.remaining() / MIN_TX_SIZE) throw ParseError("transaction count exceeds data");

    std::vector<Transaction> txs;
    txs.reserve(size_t(txCount));
    for (uint64_t i = 0; i < txCount; ++i) txs.push_back(Transaction::parse(r));
    r.requireEnd();
    return Block(header, txs);
}

} // namespace Bucks
