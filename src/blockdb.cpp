#include "blockdb.h"
#include "block.h"
#include "transaction.h"
#include "utxo.h"
#include <sqlite3.h>
#include <iostream>
#include <cstring>

namespace Bucks {

BlockDB::BlockDB(const std::string& path)
    : dbPath(path), db(nullptr) {}

BlockDB::~BlockDB() {
    if (db) sqlite3_close(db);
}

bool BlockDB::init() {
    std::lock_guard<std::mutex> lock(mtx);
    
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Failed to open blocks database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    return prepareSchema();
}

bool BlockDB::prepareSchema() {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS blocks ("
        "height INTEGER PRIMARY KEY,"
        "hash BLOB NOT NULL,"
        "data BLOB NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_hash ON blocks(hash);"
        "CREATE TABLE IF NOT EXISTS mempool ("
        "txid BLOB PRIMARY KEY,"
        "data BLOB NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS undo ("
        "height INTEGER PRIMARY KEY,"
        "data BLOB NOT NULL"
        ");";
    
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to create blocks table: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    
    return true;
}

bool BlockDB::saveBlock(const Block& block) {
    std::lock_guard<std::mutex> lock(mtx);
    
    // Serialize block to binary
    std::vector<uint8_t> serialized = block.serialize();
    Hash256 blockHash = block.getHash();
    
    const char* sql = "INSERT OR REPLACE INTO blocks (height, hash, data) VALUES (?, ?, ?);";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare saveBlock statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, block.header.height);
    sqlite3_bind_blob(stmt, 2, blockHash.data(), 32, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 3, serialized.data(), serialized.size(), SQLITE_TRANSIENT);
    
    std::cout << "DEBUG: Saving block " << block.header.height << " (size: " << serialized.size() << " bytes)" << std::endl;
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to save block: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    return true;
}

bool BlockDB::loadBlocks(std::vector<Block>& blocks) {
    std::lock_guard<std::mutex> lock(mtx);
    
    const char* sql = "SELECT data FROM blocks ORDER BY height ASC;";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare loadBlocks statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    blocks.clear();
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const uint8_t* data = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 0));
        int dataSize = sqlite3_column_bytes(stmt, 0);
        
        if (data && dataSize > 0) {
            std::cout << "DEBUG: Reading block data from DB (size: " << dataSize << " bytes)" << std::endl;
            std::vector<uint8_t> serialized(data, data + dataSize);
            try {
                Block block = Block::deserialize(serialized);
                blocks.push_back(block);
            } catch (const std::exception& e) {
                std::cerr << "DEBUG: Failed to deserialize block data of size " << dataSize << ": " << e.what() << std::endl;
                sqlite3_finalize(stmt);
                return false;
            }
        }
    }
    
    sqlite3_finalize(stmt);
    std::cout << "Loaded " << blocks.size() << " blocks from database" << std::endl;
    return true;
}

bool BlockDB::getBlockByHeight(uint32_t height, Block& block) {
    std::lock_guard<std::mutex> lock(mtx);
    
    const char* sql = "SELECT data FROM blocks WHERE height = ?;";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, height);
    
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const uint8_t* data = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 0));
        int dataSize = sqlite3_column_bytes(stmt, 0);
        
        if (data && dataSize > 0) {
            std::vector<uint8_t> serialized(data, data + dataSize);
            try {
                block = Block::deserialize(serialized);
                found = true;
            } catch (const std::exception& e) {
                std::cerr << "Failed to deserialize block: " << e.what() << std::endl;
            }
        }
    }
    
    sqlite3_finalize(stmt);
    return found;
}

bool BlockDB::getBlockByHash(const Hash256& hash, Block& block) {
    std::lock_guard<std::mutex> lock(mtx);
    
    const char* sql = "SELECT data FROM blocks WHERE hash = ?;";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_blob(stmt, 1, hash.data(), 32, SQLITE_STATIC);
    
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const uint8_t* data = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 0));
        int dataSize = sqlite3_column_bytes(stmt, 0);
        
        if (data && dataSize > 0) {
            std::vector<uint8_t> serialized(data, data + dataSize);
            try {
                block = Block::deserialize(serialized);
                found = true;
            } catch (const std::exception& e) {
                std::cerr << "Failed to deserialize block: " << e.what() << std::endl;
            }
        }
    }
    
    sqlite3_finalize(stmt);
    return found;
}

size_t BlockDB::getBlockCount() const {
    std::lock_guard<std::mutex> lock(mtx);
    
    sqlite3_stmt* stmt = nullptr;
    size_t count = 0;
    
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM blocks;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int64(stmt, 0);
        }
    }
    
    sqlite3_finalize(stmt);
    return count;
}

bool BlockDB::deleteAllBlocks() {
    std::lock_guard<std::mutex> lock(mtx);
    
    const char* sql = "DELETE FROM blocks;";
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to delete blocks: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    
    return true;
}

bool BlockDB::addMempoolTx(const Transaction& tx) {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<uint8_t> serialized = tx.serialize(true);
    Hash256 txid = tx.txid();
    
    const char* sql = "INSERT OR REPLACE INTO mempool (txid, data) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    
    sqlite3_bind_blob(stmt, 1, txid.data(), 32, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 2, serialized.data(), serialized.size(), SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BlockDB::removeMempoolTx(const Hash256& txid) {
    std::lock_guard<std::mutex> lock(mtx);
    const char* sql = "DELETE FROM mempool WHERE txid = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    
    sqlite3_bind_blob(stmt, 1, txid.data(), 32, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BlockDB::loadMempool(std::vector<Transaction>& txs) {
    std::lock_guard<std::mutex> lock(mtx);
    const char* sql = "SELECT data FROM mempool;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    
    txs.clear();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const uint8_t* blob = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 0));
        int size = sqlite3_column_bytes(stmt, 0);
        if (blob && size > 0) {
            std::vector<uint8_t> data(blob, blob + size);
            try {
                txs.push_back(Transaction::deserialize(data));
            } catch (...) {}
        }
    }
    sqlite3_finalize(stmt);
    return true;
}

bool BlockDB::deleteAllMempool() {
    std::lock_guard<std::mutex> lock(mtx);
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, "DELETE FROM mempool;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

static std::vector<uint8_t> serializeUndo(const std::vector<UTXO>& undos) {
    std::vector<uint8_t> buf;
    // Simple serialization: [count (8 bytes)][ (outpoint + utxo data) ...]
    uint64_t count = undos.size();
    for (int i=0; i<8; ++i) buf.push_back((count >> (8*i)) & 0xff);
    
    for (const auto& u : undos) {
        // outpoint
        buf.insert(buf.end(), u.outpoint.txid.begin(), u.outpoint.txid.end());
        uint32_t idx = u.outpoint.index;
        for (int i=0; i<4; ++i) buf.push_back((idx >> (8*i)) & 0xff);
        // utxo
        uint64_t amt = u.amount;
        for (int i=0; i<8; ++i) buf.push_back((amt >> (8*i)) & 0xff);
        uint64_t scriptLen = u.scriptPubKey.size();
        for (int i=0; i<8; ++i) buf.push_back((scriptLen >> (8*i)) & 0xff);
        buf.insert(buf.end(), u.scriptPubKey.begin(), u.scriptPubKey.end());
        buf.push_back(u.isCoinbase ? 1 : 0);
    }
    return buf;
}

static std::vector<UTXO> deserializeUndo(const std::vector<uint8_t>& data) {
    std::vector<UTXO> undos;
    if (data.size() < 8) return undos;
    size_t pos = 0;
    uint64_t count = 0;
    for (int i=0; i<8; ++i) count |= (static_cast<uint64_t>(data[pos++]) << (8*i));
    
    for (uint64_t i=0; i<count && pos < data.size(); ++i) {
        UTXO u;
        std::copy(data.begin() + pos, data.begin() + pos + 32, u.outpoint.txid.begin());
        pos += 32;
        u.outpoint.index = 0;
        for (int j=0; j<4; ++j) u.outpoint.index |= (static_cast<uint32_t>(data[pos++]) << (8*j));
        u.amount = 0;
        for (int j=0; j<8; ++j) u.amount |= (static_cast<uint64_t>(data[pos++]) << (8*j));
        uint64_t scriptLen = 0;
        for (int j=0; j<8; ++j) scriptLen |= (static_cast<uint64_t>(data[pos++]) << (8*j));
        u.scriptPubKey.assign(data.begin() + pos, data.begin() + pos + scriptLen);
        pos += scriptLen;
        u.isCoinbase = (data[pos++] != 0);
        undos.push_back(u);
    }
    return undos;
}

bool BlockDB::saveUndoData(uint32_t height, const std::vector<UTXO>& undoUTXOs) {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<uint8_t> serialized = serializeUndo(undoUTXOs);
    const char* sql = "INSERT OR REPLACE INTO undo (height, data) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, height);
    sqlite3_bind_blob(stmt, 2, serialized.data(), serialized.size(), SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BlockDB::loadUndoData(uint32_t height, std::vector<UTXO>& undoUTXOs) {
    std::lock_guard<std::mutex> lock(mtx);
    const char* sql = "SELECT data FROM undo WHERE height = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, height);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const uint8_t* blob = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 0));
        int size = sqlite3_column_bytes(stmt, 0);
        if (blob && size > 0) {
            std::vector<uint8_t> data(blob, blob + size);
            undoUTXOs = deserializeUndo(data);
        }
    }
    sqlite3_finalize(stmt);
    return true;
}


} // namespace Bucks
