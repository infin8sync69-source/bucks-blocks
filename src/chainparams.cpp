#include "chainparams.h"

#include "consensus.h"
#include "pow.h"

namespace Bucks {

namespace {

Hash256 hexHash(const char* hex) {
    Hash256 h{};
    auto nib = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t i = 0; i < 32; ++i)
        h[i] = uint8_t((nib(hex[2 * i]) << 4) | nib(hex[2 * i + 1]));
    return h;
}

ChainParams makeMain() {
    ChainParams p;
    p.id = NetworkId::MAIN;
    p.name = "main";
    p.netMagic = {0xB7, 0xC5, 0xE3, 0xA9};
    p.defaultP2PPort = 8733;
    p.defaultApiPort = 8080;
    p.p2pkhAddressVersion = 0x1A;
    // Genesis is chosen and mined at launch (spec section 3.5); until then
    // the node refuses to start this network.
    p.genesisTime = 0;
    p.genesisNonce = 0;
    p.genesisInscription = "";
    p.genesisMerkleRoot = Hash256{};
    p.genesisHash = Hash256{};
    p.powLimitBits = 0x1d00ffff;
    p.powNoRetargeting = false;
    // aserti3-2d with Bitcoin Cash's 2-day half-life: a block's target follows how far its parent is
    // ahead of or behind the schedule genesis.time + 600 s * height, so a miner who leaves stalls the
    // chain for one slow block instead of a 2016-block period (audit NEW-2). Anchor: section 6.5.
    p.powAlgorithm = PowAlgorithm::ASERT;
    p.asertHalfLife = 2 * 24 * 60 * 60;
    p.halvingInterval = 210000;
    p.minimumChainWork = U256();
    p.maxAutoReorgDepth = 100;
    return p;
}

ChainParams makeTest() {
    ChainParams p = makeMain();
    p.id = NetworkId::TEST;
    p.name = "test";
    // Changed with the 2026-10-14 genesis (was 0x7F) and again with ASERT (the old testnet's blocks
    // break the new difficulty rule), so peers of an older testnet cannot connect.
    p.netMagic = {0xB7, 0xC5, 0xE3, 0x6B};
    p.defaultP2PPort = 18733;
    p.defaultApiPort = 18080;
    p.p2pkhAddressVersion = 0x41;
    // Testnet starts at a much lower difficulty than mainnet: 0x1e00ffff is about 2^24 hashes per block,
    // so one laptop finds a block in seconds and coins mature within hours. ASERT with mainnet's half-life
    // then follows whatever hashrate shows up, so the soak runs mainnet's difficulty rule.
    p.powLimitBits = 0x1e00ffff;
    // Mined with: bucks-mine-genesis --network=test --time=1791958761 (about ten seconds on a laptop)
    p.genesisTime = 1791958761;
    p.genesisNonce = 35444257u;
    p.genesisInscription = "Bucks testnet genesis";
    p.genesisMerkleRoot =
        hexHash("4c9dabbe1c0b5a8c2bbdcc192d39fbfeed3f46ab1935c2d3e5b9026aa7a2afa7");
    p.genesisHash =
        hexHash("000000dd046f0c82aa64ef2a740b9db0dca8ee27fe878adbdfd625507b5bc35d");
    return p;
}

ChainParams makeRegtest() {
    ChainParams p;
    p.id = NetworkId::REGTEST;
    p.name = "regtest";
    p.netMagic = {0xB7, 0xC5, 0xE3, 0x5D};
    p.defaultP2PPort = 18744;
    p.defaultApiPort = 18090;
    p.p2pkhAddressVersion = 0x7F;
    p.genesisTime = 1767225600;  // 2026-01-01 00:00:00 UTC
    p.genesisNonce = 0;
    p.genesisInscription = "Bucks regtest genesis";
    p.genesisMerkleRoot =
        hexHash("e3cc1903035c668a5d3b09ad4873b414adaaaa5894a93dbd1b13a759b33f36d5");
    p.genesisHash =
        hexHash("3491115920c29000d16f7d776bbab4a3399e5065abeeaf73fa23a273406bf520");
    p.powLimitBits = 0x207fffff;
    p.powNoRetargeting = true;
    // Bits never change, but the algorithm also picks the time rules: same as mainnet.
    p.powAlgorithm = PowAlgorithm::ASERT;
    p.asertHalfLife = 2 * 24 * 60 * 60;
    p.halvingInterval = 150;
    p.minimumChainWork = U256();
    p.maxAutoReorgDepth = 100;
    return p;
}

// Fills the derived fields shared by all networks.
ChainParams finish(ChainParams p) {
    p.powLimit = Pow::decodeCompact(p.powLimitBits).target;
    p.checkpoints = {Checkpoint{0, p.genesisHash}};
    return p;
}

} // namespace

bool ChainParams::genesisConfigured() const {
    for (uint8_t b : genesisHash) if (b) return true;
    return false;
}

const ChainParams& chainParams(NetworkId id) {
    static const ChainParams main = finish(makeMain());
    static const ChainParams test = finish(makeTest());
    static const ChainParams regtest = finish(makeRegtest());
    switch (id) {
        case NetworkId::MAIN: return main;
        case NetworkId::TEST: return test;
        default: return regtest;
    }
}

bool parseNetworkName(const std::string& name, NetworkId& out) {
    if (name == "main") out = NetworkId::MAIN;
    else if (name == "test") out = NetworkId::TEST;
    else if (name == "regtest") out = NetworkId::REGTEST;
    else return false;
    return true;
}

bool checkChainParams(const ChainParams& p, std::string& err) {
    Pow::Compact c = Pow::decodeCompact(p.powLimitBits);
    if (c.negative || c.overflow || c.target.isZero() || c.target != p.powLimit) {
        err = "powLimit does not match a valid powLimitBits";
        return false;
    }
    if (p.powAlgorithm != PowAlgorithm::BITCOIN && p.powAlgorithm != PowAlgorithm::ASERT) {
        err = "unknown difficulty algorithm";
        return false;
    }
    if (!p.powNoRetargeting && p.powAlgorithm == PowAlgorithm::BITCOIN) {
        unsigned bitsT4 = U256(uint64_t(4 * Consensus::TARGET_TIMESPAN)).bits();
        if (p.powLimit.bits() + bitsT4 > 256) {
            err = "powLimit * 4T overflows 256 bits";
            return false;
        }
    }
    if (!p.powNoRetargeting && p.powAlgorithm == PowAlgorithm::ASERT) {
        if (p.powLimit.bits() + Pow::ASERT_FACTOR_BITS > 256) {
            err = "powLimit * 2^17 overflows 256 bits (ASERT)";
            return false;
        }
        if (p.asertHalfLife <= 0) {
            err = "ASERT half-life must be positive";
            return false;
        }
    }
    if (p.checkpoints.empty() || p.checkpoints[0].height != 0 ||
        p.checkpoints[0].hash != p.genesisHash) {
        err = "checkpoints must start with {0, genesisHash}";
        return false;
    }
    for (size_t i = 1; i < p.checkpoints.size(); ++i) {
        if (p.checkpoints[i].height <= p.checkpoints[i - 1].height) {
            err = "checkpoints are not sorted by height";
            return false;
        }
    }
    if (p.genesisInscription.size() > 96) {
        err = "genesis inscription longer than 96 bytes";
        return false;
    }
    return true;
}

} // namespace Bucks
