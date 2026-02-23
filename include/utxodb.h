#ifndef UTXODB_H
#define UTXODB_H

#include <string>
#include <vector>
#include <mutex>
#include "utxo.h"
#include "crypto.h"

struct sqlite3;

namespace Bucks {

class UTXODB {
public:
    explicit UTXODB(const std::string& path = "utxodb.sqlite");
    ~UTXODB();

    bool init();
    bool put(const OutPoint& outpoint, const UTXO& utxo);
    bool erase(const OutPoint& outpoint);
    bool get(const OutPoint& outpoint, UTXO& out) const;
    std::vector<UTXO> getUTXOsByAddress(const std::string& address) const;
    size_t getCount() const;

    // Batch/transaction semantics
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

private:
    std::string dbPath;
    sqlite3* db;
    mutable std::mutex mtx;
    bool prepareSchema();
};

} // namespace Bucks

#endif // UTXODB_H
