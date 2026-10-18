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

// Difficulty rule of a network (section 6.5).
enum class PowAlgorithm : uint8_t {
    BITCOIN = 0,  // retarget every 2016 blocks, 4x clamp, period timewarp rules H-TW1/H-TW2
    ASERT = 1,    // aserti3-2d every block, anchored at genesis; no period rules
};

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
    bool        powNoRetargeting;      // true only on regtest: every block keeps its parent's bits
    PowAlgorithm powAlgorithm;         // bits rule when retargeting; also picks the time rules
    int64_t     asertHalfLife;         // seconds per doubling of the target (ASERT only)

    uint32_t    halvingInterval;

    // "host:port" entries a node dials when it has too few peers. Empty until the owner
    // publishes seed nodes for the network; `--seed` adds more at run time.
    std::vector<std::string> seedNodes;

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
// powLimit decodes from powLimitBits, the retarget (or ASERT) product cannot
// overflow, the ASERT half-life is positive, checkpoints are sorted and start
// at genesis, the inscription is printable ASCII and fits.
// Returns false and fills `err` on failure; the node then exits with code 3.
bool checkChainParams(const ChainParams& p, std::string& err);

} // namespace Bucks

#endif // BUCKS_CHAINPARAMS_H
