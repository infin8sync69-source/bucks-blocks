#include <gtest/gtest.h>

#include <set>

#include "chainparams.h"
#include "consensus.h"
#include "pow.h"

using namespace Bucks;

namespace {
const NetworkId kAll[] = {NetworkId::MAIN, NetworkId::TEST, NetworkId::REGTEST};
}

TEST(Params, Distinct) {
    std::set<std::array<uint8_t, 4>> magics;
    std::set<uint16_t> ports;
    std::set<uint8_t> versions;
    for (NetworkId id : kAll) {
        const ChainParams& p = chainParams(id);
        magics.insert(p.netMagic);
        ports.insert(p.defaultP2PPort);
        ports.insert(p.defaultApiPort);
        versions.insert(p.p2pkhAddressVersion);
        EXPECT_NE(p.defaultP2PPort, 8333);                 // Bitcoin's port
        EXPECT_NE(p.p2pkhAddressVersion, 0x00);            // Bitcoin's address byte
        EXPECT_NE(p.netMagic, (std::array<uint8_t, 4>{0xF5, 0xB3, 0xB4, 0xD9}));  // old Bucks
    }
    EXPECT_EQ(magics.size(), 3u);
    EXPECT_EQ(ports.size(), 6u);
    EXPECT_EQ(versions.size(), 3u);
}

TEST(Params, PowLimitDecodes) {
    for (NetworkId id : kAll) {
        const ChainParams& p = chainParams(id);
        std::string err;
        EXPECT_TRUE(checkChainParams(p, err)) << p.name << ": " << err;
        EXPECT_EQ(p.powLimit, Pow::decodeCompact(p.powLimitBits).target);
    }
    EXPECT_EQ(chainParams(NetworkId::MAIN).powLimitBits, 0x1d00ffffu);
    EXPECT_TRUE(chainParams(NetworkId::REGTEST).powNoRetargeting);
    EXPECT_FALSE(chainParams(NetworkId::MAIN).powNoRetargeting);
    EXPECT_FALSE(chainParams(NetworkId::TEST).powNoRetargeting);
    // aserti3-2d with a 2-day half-life on every network (regtest only for its time rules).
    for (NetworkId id : kAll) {
        EXPECT_EQ(chainParams(id).powAlgorithm, PowAlgorithm::ASERT) << chainParams(id).name;
        EXPECT_EQ(chainParams(id).asertHalfLife, 172800) << chainParams(id).name;
    }
}

TEST(Params, OverflowingLimitRejected) {
    ChainParams p = chainParams(NetworkId::MAIN);
    p.powLimitBits = 0x1f00ffff;  // the old launch limit
    p.powLimit = Pow::decodeCompact(p.powLimitBits).target;
    std::string err;
    EXPECT_FALSE(checkChainParams(p, err));
    p.powAlgorithm = PowAlgorithm::BITCOIN;
    EXPECT_FALSE(checkChainParams(p, err));
    p.powNoRetargeting = true;  // fine when nothing retargets
    EXPECT_TRUE(checkChainParams(p, err)) << err;
}

// ASERT needs bits(powLimit) + 17 <= 256 and a positive half-life; the Bitcoin
// retarget needs bits(powLimit) + bits(4T) <= 256 (23 bits).
TEST(Params, AsertGuards) {
    auto withLimit = [](PowAlgorithm algo, uint32_t bits) {
        ChainParams p = chainParams(NetworkId::MAIN);
        p.powAlgorithm = algo;
        p.powLimitBits = bits;
        p.powLimit = Pow::decodeCompact(bits).target;
        return p;
    };
    std::string err;
    EXPECT_TRUE(checkChainParams(withLimit(PowAlgorithm::ASERT, 0x1e7fffff), err)) << err;
    EXPECT_FALSE(checkChainParams(withLimit(PowAlgorithm::ASERT, 0x1f00ffff), err));
    EXPECT_EQ(err, "powLimit * 2^17 overflows 256 bits (ASERT)");
    EXPECT_FALSE(checkChainParams(withLimit(PowAlgorithm::BITCOIN, 0x1e7fffff), err));
    EXPECT_EQ(err, "powLimit * 4T overflows 256 bits");
    EXPECT_TRUE(checkChainParams(withLimit(PowAlgorithm::BITCOIN, 0x1e00ffff), err)) << err;

    ChainParams p = chainParams(NetworkId::MAIN);
    p.asertHalfLife = 0;
    EXPECT_FALSE(checkChainParams(p, err));
    EXPECT_EQ(err, "ASERT half-life must be positive");
    p.powNoRetargeting = true;
    EXPECT_TRUE(checkChainParams(p, err)) << err;

    p = chainParams(NetworkId::MAIN);
    p.powAlgorithm = static_cast<PowAlgorithm>(7);
    EXPECT_FALSE(checkChainParams(p, err));
    EXPECT_EQ(err, "unknown difficulty algorithm");
}

TEST(Params, BadCheckpointsAndLimitMismatch) {
    std::string err;
    ChainParams p = chainParams(NetworkId::REGTEST);
    p.checkpoints.clear();
    EXPECT_FALSE(checkChainParams(p, err));
    p = chainParams(NetworkId::REGTEST);
    p.checkpoints.push_back({5, Hash256{}});
    p.checkpoints.push_back({5, Hash256{}});
    EXPECT_FALSE(checkChainParams(p, err));
    p = chainParams(NetworkId::REGTEST);
    p.powLimit = U256(1);
    EXPECT_FALSE(checkChainParams(p, err));
}

TEST(Params, GenesisConfiguration) {
    EXPECT_FALSE(chainParams(NetworkId::MAIN).genesisConfigured());
    EXPECT_TRUE(chainParams(NetworkId::REGTEST).genesisConfigured());
    EXPECT_EQ(chainParams(NetworkId::REGTEST).checkpoints.at(0).hash,
              chainParams(NetworkId::REGTEST).genesisHash);
}

TEST(Params, NetworkNames) {
    NetworkId id;
    EXPECT_TRUE(parseNetworkName("regtest", id));
    EXPECT_EQ(id, NetworkId::REGTEST);
    EXPECT_FALSE(parseNetworkName("Main", id));
}

TEST(Params, ConsensusConstants) {
    EXPECT_EQ(Consensus::TARGET_TIMESPAN, 1209600);
    EXPECT_EQ(Consensus::BLOCK_VERSION, 2u);
    EXPECT_EQ(Consensus::MAX_TX_INPUTS, 2000u);
    EXPECT_EQ(Consensus::MAX_TX_OUTPUTS, 3000u);
}
