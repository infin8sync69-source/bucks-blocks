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

#include "crypto.h"
#include "protocol_enc.h"

namespace Bucks {

// ------------------------------------------------------------
// Network constants
// ------------------------------------------------------------
constexpr uint32_t MAINNET_MAGIC = 0xD9B4B3F5; // Unique to Bucks
constexpr uint32_t PROTOCOL_VERSION = 70001;

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
 */
struct NetMessageHeader {
    uint32_t magic;
    char command[12];
    uint32_t length;
    uint32_t checksum;

    NetMessageHeader();
    static std::string typeToCommand(MessageType type);
    static MessageType commandToType(const char* cmd);
};

// ------------------------------------------------------------
// Raw network message (framing)
// ------------------------------------------------------------
struct NetMessage {
    MessageType type;
    std::vector<uint8_t> payload;

    NetMessage() : type(MessageType::UNKNOWN) {}
    NetMessage(MessageType t, const std::vector<uint8_t>& p) : type(t), payload(p) {}

    std::vector<uint8_t> serialize() const;
    static NetMessage deserialize(const std::vector<uint8_t>& data);
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
class Blockchain;

class Peer {
public:
    Peer(Network& net, int socket, const std::string& ip, uint16_t port, bool inbound);
    ~Peer();

    void disconnect();
    bool isConnected() const { return connected; }
    PeerState getState() const { return state; }
    void setState(PeerState s) { state = s; }

    bool sendMsg(const NetMessage& msg);
    
    std::string address() const;
    bool isInbound() const { return inbound; }
    const std::string& getIP() const { return ip; }

    // Misbehavior tracking
    void misbehavior(int penalty);
    int getMisbehaviorScore() const { return misbehaviorScore; }
    bool isBanned() const;
    std::chrono::steady_clock::time_point getConnectionTime() const { return connectionTime; }

    // Sync state
    std::chrono::steady_clock::time_point lastBlockRequestTime;
    std::set<Hash256> requestedBlocks;

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
};

// ------------------------------------------------------------
// Network manager (transport + routing)
// ------------------------------------------------------------
class Network {
public:
    explicit Network(Blockchain& chain);
    ~Network();

    void start(uint16_t listenPort);
    void stop();

    // Outbound peer connection
    bool connectToPeer(const std::string& ip, uint16_t port);

    // Message relay
    void broadcast(const NetMessage& msg);

    // Messaging loop integration
    void update();

    Blockchain& getChain() { return chain; }

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
    Blockchain& chain;
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
