#ifndef BUCKS_CHAINPARAMS_H
#define BUCKS_CHAINPARAMS_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "crypto.h"
#include "u256.h"

namespace Bucks {

enum class NetworkId : uint8_t { MAIN = 0, TEST = 1, REGTEST = 2 };

struct Checkpoint {
    uint32_t height;
    Hash256 hash;
};

// Per-network parameters (docs/design/consensus-validation.md section 3.3).
// There is no mutable global: components receive `const ChainParams&`.
struct ChainParams {
    NetworkId   id;
    const char* name;                  // "main", "test", "regtest"
    std::array<uint8_t, 4> netMagic;   // P2P frame start, in wire order
    uint16_t    defaultP2PPort;
    uint16_t    defaultApiPort;
    uint8_t     p2pkhAddressVersion;   // Base58Check version byte

    // Genesis (section 3.5). An all-zero genesisHash means "not configured".
    uint32_t    genesisTime;
    uint32_t    genesisNonce;
    std::string genesisInscription;    // ASCII, at most 96 bytes
    Hash256     genesisMerkleRoot;
    Hash256     genesisHash;

    // Proof of work (section 6)
    uint32_t    powLimitBits;          // minimum difficulty; also the genesis bits
    U256        powLimit;              // decodeCompact(powLimitBits)
    bool        powNoRetargeting;      // true only on regtest

    uint32_t    halvingInterval;

    // Chain selection safety (section 8.5)
    std::vector<Checkpoint> checkpoints;  // ascending height, first is genesis
    U256        minimumChainWork;
    uint32_t    maxAutoReorgDepth;

    bool genesisConfigured() const;
};

const ChainParams& chainParams(NetworkId id);

// Parses "main" / "test" / "regtest". Returns false for anything else.
bool parseNetworkName(const std::string& name, NetworkId& out);

// Startup assertions of section 3.7 that do not need the genesis block:
// powLimit decodes from powLimitBits, the retarget product cannot overflow,
// checkpoints are sorted and start at genesis, the inscription fits.
// Returns false and fills `err` on failure; the node then exits with code 3.
bool checkChainParams(const ChainParams& p, std::string& err);

} // namespace Bucks

#endif // BUCKS_CHAINPARAMS_H
