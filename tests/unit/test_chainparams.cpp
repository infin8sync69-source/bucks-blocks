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
}

TEST(Params, OverflowingLimitRejected) {
    ChainParams p = chainParams(NetworkId::MAIN);
    p.powLimitBits = 0x1f00ffff;  // the old launch limit
    p.powLimit = Pow::decodeCompact(p.powLimitBits).target;
    std::string err;
    EXPECT_FALSE(checkChainParams(p, err));
    p.powNoRetargeting = true;  // fine when nothing retargets
    EXPECT_TRUE(checkChainParams(p, err)) << err;
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
    // An all-zero genesis hash means "not configured" (mainnet until launch). A copy keeps the test the same
    // before and after the launch commit fills in makeMain() (audit ML-5).
    ChainParams unset = chainParams(NetworkId::MAIN);
    unset.genesisHash = Hash256{};
    EXPECT_FALSE(unset.genesisConfigured());
    EXPECT_TRUE(chainParams(NetworkId::TEST).genesisConfigured());
    EXPECT_TRUE(chainParams(NetworkId::REGTEST).genesisConfigured());
    EXPECT_EQ(chainParams(NetworkId::REGTEST).checkpoints.at(0).hash,
              chainParams(NetworkId::REGTEST).genesisHash);
}

// The inscription is printable ASCII and at most 96 bytes on every network (spec 3.5); bucks-mine-genesis
// runs the same check before it starts.
TEST(Params, InscriptionIsPrintableAscii) {
    std::string err;
    for (NetworkId id : {NetworkId::MAIN, NetworkId::TEST, NetworkId::REGTEST})
        EXPECT_TRUE(checkChainParams(chainParams(id), err)) << err;
    ChainParams p = chainParams(NetworkId::REGTEST);
    p.genesisInscription = " Space to tilde: 0-9 A-Z a-z !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    EXPECT_TRUE(checkChainParams(p, err)) << err;
    p.genesisInscription = std::string(97, 'a');
    EXPECT_FALSE(checkChainParams(p, err));
    for (const std::string& bad : {std::string("Bucks\tgenesis"), std::string("line\nbreak"),
                                   std::string("caf\xc3\xa9"), std::string("\x7f"), std::string("a\0b", 3)}) {
        p.genesisInscription = bad;
        EXPECT_FALSE(checkChainParams(p, err)) << bad;
        EXPECT_EQ(err, "genesis inscription must be printable ASCII");
    }
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
