#include "mempool_file.h"

#include <algorithm>
#include <cstring>

#include "policy.h"
#include "serialize.h"

namespace Bucks {

std::string encodeMempoolFile(const std::vector<std::vector<uint8_t>>& txs, const Hash256& genesisHash) {
    Writer w;
    const uint8_t magic[4] = {'B', 'K', 'M', 'P'};
    w.bytes(magic, 4);
    w.u8(MEMPOOL_FILE_VERSION);
    w.bytes(genesisHash.data(), 4);
    w.u32(static_cast<uint32_t>(txs.size()));
    for (const auto& tx : txs) w.varBytes(tx);
    const Hash256 sum = doubleSha256(w.data());
    w.bytes(sum.data(), sum.size());
    return std::string(w.data().begin(), w.data().end());
}

bool decodeMempoolFile(const std::string& bytes, const Hash256& genesisHash,
                       std::vector<std::vector<uint8_t>>& txs, std::string& err) {
    txs.clear();
    if (bytes.size() < 4 + 1 + 4 + 4 + 32) {
        err = "mempool.dat is too short";
        return false;
    }
    const std::vector<uint8_t> raw(bytes.begin(), bytes.end());
    const std::vector<uint8_t> body(raw.begin(), raw.end() - 32);
    const Hash256 sum = doubleSha256(body);
    if (std::memcmp(sum.data(), raw.data() + raw.size() - 32, 32) != 0) {
        err = "mempool.dat checksum mismatch";
        return false;
    }
    try {
        Reader r(body.data(), body.size());
        uint8_t magic[4];
        r.bytes(magic, 4);
        if (std::memcmp(magic, "BKMP", 4) != 0) {
            err = "mempool.dat has the wrong magic";
            return false;
        }
        if (r.u8() != MEMPOOL_FILE_VERSION) {
            err = "mempool.dat has an unsupported version";
            return false;
        }
        uint8_t tag[4];
        r.bytes(tag, 4);
        if (std::memcmp(tag, genesisHash.data(), 4) != 0) {
            err = "mempool.dat belongs to a different chain";
            return false;
        }
        const uint32_t count = r.u32();
        if (count > MEMPOOL_FILE_MAX_TXS) {
            err = "mempool.dat declares too many transactions";
            return false;
        }
        for (uint32_t i = 0; i < count; ++i) txs.push_back(r.varBytes(Policy::MAX_STANDARD_TX_SIZE));
        r.requireEnd();
    } catch (const ParseError& e) {
        txs.clear();
        err = std::string("mempool.dat is damaged: ") + e.what();
        return false;
    }
    return true;
}

} // namespace Bucks
