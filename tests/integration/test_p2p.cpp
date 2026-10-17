#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <future>
#include <poll.h>
#include <thread>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "consensus.h"
#include "network.h"
#include "node_harness.h"
#include "pow.h"
#include "test_chain.h"
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

// Audit CON-1: B knows a header chain with more work whose blocks nobody has. It used to ask every peer for
// those blocks only, so it never fetched the real chain an honest peer offered, and dropped that peer as
// stalling after 60 s. Blocks are now asked only from the peer that announced them.
TEST(Network, PhantomHeadersDoNotStopTheDownloadFromAnHonestPeer) {
    TwoNodes t;
    ASSERT_TRUE(t.start());
    TestChain phantom;   // same regtest genesis; its blocks are never sent anywhere
    std::vector<BlockHeader> headers;
    for (int i = 0; i < 5; ++i) {
        const Block b = phantom.extend({}, 9);
        headers.push_back(b.header);
    }
    ASSERT_TRUE(t.b.node->submitHeaders(headers).valid());

    Wallet w;
    w.generateKeys();
    for (int i = 0; i < 2; ++i) ASSERT_TRUE(mineOne(*t.a.node, w));   // less work than the phantom headers

    ASSERT_TRUE(t.connectBToA());
    bool askedForPhantom = false;
    auto synced = [&] {
        {
            std::lock_guard<std::mutex> lock(t.netB->peersMutex);
            for (auto& kv : t.netB->peers)
                for (const BlockHeader& h : headers) askedForPhantom |= kv.second->requestedBlocks.count(h.hash()) > 0;
        }
        return t.b.node->tip().hash == t.a.node->tip().hash;
    };
    EXPECT_TRUE(waitFor(synced)) << "B at " << t.b.node->height();
    EXPECT_FALSE(askedForPhantom);
    EXPECT_EQ(t.netB->peerCount(), 1u);
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

// A peer that vanishes or never reads must not kill or stall the node (SIGPIPE, short writes).
TEST(P2pSend, ClosedPeerIsAnErrorNotASignal) {
    NodeHarness h;
    ASSERT_TRUE(h.node) << h.error;
    Network net(*h.node);
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    Peer peer(net, fds[0], "127.0.0.1", 1);
    close(fds[1]);   // the other side is gone; a plain send() would raise SIGPIPE here
    EXPECT_FALSE(peer.sendMsg(NetMessage(MessageType::BLOCK, std::vector<uint8_t>(100'000, 7))));
    EXPECT_EQ(peer.getState(), PeerState::DISCONNECTED);
}

TEST(P2pSend, PeerThatNeverReadsIsDroppedWithinTheDeadline) {
    NodeHarness h;
    ASSERT_TRUE(h.node) << h.error;
    Network net(*h.node);
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    Peer peer(net, fds[0], "127.0.0.1", 1);
    const auto start = std::chrono::steady_clock::now();
    // Far more than the socket buffers hold; nobody reads fds[1].
    EXPECT_FALSE(peer.sendMsg(NetMessage(MessageType::BLOCK, std::vector<uint8_t>(8'000'000, 7))));
    const auto took = std::chrono::steady_clock::now() - start;
    EXPECT_LT(took, std::chrono::seconds(8));
    EXPECT_EQ(peer.getState(), PeerState::DISCONNECTED);
    close(fds[1]);
}

// --connect keeps trying: a peer that is down at startup is found when it comes up, and a
// connection that drops is dialled again.
TEST(Network, PersistentPeerIsRedialled) {
    NodeHarness a, b;
    ASSERT_TRUE(a.node && b.node);
    Network netB(*b.node);
    const uint16_t portA = freePort();
    const uint16_t portB = freePort();
    netB.start(portB);
    netB.addPersistentPeer("127.0.0.1", portA);   // nobody listens there yet
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    EXPECT_EQ(netB.peerCount(), 0u);

    auto netA = std::make_unique<Network>(*a.node);
    netA->start(portA);
    ASSERT_TRUE(waitFor([&] { return netA->peerCount() == 1 && netB.peerCount() == 1; }, 30))
        << "B never reached A";

    // A goes away and comes back on the same port.
    netA->stop();
    netA.reset();
    ASSERT_TRUE(waitFor([&] { return netB.peerCount() == 0; }, 30));
    netA = std::make_unique<Network>(*a.node);
    netA->start(portA);
    EXPECT_TRUE(waitFor([&] { return netA->peerCount() == 1 && netB.peerCount() == 1; }, 60))
        << "B did not dial A again";
    netB.stop();
    netA->stop();
}

// ---------------------------------------------------------------------------
// Audit findings NET-1, NET-2, NET-3 (reports/audit-2026-10-14/network.md): each was proven against a live
// node by the auditor; these tests replay the same attacks.
// ---------------------------------------------------------------------------

namespace {

int rawConnect(uint16_t port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) { close(s); return -1; }
    return s;
}

// Bytes received within `ms`, up to `want`.
size_t readSome(int s, size_t want, int ms) {
    size_t got = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    char buf[512];
    while (got < want && std::chrono::steady_clock::now() < end) {
        pollfd p{s, POLLIN, 0};
        if (poll(&p, 1, 100) > 0) {
            ssize_t n = recv(s, buf, sizeof(buf), 0);
            if (n <= 0) break;
            got += size_t(n);
        }
    }
    return got;
}

std::vector<uint8_t> frameHeader(const Network& net, const char* cmd, uint32_t len) {
    std::vector<uint8_t> h(24, 0);
    std::copy(net.magic().begin(), net.magic().end(), h.begin());
    std::memcpy(h.data() + 4, cmd, std::min<size_t>(std::strlen(cmd), 12));
    for (int i = 0; i < 4; ++i) h[16 + i] = uint8_t(len >> (8 * i));
    return h;
}

} // namespace

// NET-1: one oversized frame header used to leave a closed descriptor in the peer table, so select() failed
// forever and the network thread spun at 100% CPU, deaf to every peer and unable to stop.
TEST(P2pAudit, OversizedFrameDoesNotWedgeTheNetworkThread) {
    NodeHarness h;
    ASSERT_TRUE(h.node) << h.error;
    Network net(*h.node);
    const uint16_t port = freePort();
    net.start(port);

    int attacker = rawConnect(port);
    ASSERT_GE(attacker, 0);
    auto hdr = frameHeader(net, "ping", 5'000'000);
    ASSERT_EQ(send(attacker, hdr.data(), hdr.size(), 0), ssize_t(hdr.size()));
    // Connections from one address closer than 5 s apart are throttled, so wait that out before the honest one.
    std::this_thread::sleep_for(std::chrono::milliseconds(5500));

    // A well-behaved client is still served: the node sends its key-exchange frame at once.
    int honest = rawConnect(port);
    ASSERT_GE(honest, 0);
    EXPECT_GE(readSome(honest, 24, 3000), 24u) << "the network thread is wedged";
    close(honest);
    close(attacker);

    // And it still stops promptly.
    auto stopper = std::async(std::launch::async, [&] { net.stop(); });
    EXPECT_EQ(stopper.wait_for(std::chrono::seconds(5)), std::future_status::ready) << "stop() hangs";
    stopper.wait();
}

// NET-2: a peer-supplied address with a bad port used to throw out of std::stoi and terminate the node.
TEST(P2pAudit, BadAddressesFromAPeerDoNotThrow) {
    NodeHarness h;
    ASSERT_TRUE(h.node) << h.error;
    Network net(*h.node);
    EXPECT_NO_THROW(net.handleAddrMessage({"1.2.3.4:notaport", "1.2.3.4:99999", "1.2.3.4:", ":80", "1.2.3.4:-5",
                                           "no-colon", std::string(400, 'a') + ":80", "1.2.3.4:99999999999999999999"}));
}

// NET-3: a peer that never does the key exchange used to be served in plaintext for the whole session.
TEST(P2pAudit, PlaintextWithoutKeyExchangeIsRefused) {
    NodeHarness h;
    ASSERT_TRUE(h.node) << h.error;
    Network net(*h.node);
    const uint16_t port = freePort();
    net.start(port);

    int s = rawConnect(port);
    ASSERT_GE(s, 0);
    ASSERT_GE(readSome(s, 24, 3000), 24u);   // the node's own ENCINIT; we never answer it
    NetMessage version(MessageType::VERSION, std::vector<uint8_t>{0, 0, 0, 0});
    auto frame = version.serialize(net.magic());
    ASSERT_EQ(send(s, frame.data(), frame.size(), 0), ssize_t(frame.size()));

    // The node must drop us: the next read ends with EOF and the peer table empties.
    char c;
    bool closed = false;
    for (int i = 0; i < 30 && !closed; ++i) {
        pollfd p{s, POLLIN, 0};
        if (poll(&p, 1, 100) > 0) closed = recv(s, &c, 1, 0) == 0;
    }
    EXPECT_TRUE(closed) << "plaintext was accepted";
    EXPECT_TRUE(waitFor([&] { return net.peerCount() == 0; }, 5));
    close(s);
    net.stop();
}
