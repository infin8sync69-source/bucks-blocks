#include <gtest/gtest.h>

#include "chainparams.h"
#include "genesis.h"
#include "pow.h"

using namespace Bucks;

namespace {
std::string hex(const Hash256& h) { return hexStr(std::vector<uint8_t>(h.begin(), h.end())); }

// Mainnet as it is before launch, whatever makeMain() says: the refusal path stays tested after the launch
// commit (audit ML-5).
ChainParams unconfiguredMain() {
    ChainParams p = chainParams(NetworkId::MAIN);
    p.genesisTime = 0;
    p.genesisNonce = 0;
    p.genesisInscription.clear();
    p.genesisMerkleRoot = Hash256{};
    p.genesisHash = Hash256{};
    p.checkpoints = {Checkpoint{0, Hash256{}}};
    return p;
}
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
    const ChainParams p = unconfiguredMain();
    EXPECT_TRUE(checkChainParams(p, err)) << err;
    EXPECT_FALSE(Genesis::check(p, err));
    EXPECT_NE(err.find("not configured"), std::string::npos) << err;
}

// Skipped until the launch commit sets the mainnet genesis (docs/MAINNET_INPUTS.md, item 1); from then on it
// holds the values to the rules. tools/verify_genesis.py (ctest genesis_independent) recomputes them without this
// code.
TEST(Genesis, MainnetIsMinedAndConfigured) {
    const ChainParams& p = chainParams(NetworkId::MAIN);
    if (!p.genesisConfigured()) GTEST_SKIP() << "the mainnet genesis is not set yet";
    std::string err;
    EXPECT_TRUE(checkChainParams(p, err)) << err;   // inscription: printable ASCII, at most 96 bytes
    EXPECT_TRUE(Genesis::check(p, err)) << err;     // merkle root, hash and proof of work
    const Block g = Genesis::build(p);
    EXPECT_EQ(g.header.merkleRoot, p.genesisMerkleRoot);
    EXPECT_EQ(g.getHash(), p.genesisHash);
    EXPECT_EQ(p.checkpoints.at(0).hash, p.genesisHash);
    EXPECT_FALSE(p.genesisInscription.empty());
    EXPECT_GT(p.genesisTime, 1'790'000'000u);       // after the testnet genesis, not a placeholder
    // Real work went into it: mined at the proof-of-work limit, and the hash is as small as that limit demands
    // (the limit is an owner decision, so nothing here depends on its value).
    EXPECT_EQ(g.header.bits, p.powLimitBits);
    EXPECT_TRUE(Pow::checkProofOfWork(g.getHash(), g.header.bits, p.powLimit));
    EXPECT_LE(U256::fromBytesBE(g.getHash()).bits(), p.powLimit.bits());
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
