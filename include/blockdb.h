#ifndef BLOCKDB_H
#define BLOCKDB_H

#include <string>
#include <vector>
#include <mutex>
#include "block.h"
#include "crypto.h"
#include "utxo.h"

struct sqlite3;

namespace Bucks {

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
