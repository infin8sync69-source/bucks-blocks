#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <future>
#include <poll.h>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "consensus.h"
#include "network.h"
#include "node_harness.h"
#include "pow.h"
#include "serialize.h"
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

// NET-NEW-1, NET-NEW-8: seeds used to be dialled once at start and then every 30 minutes, so a seed that was down
// at start or restarted later left seed-only nodes alone for that long. A seed is now redialled with backoff, also
// when it is added after start().
TEST(Network, SeedIsRedialled) {
    NodeHarness a, b;
    ASSERT_TRUE(a.node && b.node);
    Network netB(*b.node);
    const uint16_t portA = freePort();
    netB.start(freePort());
    EXPECT_FALSE(netB.addSeedNode("127.0.0.1"));
    EXPECT_FALSE(netB.addSeedNode("127.0.0.1:0"));
    ASSERT_TRUE(netB.addSeedNode("127.0.0.1:" + std::to_string(portA)));   // nobody listens there yet
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    EXPECT_EQ(netB.peerCount(), 0u);

    auto netA = std::make_unique<Network>(*a.node);
    netA->start(portA);
    ASSERT_TRUE(waitFor([&] { return netA->peerCount() == 1 && netB.peerCount() == 1; }, 30))
        << "B never reached the seed";

    // The seed restarts on the same port.
    netA->stop();
    netA.reset();
    ASSERT_TRUE(waitFor([&] { return netB.peerCount() == 0; }, 30));
    netA = std::make_unique<Network>(*a.node);
    netA->start(portA);
    EXPECT_TRUE(waitFor([&] { return netA->peerCount() == 1 && netB.peerCount() == 1; }, 60))
        << "B did not dial the seed again";
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

// A listener that never accepts, with its accept queue full: the kernel drops further SYNs, so a dial to it
// neither succeeds nor fails until the dialler gives up (an address behind a firewall that drops packets).
struct BlackHole {
    int listener = -1;
    std::vector<int> fillers;
    uint16_t port = 0;
    BlackHole() {
        listener = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(listener, 0) != 0) return;
        socklen_t len = sizeof(a);
        getsockname(listener, reinterpret_cast<sockaddr*>(&a), &len);
        for (int i = 0; i < 8; ++i) {
            int s = socket(AF_INET, SOCK_STREAM, 0);
            fcntl(s, F_SETFL, O_NONBLOCK);
            connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
            fillers.push_back(s);
            pollfd p{s, POLLOUT, 0};
            if (poll(&p, 1, 300) == 0) {   // this SYN went unanswered: the queue is full
                port = ntohs(a.sin_port);
                return;
            }
        }
    }
    ~BlackHole() {
        for (int s : fillers) close(s);
        if (listener >= 0) close(listener);
    }
};

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
    size_t added = 1;
    EXPECT_NO_THROW(added = net.addAddresses({"1.2.3.4:notaport", "1.2.3.4:99999", "1.2.3.4:", ":80", "1.2.3.4:-5",
                                              "no-colon", std::string(400, 'a') + ":80",
                                              "1.2.3.4:99999999999999999999"}, "8.8.8.8"));
    EXPECT_EQ(added, 0u);
    EXPECT_EQ(net.addresses().size(), 0u);
}

namespace {

std::vector<uint8_t> addrPayload(const std::vector<std::string>& list) {
    Writer w;
    w.u32(uint32_t(list.size()));
    for (const std::string& s : list) {
        w.u16(uint16_t(s.size()));
        w.bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }
    return w.take();
}

} // namespace

// NET-11, NET-NEW-4: an address from a peer goes into the address table and is dialled from there.
TEST(P2pAudit, AddressFromAPeerIsDialledFromTheTable) {
    NodeHarness a, b, c;
    ASSERT_TRUE(a.node && b.node && c.node);
    Network netA(*a.node), netB(*b.node), netC(*c.node);
    const uint16_t portA = freePort(), portC = freePort();
    netA.start(portA);
    netB.start(freePort());
    netC.start(portC);
    ASSERT_TRUE(netB.connectToPeer("127.0.0.1", portA));
    ASSERT_TRUE(waitFor([&] { return netA.peerCount() == 1 && netB.peerCount() == 1; }));

    sendToAll(netB, NetMessage(MessageType::ADDR, addrPayload({"127.0.0.1:" + std::to_string(portC)})));
    EXPECT_TRUE(waitFor([&] { return netC.peerCount() >= 1 && netA.peerCount() >= 2; }))
        << "A did not dial the address B gave it";
    NetAddress ac;
    ASSERT_TRUE(NetAddress::parse("127.0.0.1:" + std::to_string(portC), ac));
    auto e = netA.addresses().find(ac);
    ASSERT_TRUE(e);
    EXPECT_TRUE(e->tried);
    netC.stop();
    netB.stop();
    netA.stop();
}

// A node that learns its own address (here from a peer) dials it once, sees its own nonce in the
// VERSION and never uses that address again.
TEST(P2pAudit, DialToSelfIsDetected) {
    TwoNodes t;
    ASSERT_TRUE(t.start());
    ASSERT_TRUE(t.connectBToA());
    const std::string selfA = "127.0.0.1:" + std::to_string(t.portA);
    sendToAll(*t.netB, NetMessage(MessageType::ADDR, addrPayload({selfA})));
    NetAddress a;
    ASSERT_TRUE(NetAddress::parse(selfA, a));
    ASSERT_TRUE(waitFor([&] { return !t.netA->addresses().find(a); }, 15)) << "A kept its own address";
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    EXPECT_EQ(t.netA->peerCount(), 1u);
    EXPECT_FALSE(t.netA->addresses().find(a));
    // Learning it again changes nothing.
    sendToAll(*t.netB, NetMessage(MessageType::ADDR, addrPayload({selfA})));
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    EXPECT_FALSE(t.netA->addresses().find(a));
    EXPECT_EQ(t.netA->peerCount(), 1u);
}

// NET-NEW-2: nodes that know only the seed find each other through it, so the network is not a star
// around the seed and survives the seed going away and coming back.
TEST(Network, NodesFindEachOtherThroughTheSeed) {
    NodeHarness hs, ha, hb, hc;
    ASSERT_TRUE(hs.node && ha.node && hb.node && hc.node);
    const uint16_t portS = freePort();
    auto netS = std::make_unique<Network>(*hs.node);
    netS->start(portS);
    std::vector<std::unique_ptr<Network>> nets;
    for (NodeHarness* h : {&ha, &hb, &hc}) {
        nets.push_back(std::make_unique<Network>(*h->node));
        ASSERT_TRUE(nets.back()->addSeedNode("127.0.0.1:" + std::to_string(portS)));
        nets.back()->start(freePort());
    }
    auto meshed = [&] {
        for (auto& n : nets)
            if (n->peerCount() < 3) return false;   // the seed and the two others
        return true;
    };
    ASSERT_TRUE(waitFor(meshed, 30)) << nets[0]->peerCount() << " " << nets[1]->peerCount() << " "
                                     << nets[2]->peerCount();

    // Without the seed the three stay connected.
    netS->stop();
    netS.reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    for (auto& n : nets) EXPECT_GE(n->peerCount(), 2u);

    // The seed comes back and every node dials it again.
    netS = std::make_unique<Network>(*hs.node);
    netS->start(portS);
    EXPECT_TRUE(waitFor([&] { return netS->peerCount() >= 3 && meshed(); }, 60))
        << "seed has " << netS->peerCount() << " peers";
    for (auto& n : nets) n->stop();
    netS->stop();
}

// NET-6, NET-12: inbound and outbound shared one cap, so enough inbound connections left no room for the
// node's own dials (seeds, --connect). Inbound peers now have their own limit.
TEST(P2pAudit, InboundPeersCannotTakeOutboundSlots) {
    NodeHarness a, c;
    ASSERT_TRUE(a.node && c.node);
    NetworkOptions small;
    small.maxInbound = 2;
    Network netA(*a.node, small), netC(*c.node);
    const uint16_t portA = freePort(), portC = freePort();
    netA.start(portA);
    netC.start(portC);
    std::vector<int> crowd;
    for (int i = 0; i < 4; ++i) crowd.push_back(rawConnect(portA));
    // Two are kept (their key-exchange frame arrives), the others are closed.
    size_t served = 0;
    for (int s : crowd) served += readSome(s, 24, 1000) >= 24 ? 1 : 0;
    EXPECT_EQ(served, 2u);

    ASSERT_TRUE(netA.connectToPeer("127.0.0.1", portC));
    EXPECT_TRUE(waitFor([&] { return netC.peerCount() == 1; })) << "the outbound dial found no slot";
    for (int s : crowd) close(s);
    netC.stop();
    netA.stop();
}

// NET-12: an active peer that stops talking held its slot for ever. Peers are pinged, and one that sends
// nothing for the inactivity timeout is dropped; a healthy idle peer answers the pings and stays.
TEST(P2pAudit, SilentPeerIsDropped) {
    NodeHarness a, b;
    ASSERT_TRUE(a.node && b.node);
    NetworkOptions fast;
    fast.pingInterval = std::chrono::seconds(1);
    fast.inactivityTimeout = std::chrono::seconds(3);
    Network netA(*a.node, fast), netB(*b.node, fast);
    const uint16_t portA = freePort();
    netA.start(portA);
    netB.start(freePort());
    ASSERT_TRUE(netB.connectToPeer("127.0.0.1", portA));
    ASSERT_TRUE(waitFor([&] { return netA.peerCount() == 1 && netB.peerCount() == 1; }));
    std::this_thread::sleep_for(std::chrono::seconds(5));
    EXPECT_EQ(netA.peerCount(), 1u) << "an idle but healthy peer was dropped";

    {
        // B's network thread stops: it reads nothing and answers nothing.
        std::lock_guard<std::mutex> freeze(netB.peersMutex);
        EXPECT_TRUE(waitFor([&] { return netA.peerCount() == 0; }, 8)) << "A kept a silent peer";
    }
    netB.stop();
    netA.stop();
}

// NET-NEW-2: the address table is kept across restarts, so a node whose seed is down at start can still
// reach the peers it knew.
TEST(Network, AddressTableSurvivesRestart) {
    NodeHarness h;
    ASSERT_TRUE(h.node) << h.error;
    const std::string file = h.root() + "/peers.dat";
    {
        Network net(*h.node);
        net.setAddressFile(file);
        EXPECT_EQ(net.addAddresses({"9.9.9.9:8733", "8.8.8.8:8733"}, "1.1.1.1"), 2u);
    }   // written when the network goes away
    Network again(*h.node);
    again.setAddressFile(file);
    EXPECT_EQ(again.addresses().size(), 2u);
}

// NET-NEW-4: a peer cannot fill the table; what is beyond its allowance is dropped unread.
TEST(P2pAudit, AddressFloodIsRateLimited) {
    TwoNodes t;
    ASSERT_TRUE(t.start());
    ASSERT_TRUE(t.connectBToA());
    const size_t before = t.netA->addresses().size();   // B's own listen address
    std::vector<std::string> flood;
    for (int i = 0; i < 1000; ++i) flood.push_back("11." + std::to_string(i / 256) + "." + std::to_string(i % 256) + ".1:8733");
    sendToAll(*t.netB, NetMessage(MessageType::ADDR, addrPayload(flood)));
    ASSERT_TRUE(waitFor([&] { return t.netA->addresses().size() >= before + 10; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(t.netA->addresses().size(), before + 10);
    EXPECT_EQ(t.netA->peerCount(), 1u) << "the flooding peer is not punished for an unsolicited ADDR";
}

// NET-4, NET-11: dials ran one after another on the network thread and each waited up to 3 s, so addresses that
// never answer stalled every peer (a PING took 60 s to be answered). Dials now finish in the background.
TEST(P2pAudit, UnreachableDialsDoNotStallTheNetworkThread) {
    NodeHarness h;
    ASSERT_TRUE(h.node) << h.error;
    Network net(*h.node);
    std::vector<std::unique_ptr<BlackHole>> holes;
    for (int i = 0; i < 4; ++i) {
        holes.push_back(std::make_unique<BlackHole>());
        ASSERT_NE(holes.back()->port, 0) << "could not build a black hole";
        ASSERT_TRUE(net.addSeedNode("127.0.0.1:" + std::to_string(holes.back()->port)));
    }
    const uint16_t port = freePort();
    net.start(port);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));   // all four seed dials are under way

    const auto start = std::chrono::steady_clock::now();
    int honest = rawConnect(port);
    ASSERT_GE(honest, 0);
    EXPECT_GE(readSome(honest, 24, 1500), 24u) << "the network thread is busy dialling";
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(1500));
    close(honest);

    auto stopper = std::async(std::launch::async, [&] { net.stop(); });
    EXPECT_EQ(stopper.wait_for(std::chrono::seconds(5)), std::future_status::ready) << "stop() hangs";
    stopper.wait();
}

// Seed host names are looked up by the resolver thread, not the network thread.
TEST(Network, SeedByHostName) {
    NodeHarness a, b;
    ASSERT_TRUE(a.node && b.node);
    Network netA(*a.node), netB(*b.node);
    const uint16_t portA = freePort();
    netA.start(portA);
    ASSERT_TRUE(netB.addSeedNode("localhost:" + std::to_string(portA)));
    netB.start(freePort());
    EXPECT_TRUE(waitFor([&] { return netA.peerCount() == 1 && netB.peerCount() == 1; }, 30));
    netB.stop();
    netA.stop();
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
