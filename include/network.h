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
// Peer (transport only, non-consensus)
// ------------------------------------------------------------
enum class PeerState {
    CONNECTING,
    HANDSHAKE,
    ACTIVE,
    DISCONNECTED
};

class Network;
class Node;

class Peer {
public:
    Peer(Network& net, int socket, const std::string& ip, uint16_t port, bool inbound);
    ~Peer();

    void disconnect();
    PeerState getState() const { return state; }

    bool sendMsg(const NetMessage& msg);
    
    std::string address() const;
    const std::string& getIP() const { return ip; }

    // Misbehavior tracking
    void misbehavior(int penalty);
    bool isBanned() const;
    std::chrono::steady_clock::time_point getConnectionTime() const { return connectionTime; }

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
    bool connected;
    bool inbound;
    PeerState state;
    
    std::vector<uint8_t> receiveBuffer;
    std::mutex sendMutex;
    int misbehaviorScore;
    std::chrono::steady_clock::time_point connectionTime;
    
    // Encryption (Phase 5)
    std::unique_ptr<P2PSessionEncryption> encryption;
    bool encryptionNegotiated;

    void handleMessage(const NetMessage& msg);
    // Sync helpers (section 8.6): ask for headers with our locator, and for the block
    // data of the best header chain, at most 16 blocks in flight per peer.
    void requestHeaders();
    void requestMissingBlocks();
};

// ------------------------------------------------------------
// Network manager (transport + routing)
// ------------------------------------------------------------
class Network {
public:
    explicit Network(Node& node);
    ~Network();

    void start(uint16_t listenPort);
    void stop();

    // Outbound peer connection
    bool connectToPeer(const std::string& ip, uint16_t port);

    // Message relay
    void broadcast(const NetMessage& msg);

    // Messaging loop integration
    void update();

    Node& getNode() { return node; }
    const std::array<uint8_t, 4>& magic() const { return magic_; }

    // How many blocks from peers were rejected with each result category. Loopback peers
    // are never banned, so this is how an invalid block from one is observed.
    void recordRejectedBlock(ValidationResult r) { ++rejectedBlocks_[size_t(r) % 16]; }
    uint64_t rejectedBlocks(ValidationResult r) const { return rejectedBlocks_[size_t(r) % 16].load(); }
    size_t peerCount() const;

    // Queued for the network thread, which sends it outside the peer lock. Used by
    // message handlers (which run under that lock) and by the node's listeners.
    void queueBroadcast(const NetMessage& msg, int exceptFd = -1);
    void queueAddresses(const std::vector<std::string>& addresses);

    // Security & Banning
    void banIP(const std::string& ip);
    bool isIPBanned(const std::string& ip) const;

    // Peer discovery (Phase 6)
    void handleAddrMessage(const std::vector<std::string>& addresses);

    // Peer discovery access (for Peer class)
    std::mutex peersMutex;
    std::set<std::string> knownPeers;
    std::unordered_map<int, std::unique_ptr<Peer>> peers;
    uint16_t listenPort;

private:
    Node& node;
    std::array<uint8_t, 4> magic_;
    std::array<std::atomic<uint64_t>, 16> rejectedBlocks_{};
    size_t tipListenerId_ = 0;
    size_t txListenerId_ = 0;
    std::mutex queueMutex;
    std::vector<std::pair<NetMessage, int>> relayQueue;
    std::vector<std::vector<std::string>> addrQueue;
    void flushQueues();
    std::atomic<bool> running;
    int listenSocket;

    // Security
    std::set<std::string> bannedIPs;
    std::map<std::string, std::chrono::steady_clock::time_point> lastConnectionTimes;
    mutable std::mutex securityMutex;

    // Peer Discovery (Phase 6)
    std::vector<std::string> seedNodes;
    std::chrono::steady_clock::time_point lastDiscoveryTime;
    void discoverPeers();
    void loadSeedNodes();

    std::thread networkThread;
    
    void run();
    void setupListenSocket();
    void acceptNewConnection();
    void handleSocketIO();
    void pruneZombiePeers();
};

} // namespace Bucks

#endif // NETWORK_H
