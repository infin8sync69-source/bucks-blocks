#ifndef UTXO_H
#define UTXO_H

#include <cstdint>
#include <vector>
#include "crypto.h"

namespace Bucks {

// Outpoint = txid + output index
struct OutPoint {
    Hash256 txid;
    uint32_t index;

    bool operator==(const OutPoint& o) const {
        return txid == o.txid && index == o.index;
    }
};

// Unspent Transaction Output
class UTXO {
public:
    OutPoint outpoint;
    uint64_t amount;
    std::vector<uint8_t> scriptPubKey;
    bool isCoinbase = false;
    uint32_t height = 0;

    UTXO() = default;

    UTXO(
        const Hash256& txid,
        uint32_t index,
        uint64_t amount,
        const std::vector<uint8_t>& script
    );

    // Convenience constructor for tests/examples that provide string ids/scripts
    UTXO(const std::string& txidStr, uint32_t index, uint64_t amount, const std::string& scriptStr);

};

} // namespace Bucks

#endif // UTXO_H
