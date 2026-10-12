#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

#include "block.h"
#include "chainparams.h"
#include "utxodb.h"
#include "blockdb.h"
#include "blockheader.h"

namespace Bucks {

constexpr size_t MAX_MEMPOOL_COUNT = 50000;
constexpr size_t MAX_MEMPOOL_SIZE = 300 * 1024 * 1024; // 300MB

/**
 * In-memory block index entry
 */
struct BlockIndex {
    Hash256 hash;
    Hash256 prev;
    uint32_t height;
    uint32_t bits;
    uint64_t chainWork;
};

/**
 * Blockchain (CONSENSUS STATE MANAGER)
 *
 * - No networking
 * - No mining
 * - No wallets
 */
class Blockchain {
public:
    // `params` selects the network; the default is regtest (tests). The node
    // passes the network chosen with --network. Throws std::runtime_error if
    // the genesis check fails or the stored chain belongs to another network.
    Blockchain(const std::string& utxoPath = "utxodb.sqlite",
               const std::string& blockPath = "blockchain.db",
               const ChainParams& params = chainParams(NetworkId::REGTEST));

    const ChainParams& params() const { return params_; }

    // ---- Chain state ----
    const Block* tip() const;
    uint32_t height() const;

    // ---- Block processing ----
    bool addBlock(const Block& block);

    // ---- Block lookup ----
    const Block* getBlock(const Hash256& hash) const;
    const Block* getBlockByHeight(uint32_t height) const;

    // ---- UTXO access ----
    const UTXODB& utxo() const;

    // ---- Genesis ----
    static const Block& genesis();
    // Hash of block 0, committed to by every signature (sighash v1).
    Hash256 genesisHash() const { return params_.genesisHash; }
    
    // ---- Median Time Past ----
    uint32_t getMedianTimePast() const;

    // ---- Difficulty Retargeting ----
    uint32_t getNextWorkRequired(uint32_t height, const Hash256& prevHash) const;
    static uint32_t adjustBits(uint32_t bits, int64_t actual, int64_t target);

    // ---- Reorg support ----
    bool disconnectTip();

    // Compatibility helpers
    const Block& getLastBlock() const { return *tip(); }
    uint32_t getChainHeight() const { return height(); }
    bool isValidBlock(const Block& block, const Block& prev) const;

    // ---- Mempool ----
    bool addToMempool(const Transaction& tx);
    void removeFromMempool(const Hash256& txid);
    const std::vector<Transaction>& getMempool() const;
    void clearMempool();

    // Public fields for compatibility
    uint32_t difficulty = 0x1d00ffff; // default
    std::unique_ptr<UTXODB> utxoDB; // made public for tests
    std::unique_ptr<BlockDB> blockDB; // block persistence

    // Gold Standard
    uint64_t getGoldReserve() const { return goldReserve; }
    uint64_t getCirculatingSupply() const;

private:
    const ChainParams& params_;

    // Block storage
    std::vector<Block> blocks;                    // All blocks in order
    size_t tipIndex;                              // Index of best tip
    
    // Block index: hash → index in blocks vector
    std::unordered_map<Hash256, size_t> blockIndex;

    // Gold State
    uint64_t goldReserve = 0; // In grams

    // Mempool
    std::vector<Transaction> mempool;

    // Internal helpers
    bool connectBlock(const Block& block);
    bool disconnectBlock(const Block& block);
    bool loadPersistedBlocks();
};

} // namespace Bucks

#endif // BLOCKCHAIN_H
