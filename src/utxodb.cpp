#include "utxodb.h"
#include <sqlite3.h>
#include <cstring>
#include <iostream>
#include "crypto.h"

namespace Bucks {

// ------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------

UTXODB::UTXODB(const std::string& path)
    : dbPath(path), db(nullptr) {}

UTXODB::~UTXODB() {
    if (db) sqlite3_close(db);
}

bool UTXODB::init() {
    std::lock_guard<std::mutex> lock(mtx);
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK)
        return false;
    return prepareSchema();
}

// ------------------------------------------------------------
// Schema
// ------------------------------------------------------------

bool UTXODB::prepareSchema() {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS utxos ("
        "txid BLOB NOT NULL,"
        "vout INTEGER NOT NULL,"
        "amount INTEGER NOT NULL,"
        "script BLOB NOT NULL,"
        "is_coinbase INTEGER NOT NULL DEFAULT 0,"
        "height INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY (txid, vout)"
        ");";

    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

// ------------------------------------------------------------
// Core operations
// ------------------------------------------------------------

bool UTXODB::put(const OutPoint& outpoint, const UTXO& utxo) {
    std::lock_guard<std::mutex> lock(mtx);

    const char* sql =
        "INSERT OR REPLACE INTO utxos "
        "(txid, vout, amount, script, is_coinbase, height) VALUES (?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "DEBUG: UTXODB::put - Failed to prepare SQL: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    sqlite3_bind_blob(stmt, 1,
        utxo.outpoint.txid.data(), 32, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, utxo.outpoint.index);
    sqlite3_bind_int64(stmt, 3, utxo.amount);
    sqlite3_bind_blob(stmt, 4,
        utxo.scriptPubKey.data(),
        utxo.scriptPubKey.size(),
        SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, utxo.isCoinbase ? 1 : 0);
    sqlite3_bind_int(stmt, 6, utxo.height);

    int rc = sqlite3_step(stmt);
    bool ok = (rc == SQLITE_DONE);
    if (!ok) {
        std::cerr << "DEBUG: UTXODB::put - Failed to execute INSERT: " << sqlite3_errmsg(db) 
                  << " (rc=" << rc << ")" << std::endl;
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool UTXODB::erase(const OutPoint& outpoint) {
    std::lock_guard<std::mutex> lock(mtx);

    const char* sql =
        "DELETE FROM utxos WHERE txid = ? AND vout = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_blob(stmt, 1,
        outpoint.txid.data(), 32, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, outpoint.index);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool UTXODB::get(const OutPoint& outpoint, UTXO& out) const {
    std::lock_guard<std::mutex> lock(mtx);

    const char* sql =
        "SELECT amount, script, is_coinbase, height FROM utxos WHERE txid = ? AND vout = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_blob(stmt, 1,
        outpoint.txid.data(), 32, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, outpoint.index);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false;
    }

    out.outpoint = outpoint;
    out.amount = sqlite3_column_int64(stmt, 0);

    const uint8_t* blob =
        static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 1));
    int len = sqlite3_column_bytes(stmt, 1);
    out.scriptPubKey.assign(blob, blob + len);
    out.isCoinbase = (sqlite3_column_int(stmt, 2) != 0);
    out.height = sqlite3_column_int(stmt, 3);

    sqlite3_finalize(stmt);
    return true;
}

size_t UTXODB::getCount() const {
    std::lock_guard<std::mutex> lock(mtx);

    sqlite3_stmt* stmt = nullptr;
    size_t n = 0;

    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM utxos;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            n = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return n;
}

// ------------------------------------------------------------
// Transactions (block atomicity)
// ------------------------------------------------------------

bool UTXODB::beginTransaction() {
    return sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool UTXODB::commitTransaction() {
    return sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool UTXODB::rollbackTransaction() {
    return sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

std::vector<UTXO> UTXODB::getUTXOsByAddress(const std::string& address) const {
    std::vector<UTXO> result;
    std::lock_guard<std::mutex> lock(mtx);

    const char* sql = "SELECT txid, vout, amount, script FROM utxos";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        // Get data
        const void* txidData = sqlite3_column_blob(stmt, 0);
        int txidSize = sqlite3_column_bytes(stmt, 0);
        uint32_t vout = sqlite3_column_int(stmt, 1);
        uint64_t amount = sqlite3_column_int64(stmt, 2);
        const void* scriptData = sqlite3_column_blob(stmt, 3);
        int scriptSize = sqlite3_column_bytes(stmt, 3);

        if (txidSize != 32 || scriptSize < 25) continue;

        Hash256 txid;
        memcpy(txid.data(), txidData, 32);

        std::vector<uint8_t> script(scriptSize);
        memcpy(script.data(), scriptData, scriptSize);

        // Check if P2PKH script
        if (script.size() == 25 && script[0] == 0x76 && script[1] == 0xa9 && script[2] == 0x14 &&
            script[23] == 0x88 && script[24] == 0xac) {
            // Extract hash160
            std::vector<uint8_t> hash160(script.begin() + 3, script.begin() + 23);
            // Base58 encode with version 0
            std::string computedAddress = base58CheckEncode(0, hash160);
            if (computedAddress == address) {
                UTXO utxo;
                utxo.outpoint.txid = txid;
                utxo.outpoint.index = vout;
                utxo.amount = amount;
                utxo.scriptPubKey = script;
                result.push_back(utxo);
            }
        }
    }

    sqlite3_finalize(stmt);
    return result;
}

} // namespace Bucks
