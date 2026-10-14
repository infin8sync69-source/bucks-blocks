#include <gtest/gtest.h>

#include "chainparams.h"
#include "genesis.h"
#include "pow.h"

using namespace Bucks;

namespace {
std::string hex(const Hash256& h) { return hexStr(std::vector<uint8_t>(h.begin(), h.end())); }
}

TEST(Genesis, RegtestVector) {
    // BLK-1: the rebuilt block equals the hardcoded values and the spec vector.
    const ChainParams& p = chainParams(NetworkId::REGTEST);
    Block g = Genesis::build(p);
    EXPECT_EQ(hex(g.transactions[0].txid()),
              "e3cc1903035c668a5d3b09ad4873b414adaaaa5894a93dbd1b13a759b33f36d5");
    EXPECT_EQ(hex(g.getHash()),
              "3491115920c29000d16f7d776bbab4a3399e5065abeeaf73fa23a273406bf520");
    EXPECT_EQ(g.serialize().size(), 84u + 1 + 86);
    std::string err;
    EXPECT_TRUE(Genesis::check(p, err)) << err;
}

TEST(Genesis, TestnetIsMinedAndConfigured) {
    const ChainParams& p = chainParams(NetworkId::TEST);
    std::string err;
    EXPECT_TRUE(Genesis::check(p, err)) << err;
    Block g = Genesis::build(p);
    EXPECT_EQ(hex(g.getHash()),
              "000000dd046f0c82aa64ef2a740b9db0dca8ee27fe878adbdfd625507b5bc35d");
    // Real work went into it: at least 24 leading zero bits at the testnet limit 0x1e00ffff.
    EXPECT_EQ(p.powLimitBits, 0x1e00ffffu);
    EXPECT_EQ(g.getHash()[0], 0);
    EXPECT_EQ(g.getHash()[1], 0);
    EXPECT_EQ(g.getHash()[2], 0);
}

TEST(Genesis, CreatesNoCoins) {
    for (NetworkId id : {NetworkId::MAIN, NetworkId::TEST, NetworkId::REGTEST}) {
        Block g = Genesis::build(chainParams(id));
        ASSERT_EQ(g.transactions.size(), 1u);
        const Transaction& cb = g.transactions[0];
        EXPECT_TRUE(cb.isCoinbase());
        ASSERT_EQ(cb.outputs.size(), 1u);
        EXPECT_EQ(cb.outputs[0].amount, 0u);
        EXPECT_EQ(cb.outputs[0].scriptPubKey, Script{0x6a});
        EXPECT_EQ(g.header.height, 0u);
        EXPECT_EQ(g.header.bits, chainParams(id).powLimitBits);
        EXPECT_EQ(g.header.version, 2u);
    }
}

TEST(Genesis, MainnetUnconfigured) {
    // ST-7 (library part): the node refuses a network without a genesis hash.
    std::string err;
    EXPECT_FALSE(Genesis::check(chainParams(NetworkId::MAIN), err));
    EXPECT_NE(err.find("not configured"), std::string::npos) << err;
}

TEST(Genesis, MismatchesAreDetected) {
    std::string err;
    ChainParams p = chainParams(NetworkId::REGTEST);

    p.genesisInscription = "Bucks regtest genesis!";  // different merkle root
    EXPECT_FALSE(Genesis::check(p, err));
    EXPECT_NE(err.find("merkle"), std::string::npos) << err;

    p = chainParams(NetworkId::REGTEST);
    p.genesisTime += 1;  // same merkle root, different hash
    EXPECT_FALSE(Genesis::check(p, err));
    EXPECT_NE(err.find("expected"), std::string::npos) << err;

    p = chainParams(NetworkId::REGTEST);
    p.genesisHash[5] ^= 1;
    EXPECT_FALSE(Genesis::check(p, err));
}

TEST(Genesis, ProofOfWorkIsChecked) {
    // A correct hash that does not meet the target is still refused: use a
    // harder limit than the regtest genesis satisfies.
    std::string err;
    ChainParams p = chainParams(NetworkId::REGTEST);
    p.powLimitBits = 0x1d00ffff;
    p.powLimit = Bucks::Pow::decodeCompact(p.powLimitBits).target;
    p.genesisMerkleRoot = Genesis::build(p).header.merkleRoot;
    p.genesisHash = Genesis::build(p).getHash();
    EXPECT_FALSE(Genesis::check(p, err));
    EXPECT_NE(err.find("proof of work"), std::string::npos) << err;
}
