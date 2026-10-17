#ifndef NETWORK_H
#define NETWORK_H

#include <array>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <set>
#include <chrono>
#include <map>
#include <functional>
#include <optional>
#include <random>
#include <stdexcept>

#include "chainparams.h"
#include "crypto.h"
#include "protocol_enc.h"
#include "validation_rules.h"

namespace Bucks {

// ------------------------------------------------------------
// Network constants
// ------------------------------------------------------------
constexpr uint32_t PROTOCOL_VERSION = 70002;

// Vicious attack protection constants
constexpr uint32_t MAX_MESSAGE_PAYLOAD = 4 * 1024 * 1024; // 4MB
constexpr uint32_t MAX_PEER_BUFFER = MAX_MESSAGE_PAYLOAD + 1024; // 4MB + safety overhead
constexpr size_t MAX_PEERS = 125;
constexpr size_t MAX_PEERS_PER_IP = 8;
// Outbound connections the node opens on its own (seeds and discovered addresses), and slots for
// --connect peers. Inbound peers can never take these (audit NET-6, NET-12).
constexpr size_t MAX_OUTBOUND_PEERS = 8;
constexpr size_t MAX_MANUAL_PEERS = 8;
constexpr size_t MAX_INBOUND_PEERS = MAX_PEERS - MAX_OUTBOUND_PEERS - MAX_MANUAL_PEERS;
// Automatic outbound peers in one public /16; addresses in a /16 we have no outbound peer in yet
// are dialled first, so one network cannot hold all our outbound slots.
constexpr size_t MAX_OUTBOUND_PER_GROUP = 2;

// Tunables; tests shorten the timers.
struct NetworkOptions {
    size_t maxInbound = MAX_INBOUND_PEERS;
    std::chrono::seconds pingInterval{120};        // to every active peer
    std::chrono::seconds inactivityTimeout{1200};  // nothing received for this long: disconnect
    // Headers a peer may make us store that do not lead to more work than our tip (forks that
    // go nowhere). Every header on main costs 2^32 hashes but is kept for ever (audit NET-NEW-3).
    size_t lowWorkHeaderBudget = 4000;
};

std::vector<uint8_t> serializeInvs(const std::vector<struct Inventory>& invs);

// ------------------------------------------------------------
// Inventory types
// ------------------------------------------------------------
enum class InvType : uint32_t {
    TX    = 1,
    BLOCK = 2
};

struct Inventory {
    InvType type;
    Hash256 hash;
};

// ------------------------------------------------------------
// P2P message types (Bitcoin-style)
// ------------------------------------------------------------
enum class MessageType : uint8_t {
    VERSION    = 0,
    VERACK     = 1,
    INV        = 2,
    GETDATA    = 3,
    GETHEADERS = 4,
    HEADERS    = 5,
    BLOCK      = 6,
    TX         = 7,
    PING       = 8,
    PONG       = 9,
    ADDR       = 10,
    GETADDR    = 11,
    REJECT     = 12,
    ENCINIT    = 13,
    UNKNOWN    = 255
};

/**
 * Message Header (24 bytes)
 * Magic (4) | Command (12) | Length (4) | Checksum (4)
 * The magic is the network's ChainParams::netMagic, in wire order.
 */
struct NetMessageHeader {
    static std::string typeToCommand(MessageType type);
    // Exact match of a 12-byte command field: lowercase letters/digits followed only by zeros.
    // Anything else (including a known command as a prefix of a longer name) is UNKNOWN.
    static MessageType commandToType(const char* cmd);
    // Largest payload accepted for a command (section 11.2); encrypted payloads carry
    // a small fixed overhead on top.
    static uint32_t maxPayload(MessageType type);
};

// Thrown by NetMessage::deserialize. Malformed frames cost the peer reputation;
// an incomplete frame is just not there yet.
struct NetMalformed : std::runtime_error { using std::runtime_error::runtime_error; };
struct NetIncomplete : std::runtime_error { using std::runtime_error::runtime_error; };

// ------------------------------------------------------------
// Raw network message (framing)
// ------------------------------------------------------------
struct NetMessage {
    MessageType type;
    std::vector<uint8_t> payload;

    NetMessage() : type(MessageType::UNKNOWN) {}
    NetMessage(MessageType t, const std::vector<uint8_t>& p) : type(t), payload(p) {}

    std::vector<uint8_t> serialize(const std::array<uint8_t, 4>& magic) const;
    // Checks, in order: 24 bytes present, magic, command, length (in size_t) against the
    // per-command cap, payload present, checksum.
    static NetMessage deserialize(const std::vector<uint8_t>& data, const std::array<uint8_t, 4>& magic);
};

// ------------------------------------------------------------
// Peer addresses (discovery, non-consensus)
// ------------------------------------------------------------

// A numeric IPv4 endpoint, as peers advertise it ("a.b.c.d:port").
struct NetAddress {
    uint32_t ip = 0;              // host byte order
    uint16_t port = 0;

    // Exactly "a.b.c.d:port" with port 1..65535: no names, no IPv6, nothing around it.
    static bool parse(const std::string& s, NetAddress& out);
    static bool parseIP(const std::string& ip, uint32_t& out);
    std::string ipString() const;
    std::string toString() const;
    uint32_t group() const { return ip >> 16; }   // the /16 network, for diversity
    bool isLoopback() const;      // 127/8
    bool isPrivate() const;       // 10/8, 172.16/12, 192.168/16, 100.64/10, 169.254/16
    bool isValid() const;         // a port, and not 0/8, multicast, reserved or broadcast
    bool isRoutable() const;      // valid, public unicast
    // Whether a peer at `source` may tell us about this address: public addresses from anyone,
    // private ones from private or loopback peers, loopback ones from loopback peers only. A
    // remote peer cannot point us at our own LAN or at services on this host.
    bool acceptableFrom(uint32_t source) const;

    friend bool operator==(const NetAddress& a, const NetAddress& b) { return a.ip == b.ip && a.port == b.port; }
    friend bool operator<(const NetAddress& a, const NetAddress& b) {
        return a.ip != b.ip ? a.ip < b.ip : a.port < b.port;
    }
};

// Bounded table of addresses learned from peers (audit NET-NEW-4, NET-11). An address a peer told
// us about is "new"; one we completed a handshake with is "tried". New entries can never push out
// tried ones, and the peers of one /16 can fill only MAX_NEW_PER_SOURCE_GROUP new entries, so one
// flooding source churns only its own share. Thread-safe; times are Unix seconds.
class AddrMan {
public:
    static constexpr size_t MAX_NEW = 1024;
    static constexpr size_t MAX_TRIED = 256;
    static constexpr size_t MAX_NEW_PER_SOURCE_GROUP = 64;

    struct Entry {
        NetAddress addr;
        uint32_t sourceGroup = 0;
        int64_t lastSeen = 0;       // last advertised, or last connected
        int64_t lastTry = 0;
        int64_t lastSuccess = 0;
        int attempts = 0;           // dials since the last handshake
        bool tried = false;
    };

    AddrMan();

    // Adds `a` as new, or refreshes it. True if it was not in the table.
    bool add(const NetAddress& a, uint32_t sourceGroup, int64_t now);
    void attempt(const NetAddress& a, int64_t now);        // a dial starts
    void good(const NetAddress& a, int64_t now);           // a handshake completed
    void remove(const NetAddress& a);
    // A random entry to dial: not excluded by `skip`, past its retry delay (which grows with
    // failed dials), from the tried or the new entries with equal chance. Entries that `prefer`
    // accepts are taken first.
    std::optional<NetAddress> select(int64_t now, const std::function<bool(const NetAddress&)>& skip,
                                     const std::function<bool(const NetAddress&)>& prefer = {});
    // Up to `max` random entries that still look reachable, for a getaddr reply.
    std::vector<NetAddress> sample(size_t max, int64_t now);
    std::optional<Entry> find(const NetAddress& a) const;
    size_t size() const;
    size_t triedCount() const;

    // One line per entry, for peers.dat. load() keeps the bounds above and skips lines it
    // cannot read; it returns how many entries it took.
    std::string serialize() const;
    size_t load(const std::string& text);

private:
    bool isTerrible(const Entry& e, int64_t now) const;
    void evictNew(uint32_t sourceGroup, int64_t now);
    void evictTried();

    mutable std::mutex mu_;
    std::map<NetAddress, Entry> entries_;
    std::map<uint32_t, size_t> newPerSource_;
    size_t newCount_ = 0;
    size_t triedCount_ = 0;
    std::mt19937_64 rng_;
};

// ------------------------------------------------------------
// Peer (transport only, non-consensus)
// ------------------------------------------------------------
enum class PeerState {
    CONNECTING,
    HANDSHAKE,
    ACTIVE,
    DISCONNECTED
};

// How a connection came about. Outbound connections carry the port we dialled.
enum class ConnKind {
    INBOUND,
    MANUAL,     // --connect, or connectToPeer
    SEED,       // --seed, ChainParams::seedNodes
    AUTO,       // an address learned from peers
};

class Network;
class Node;

class Peer {
public:
    Peer(Network& net, int socket, const std::string& ip, uint16_t port, ConnKind kind = ConnKind::INBOUND);
    ~Peer();

    void disconnect();
    PeerState getState() const { return state; }

    bool sendMsg(const NetMessage& msg);
    void askForAddresses();         // getaddr, with the token allowance for the reply

    std::string address() const;
    const std::string& getIP() const { return ip; }
    uint16_t getPort() const { return port; }
    ConnKind kind() const { return kind_; }
    bool outbound() const { return kind_ != ConnKind::INBOUND; }
    // Where the peer accepts connections: the port we dialled, or the one its VERSION gave.
    // Empty for an inbound peer that does not listen (or has not said yet).
    std::optional<NetAddress> listenAddress() const;

    // Misbehavior tracking
    void misbehavior(int penalty);
    bool isBanned() const;
    std::chrono::steady_clock::time_point getConnectionTime() const { return connectionTime; }
    // When the last complete message arrived, and when we last pinged (idle detection).
    std::chrono::steady_clock::time_point lastReceived;
    std::chrono::steady_clock::time_point lastPingSent;

    // Sync state
    std::chrono::steady_clock::time_point lastBlockRequestTime;
    std::chrono::steady_clock::time_point lastHeadersRequestTime;
    bool headersInFlight = false;        // one getheaders at a time; forgotten after 10 s
    std::set<Hash256> requestedBlocks;
    int fd() const { return socket_fd; }

    // Buffer management for non-blocking receive
    std::vector<uint8_t>& getReceiveBuffer() { return receiveBuffer; }
    void processBuffer();

private:
    Network& network;
    int socket_fd;
    std::string ip;
    uint16_t port;
    ConnKind kind_;
    bool connected;
    PeerState state;
    
    std::vector<uint8_t> receiveBuffer;
    std::mutex sendMutex;
    int misbehaviorScore;
    std::chrono::steady_clock::time_point connectionTime;

    // Addresses this peer may still give us: one per token, 0.1 tokens a second, and 1000 more
    // when we ask it (getaddr). The rest of an ADDR is dropped (audit NET-NEW-4).
    double addrTokens = 10;
    std::chrono::steady_clock::time_point addrTokensAt;
    uint16_t listenPort_ = 0;
    bool answeredGetAddr_ = false;
    size_t lowWorkHeaders_ = 0;

    // Encryption (Phase 5)
    std::unique_ptr<P2PSessionEncryption> encryption;
    bool encryptionNegotiated;

    void handleMessage(const NetMessage& msg);
    // Sync helpers (section 8.6): ask for headers with our locator, and for the block
    // data of the best header chain, at most 16 blocks in flight per peer.
    void requestHeaders();
    void requestMissingBlocks();
    bool withinHeaderBudget(const std::vector<BlockHeader>& headers);
};

// ------------------------------------------------------------
// Network manager (transport + routing)
// ------------------------------------------------------------
class Network {
public:
    explicit Network(Node& node, const NetworkOptions& options = {});
    ~Network();

    void start(uint16_t listenPort);
    void stop();

    // Outbound peer connection. Starts the dial and returns: false if the host does not resolve
    // (looked up on the calling thread) or the peer table is full. The network thread completes it.
    bool connectToPeer(const std::string& host, uint16_t port);

    // A peer to keep (--connect): redialled with exponential backoff (5 s up to 5 min) whenever
    // the connection is missing.
    void addPersistentPeer(const std::string& host, uint16_t port);
    // A seed ("host:port"): dialled with backoff (6 s up to 5 min) whenever it is not connected
    // and the node has fewer than MAX_OUTBOUND_PEERS outbound peers. False if it does not parse.
    // Both may be called before or after start().
    bool addSeedNode(const std::string& hostPort);

    // Message relay
    void broadcast(const NetMessage& msg);

    // Messaging loop integration: periodic work of the network thread
    void update();

    Node& getNode() { return node; }
    const NetworkOptions& options() const { return options_; }
    const std::array<uint8_t, 4>& magic() const { return magic_; }

    // How many blocks from peers were rejected with each result category. Loopback peers
    // are never banned, so this is how an invalid block from one is observed.
    void recordRejectedBlock(ValidationResult r) { ++rejectedBlocks_[size_t(r) % 16]; }
    uint64_t rejectedBlocks(ValidationResult r) const { return rejectedBlocks_[size_t(r) % 16].load(); }
    size_t peerCount() const;

    // Queued for the network thread, which sends it outside the peer lock. Used by
    // message handlers (which run under that lock) and by the node's listeners.
    void queueBroadcast(const NetMessage& msg, int exceptFd = -1);

    // Security & Banning
    void banIP(const std::string& ip);
    bool isIPBanned(const std::string& ip) const;

    // Peer discovery: addresses a peer at `sourceIp` told us about go into the address table
    // (never dialled straight away); entries that do not parse or that this source may not
    // give us are dropped. Returns how many were new.
    size_t addAddresses(const std::vector<std::string>& addresses, const std::string& sourceIp,
                        std::vector<NetAddress>* fresh = nullptr);
    AddrMan& addresses() { return addrman_; }
    // For message handlers (network thread, under peersMutex): an address a peer announced
    // (its own, from VERSION) goes into the table and, if it is news, to two other peers.
    void learnAddress(const NetAddress& a, const std::string& sourceIp, int fromFd);
    void relayAddress(const NetAddress& a, int exceptFd);
    // Random per process; a VERSION carrying it means we dialled ourselves.
    uint64_t localNonce() const { return localNonce_; }
    void markSelf(const std::string& hostPort);
    // Keeps the address table in this file across restarts: read now, written every 15 minutes
    // and at stop(). Call before start().
    void setAddressFile(const std::string& path);
    // Whether one more automatic outbound peer at `a` would exceed MAX_OUTBOUND_PER_GROUP, given
    // how many outbound peers each /16 has. Loopback and LAN addresses are not limited.
    static bool outboundGroupFull(const NetAddress& a, const std::map<uint32_t, size_t>& outboundGroups);

    // Peer discovery access (for Peer class)
    std::mutex peersMutex;
    std::unordered_map<int, std::unique_ptr<Peer>> peers;
    uint16_t listenPort;

private:
    Node& node;
    NetworkOptions options_;
    std::array<uint8_t, 4> magic_;
    std::array<std::atomic<uint64_t>, 16> rejectedBlocks_{};
    size_t tipListenerId_ = 0;
    size_t txListenerId_ = 0;
    std::mutex queueMutex;
    std::vector<std::pair<NetMessage, int>> relayQueue;
    void flushQueues();
    std::atomic<bool> running;
    int listenSocket;

    // Security
    std::set<std::string> bannedIPs;
    std::map<std::string, std::chrono::steady_clock::time_point> lastConnectionTimes;
    mutable std::mutex securityMutex;

    // Peer Discovery (Phase 6)
    AddrMan addrman_;
    uint64_t localNonce_ = 0;
    std::mt19937_64 relayRng_;              // network thread only
    std::set<NetAddress> selfAddresses_;    // network thread only
    void dialFromAddressTable(size_t outbound, const std::set<NetAddress>& linked,
                              const std::map<uint32_t, size_t>& outboundGroups);
    void sendPings();
    std::string addressFile_;
    std::chrono::steady_clock::time_point lastAddressSave_;
    void saveAddresses();

    // --connect peers (MANUAL) and seeds (SEED), redialled by maintainConnections. Guarded by
    // peersMutex; entries are only ever appended.
    struct DialTarget {
        std::string host;
        uint16_t port;
        ConnKind kind;
        std::chrono::steady_clock::time_point nextAttempt;
        int backoffSeconds;
    };
    std::vector<DialTarget> dialTargets_;
    void addDialTarget(const std::string& host, uint16_t port, ConnKind kind);
    std::chrono::steady_clock::time_point lastMaintain_;
    void maintainConnections();

    // Dials in progress: non-blocking connects in run()'s poll() set. Network thread only.
    struct PendingConnect {
        int fd;
        std::string ip;
        uint16_t port;
        ConnKind kind;
        std::chrono::steady_clock::time_point deadline;
    };
    std::vector<PendingConnect> pending_;
    struct QueuedDial {
        std::string ip;
        uint16_t port;
        ConnKind kind;
    };
    std::vector<QueuedDial> dialQueue_;   // from connectToPeer; guarded by queueMutex
    bool startConnect(const std::string& ip, uint16_t port, ConnKind kind);
    void completeConnects(const std::vector<short>& revents);

    // Host names of seeds and --connect peers are looked up on this thread, never on the
    // network thread.
    struct DnsEntry {
        std::string ip;                   // last good answer
        bool ok = false;                  // the last lookup succeeded
        bool asked = false;
        bool queued = false;
        std::chrono::steady_clock::time_point at;
    };
    std::mutex dnsMutex_;
    std::condition_variable dnsCv_;
    std::map<std::string, DnsEntry> dnsCache_;
    std::vector<std::string> dnsQueue_;
    std::thread dnsThread_;
    bool lookupHost(const std::string& host, std::string& ip);
    void resolverLoop();

    std::thread networkThread;
    
    void run();
    void setupListenSocket();
    void acceptNewConnection();
    void handleSocketIO();
    void pruneZombiePeers();
    void reapDisconnectedPeers();
};

} // namespace Bucks

#endif // NETWORK_H
