#include <gtest/gtest.h>

#include "consensus.h"
#include "node_harness.h"
#include "wallet.h"

using namespace Bucks;
using namespace Bucks::TestUtil;

// End-to-end through the node: wallet -> signed spend -> mempool -> block -> coins.
// This replaces the test of the old chain code; the same forgeries are refused.

namespace {

Script minerScript(const Wallet& w) { return w.getP2PKHScript(); }

bool mine(Node& node, const Wallet& miner, std::string& err) {
    std::atomic<bool> stop{false};
    return node.mine(minerScript(miner), {}, stop, err) == MineResult::FOUND;
}

} // namespace

TEST(NodeSpend, SignedSpendConfirmsAndForgeriesAreRejected) {
    NodeHarness h;
    ASSERT_TRUE(h.node) << h.error;
    Node& node = *h.node;
    Wallet alice, bob;
    alice.generateKeys();
    bob.generateKeys();

    std::string err;
    for (int i = 0; i < 101; ++i) ASSERT_TRUE(mine(node, alice, err)) << i << " " << err;
    ASSERT_EQ(node.height(), 101u);

    // Only the first coinbase is mature; younger ones are skipped by coin selection.
    Transaction good = alice.createTransaction(bob.getAddress(), 100'000'000, node);
    EXPECT_EQ(good.version, 2u);

    // Output changed after signing.
    Transaction tampered = good;
    tampered.outputs[0].amount += 1;
    EXPECT_EQ(node.submitTransaction(tampered).code, "bad-sig");

    // Signed for another network (a different genesis hash).
    std::vector<SpentOutput> spent;
    for (const auto& in : good.inputs) {
        for (const auto& row : node.coinsByHash160(hash160(alice.getPublicKey()), 1000)) {
            if (row.op.txid == in.prevTxid && row.op.index == in.prevIndex)
                spent.push_back({row.coin.amount, row.coin.scriptPubKey});
        }
    }
    ASSERT_EQ(spent.size(), good.inputs.size());
    Transaction foreign = good;
    for (auto& in : foreign.inputs) in.scriptSig.clear();
    ASSERT_TRUE(alice.signTransaction(foreign, spent, Hash256{}));
    EXPECT_EQ(node.submitTransaction(foreign).code, "bad-sig");

    // Someone else's key cannot sign alice's coins.
    Transaction stolen = good;
    for (auto& in : stolen.inputs) in.scriptSig.clear();
    EXPECT_FALSE(bob.signTransaction(stolen, spent, node.params().genesisHash));

    // The genuine transaction is accepted, then confirms in the next block.
    ValidationState st = node.submitTransaction(good);
    ASSERT_TRUE(st.valid()) << st.code;
    EXPECT_EQ(node.mempoolSize(), 1u);
    ASSERT_TRUE(mine(node, alice, err)) << err;
    EXPECT_EQ(node.mempoolSize(), 0u);
    EXPECT_EQ(node.balance(hash160(bob.getPublicKey())), 100'000'000u);

    // A restart finds the same state.
    const Hash256 tip = node.tip().hash;
    const UtxoStats stats = node.tip().stats;
    h.node.reset();
    std::string reopenErr;
    int code = 0;
    h.node = Node::open(h.root() + "/chain", chainParams(NetworkId::REGTEST), {}, reopenErr, code);
    ASSERT_TRUE(h.node) << reopenErr;
    EXPECT_EQ(h.node->tip().hash, tip);
    EXPECT_EQ(h.node->tip().stats, stats);
    EXPECT_EQ(h.node->balance(hash160(bob.getPublicKey())), 100'000'000u);
}

TEST(NodeSpend, WalletAddressesUseTheNetworkVersion) {
    NodeHarness h;
    ASSERT_TRUE(h.node) << h.error;
    Wallet w;
    w.generateKeys();
    // A regtest address is refused on another network and the other way round.
    Wallet other = w;
    other.setAddressVersion(chainParams(NetworkId::MAIN).p2pkhAddressVersion);
    EXPECT_NE(w.getAddress(), other.getAddress());
    EXPECT_THROW(Wallet::addressToP2PKHScript(other.getAddress(), w.addressVersion()), std::runtime_error);
    EXPECT_NO_THROW(Wallet::addressToP2PKHScript(w.getAddress(), w.addressVersion()));
    // Sending to the wrong network's address fails before any coin is touched.
    EXPECT_THROW(w.createTransaction(other.getAddress(), 1000, *h.node), std::runtime_error);
}

TEST(NodeOpen, UnconfiguredMainnetRefusesToStart) {
    // Mainnet has no genesis yet: opening it fails with exit code 3 (ST-7).
    std::string root = (std::filesystem::temp_directory_path() / ("bucks-test-main-" + std::to_string(getpid()))).string();
    std::filesystem::remove_all(root);
    std::string err;
    int code = 0;
    auto node = Node::open(root + "/main", chainParams(NetworkId::MAIN), {}, err, code);
    EXPECT_EQ(node, nullptr);
    EXPECT_EQ(code, EXIT_PARAMS_MISMATCH) << err;
    std::filesystem::remove_all(root);
}
