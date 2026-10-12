#ifndef BUCKS_UNDO_H
#define BUCKS_UNDO_H

#include <cstdint>
#include <vector>

#include "coins.h"
#include "crypto.h"

namespace Bucks {

// Block undo record v1 (docs/design/consensus-validation.md section 4.7).
// One record per connected block except genesis; the storage layer keeps it
// as a blob keyed by block hash.
//
//   u8 version = 1 | Hash256 blockHash | CompactSize spentCount |
//   spent[] coin records (height u32, flags u8, amount u64, VarBytes(25)) |
//   CompactSize goldUndoLen | goldUndo bytes | Hash256 SHA256d(all previous bytes)
struct BlockUndo {
    Hash256 blockHash{};
    std::vector<Coin> spent;           // transaction order, then input order
    std::vector<uint8_t> goldUndo;     // format owned by the gold spec

    std::vector<uint8_t> serialize() const;

    // Throws ParseError for a wrong version, bad checksum, bad flags, a
    // non-P2PKH script, an amount outside 1..MAX_MONEY, oversize gold data,
    // a count larger than the data, truncation or trailing bytes. Callers
    // also compare blockHash and spent.size() with the block being undone; a
    // mismatch means the database is corrupt.
    static BlockUndo deserialize(const std::vector<uint8_t>& data);
};

constexpr size_t MAX_GOLD_UNDO = 65536;

} // namespace Bucks

#endif // BUCKS_UNDO_H
