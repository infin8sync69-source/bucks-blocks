#ifndef BLOCKDB_H
#define BLOCKDB_H

#include <string>
#include <vector>
#include <tuple>
#include <mutex>
#include "block.h"
#include "crypto.h"
#include "utxo.h"

struct sqlite3;

namespace Bucks {

/**
 * A single governance event decoded from an OP_RETURN output.
 */
struct GovernanceEvent {
    std::string id;          // Unique record ID (txhash:height:type)
    uint32_t    blockHeight; // Block the event was confirmed in
    uint8_t     eventType;   // GOV_VOTE / GOV_PROP / etc.
    std::vector<uint8_t> payload; // Remaining bytes after type byte
    Hash256     txHash;      // Transaction that emitted the event
};

/**
 * BlockDB - Persistent storage for blockchain blocks
 * Stores blocks in SQLite database with indexed lookups
 */
class BlockDB {
public:
    explicit BlockDB(const std::string& path = "blockchain.db");
    ~BlockDB();

    // Initialize database and create schema
    bool init();

    // Block operations
    bool saveBlock(const Block& block);
    bool loadBlocks(std::vector<Block>& blocks);
    bool getBlockByHeight(uint32_t height, Block& block);
    bool getBlockByHash(const Hash256& hash, Block& block);
    
    // Mempool operations
    bool addMempoolTx(const Transaction& tx);
    bool removeMempoolTx(const Hash256& txid);
    bool loadMempool(std::vector<Transaction>& txs);
    bool deleteAllMempool();

    // Undo Data (for reorgs)
    bool saveUndoData(uint32_t height, const std::vector<UTXO>& undoUTXOs);
    bool loadUndoData(uint32_t height, std::vector<UTXO>& undoUTXOs);

    // FIX: Gold reserve persistence — Bucks v1 security patch
    // Without persistence, goldReserve resets to 0 on node restart.
    bool saveGoldReserve(uint64_t reserve);
    bool loadGoldReserve(uint64_t& reserve);

    // Governance events (immutable log — never reversed on reorg)
    bool saveGovernanceEvent(uint32_t height, uint8_t eventType,
                             const std::vector<uint8_t>& payload,
                             const Hash256& txHash);
    bool loadGovernanceEvents(uint32_t height,
                              std::vector<GovernanceEvent>& events);

    // Utilities
    size_t getBlockCount() const;
    bool deleteAllBlocks(); // For testing/reset

private:
    std::string dbPath;
    sqlite3* db;
    mutable std::mutex mtx;
    
    bool prepareSchema();
};

} // namespace Bucks

#endif // BLOCKDB_H
