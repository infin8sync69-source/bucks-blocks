#ifndef UTXO_H
#define UTXO_H

#include <cstdint>

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

} // namespace Bucks

#endif // UTXO_H
