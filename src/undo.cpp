#include "undo.h"

#include "consensus.h"
#include "serialize.h"

namespace Bucks {

namespace {
constexpr uint8_t UNDO_VERSION = 1;
constexpr size_t MIN_COIN_RECORD = 4 + 1 + 8 + 1 + P2PKH_SCRIPT_SIZE;  // 39
constexpr size_t CHECKSUM_SIZE = 32;
}

std::vector<uint8_t> BlockUndo::serialize() const {
    Writer w;
    w.u8(UNDO_VERSION).hash256(blockHash).compactSize(spent.size());
    for (const Coin& c : spent) {
        w.u32(c.height).u8(c.isCoinbase ? 1 : 0).u64(c.amount).varBytes(c.scriptPubKey);
    }
    w.compactSize(goldUndo.size());
    w.bytes(goldUndo.data(), goldUndo.size());
    Hash256 checksum = doubleSha256(w.data());
    w.hash256(checksum);
    return w.take();
}

BlockUndo BlockUndo::deserialize(const std::vector<uint8_t>& data) {
    // version + hash + two counts + checksum
    if (data.size() < 1 + 32 + 1 + 1 + CHECKSUM_SIZE) throw ParseError("undo record too short");

    const size_t bodySize = data.size() - CHECKSUM_SIZE;
    std::vector<uint8_t> body(data.begin(), data.begin() + bodySize);
    Hash256 expected = doubleSha256(body);
    if (!std::equal(expected.begin(), expected.end(), data.begin() + bodySize))
        throw ParseError("undo checksum mismatch");

    Reader r(body);
    if (r.u8() != UNDO_VERSION) throw ParseError("unknown undo version");

    BlockUndo undo;
    undo.blockHash = r.hash256();

    uint64_t count = r.compactSize(Consensus::MAX_BLOCK_TXS_PARSE * uint64_t(Consensus::MAX_TX_INPUTS));
    if (count > r.remaining() / MIN_COIN_RECORD) throw ParseError("undo count exceeds data");
    undo.spent.reserve(size_t(count));
    for (uint64_t i = 0; i < count; ++i) {
        Coin c;
        c.height = r.u32();
        uint8_t flags = r.u8();
        if (flags & 0xFE) throw ParseError("undo coin: reserved flag bits set");
        c.isCoinbase = (flags & 1) != 0;
        c.amount = r.u64();
        if (c.amount < 1 || c.amount > Consensus::MAX_MONEY) throw ParseError("undo coin: bad amount");
        c.scriptPubKey = r.varBytes(P2PKH_SCRIPT_SIZE);
        if (!isP2PKH(c.scriptPubKey)) throw ParseError("undo coin: not a P2PKH script");
        undo.spent.push_back(std::move(c));
    }

    undo.goldUndo = r.varBytes(MAX_GOLD_UNDO);
    r.requireEnd();
    return undo;
}

} // namespace Bucks
