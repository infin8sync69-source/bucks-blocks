#include <gtest/gtest.h>

#include <fstream>

#include "consensus.h"
#include "fsutil.h"
#include "mempool_file.h"
#include "node_harness.h"
#include "wallet.h"

using namespace Bucks;
using namespace Bucks::TestUtil;

// Spec: docs/design/storage-concurrency.md (MP-9): the mempool survives a restart, and a
// damaged, foreign or stale file never gets in the way of starting.

namespace {

Hash256 genesisOf(uint8_t fill) {
    Hash256 h;
    h.fill(fill);
    return h;
}

bool mineTo(Node& node, const Wallet& w) {
    std::atomic<bool> stop{false};
    std::string err;
    return node.mine(w.getP2PKHScript(), {}, stop, err) == MineResult::FOUND;
}

void writeFile(const std::string& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

} // namespace

TEST(MempoolFile, RoundTripAndRejectedInputs) {
    const std::vector<std::vector<uint8_t>> txs = {{1, 2, 3}, {}, std::vector<uint8_t>(500, 9)};
    const std::string good = encodeMempoolFile(txs, genesisOf(7));

    std::vector<std::vector<uint8_t>> out;
    std::string err;
    ASSERT_TRUE(decodeMempoolFile(good, genesisOf(7), out, err)) << err;
    EXPECT_EQ(out, txs);

    // A file for another chain.
    EXPECT_FALSE(decodeMempoolFile(good, genesisOf(8), out, err));
    EXPECT_NE(err.find("different chain"), std::string::npos) << err;

    // Every single-bit flip and every truncation is refused (the checksum covers the file).
    for (size_t i = 0; i < good.size(); ++i) {
        std::string bad = good;
        bad[i] = char(bad[i] ^ 0x01);
        EXPECT_FALSE(decodeMempoolFile(bad, genesisOf(7), out, err)) << "flip at " << i;
        EXPECT_TRUE(out.empty());
    }
    for (size_t n = 0; n < good.size(); ++n)
        EXPECT_FALSE(decodeMempoolFile(good.substr(0, n), genesisOf(7), out, err)) << "cut at " << n;
    EXPECT_FALSE(decodeMempoolFile("", genesisOf(7), out, err));
}

TEST(MempoolFile, NodeKeepsUnconfirmedTransactionsAcrossRestart) {
    NodeHarness h;
    ASSERT_TRUE(h.node) << h.error;
    Wallet alice, bob;
    alice.generateKeys();
    bob.generateKeys();
    for (int i = 0; i < 101; ++i) ASSERT_TRUE(mineTo(*h.node, alice)) << i;

    Transaction tx = alice.createTransaction(bob.getAddress(), 100'000'000, *h.node);
    ASSERT_TRUE(h.node->submitTransaction(tx).valid());
    ASSERT_EQ(h.node->mempoolSize(), 1u);

    h.node.reset();   // orderly shutdown writes mempool.dat
    const std::string dat = h.root() + "/chain/mempool.dat";
    std::string bytes;
    ASSERT_TRUE(readWholeFile(dat, bytes));

    std::string err;
    int code = 0;
    h.node = Node::open(h.root() + "/chain", chainParams(NetworkId::REGTEST), {}, err, code);
    ASSERT_TRUE(h.node) << err;
    EXPECT_EQ(h.node->mempoolSize(), 1u);

    // The restored transaction still confirms.
    ASSERT_TRUE(mineTo(*h.node, alice));
    EXPECT_EQ(h.node->mempoolSize(), 0u);
    EXPECT_EQ(h.node->balance(hash160(bob.getPublicKey())), 100'000'000u);

    // A transaction that is already confirmed is dropped on reload, not resurrected.
    h.node.reset();
    writeFile(dat, bytes);   // the stale file from before the block
    h.node = Node::open(h.root() + "/chain", chainParams(NetworkId::REGTEST), {}, err, code);
    ASSERT_TRUE(h.node) << err;
    EXPECT_EQ(h.node->mempoolSize(), 0u);

    // A damaged file never blocks startup.
    h.node.reset();
    bytes[bytes.size() / 2] ^= 0x40;
    writeFile(dat, bytes);
    h.node = Node::open(h.root() + "/chain", chainParams(NetworkId::REGTEST), {}, err, code);
    ASSERT_TRUE(h.node) << err;
    EXPECT_EQ(h.node->mempoolSize(), 0u);
}
