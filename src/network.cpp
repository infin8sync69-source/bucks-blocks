#include "network.h"
#include "blockchain.h"
#include <iostream>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#endif

namespace Bucks {

// ------------------------------------------------------------
// NetMessageHeader
// ------------------------------------------------------------

NetMessageHeader::NetMessageHeader() : magic(MAINNET_MAGIC), length(0), checksum(0) {
    memset(command, 0, 12);
}

std::string NetMessageHeader::typeToCommand(MessageType type) {
    switch (type) {
        case MessageType::VERSION:    return "version";
        case MessageType::VERACK:     return "verack";
        case MessageType::INV:        return "inv";
        case MessageType::GETDATA:    return "getdata";
        case MessageType::GETHEADERS: return "getheaders";
        case MessageType::HEADERS:    return "headers";
        case MessageType::BLOCK:      return "block";
        case MessageType::TX:         return "tx";
        case MessageType::PING:       return "ping";
        case MessageType::PONG:       return "pong";
        case MessageType::ADDR:       return "addr";
        case MessageType::GETADDR:    return "getaddr";
        case MessageType::REJECT:     return "reject";
        case MessageType::ENCINIT:    return "encinit";
        default:                      return "unknown";
    }
}

MessageType NetMessageHeader::commandToType(const char* cmd) {
    if (strncmp(cmd, "version", 7) == 0)    return MessageType::VERSION;
    if (strncmp(cmd, "verack", 6) == 0)     return MessageType::VERACK;
    if (strncmp(cmd, "inv", 3) == 0)        return MessageType::INV;
    if (strncmp(cmd, "getdata", 7) == 0)    return MessageType::GETDATA;
    if (strncmp(cmd, "getheaders", 10) == 0) return MessageType::GETHEADERS;
    if (strncmp(cmd, "headers", 7) == 0)    return MessageType::HEADERS;
    if (strncmp(cmd, "block", 5) == 0)      return MessageType::BLOCK;
    if (strncmp(cmd, "tx", 2) == 0)         return MessageType::TX;
    if (strncmp(cmd, "ping", 4) == 0)       return MessageType::PING;
    if (strncmp(cmd, "pong", 4) == 0)       return MessageType::PONG;
    if (strncmp(cmd, "addr", 4) == 0)       return MessageType::ADDR;
    if (strncmp(cmd, "getaddr", 7) == 0)    return MessageType::GETADDR;
    if (strncmp(cmd, "reject", 6) == 0)     return MessageType::REJECT;
    if (strncmp(cmd, "encinit", 7) == 0)    return MessageType::ENCINIT;
    return MessageType::UNKNOWN;
}

std::vector<uint8_t> serializeInvs(const std::vector<Inventory>& invs) {
    std::vector<uint8_t> data;
    uint32_t count = static_cast<uint32_t>(invs.size());
    data.push_back(count & 0xFF);
    data.push_back((count >> 8) & 0xFF);
    data.push_back((count >> 16) & 0xFF);
    data.push_back((count >> 24) & 0xFF);
    for (const auto& inv : invs) {
        uint32_t type = static_cast<uint32_t>(inv.type);
        data.push_back(type & 0xFF);
        data.push_back((type >> 8) & 0xFF);
        data.push_back((type >> 16) & 0xFF);
        data.push_back((type >> 24) & 0xFF);
        data.insert(data.end(), inv.hash.begin(), inv.hash.end());
    }
    return data;
}

// ------------------------------------------------------------
// NetMessage
// ------------------------------------------------------------

std::vector<uint8_t> NetMessage::serialize() const {
    std::vector<uint8_t> data;
    data.reserve(24 + payload.size());

    // Magic
    data.push_back(MAINNET_MAGIC & 0xFF);
    data.push_back((MAINNET_MAGIC >> 8) & 0xFF);
    data.push_back((MAINNET_MAGIC >> 16) & 0xFF);
    data.push_back((MAINNET_MAGIC >> 24) & 0xFF);

    // Command
    std::string cmd = NetMessageHeader::typeToCommand(type);
    for (int i = 0; i < 12; ++i) {
        data.push_back(i < static_cast<int>(cmd.size()) ? static_cast<uint8_t>(cmd[i]) : 0);
    }

    // Length
    uint32_t len = static_cast<uint32_t>(payload.size());
    data.push_back(len & 0xFF);
    data.push_back((len >> 8) & 0xFF);
    data.push_back((len >> 16) & 0xFF);
    data.push_back((len >> 24) & 0xFF);

    // Checksum
    Hash256 hash = doubleSha256(payload);
    data.push_back(hash[0]);
    data.push_back(hash[1]);
    data.push_back(hash[2]);
    data.push_back(hash[3]);

    // Payload
    data.insert(data.end(), payload.begin(), payload.end());

    return data;
}

NetMessage NetMessage::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 24) throw std::runtime_error("Header too short");

    char cmd[13];
    memcpy(cmd, data.data() + 4, 12);
    cmd[12] = 0;
    
    uint32_t len = data[16] | (data[17] << 8) | (data[18] << 16) | (data[19] << 24);
    if (data.size() < 24 + len) throw std::runtime_error("Payload incomplete");

    std::vector<uint8_t> payload(data.begin() + 24, data.begin() + 24 + len);
    
    Hash256 hash = doubleSha256(payload);
    if (memcmp(hash.data(), data.data() + 20, 4) != 0) {
        throw std::runtime_error("Checksum mismatch");
    }

    return NetMessage(NetMessageHeader::commandToType(cmd), payload);
}

// ------------------------------------------------------------
// Peer implementation
// ------------------------------------------------------------

Peer::Peer(Network& net, int socket, const std::string& ip, uint16_t port, bool inbound)
    : network(net), socket_fd(socket), ip(ip), port(port), connected(true), inbound(inbound), 
      state(PeerState::HANDSHAKE), misbehaviorScore(0), encryptionNegotiated(false) {
    
    connectionTime = std::chrono::steady_clock::now();
    lastBlockRequestTime = connectionTime;
    
    // Initialize session encryption
    encryption = std::make_unique<P2PSessionEncryption>();

    // Send our ephemeral public key immediately
    PublicKey myPub = encryption->getEphemeralPubKey();
    sendMsg(NetMessage(MessageType::ENCINIT, myPub));
}

void Peer::misbehavior(int penalty) {
    misbehaviorScore += penalty;
    std::cout << "DEBUG: Peer " << ip << " misbehavior: +" << penalty 
              << " (total: " << misbehaviorScore << ")" << std::endl;
    
    if (misbehaviorScore >= 100) {
        std::cout << "DEBUG: Peer " << ip << " exceeded misbehavior threshold. Banning IP." << std::endl;
        network.banIP(ip);
        disconnect();
    }
}

bool Peer::isBanned() const {
    return network.isIPBanned(ip);
}

Peer::~Peer() {
    disconnect();
}

void Peer::disconnect() {
    if (connected) {
        connected = false;
        state = PeerState::DISCONNECTED;
#ifdef _WIN32
        closesocket(socket_fd);
#else
        close(socket_fd);
#endif
    }
}

bool Peer::sendMsg(const NetMessage& msg) {
    if (!connected) return false;

    NetMessage toSend = msg;
    // Transparently encrypt payload if negotiated (and not ENCINIT itself)
    if (encryptionNegotiated && msg.type != MessageType::ENCINIT) {
        toSend.payload = encryption->encrypt(msg.payload);
    }

    std::vector<uint8_t> data = toSend.serialize();
    std::lock_guard<std::mutex> lock(sendMutex);
    
    int sent = ::send(socket_fd, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0);
    return sent == static_cast<int>(data.size());
}

void Peer::processBuffer() {
    while (receiveBuffer.size() >= 24) {
        bool found = false;
        for (size_t i = 0; i <= receiveBuffer.size() - 24; ++i) {
            uint32_t m = receiveBuffer[i] | (receiveBuffer[i+1] << 8) | (receiveBuffer[i+2] << 16) | (receiveBuffer[i+3] << 24);
            if (m == MAINNET_MAGIC) {
                if (i > 0) {
                    receiveBuffer.erase(receiveBuffer.begin(), receiveBuffer.begin() + i);
                }
                found = true;
                break;
            }
        }

        if (!found) {
            if (receiveBuffer.size() > 3) {
                receiveBuffer.erase(receiveBuffer.begin(), receiveBuffer.end() - 3);
            }
            return;
        }

        uint32_t len = receiveBuffer[16] | (receiveBuffer[17] << 8) | (receiveBuffer[18] << 16) | (receiveBuffer[19] << 24);
        
        if (len > MAX_MESSAGE_PAYLOAD) {
            std::cerr << "DEBUG: Peer " << ip << " sent oversized message (" << len << " bytes). Vicious intent detected. Banning." << std::endl;
            network.banIP(ip);
            disconnect();
            return;
        }

        if (receiveBuffer.size() < 24 + len) return;

        std::vector<uint8_t> raw(receiveBuffer.begin(), receiveBuffer.begin() + 24 + len);
        receiveBuffer.erase(receiveBuffer.begin(), receiveBuffer.begin() + 24 + len);

        try {
            NetMessage msg = NetMessage::deserialize(raw);
            handleMessage(msg);
        } catch (const std::exception& e) {
            std::cerr << "DEBUG: Peer " << ip << " msg error: " << e.what() << std::endl;
            misbehavior(20);
        }
    }
}

void Peer::handleMessage(const NetMessage& msg) {
    Blockchain& chain = network.getChain();
    
    NetMessage decrypted = msg;
    if (encryptionNegotiated && msg.type != MessageType::ENCINIT) {
        std::vector<uint8_t> clear;
        if (!encryption->decrypt(msg.payload, clear)) {
            std::cerr << "DEBUG: Decryption failed for message from " << ip << std::endl;
            misbehavior(10);
            return;
        }
        decrypted.payload = clear;
    }

    std::cout << "DEBUG: Peer " << ip << " msg: '" << NetMessageHeader::typeToCommand(decrypted.type) << "'" << (encryptionNegotiated ? " [ENCRYPTED]" : "") << std::endl;

    switch (decrypted.type) {
        case MessageType::ENCINIT: {
            if (decrypted.payload.size() >= 33) {
                 PublicKey peerPub(decrypted.payload.begin(), decrypted.payload.begin() + 33);
                 if (encryption->establishSharedSecret(peerPub)) {
                     encryptionNegotiated = true;
                     std::cout << "DEBUG: P2P Encryption established with Peer " << ip << std::endl;
                     
                     // Now send our VERSION message (it will be encrypted)
                     uint32_t height = chain.height();
                     std::vector<uint8_t> payload;
                     payload.push_back(height & 0xFF);
                     payload.push_back((height >> 8) & 0xFF);
                     payload.push_back((height >> 16) & 0xFF);
                     payload.push_back((height >> 24) & 0xFF);
                     sendMsg(NetMessage(MessageType::VERSION, payload));
                 }
            }
            break;
        }
        case MessageType::VERSION: {
            if (decrypted.payload.size() >= 4) {
                uint32_t peerHeight = decrypted.payload[0] | (decrypted.payload[1] << 8) | (decrypted.payload[2] << 16) | (decrypted.payload[3] << 24);
                std::cout << "DEBUG: Peer " << ip << " version: height=" << peerHeight << std::endl;
                
                if (peerHeight > chain.height()) {
                    // We are behind, request headers
                    std::cout << "DEBUG: Requesting headers from Peer " << ip << std::endl;
                    // Simplify: send GETHEADERS with our height + 1
                    uint32_t nextHeight = chain.height() + 1;
                    std::vector<uint8_t> payload;
                    payload.push_back(nextHeight & 0xFF);
                    payload.push_back((nextHeight >> 8) & 0xFF);
                    payload.push_back((nextHeight >> 16) & 0xFF);
                    payload.push_back((nextHeight >> 24) & 0xFF);
                    sendMsg(NetMessage(MessageType::GETHEADERS, payload));
                }
            } else {
                misbehavior(10); // Penalty for invalid VERSION payload
            }
            sendMsg(NetMessage(MessageType::VERACK, {}));
            break;
        }
        case MessageType::VERACK:
            state = PeerState::ACTIVE;
            std::cout << "DEBUG: Peer " << ip << " is now ACTIVE" << std::endl;
            break;
        case MessageType::INV: {
            if (decrypted.payload.size() < 4) break;
            uint32_t count = decrypted.payload[0] | (decrypted.payload[1] << 8) | (decrypted.payload[2] << 16) | (decrypted.payload[3] << 24);
            if (count > 50000) {
                misbehavior(50); // Severe penalty for INV spam
                break;
            }
            size_t pos = 4;
            for (uint32_t i = 0; i < count && pos + 35 < decrypted.payload.size(); ++i) {
                uint32_t type = decrypted.payload[pos] | (decrypted.payload[pos+1] << 8) | (decrypted.payload[pos+2] << 16) | (decrypted.payload[pos+3] << 24);
                pos += 4;
                Hash256 hash;
                std::copy(decrypted.payload.begin() + pos, decrypted.payload.begin() + pos + 32, hash.begin());
                pos += 32;
                
                if (type == static_cast<uint32_t>(InvType::BLOCK)) {
                    if (!chain.getBlock(hash) && requestedBlocks.count(hash) == 0) {
                        std::vector<uint8_t> gdPayload(hash.begin(), hash.end());
                        requestedBlocks.insert(hash);
                        lastBlockRequestTime = std::chrono::steady_clock::now();
                        sendMsg(NetMessage(MessageType::GETDATA, gdPayload));
                    }
                } else if (type == static_cast<uint32_t>(InvType::TX)) {
                    // TX deduplication
                    std::vector<uint8_t> gdPayload(hash.begin(), hash.end());
                    sendMsg(NetMessage(MessageType::GETDATA, gdPayload));
                }
            }
            break;
        }
        case MessageType::GETHEADERS: {
            if (decrypted.payload.size() >= 4) {
                uint32_t startHeight = decrypted.payload[0] | (decrypted.payload[1] << 8) | (decrypted.payload[2] << 16) | (decrypted.payload[3] << 24);
                std::vector<uint8_t> payload;
                // Send up to 100 block hashes (inventory-style for HEADERS)
                uint32_t count = 0;
                for (uint32_t h = startHeight; h <= chain.height() && count < 100; ++h) {
                    const Block* b = chain.getBlockByHeight(h);
                    if (b) {
                        Hash256 hash = b->getHash();
                        payload.insert(payload.end(), hash.begin(), hash.end());
                        count++;
                    }
                }
                if (count > 0) {
                    sendMsg(NetMessage(MessageType::HEADERS, payload));
                }
            }
            break;
        }
        case MessageType::HEADERS: {
            // payload is list of hashes (32 bytes each)
            if (decrypted.payload.size() > 2000 * 32) {
                misbehavior(30); // Penalty for excessive headers
                break;
            }
            for (size_t i = 0; i + 31 < decrypted.payload.size(); i += 32) {
                Hash256 hash;
                std::copy(decrypted.payload.begin() + i, decrypted.payload.begin() + i + 32, hash.begin());
                if (!chain.getBlock(hash) && requestedBlocks.count(hash) == 0) {
                    // Unknown block, request data
                    std::vector<uint8_t> payload(hash.begin(), hash.end());
                    requestedBlocks.insert(hash);
                    lastBlockRequestTime = std::chrono::steady_clock::now();
                    sendMsg(NetMessage(MessageType::GETDATA, payload));
                }
            }
            break;
        }
        case MessageType::GETDATA: {
            if (decrypted.payload.size() >= 32) {
                Hash256 hash;
                std::copy(decrypted.payload.begin(), decrypted.payload.begin() + 32, hash.begin());
                const Block* b = chain.getBlock(hash);
                if (b) {
                    sendMsg(NetMessage(MessageType::BLOCK, b->serialize()));
                }
            }
            break;
        }
        case MessageType::BLOCK: {
            try {
                Block b = Block::deserialize(decrypted.payload);
                Hash256 hash = b.getHash();
                
                if (requestedBlocks.count(hash) == 0) {
                    std::cerr << "DEBUG: Received unsolicited block " << b.header.height << " from " << ip << std::endl;
                    misbehavior(10);
                    break;
                }
                
                requestedBlocks.erase(hash);
                std::cout << "DEBUG: Received Block " << b.header.height << " from " << ip << std::endl;
                
                if (chain.addBlock(b)) {
                    std::cout << "DEBUG: Successfully synced block " << b.header.height << std::endl;
                    // If we just got a block, maybe ask for more headers
                    if (b.header.height == chain.height()) {
                         uint32_t nextHeight = chain.height() + 1;
                         std::vector<uint8_t> payload;
                         payload.push_back(nextHeight & 0xFF);
                         payload.push_back((nextHeight >> 8) & 0xFF);
                         payload.push_back((nextHeight >> 16) & 0xFF);
                         payload.push_back((nextHeight >> 24) & 0xFF);
                         sendMsg(NetMessage(MessageType::GETHEADERS, payload));
                    }
                } else {
                    std::cerr << "DEBUG: Block validation failed for block from " << ip << std::endl;
                    misbehavior(20);
                }
            } catch (const std::exception& e) {
                std::cerr << "DEBUG: Failed to process received block: " << e.what() << std::endl;
                misbehavior(10);
            }
            break;
        }
        case MessageType::PING:
            sendMsg(NetMessage(MessageType::PONG, decrypted.payload));
            break;
        case MessageType::GETADDR: {
            // Peer is requesting our known peer addresses
            std::cout << "DEBUG: Peer " << ip << " requested peer addresses" << std::endl;
            
            // Collect known peer addresses from network
            std::vector<std::string> addresses;
            {
                std::lock_guard<std::mutex> lock(network.peersMutex);
                for (const auto& [fd, peer] : network.peers) {
                    if (peer->getState() == PeerState::ACTIVE && peer->getIP() != ip) {
                        addresses.push_back(peer->getIP() + ":" + std::to_string(network.listenPort));
                    }
                }
            }
            
            // Add known peers from discovery
            for (const auto& addr : network.knownPeers) {
                if (addresses.size() >= 100) break;  // Limit to 100 addresses
                addresses.push_back(addr);
            }
            
            // Serialize addresses
            std::vector<uint8_t> payload;
            uint32_t count = static_cast<uint32_t>(std::min(addresses.size(), size_t(100)));
            payload.push_back(count & 0xFF);
            payload.push_back((count >> 8) & 0xFF);
            payload.push_back((count >> 16) & 0xFF);
            payload.push_back((count >> 24) & 0xFF);
            
            for (size_t i = 0; i < count; ++i) {
                const auto& addr = addresses[i];
                uint16_t addrLen = static_cast<uint16_t>(addr.size());
                payload.push_back(addrLen & 0xFF);
                payload.push_back((addrLen >> 8) & 0xFF);
                payload.insert(payload.end(), addr.begin(), addr.end());
            }
            
            sendMsg(NetMessage(MessageType::ADDR, payload));
            break;
        }
        case MessageType::ADDR: {
            // Received peer addresses
            if (decrypted.payload.size() < 4) break;
            
            uint32_t count = decrypted.payload[0] | (decrypted.payload[1] << 8) | 
                            (decrypted.payload[2] << 16) | (decrypted.payload[3] << 24);
            
            if (count > 1000) {
                misbehavior(20);  // Penalty for excessive addresses
                break;
            }
            
            std::vector<std::string> addresses;
            size_t pos = 4;
            for (uint32_t i = 0; i < count && pos + 1 < decrypted.payload.size(); ++i) {
                uint16_t addrLen = decrypted.payload[pos] | (decrypted.payload[pos+1] << 8);
                pos += 2;
                
                if (pos + addrLen > decrypted.payload.size()) break;
                
                std::string addr(decrypted.payload.begin() + pos, 
                                decrypted.payload.begin() + pos + addrLen);
                addresses.push_back(addr);
                pos += addrLen;
            }
            
            // Forward to network for processing
            network.handleAddrMessage(addresses);
            break;
        }
        default:
            break;
    }
}

std::string Peer::address() const {
    return ip + ":" + std::to_string(port);
}

// ------------------------------------------------------------
// Network implementation
// ------------------------------------------------------------

Network::Network(Blockchain& bc) : chain(bc), running(false), listenPort(0), listenSocket(-1) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    
    // Initialize peer discovery
    lastDiscoveryTime = std::chrono::steady_clock::now();
    loadSeedNodes();
}

Network::~Network() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

void Network::start(uint16_t port) {
    if (running) return;
    listenPort = port;
    running = true;

    setupListenSocket();
    networkThread = std::thread(&Network::run, this);
}

void Network::stop() {
    if (!running) return;
    running = false;
    if (networkThread.joinable()) networkThread.join();

    std::lock_guard<std::mutex> lock(peersMutex);
    peers.clear();

    if (listenSocket != -1) {
#ifdef _WIN32
        closesocket(listenSocket);
#else
        close(listenSocket);
#endif
        listenSocket = -1;
    }
}

void Network::setupListenSocket() {
    listenSocket = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (listenSocket == -1) throw std::runtime_error("Failed to create listen socket");

    int opt = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(listenSocket, FIONBIO, &mode);
#else
    fcntl(listenSocket, F_SETFL, O_NONBLOCK);
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listenPort);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        throw std::runtime_error("Failed to bind listen socket on port " + std::to_string(listenPort));
    }

    listen(listenSocket, 10);
    std::cout << "DEBUG: Network listening on port " << listenPort << std::endl;
}

bool Network::connectToPeer(const std::string& ip, uint16_t port) {
    int s = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (s == -1) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            closesocket(s);
            return false;
        }
#else
        if (errno != EINPROGRESS) {
            close(s);
            return false;
        }
#endif
    }

    std::lock_guard<std::mutex> lock(peersMutex);
    peers[s] = std::make_unique<Peer>(*this, s, ip, port, false);
    
    return true;
}

void Network::run() {
    while (running) {
        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(listenSocket, &readFds);

        int maxFd = static_cast<int>(listenSocket);
        {
            std::lock_guard<std::mutex> lock(peersMutex);
            for (auto const& [fd, peer] : peers) {
                FD_SET(fd, &readFds);
                if (fd > maxFd) maxFd = fd;
            }
        }

        timeval tv{0, 100000}; 
        int activity = select(maxFd + 1, &readFds, nullptr, nullptr, &tv);

        if (activity < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
            continue;
        }

        pruneZombiePeers();

        if (FD_ISSET(listenSocket, &readFds)) {
            acceptNewConnection();
        }

        handleSocketIO();
    }
}

void Network::acceptNewConnection() {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    int s = static_cast<int>(accept(listenSocket, reinterpret_cast<sockaddr*>(&addr), &len));
    if (s == -1) return;

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, INET_ADDRSTRLEN);
    
    {
        std::lock_guard<std::mutex> lock(securityMutex);
        if (bannedIPs.count(ip) > 0) {
            std::cout << "DEBUG: Dropping connection from banned IP " << ip << std::endl;
#ifdef _WIN32
            closesocket(s);
#else
            close(s);
#endif
            return;
        }

        auto now = std::chrono::steady_clock::now();
        if (lastConnectionTimes.count(ip) > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastConnectionTimes[ip]).count();
            if (elapsed < 5) {
                std::cout << "DEBUG: Throttling connection attempt from " << ip << " (too fast)" << std::endl;
#ifdef _WIN32
                closesocket(s);
#else
                close(s);
#endif
                return;
            }
        }
        lastConnectionTimes[ip] = now;
    }

    std::cout << "DEBUG: Accepted connection from " << ip << std::endl;

    std::lock_guard<std::mutex> lock(peersMutex);
    
    // Sybil Guard: Limit peers per IP
    size_t ipCount = 0;
    for (auto const& [fd, p] : peers) {
        if (p->getIP() == ip) {
            ipCount++;
        }
    }

    if (ipCount >= MAX_PEERS_PER_IP) {
        std::cout << "DEBUG: Sybil Guard: Too many connections from " << ip << " (" << ipCount << "). Dropping." << std::endl;
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return;
    }

    if (peers.size() >= MAX_PEERS) {
        std::cout << "DEBUG: Max peers reached (" << MAX_PEERS << "). Dropping connection from " << ip << std::endl;
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return;
    }

    peers[s] = std::make_unique<Peer>(*this, s, ip, ntohs(addr.sin_port), true);
}

void Network::pruneZombiePeers() {
    std::lock_guard<std::mutex> lock(peersMutex);
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = peers.begin(); it != peers.end();) {
        Peer* peer = it->second.get();
        if (peer->getState() == PeerState::HANDSHAKE) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - peer->getConnectionTime()).count();
            if (elapsed > 30) {
                std::cout << "DEBUG: Pruning zombie peer " << peer->address() << " (handshake timeout)" << std::endl;
                it = peers.erase(it);
                continue;
            }
        }

        // Round 4: Sync Stall Detection
        if (!peer->requestedBlocks.empty()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - peer->lastBlockRequestTime).count();
            if (elapsed > 60) {
                std::cout << "DEBUG: Pruning stalling peer " << peer->address() << " (sync timeout)" << std::endl;
                it = peers.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void Network::banIP(const std::string& ip) {
    std::lock_guard<std::mutex> lock(securityMutex);
    bannedIPs.insert(ip);
    std::cout << "DEBUG: IP " << ip << " added to ban list." << std::endl;
}

bool Network::isIPBanned(const std::string& ip) const {
    std::lock_guard<std::mutex> lock(securityMutex);
    return bannedIPs.count(ip) > 0;
}

void Network::handleSocketIO() {
    std::lock_guard<std::mutex> lock(peersMutex);
    char buffer[4096];
    
    for (auto it = peers.begin(); it != peers.end();) {
        int fd = it->first;
        Peer* peer = it->second.get();

        int n = recv(fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            auto& peerBuffer = peer->getReceiveBuffer();
            if (peerBuffer.size() + n > MAX_PEER_BUFFER) {
                std::cerr << "DEBUG: Peer " << peer->address() << " buffer overflow. Banning." << std::endl;
                banIP(peer->getIP());
                peer->disconnect();
                it = peers.erase(it);
                continue;
            }
            peerBuffer.insert(peerBuffer.end(), buffer, buffer + n);
            peer->processBuffer();
            ++it;
        } else if (n == 0) {
            std::cout << "DEBUG: Peer " << peer->address() << " disconnected" << std::endl;
            it = peers.erase(it);
        } else {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                it = peers.erase(it);
            } else {
                ++it;
            }
#else
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                it = peers.erase(it);
              } else {
                ++it;
            }
#endif
        }
    }
}

void Network::broadcast(const NetMessage& msg) {
    std::lock_guard<std::mutex> lock(peersMutex);
    for (auto const& [fd, peer] : peers) {
        if (peer->getState() == PeerState::ACTIVE) {
            peer->sendMsg(msg);
        }
    }
}

void Network::update() {
    // Run periodic peer discovery
    discoverPeers();
}

// Peer Discovery Implementation (Phase 6)

void Network::loadSeedNodes() {
    // Hardcoded seed nodes for initial peer discovery
    seedNodes = {
        "127.0.0.1:18333",  // Local seed for testing
        // Add more seed nodes as needed
    };
    
    std::cout << "DEBUG: Loaded " << seedNodes.size() << " seed nodes" << std::endl;
}

void Network::discoverPeers() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastDiscoveryTime).count();
    
    // Run discovery every 30 minutes
    if (elapsed < 30) return;
    
    lastDiscoveryTime = now;
    std::cout << "DEBUG: Running peer discovery..." << std::endl;
    
    // Connect to seed nodes if we have few peers
    {
        std::lock_guard<std::mutex> lock(peersMutex);
        if (peers.size() < 3) {
            for (const auto& seed : seedNodes) {
                size_t colonPos = seed.find(':');
                if (colonPos != std::string::npos) {
                    std::string ip = seed.substr(0, colonPos);
                    uint16_t port = static_cast<uint16_t>(std::stoi(seed.substr(colonPos + 1)));
                    
                    // Check if already connected
                    bool alreadyConnected = false;
                    for (const auto& [fd, peer] : peers) {
                        if (peer->getIP() == ip) {
                            alreadyConnected = true;
                            break;
                        }
                    }
                    
                    if (!alreadyConnected && !isIPBanned(ip)) {
                        std::cout << "DEBUG: Connecting to seed node " << ip << ":" << port << std::endl;
                        connectToPeer(ip, port);
                    }
                }
            }
        }
    }
    
    // Send GETADDR to all active peers to discover more peers
    {
        std::lock_guard<std::mutex> lock(peersMutex);
        for (auto& [fd, peer] : peers) {
            if (peer->getState() == PeerState::ACTIVE) {
                std::cout << "DEBUG: Sending GETADDR to " << peer->getIP() << std::endl;
                peer->sendMsg(NetMessage(MessageType::GETADDR, {}));
            }
        }
    }
}

void Network::handleAddrMessage(const std::vector<std::string>& addresses) {
    std::cout << "DEBUG: Received " << addresses.size() << " peer addresses" << std::endl;
    
    for (const auto& addr : addresses) {
        // Add to known peers
        knownPeers.insert(addr);
        
        // Try to connect to new peers if we have capacity
        std::lock_guard<std::mutex> lock(peersMutex);
        if (peers.size() < MAX_PEERS / 2) {  // Keep some slots for inbound
            size_t colonPos = addr.find(':');
            if (colonPos != std::string::npos) {
                std::string ip = addr.substr(0, colonPos);
                uint16_t port = static_cast<uint16_t>(std::stoi(addr.substr(colonPos + 1)));
                
                // Check if already connected
                bool alreadyConnected = false;
                for (const auto& [fd, peer] : peers) {
                    if (peer->getIP() == ip) {
                        alreadyConnected = true;
                        break;
                    }
                }
                
                if (!alreadyConnected && !isIPBanned(ip)) {
                    std::cout << "DEBUG: Connecting to discovered peer " << ip << ":" << port << std::endl;
                    connectToPeer(ip, port);
                }
            }
        }
    }
}

} // namespace Bucks