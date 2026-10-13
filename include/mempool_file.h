#ifndef BUCKS_MEMPOOL_FILE_H
#define BUCKS_MEMPOOL_FILE_H

#include <string>
#include <vector>

#include "crypto.h"
#include "mempool.h"

namespace Bucks {

// <networkDir>/mempool.dat: the unconfirmed transactions kept across restarts.
//   "BKMP" | u8 version=1 | 4-byte chain tag (genesis hash bytes 0-3) | u32 count |
//   count x (compactSize length, serialized transaction) | SHA256d of everything before it.
// The file is a cache, never a source of truth: it is rewritten atomically at shutdown, and a
// missing, foreign, damaged or stale file is ignored. Every transaction in it is fully
// re-validated against the current chain (Mempool::reloadValidated) before it is used.
constexpr uint8_t MEMPOOL_FILE_VERSION = 1;
constexpr size_t MEMPOOL_FILE_MAX_TXS = 100'000;

std::string encodeMempoolFile(const std::vector<std::vector<uint8_t>>& txs, const Hash256& genesisHash);

// False (with `err`) if the bytes are not a valid file for this chain.
bool decodeMempoolFile(const std::string& bytes, const Hash256& genesisHash,
                       std::vector<std::vector<uint8_t>>& txs, std::string& err);

} // namespace Bucks

#endif // BUCKS_MEMPOOL_FILE_H
