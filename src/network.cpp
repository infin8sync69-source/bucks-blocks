#include "network.h"
#include "log.h"
#include "node.h"
#include "serialize.h"
#include "consensus.h"
#include <iostream>
#include <cerrno>
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
#include <poll.h>
#include <netdb.h>
#endif

namespace Bucks {

// ------------------------------------------------------------
// Local / LAN address whitelist helper
// Returns true for addresses that must never be banned:
//   127.0.0.1, ::1, 192.168.x.x, 10.x.x.x
// ------------------------------------------------------------
static bool isLocalAddress(const std::string& ip) {
    if (ip == "127.0.0.1" || ip == "::1") return true;
    if (ip.find("192.168.") == 0)          return true;
    if (ip.find("10.")      == 0)          return true;
    return false;
}

// Extra bytes an encrypted payload may carry (IV, tag).
constexpr uint32_t ENCRYPTION_OVERHEAD = 256;
constexpr size_t MAX_BLOCKS_IN_FLIGHT = 16;
constexpr size_t MAX_HEADERS_PER_MESSAGE = 2000;
constexpr size_t MAX_LOCATOR_HASHES = 101;
constexpr uint32_t GETHEADERS_VERSION = 1;

// ------------------------------------------------------------
// NetMessageHeader
// ------------------------------------------------------------

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
    // 1..12 characters from [a-z0-9], then only zero bytes.
    size_t len = 0;
    while (len < 12 && cmd[len] != 0) {
        const char c = cmd[len];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return MessageType::UNKNOWN;
        ++len;
    }
    if (len == 0) return MessageType::UNKNOWN;
    for (size_t i = len; i < 12; ++i) {
        if (cmd[i] != 0) return MessageType::UNKNOWN;
    }
    static const MessageType all[] = {
        MessageType::VERSION, MessageType::VERACK, MessageType::INV, MessageType::GETDATA,
        MessageType::GETHEADERS, MessageType::HEADERS, MessageType::BLOCK, MessageType::TX,
        MessageType::PING, MessageType::PONG, MessageType::ADDR, MessageType::GETADDR,
        MessageType::REJECT, MessageType::ENCINIT};
    const std::string name(cmd, len);
    for (MessageType t : all) {
        if (typeToCommand(t) == name) return t;
    }
    return MessageType::UNKNOWN;
}

uint32_t NetMessageHeader::maxPayload(MessageType type) {
    switch (type) {
        case MessageType::BLOCK:      return 1'000'000 + ENCRYPTION_OVERHEAD;
        case MessageType::TX:         return 100'000 + ENCRYPTION_OVERHEAD;
        case MessageType::HEADERS:    return 168'003 + ENCRYPTION_OVERHEAD;
        case MessageType::GETHEADERS: return 3'269 + ENCRYPTION_OVERHEAD;
        case MessageType::ENCINIT:    return 33;
        default:                      return MAX_MESSAGE_PAYLOAD;
    }
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

// Exactly: u32 count, then count * (u32 type, 32-byte hash).
static bool parseInvs(const std::vector<uint8_t>& payload, size_t maxCount, std::vector<Inventory>& out) {
    try {
        Reader r(payload);
        const uint32_t count = r.u32();
        if (count > maxCount) return false;
        if (uint64_t(count) * 36 != r.remaining()) return false;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t type = r.u32();
            Hash256 h = r.hash256();
            if (type != uint32_t(InvType::TX) && type != uint32_t(InvType::BLOCK)) continue;
            out.push_back(Inventory{InvType(type), h});
        }
        return true;
    } catch (const ParseError&) {
        return false;
    }
}

// getheaders: u32 version = 1, CompactSize n (1..101), n hashes (newest first), stop hash.
static std::vector<uint8_t> makeGetHeaders(const std::vector<Hash256>& locator) {
    Writer w;
    w.u32(GETHEADERS_VERSION);
    const size_t n = std::min(locator.size(), MAX_LOCATOR_HASHES);
    w.compactSize(n);
    for (size_t i = 0; i < n; ++i) w.hash256(locator[i]);
    w.hash256(Hash256{});
    return w.take();
}

static bool parseGetHeaders(const std::vector<uint8_t>& payload, std::vector<Hash256>& locator, Hash256& stop) {
    try {
        Reader r(payload);
        if (r.u32() != GETHEADERS_VERSION) return false;
        const uint64_t n = r.compactSize(MAX_LOCATOR_HASHES);
        if (n == 0 || n * 32 + 32 != r.remaining()) return false;
        for (uint64_t i = 0; i < n; ++i) locator.push_back(r.hash256());
        stop = r.hash256();
        r.requireEnd();
        return true;
    } catch (const ParseError&) {
        return false;
    }
}

// headers: CompactSize n (0..2000), n * 84-byte headers, exact length.
static bool parseHeaders(const std::vector<uint8_t>& payload, std::vector<BlockHeader>& out) {
    try {
        Reader r(payload);
        const uint64_t n = r.compactSize(MAX_HEADERS_PER_MESSAGE);
        if (n * Consensus::BLOCK_HEADER_SIZE != r.remaining()) return false;
        for (uint64_t i = 0; i < n; ++i) out.push_back(BlockHeader::parse(r));
        r.requireEnd();
        return true;
    } catch (const ParseError&) {
        return false;
    }
}

// ------------------------------------------------------------
// NetMessage
// ------------------------------------------------------------

std::vector<uint8_t> NetMessage::serialize(const std::array<uint8_t, 4>& magic) const {
    std::vector<uint8_t> data;
    data.reserve(24 + payload.size());
    data.insert(data.end(), magic.begin(), magic.end());

    const std::string cmd = NetMessageHeader::typeToCommand(type);
    for (int i = 0; i < 12; ++i) {
        data.push_back(i < static_cast<int>(cmd.size()) ? static_cast<uint8_t>(cmd[i]) : 0);
    }

    const uint32_t len = static_cast<uint32_t>(payload.size());
    for (int i = 0; i < 4; ++i) data.push_back(uint8_t(len >> (8 * i)));

    const Hash256 hash = doubleSha256(payload);
    data.insert(data.end(), hash.begin(), hash.begin() + 4);
    data.insert(data.end(), payload.begin(), payload.end());
    return data;
}

NetMessage NetMessage::deserialize(const std::vector<uint8_t>& data, const std::array<uint8_t, 4>& magic) {
    if (data.size() < 24) throw NetIncomplete("header too short");
    if (!std::equal(magic.begin(), magic.end(), data.begin())) throw NetMalformed("wrong network magic");

    char cmd[12];
    memcpy(cmd, data.data() + 4, 12);
    const MessageType type = NetMessageHeader::commandToType(cmd);
    if (type == MessageType::UNKNOWN) throw NetMalformed("unknown command");

    // Lengths are compared as size_t: a 32-bit sum could wrap and read out of bounds.
    const size_t length = size_t(data[16]) | (size_t(data[17]) << 8) | (size_t(data[18]) << 16) | (size_t(data[19]) << 24);
    if (length > NetMessageHeader::maxPayload(type)) throw NetMalformed("payload too large for its command");
    if (data.size() - 24 < length) throw NetIncomplete("payload incomplete");
    if (data.size() - 24 > length) throw NetMalformed("bytes after the payload");

    std::vector<uint8_t> payload(data.begin() + 24, data.begin() + 24 + length);
    const Hash256 hash = doubleSha256(payload);
    if (memcmp(hash.data(), data.data() + 20, 4) != 0) throw NetMalformed("checksum mismatch");
    return NetMessage(type, payload);
}

// ------------------------------------------------------------
// Peer implementation
// ------------------------------------------------------------

Peer::Peer(Network& net, int socket, const std::string& ip, uint16_t port)
    : network(net), socket_fd(socket), ip(ip), port(port), connected(true),
      state(PeerState::HANDSHAKE), misbehaviorScore(0), encryptionNegotiated(false) {

#ifndef _WIN32
    // Non-blocking, so sendMsg's deadline holds (macOS ignores MSG_DONTWAIT for send).
    fcntl(socket_fd, F_SETFL, fcntl(socket_fd, F_GETFL, 0) | O_NONBLOCK);
#endif
    connectionTime = std::chrono::steady_clock::now();
    lastBlockRequestTime = connectionTime;
    lastHeadersRequestTime = connectionTime - std::chrono::seconds(60);

    // Initialize session encryption
    encryption = std::make_unique<P2PSessionEncryption>();

    // Send our ephemeral public key immediately
    PublicKey myPub = encryption->getEphemeralPubKey();
    sendMsg(NetMessage(MessageType::ENCINIT, myPub));
}

void Peer::misbehavior(int penalty) {
    misbehaviorScore += penalty;
    Bucks::logOut() << "DEBUG: Peer " << ip << " misbehavior: +" << penalty
              << " (total: " << misbehaviorScore << ")" << std::endl;

    if (misbehaviorScore >= 100) {
        if (isLocalAddress(ip)) {
            Bucks::logOut() << "WARNING: Peer " << ip << " exceeded misbehavior threshold but is a local/LAN address — skipping ban and disconnect." << std::endl;
            return;
        }
        Bucks::logOut() << "DEBUG: Peer " << ip << " exceeded misbehavior threshold. Banning IP." << std::endl;
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

    std::vector<uint8_t> data = toSend.serialize(network.magic());
    std::lock_guard<std::mutex> lock(sendMutex);

    // Send everything or drop the peer: a short write would leave a half frame in the
    // stream, and a peer that never reads must not stall the network thread for long.
    // No SIGPIPE: a peer that closed under us is an error return, never a signal.
#ifdef MSG_NOSIGNAL
    constexpr int kSendFlags = MSG_NOSIGNAL | MSG_DONTWAIT;
#else
    constexpr int kSendFlags = MSG_DONTWAIT;
#endif
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    size_t off = 0;
    while (off < data.size()) {
        const ssize_t n = ::send(socket_fd, reinterpret_cast<const char*>(data.data()) + off,
                                 data.size() - off, kSendFlags);
        if (n > 0) {
            off += size_t(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0) break;
            struct pollfd pfd = {socket_fd, POLLOUT, 0};
            if (::poll(&pfd, 1, int(std::min<int64_t>(left, 1000))) >= 0) continue;
        }
        break;
    }
    if (off == data.size()) return true;
    disconnect();
    return false;
}

void Peer::processBuffer() {
    const std::array<uint8_t, 4>& magic = network.magic();
    while (receiveBuffer.size() >= 24) {
        bool found = false;
        for (size_t i = 0; i + 24 <= receiveBuffer.size(); ++i) {
            if (std::equal(magic.begin(), magic.end(), receiveBuffer.begin() + i)) {
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

        char cmd[12];
        memcpy(cmd, receiveBuffer.data() + 4, 12);
        const MessageType type = NetMessageHeader::commandToType(cmd);
        const size_t len = size_t(receiveBuffer[16]) | (size_t(receiveBuffer[17]) << 8) |
                           (size_t(receiveBuffer[18]) << 16) | (size_t(receiveBuffer[19]) << 24);

        if (len > NetMessageHeader::maxPayload(type)) {
            Bucks::logErr() << "DEBUG: Peer " << ip << " sent an oversized " << NetMessageHeader::typeToCommand(type)
                      << " (" << len << " bytes). Banning." << std::endl;
            network.banIP(ip);
            disconnect();
            return;
        }

        const size_t frame = len + 24;
        if (receiveBuffer.size() < frame) return;

        std::vector<uint8_t> raw(receiveBuffer.begin(), receiveBuffer.begin() + frame);
        receiveBuffer.erase(receiveBuffer.begin(), receiveBuffer.begin() + frame);

        try {
            NetMessage msg = NetMessage::deserialize(raw, magic);
            handleMessage(msg);
        } catch (const NetIncomplete&) {
            return;   // cannot happen: the frame is complete
        } catch (const std::exception& e) {
            Bucks::logErr() << "DEBUG: Peer " << ip << " msg error: " << e.what() << std::endl;
            misbehavior(20);
        }
    }
}

void Peer::requestHeaders() {
    const auto now = std::chrono::steady_clock::now();
    if (headersInFlight && now - lastHeadersRequestTime < std::chrono::seconds(10)) return;
    headersInFlight = true;
    lastHeadersRequestTime = now;
    sendMsg(NetMessage(MessageType::GETHEADERS, makeGetHeaders(network.getNode().locator())));
}

void Peer::requestMissingBlocks() {
    if (requestedBlocks.size() >= MAX_BLOCKS_IN_FLIGHT) return;
    std::vector<Hash256> missing = network.getNode().missingBlocksOnBestHeaderChain(MAX_BLOCKS_IN_FLIGHT);
    std::vector<Inventory> wanted;
    for (const Hash256& h : missing) {
        if (requestedBlocks.size() + wanted.size() >= MAX_BLOCKS_IN_FLIGHT) break;
        if (requestedBlocks.count(h)) continue;
        wanted.push_back(Inventory{InvType::BLOCK, h});
    }
    if (wanted.empty()) return;
    for (const Inventory& i : wanted) requestedBlocks.insert(i.hash);
    lastBlockRequestTime = std::chrono::steady_clock::now();
    sendMsg(NetMessage(MessageType::GETDATA, serializeInvs(wanted)));
}

// Penalties by result category (consensus spec section 7.1).
static int penaltyFor(ValidationResult r) {
    switch (r) {
        case ValidationResult::INVALID:   return 100;
        case ValidationResult::MALFORMED: return 20;
        case ValidationResult::MUTATED:   return 20;
        default:                          return 0;   // missing parent, too new, duplicate, policy, internal
    }
}

void Peer::handleMessage(const NetMessage& msg) {
    Node& node = network.getNode();

    NetMessage decrypted = msg;
    if (encryptionNegotiated && msg.type != MessageType::ENCINIT) {
        std::vector<uint8_t> clear;
        if (!encryption->decrypt(msg.payload, clear)) {
            Bucks::logErr() << "DEBUG: Decryption failed for message from " << ip << std::endl;
            misbehavior(10);
            return;
        }
        decrypted.payload = clear;
    }

    Bucks::logOut() << "DEBUG: Peer " << ip << " msg: '" << NetMessageHeader::typeToCommand(decrypted.type) << "'" << (encryptionNegotiated ? " [ENCRYPTED]" : "") << std::endl;

    switch (decrypted.type) {
        case MessageType::ENCINIT: {
            if (decrypted.payload.size() >= 33) {
                 PublicKey peerPub(decrypted.payload.begin(), decrypted.payload.begin() + 33);
                 if (encryption->establishSharedSecret(peerPub)) {
                     encryptionNegotiated = true;
                     Bucks::logOut() << "DEBUG: P2P Encryption established with Peer " << ip << std::endl;

                     // Now send our VERSION message (it will be encrypted)
                     Writer w;
                     w.u32(node.height());
                     sendMsg(NetMessage(MessageType::VERSION, w.take()));
                 }
            }
            break;
        }
        case MessageType::VERSION: {
            if (decrypted.payload.size() >= 4) {
                Reader r(decrypted.payload);
                const uint32_t peerHeight = r.u32();
                Bucks::logOut() << "DEBUG: Peer " << ip << " version: height=" << peerHeight << std::endl;
                if (peerHeight > node.height()) requestHeaders();
            } else {
                misbehavior(10); // Penalty for invalid VERSION payload
            }
            sendMsg(NetMessage(MessageType::VERACK, {}));
            break;
        }
        case MessageType::VERACK:
            state = PeerState::ACTIVE;
            Bucks::logOut() << "DEBUG: Peer " << ip << " is now ACTIVE" << std::endl;
            break;
        case MessageType::INV: {
            std::vector<Inventory> invs;
            if (!parseInvs(decrypted.payload, 50000, invs)) {
                misbehavior(50);   // count above the cap or a length that does not fit
                break;
            }
            std::vector<Inventory> wantTx;
            bool unknownBlock = false;
            for (const Inventory& inv : invs) {
                if (inv.type == InvType::BLOCK) {
                    // A new block is learned through its header; data is requested only for
                    // headers on the best header chain.
                    if (!node.knowsHeader(inv.hash)) unknownBlock = true;
                } else if (!node.mempoolTransaction(inv.hash) && wantTx.size() < 100) {
                    wantTx.push_back(inv);
                }
            }
            if (unknownBlock) requestHeaders();
            if (!wantTx.empty()) sendMsg(NetMessage(MessageType::GETDATA, serializeInvs(wantTx)));
            break;
        }
        case MessageType::GETHEADERS: {
            std::vector<Hash256> locator;
            Hash256 stop{};
            if (!parseGetHeaders(decrypted.payload, locator, stop)) {
                misbehavior(20);
                break;
            }
            const std::vector<BlockHeader> headers = node.headersAfter(locator, stop, MAX_HEADERS_PER_MESSAGE);
            Writer w;
            w.compactSize(headers.size());
            for (const BlockHeader& h : headers) {
                const std::vector<uint8_t> raw = h.serialize();
                w.bytes(raw.data(), raw.size());
            }
            sendMsg(NetMessage(MessageType::HEADERS, w.take()));
            break;
        }
        case MessageType::HEADERS: {
            headersInFlight = false;
            std::vector<BlockHeader> headers;
            if (!parseHeaders(decrypted.payload, headers)) {
                misbehavior(20);
                break;
            }
            if (headers.empty()) break;
            // Headers must connect to each other; the first one's parent must be known.
            const ValidationState st = node.submitHeaders(headers);
            if (st.result == ValidationResult::MISSING_PARENT) {
                requestHeaders();                 // we are on a different branch: ask again from our locator
            } else if (!st.valid()) {
                if (st.result != ValidationResult::TIME_TOO_NEW && st.result != ValidationResult::INTERNAL)
                    misbehavior(penaltyFor(st.result));
            } else {
                requestMissingBlocks();
                if (headers.size() == MAX_HEADERS_PER_MESSAGE) requestHeaders();   // more to come
            }
            break;
        }
        case MessageType::GETDATA: {
            std::vector<Inventory> invs;
            if (!parseInvs(decrypted.payload, 1000, invs)) {
                misbehavior(20);
                break;
            }
            for (const Inventory& inv : invs) {
                if (inv.type == InvType::BLOCK) {
                    if (auto b = node.blockByHash(inv.hash)) sendMsg(NetMessage(MessageType::BLOCK, b->serialize()));
                } else if (auto tx = node.mempoolTransaction(inv.hash)) {
                    sendMsg(NetMessage(MessageType::TX, tx->serialize()));
                }
            }
            break;
        }
        case MessageType::BLOCK: {
            Block b;
            try {
                b = Block::deserialize(decrypted.payload);
            } catch (const std::exception& e) {
                Bucks::logErr() << "DEBUG: Unparsable block from " << ip << ": " << e.what() << std::endl;
                misbehavior(20);
                break;
            }
            const Hash256 hash = b.getHash();
            const bool requested = requestedBlocks.erase(hash) > 0;
            const ValidationState st = node.submitBlock(b, requested);
            if (st.valid()) {
                Bucks::logOut() << "DEBUG: Accepted block " << b.header.height << " from " << ip << std::endl;
                requestMissingBlocks();
                if (requestedBlocks.empty()) requestHeaders();
            } else if (st.result == ValidationResult::MISSING_PARENT) {
                requestHeaders();                 // no penalty: the parent may simply not have arrived
            } else {
                network.recordRejectedBlock(st.result);
                const int penalty = penaltyFor(st.result);
                if (penalty) {
                    Bucks::logErr() << "DEBUG: Block from " << ip << " rejected: " << st.code << std::endl;
                    misbehavior(penalty);
                }
            }
            break;
        }
        case MessageType::TX: {
            Transaction tx;
            try {
                tx = Transaction::deserialize(decrypted.payload);
            } catch (const std::exception&) {
                misbehavior(20);
                break;
            }
            AcceptOptions options;
            options.fromPeer = true;
            options.peerId = uint64_t(socket_fd);
            const ValidationState st = node.submitTransaction(tx, options);
            // Relay to the other peers happens through the node's transaction listener.
            if (st.result == ValidationResult::INVALID) misbehavior(10);
            break;
        }
        case MessageType::PING:
            sendMsg(NetMessage(MessageType::PONG, decrypted.payload));
            break;
        case MessageType::GETADDR: {
            // Peer is requesting our known peer addresses. This handler already runs under
            // peersMutex (Network::handleSocketIO), so it must not lock it again.
            Bucks::logOut() << "DEBUG: Peer " << ip << " requested peer addresses" << std::endl;

            std::vector<std::string> addresses;
            for (const auto& [fd, peer] : network.peers) {
                if (peer->getState() == PeerState::ACTIVE && peer->getIP() != ip) {
                    addresses.push_back(peer->getIP() + ":" + std::to_string(network.listenPort));
                }
            }
            for (const auto& addr : network.knownPeers) {
                if (addresses.size() >= 100) break;  // Limit to 100 addresses
                addresses.push_back(addr);
            }

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

            // Connecting takes peersMutex, which is held here: hand them to the network thread.
            network.queueAddresses(addresses);
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

Network::Network(Node& n)
    : listenPort(0), node(n), magic_(n.params().netMagic), running(false), listenSocket(-1) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    
    // Initialize peer discovery
    lastDiscoveryTime = std::chrono::steady_clock::now() - std::chrono::minutes(31);   // first discovery at once
    lastMaintain_ = std::chrono::steady_clock::now();
    loadSeedNodes();

    // New tips and accepted transactions are announced to every peer. The listeners only
    // queue the message: they run on arbitrary threads and the peer lock is not theirs.
    tipListenerId_ = node.addTipListener([this](const Hash256& hash, uint32_t) {
        queueBroadcast(NetMessage(MessageType::INV, serializeInvs({Inventory{InvType::BLOCK, hash}})));
    });
    txListenerId_ = node.addTxListener([this](const Transaction& tx) {
        queueBroadcast(NetMessage(MessageType::INV, serializeInvs({Inventory{InvType::TX, tx.txid()}})));
    });
}

Network::~Network() {
    node.removeTipListener(tipListenerId_);
    node.removeTxListener(txListenerId_);
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
    if (setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) < 0) {
        Bucks::logErr() << "WARNING: setsockopt SO_REUSEADDR failed: " << strerror(errno) << "\n";
    }

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
        throw std::runtime_error("Failed to bind listen socket on port " + std::to_string(listenPort) + ": " + strerror(errno) + " (try: pkill -9 -f bucks_node && sleep 3)");
    }

    listen(listenSocket, 10);
    Bucks::logOut() << "DEBUG: Network listening on port " << listenPort << std::endl;
}

namespace {

// IPv4 dotted quad for a literal or a host name; false if it does not resolve.
bool resolveIPv4(const std::string& host, std::string& out) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    char buf[INET_ADDRSTRLEN] = {};
    const auto* sin = reinterpret_cast<const sockaddr_in*>(res->ai_addr);
    const bool ok = inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf)) != nullptr;
    freeaddrinfo(res);
    if (ok) out = buf;
    return ok;
}

} // namespace

bool Network::connectToPeer(const std::string& host, uint16_t port) {
    std::string ip;
    if (!resolveIPv4(host, ip)) return false;

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
    peers[s] = std::make_unique<Peer>(*this, s, ip, port);
    
    return true;
}

void Network::addPersistentPeer(const std::string& host, uint16_t port) {
    std::lock_guard<std::mutex> lock(peersMutex);
    for (const auto& p : persistentPeers_)
        if (p.host == host && p.port == port) return;
    persistentPeers_.push_back({host, port, std::chrono::steady_clock::now(), 5});
}

void Network::addSeedNode(const std::string& hostPort) {
    std::lock_guard<std::mutex> lock(peersMutex);
    if (std::find(seedNodes.begin(), seedNodes.end(), hostPort) == seedNodes.end())
        seedNodes.push_back(hostPort);
}

// Called from the network thread about once a second.
void Network::maintainConnections() {
    const auto now = std::chrono::steady_clock::now();
    if (now - lastMaintain_ < std::chrono::seconds(1)) return;
    lastMaintain_ = now;

    struct Dial { size_t index; };
    std::vector<size_t> due;
    {
        std::lock_guard<std::mutex> lock(peersMutex);
        for (size_t i = 0; i < persistentPeers_.size(); ++i) {
            auto& pp = persistentPeers_[i];
            std::string ip;
            bool connected = false;
            if (resolveIPv4(pp.host, ip)) {
                for (const auto& [fd, peer] : peers) {
                    // Outbound entries carry the dialled port; an inbound socket from the same
                    // host also means the link exists, whichever side opened it.
                    if (peer->getIP() == ip && peer->getState() != PeerState::DISCONNECTED) {
                        connected = true;
                        break;
                    }
                }
            }
            if (connected) {
                pp.backoffSeconds = 5;
                pp.nextAttempt = now;
            } else if (now >= pp.nextAttempt) {
                due.push_back(i);
            }
        }
    }
    for (size_t i : due) {
        std::string host;
        uint16_t port;
        {
            std::lock_guard<std::mutex> lock(peersMutex);
            host = persistentPeers_[i].host;
            port = persistentPeers_[i].port;
        }
        const bool ok = connectToPeer(host, port);
        std::lock_guard<std::mutex> lock(peersMutex);
        auto& pp = persistentPeers_[i];
        // Even a connect that succeeded is re-checked next round; back off either way so a peer
        // that accepts and then drops us is not hammered.
        pp.nextAttempt = now + std::chrono::seconds(pp.backoffSeconds);
        pp.backoffSeconds = std::min(pp.backoffSeconds * 2, 300);
        if (!ok) Bucks::logOut() << "DEBUG: cannot reach " << host << ":" << port
                                 << ", retrying in " << pp.backoffSeconds / 2 << " s" << std::endl;
    }
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
        maintainConnections();
        update();

        if (FD_ISSET(listenSocket, &readFds)) {
            acceptNewConnection();
        }

        handleSocketIO();
        flushQueues();
    }
}

size_t Network::peerCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(peersMutex));
    size_t n = 0;
    for (const auto& kv : peers)
        if (kv.second->getState() == PeerState::ACTIVE) ++n;
    return n;
}

void Network::queueBroadcast(const NetMessage& msg, int exceptFd) {
    std::lock_guard<std::mutex> lock(queueMutex);
    if (relayQueue.size() < 1000) relayQueue.emplace_back(msg, exceptFd);
}

void Network::queueAddresses(const std::vector<std::string>& addresses) {
    std::lock_guard<std::mutex> lock(queueMutex);
    if (addrQueue.size() < 100) addrQueue.push_back(addresses);
}

void Network::flushQueues() {
    std::vector<std::pair<NetMessage, int>> relay;
    std::vector<std::vector<std::string>> addrs;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        relay.swap(relayQueue);
        addrs.swap(addrQueue);
    }
    if (!relay.empty()) {
        std::lock_guard<std::mutex> lock(peersMutex);
        for (const auto& [msg, except] : relay) {
            for (auto const& [fd, peer] : peers) {
                if (fd != except && peer->getState() == PeerState::ACTIVE) peer->sendMsg(msg);
            }
        }
    }
    for (const auto& list : addrs) handleAddrMessage(list);   // takes peersMutex itself
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
            Bucks::logOut() << "DEBUG: Dropping connection from banned IP " << ip << std::endl;
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
                Bucks::logOut() << "DEBUG: Throttling connection attempt from " << ip << " (too fast)" << std::endl;
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

    Bucks::logOut() << "DEBUG: Accepted connection from " << ip << std::endl;

    std::lock_guard<std::mutex> lock(peersMutex);
    
    // Sybil Guard: Limit peers per IP
    size_t ipCount = 0;
    for (auto const& [fd, p] : peers) {
        if (p->getIP() == ip) {
            ipCount++;
        }
    }

    if (ipCount >= MAX_PEERS_PER_IP) {
        Bucks::logOut() << "DEBUG: Sybil Guard: Too many connections from " << ip << " (" << ipCount << "). Dropping." << std::endl;
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return;
    }

    if (peers.size() >= MAX_PEERS) {
        Bucks::logOut() << "DEBUG: Max peers reached (" << MAX_PEERS << "). Dropping connection from " << ip << std::endl;
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return;
    }

    peers[s] = std::make_unique<Peer>(*this, s, ip, ntohs(addr.sin_port));
}

void Network::pruneZombiePeers() {
    std::lock_guard<std::mutex> lock(peersMutex);
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = peers.begin(); it != peers.end();) {
        Peer* peer = it->second.get();
        if (peer->getState() == PeerState::HANDSHAKE) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - peer->getConnectionTime()).count();
            if (elapsed > 30) {
                Bucks::logOut() << "DEBUG: Pruning zombie peer " << peer->address() << " (handshake timeout)" << std::endl;
                it = peers.erase(it);
                continue;
            }
        }

        // Round 4: Sync Stall Detection
        if (!peer->requestedBlocks.empty()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - peer->lastBlockRequestTime).count();
            if (elapsed > 60) {
                Bucks::logOut() << "DEBUG: Pruning stalling peer " << peer->address() << " (sync timeout)" << std::endl;
                it = peers.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void Network::banIP(const std::string& ip) {
    // Bug 1 fix: localhost and LAN peers must never be banned.
    // All three pilot nodes share 127.0.0.1; banning that address
    // would cut off the entire local network.
    if (isLocalAddress(ip)) {
        Bucks::logOut() << "WARNING: Refusing to ban local/LAN address " << ip
                  << " — local peers are whitelisted from the ban list." << std::endl;
        return;
    }
    std::lock_guard<std::mutex> lock(securityMutex);
    bannedIPs.insert(ip);
    Bucks::logOut() << "DEBUG: IP " << ip << " added to ban list." << std::endl;
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

        // Never block here: this loop holds peersMutex and visits every peer, so one idle
        // connection would stall the whole network thread (and Network::stop()).
#ifdef _WIN32
        int n = recv(fd, buffer, sizeof(buffer), 0);
#else
        int n = recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
#endif
        if (n > 0) {
            auto& peerBuffer = peer->getReceiveBuffer();
            if (peerBuffer.size() + n > MAX_PEER_BUFFER) {
                Bucks::logErr() << "DEBUG: Peer " << peer->address() << " buffer overflow. Banning." << std::endl;
                banIP(peer->getIP());
                peer->disconnect();
                it = peers.erase(it);
                continue;
            }
            peerBuffer.insert(peerBuffer.end(), buffer, buffer + n);
            peer->processBuffer();
            ++it;
        } else if (n == 0) {
            Bucks::logOut() << "DEBUG: Peer " << peer->address() << " disconnected" << std::endl;
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
    seedNodes = node.params().seedNodes;
    Bucks::logOut() << "DEBUG: Loaded " << seedNodes.size() << " seed nodes" << std::endl;
}

void Network::discoverPeers() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastDiscoveryTime).count();
    
    // Run discovery every 30 minutes
    if (elapsed < 30) return;
    
    lastDiscoveryTime = now;
    Bucks::logOut() << "DEBUG: Running peer discovery..." << std::endl;
    
    // FIX: Same deadlock fix as handleAddrMessage — Bucks v1 security patch
    // Collect seed peers to connect under the lock, then connect outside the lock.
    std::vector<std::pair<std::string, uint16_t>> seedsToConnect;
    {
        std::lock_guard<std::mutex> lock(peersMutex);
        if (peers.size() < 3) {
            for (const auto& seed : seedNodes) {
                size_t colonPos = seed.find(':');
                if (colonPos == std::string::npos) continue;
                std::string ip  = seed.substr(0, colonPos);
                int portNum = 0;
                try { portNum = std::stoi(seed.substr(colonPos + 1)); } catch (...) { continue; }
                if (portNum < 1 || portNum > 65535) continue;
                uint16_t   port = static_cast<uint16_t>(portNum);

                bool alreadyConnected = false;
                for (const auto& [fd, peer] : peers) {
                    if (peer->getIP() == ip) { alreadyConnected = true; break; }
                }
                if (!alreadyConnected && !isIPBanned(ip))
                    seedsToConnect.emplace_back(ip, port);
            }
        }
    }  // peersMutex released

    for (auto& [ip, port] : seedsToConnect) {
        Bucks::logOut() << "DEBUG: Connecting to seed node " << ip << ":" << port << std::endl;
        connectToPeer(ip, port);
    }

    // FIX: Log clearly if no peers connected after seed attempts — Bucks v1 security patch
    {
        std::lock_guard<std::mutex> lock(peersMutex);
        if (!seedsToConnect.empty() && peers.empty()) {
            Bucks::logErr() << "Cannot connect to Bucks network. "
                      << "Check your internet connection or run a local node." << std::endl;
        }
    }
    
    // Send GETADDR to all active peers to discover more peers
    {
        std::lock_guard<std::mutex> lock(peersMutex);
        for (auto& [fd, peer] : peers) {
            if (peer->getState() == PeerState::ACTIVE) {
                Bucks::logOut() << "DEBUG: Sending GETADDR to " << peer->getIP() << std::endl;
                peer->sendMsg(NetMessage(MessageType::GETADDR, {}));
            }
        }
    }
}

void Network::handleAddrMessage(const std::vector<std::string>& addresses) {
    // FIX: Deadlock prevention — Bucks v1 security patch
    // The old code called connectToPeer() while holding peersMutex.
    // connectToPeer() also acquires peersMutex → deadlock → node hangs.
    // Fix: collect the (ip, port) pairs to connect inside the mutex, release the
    // lock, then call connectToPeer() for each pair outside the critical section.
    Bucks::logOut() << "DEBUG: Received " << addresses.size() << " peer addresses" << std::endl;

    for (const auto& addr : addresses)
        knownPeers.insert(addr);

    std::vector<std::pair<std::string, uint16_t>> toConnect;
    {
        std::lock_guard<std::mutex> lock(peersMutex);
        for (const auto& addr : addresses) {
            if (peers.size() + toConnect.size() >= MAX_PEERS / 2) break;

            size_t colonPos = addr.find(':');
            if (colonPos == std::string::npos) continue;

            std::string ip  = addr.substr(0, colonPos);
            uint16_t   port = static_cast<uint16_t>(std::stoi(addr.substr(colonPos + 1)));

            bool alreadyConnected = false;
            for (const auto& [fd, peer] : peers) {
                if (peer->getIP() == ip) { alreadyConnected = true; break; }
            }

            if (!alreadyConnected && !isIPBanned(ip))
                toConnect.emplace_back(ip, port);
        }
    }  // peersMutex released before any connectToPeer() call

    for (auto& [ip, port] : toConnect) {
        Bucks::logOut() << "DEBUG: Connecting to discovered peer " << ip << ":" << port << std::endl;
        connectToPeer(ip, port);
    }
}

} // namespace Bucks