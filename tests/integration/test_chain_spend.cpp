#include <gtest/gtest.h>

#include <filesystem>

#include "block.h"
#include "blockchain.h"
#include "consensus.h"
#include "sighash.h"
#include "validation.h"
#include "wallet.h"

namespace fs = std::filesystem;
using namespace Bucks;

// End-to-end check of the old chain with transaction v2 and sighash v1:
// wallet -> signed spend -> mempool -> block -> UTXO set.
class ChainSpendTest : public ::testing::Test {
protected:
    std::string dir = (fs::temp_directory_path() / "bucks-test-chain-spend").string();
    void SetUp() override {
        fs::remove_all(dir);
        fs::create_directory(dir);
    }
    void TearDown() override { fs::remove_all(dir); }

    static bool mineAndAdd(Blockchain& chain, const Wallet& miner) {
        const Block* tip = chain.tip();
        BlockHeader h;
        h.version = Consensus::BLOCK_VERSION;
        h.height = chain.height() + 1;
        h.prevBlockHash = tip->getHash();
        h.timestamp = tip->header.timestamp + 1;
        h.bits = chain.getNextWorkRequired(h.height, h.prevBlockHash);

        Transaction cb;
        cb.inputs.emplace_back();
        cb.inputs[0].prevIndex = 0xFFFFFFFF;
        cb.inputs[0].scriptSig = {0x01, 0x02, 0x03, uint8_t(h.height)};
        cb.outputs.emplace_back(Consensus::INITIAL_SUBSIDY, miner.getP2PKHScript());

        std::vector<Transaction> txs = {cb};
        for (const auto& t : chain.getMempool()) txs.push_back(t);
        Block block(h, txs);
        block.header.merkleRoot = block.computeMerkleRoot();
        Validation::ValidationError err;
        while (!Validation::checkBlockProofOfWork(block.header, err)) ++block.header.nonce;
        return chain.addBlock(block);
    }
};

// One test, one chain: mining the 101 blocks needed for coinbase maturity is
// the slow part.
TEST_F(ChainSpendTest, SignedSpendConfirmsAndForgeriesAreRejected) {
    Blockchain chain(dir + "/utxo.db", dir + "/blocks.db");
    Wallet alice, bob;
    alice.generateKeys();
    bob.generateKeys();

    for (int i = 0; i < 101; ++i) ASSERT_TRUE(mineAndAdd(chain, alice)) << i;
    ASSERT_EQ(chain.height(), 101u);

    // Young coinbase outputs are skipped by coin selection; the first one is mature.
    Transaction good = alice.createTransaction(bob.getAddress(), 100000000, chain);
    EXPECT_EQ(good.version, 2u);

    // Output changed after signing.
    Transaction tampered = good;
    tampered.outputs[0].amount += 1;
    EXPECT_FALSE(chain.addToMempool(tampered));

    // Signed for a different genesis (another network).
    std::vector<SpentOutput> spent;
    for (const auto& in : good.inputs) {
        UTXO u;
        ASSERT_TRUE(chain.utxo().get({in.prevTxid, in.prevIndex}, u));
        spent.push_back({u.amount, u.scriptPubKey});
    }
    Transaction foreign = good;
    for (auto& in : foreign.inputs) in.scriptSig.clear();
    ASSERT_TRUE(alice.signTransaction(foreign, spent, Hash256{}));
    EXPECT_FALSE(chain.addToMempool(foreign));

    // Someone else's key cannot sign alice's coins.
    Transaction stolen = good;
    for (auto& in : stolen.inputs) in.scriptSig.clear();
    EXPECT_FALSE(bob.signTransaction(stolen, spent, chain.genesisHash()));

    // The genuine transaction is accepted and confirms.
    ASSERT_TRUE(chain.addToMempool(good));
    ASSERT_TRUE(mineAndAdd(chain, alice));

    uint64_t bobBalance = 0;
    for (const auto& u : chain.utxo().getUTXOsByAddress(bob.getAddress())) bobBalance += u.amount;
    EXPECT_EQ(bobBalance, 100000000u);
}
