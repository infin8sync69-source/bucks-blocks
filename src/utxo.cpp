#include "utxo.h"
#include <cstring>
#include <algorithm>

namespace Bucks {

UTXO::UTXO(
    const Hash256& txid,
    uint32_t index,
    uint64_t amt,
    const std::vector<uint8_t>& script
) : amount(amt), scriptPubKey(script) {
    outpoint.txid = txid;
    outpoint.index = index;
}

UTXO::UTXO(const std::string& txidStr, uint32_t index, uint64_t amt, const std::string& scriptStr)
    : amount(amt) {
    // Build a Hash256 by zero-filling and copying up to 32 bytes of the string
    Hash256 h{};
    size_t copyLen = std::min<size_t>(txidStr.size(), 32);
    std::memcpy(h.data(), txidStr.data(), copyLen);
    outpoint.txid = h;
    outpoint.index = index;
    scriptPubKey.assign(scriptStr.begin(), scriptStr.end());
}

} // namespace Bucks
