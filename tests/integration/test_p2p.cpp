#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "consensus.h"
#include "network.h"
#include "node_harness.h"
#include "pow.h"
#include "wallet.h"

using namespace Bucks;
using namespace Bucks::TestUtil;

// Spec: docs/design/consensus-validation.md section 15.8 (P2P-1, P2P-2).

namespace {

uint16_t freePort() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    socklen_t len = sizeof(a);
    getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
    const uint16_t port = ntohs(a.sin_port);
    close(s);
    return port;
}

template <class F>
bool waitFor(F cond, int seconds = 15) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return cond();
}

bool mineOne(Node& node, const Wallet& w) {
    std::atomic<bool> stop{false};
    std::string err;
    return node.mine(w.getP2PKHScript(), {}, stop, err) == MineResult::FOUND;
}

// Sends a message to every peer of `net` (peersMutex is public; Peer::sendMsg locks its own mutex).
void sendToAll(Network& net, const NetMessage& msg) {
    std::lock_guard<std::mutex> lock(net.peersMutex);
    for (auto& kv : net.peers) kv.second->sendMsg(msg);
}

struct TwoNodes {
    NodeHarness a, b;
    std::unique_ptr<Network> netA, netB;
    uint16_t portA = 0, portB = 0;

    bool start() {
        if (!a.node || !b.node) return false;
        netA = std::make_unique<Network>(*a.node);
        netB = std::make_unique<Network>(*b.node);
        portA = freePort();
        portB = freePort();
        netA->start(portA);
        netB->start(portB);
        return true;
    }
    bool connectBToA() {
        if (!netB->connectToPeer("127.0.0.1", portA)) return false;
        return waitFor([&] { return netA->peerCount() == 1 && netB->peerCount() == 1; });
    }
    ~TwoNodes() {
        if (netB) netB->stop();
        if (netA) netA->stop();
    }
};

} // namespace

// P2P-2: two nodes mine different branches; after they connect both are on the branch with more work.
TEST(Network, ConvergeAfterFork) {
    TwoNodes t;
    ASSERT_TRUE(t.start());
    Wallet wa, wb;
    wa.generateKeys();
    wb.generateKeys();
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(mineOne(*t.a.node, wa));
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(mineOne(*t.b.node, wb));
    ASSERT_NE(t.a.node->tip().hash, t.b.node->tip().hash);

    ASSERT_TRUE(t.connectBToA());
    ASSERT_TRUE(waitFor([&] { return t.a.node->tip().hash == t.b.node->tip().hash; }))
        << "A at " << t.a.node->height() << ", B at " << t.b.node->height();
    EXPECT_EQ(t.a.node->height(), 4u);
    EXPECT_EQ(t.a.node->tip().stats, t.b.node->tip().stats);
    // No block was held against anybody.
    for (ValidationResult r : {ValidationResult::INVALID, ValidationResult::MALFORMED, ValidationResult::MUTATED}) {
        EXPECT_EQ(t.netA->rejectedBlocks(r), 0u);
        EXPECT_EQ(t.netB->rejectedBlocks(r), 0u);
    }
}

// A new block mined on one node is announced and fetched by the other.
TEST(Network, NewBlockIsAnnouncedAndFetched) {
    TwoNodes t;
    ASSERT_TRUE(t.start());
    ASSERT_TRUE(t.connectBToA());
    Wallet w;
    w.generateKeys();
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(mineOne(*t.a.node, w));
        ASSERT_TRUE(waitFor([&] { return t.b.node->tip().hash == t.a.node->tip().hash; })) << i;
    }
    EXPECT_EQ(t.b.node->height(), 3u);
}

// P2P-1: a block with a bad coinbase is counted in the INVALID category.
TEST(Network, InvalidBlockCategory) {
    TwoNodes t;
    ASSERT_TRUE(t.start());
    ASSERT_TRUE(t.connectBToA());

    // A block on the genesis with a one-byte coinbase scriptSig (rule bad-cb-length), properly mined.
    const Node::TipInfo tip = t.a.node->tip();
    Transaction cb;
    TransactionInput in;
    in.prevIndex = 0xFFFFFFFF;
    in.scriptSig = {0x01};
    cb.inputs.push_back(in);
    Wallet w;
    w.generateKeys();
    cb.outputs.emplace_back(50 * Consensus::COIN, w.getP2PKHScript());
    Block bad;
    bad.header.height = tip.height + 1;
    bad.header.prevBlockHash = tip.hash;
    bad.header.timestamp = tip.time + 600;
    bad.header.bits = tip.bits;
    bad.transactions = {cb};
    bad.header.merkleRoot = bad.computeMerkleRoot();
    const ChainParams& p = t.a.node->params();
    for (uint32_t n = 0;; ++n) {
        bad.header.nonce = n;
        if (Pow::checkProofOfWork(bad.header.hash(), bad.header.bits, p.powLimit)) break;
    }

    sendToAll(*t.netA, NetMessage(MessageType::BLOCK, bad.serialize()));
    ASSERT_TRUE(waitFor([&] { return t.netB->rejectedBlocks(ValidationResult::INVALID) == 1; }));
    EXPECT_EQ(t.b.node->height(), 0u);                       // nothing was added
    EXPECT_EQ(t.netB->rejectedBlocks(ValidationResult::MALFORMED), 0u);

    // Bytes that are not a block at all are MALFORMED traffic: no block counter moves, the peer is scored.
    sendToAll(*t.netA, NetMessage(MessageType::BLOCK, std::vector<uint8_t>(200, 0x41)));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(t.netB->rejectedBlocks(ValidationResult::INVALID), 1u);
    EXPECT_EQ(t.b.node->height(), 0u);
}

// A peer on the wrong network never gets a connection as far as an exchange of messages.
TEST(Network, DifferentNetworkMagicIsIgnored) {
    NodeHarness a;                                   // regtest
    NodeHarness b(chainParams(NetworkId::TEST));     // test network (separate magic)
    ASSERT_TRUE(a.node) << a.error;
    ASSERT_TRUE(b.node) << b.error;
    Network netA(*a.node), netB(*b.node);
    const uint16_t pa = freePort();
    netA.start(pa);
    netB.start(freePort());
    ASSERT_TRUE(netB.connectToPeer("127.0.0.1", pa));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    EXPECT_EQ(netA.peerCount(), 0u);
    EXPECT_EQ(netB.peerCount(), 0u);
    netB.stop();
    netA.stop();
}
