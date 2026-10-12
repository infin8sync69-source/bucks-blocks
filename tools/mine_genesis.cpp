// bucks-mine-genesis: mines the genesis nonce for a network (offline tool,
// not linked into the node). See docs/design/consensus-validation.md 3.5.
//
//   bucks-mine-genesis --network=test --time=1791590400 [--inscription="..."]
//
// Prints the values to paste into src/chainparams.cpp. If the 32-bit nonce
// space is exhausted the time is incremented by one and the search restarts.

#include <openssl/sha.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "chainparams.h"
#include "crypto.h"
#include "genesis.h"
#include "pow.h"

using namespace Bucks;

// SHA256d of header (84 bytes) with a given nonce, using the low-level API so
// threads do not contend on OpenSSL 3 provider lookups. The first 64 bytes are
// hashed once (midstate); only the tail and the second hash run per nonce.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
struct Midstate {
    SHA256_CTX ctx;
    uint8_t tail[20];
    explicit Midstate(const std::vector<uint8_t>& header) {
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, header.data(), 64);
        std::memcpy(tail, header.data() + 64, 20);
    }
    Hash256 hash(uint32_t nonce) const {
        uint8_t t[20];
        std::memcpy(t, tail, 16);
        for (int b = 0; b < 4; ++b) t[16 + b] = uint8_t(nonce >> (8 * b));
        SHA256_CTX c = ctx;
        SHA256_Update(&c, t, 20);
        uint8_t first[32];
        SHA256_Final(first, &c);
        Hash256 out;
        SHA256(first, 32, out.data());
        return out;
    }
};

static std::string hex(const Hash256& h) {
    return hexStr(std::vector<uint8_t>(h.begin(), h.end()));
}

int main(int argc, char* argv[]) {
    NetworkId id = NetworkId::TEST;
    bool haveTime = false;
    uint32_t time = 0;
    std::string inscription;
    bool haveInscription = false;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&](const char* name) -> const char* {
            size_t n = std::strlen(name);
            if (a.compare(0, n, name) == 0 && a.size() > n && a[n] == '=') return argv[i] + n + 1;
            return nullptr;
        };
        if (const char* v = value("--network")) {
            if (!parseNetworkName(v, id)) { std::cerr << "unknown network\n"; return 2; }
        } else if (const char* v = value("--time")) {
            time = uint32_t(std::strtoul(v, nullptr, 10));
            haveTime = true;
        } else if (const char* v = value("--inscription")) {
            inscription = v;
            haveInscription = true;
        } else if (const char* v = value("--threads")) {
            threads = std::max(1u, unsigned(std::strtoul(v, nullptr, 10)));
        } else {
            std::cerr << "usage: bucks-mine-genesis --network=main|test|regtest --time=<unix> "
                         "[--inscription=<ascii, max 96 bytes>] [--threads=<n>]\n";
            return 2;
        }
    }

    ChainParams params = chainParams(id);
    if (haveTime) params.genesisTime = time;
    if (haveInscription) params.genesisInscription = inscription;
    if (params.genesisTime == 0) { std::cerr << "--time is required\n"; return 2; }
    if (params.genesisInscription.size() > 96) { std::cerr << "inscription longer than 96 bytes\n"; return 2; }

    std::cerr << "Mining " << params.name << " genesis, bits=0x" << std::hex << params.powLimitBits
              << std::dec << ", " << threads << " threads\n";

    for (;;) {
        params.genesisNonce = 0;
        Block genesis = Genesis::build(params);
        std::atomic<bool> found{false};
        std::atomic<uint32_t> winner{0};

        auto worker = [&](unsigned index) {
            const Midstate mid(genesis.header.serialize());  // 84 bytes, nonce at 80
            for (uint64_t n = index; n <= 0xFFFFFFFFull && !found.load(); n += threads) {
                if (Pow::checkProofOfWork(mid.hash(uint32_t(n)), genesis.header.bits, params.powLimit)) {
                    bool expected = false;
                    if (found.compare_exchange_strong(expected, true)) winner = uint32_t(n);
                    return;
                }
            }
        };
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threads; ++t) pool.emplace_back(worker, t);
        for (auto& t : pool) t.join();

        if (found) {
            params.genesisNonce = winner;
            Block g = Genesis::build(params);
            std::cout << "network:     " << params.name << "\n"
                      << "time:        " << params.genesisTime << "\n"
                      << "nonce:       " << params.genesisNonce << "\n"
                      << "inscription: " << params.genesisInscription << "\n"
                      << "merkle root: " << hex(g.header.merkleRoot) << "\n"
                      << "hash:        " << hex(g.getHash()) << "\n";
            return 0;
        }
        std::cerr << "nonce space exhausted, trying time " << params.genesisTime + 1 << "\n";
        ++params.genesisTime;
    }
}
