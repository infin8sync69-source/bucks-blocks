#include "network.h"
#include "log.h"
#include "node.h"
#include "serialize.h"
#include "consensus.h"
#include "fsutil.h"
#include "pow.h"
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
// Loopback (127/8, ::1): the only addresses never banned, because several local nodes share them.
// LAN addresses are banned like any other: a seed in a cloud VPC or behind NAT must still be able
// to shut out a misbehaving peer (audit NET-NEW-5, NET-13).
// ------------------------------------------------------------
static bool isLoopbackAddress(const std::string& ip) {
    if (ip == "::1") return true;
    return ip.compare(0, 4, "127.") == 0;
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
    // The small messages are capped near their real size: a 4 MiB ping used to be echoed in full,
    // under the peer lock (audit NET-14, NET-5).
    switch (type) {
        case MessageType::BLOCK:      return 1'000'000 + ENCRYPTION_OVERHEAD;
        case MessageType::TX:         return 100'000 + ENCRYPTION_OVERHEAD;
        case MessageType::HEADERS:    return 168'003 + ENCRYPTION_OVERHEAD;
        case MessageType::GETHEADERS: return 3'269 + ENCRYPTION_OVERHEAD;
        case MessageType::ENCINIT:    return 33;
        case MessageType::VERSION:    return 64 + ENCRYPTION_OVERHEAD;        // height, listen port, nonce
        case MessageType::VERACK:
        case MessageType::GETADDR:    return ENCRYPTION_OVERHEAD;             // empty
        case MessageType::PING:
        case MessageType::PONG:       return 8 + ENCRYPTION_OVERHEAD;         // a nonce
        case MessageType::ADDR:       return 4 + 1000 * (2 + 21) + ENCRYPTION_OVERHEAD;
        case MessageType::GETDATA:    return 4 + 1000 * 36 + ENCRYPTION_OVERHEAD;
        case MessageType::INV:        return 4 + 50'000 * 36 + ENCRYPTION_OVERHEAD;
        case MessageType::REJECT:     return 1'024 + ENCRYPTION_OVERHEAD;     // never sent; ignored
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
// NetAddress, AddrMan
// ------------------------------------------------------------

namespace {

int64_t unixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

bool inNet(uint32_t ip, uint32_t net, int bits) {
    const uint32_t mask = bits == 0 ? 0 : ~uint32_t(0) << (32 - bits);
    return (ip & mask) == (net & mask);
}

constexpr uint32_t ipv4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return a << 24 | b << 16 | c << 8 | d; }

} // namespace

bool NetAddress::parseIP(const std::string& ip, uint32_t& out) {
    in_addr a{};
    if (ip.size() > 15 || inet_pton(AF_INET, ip.c_str(), &a) != 1) return false;
    out = ntohl(a.s_addr);
    return true;
}

bool NetAddress::parse(const std::string& s, NetAddress& out) {
    const size_t colon = s.find(':');
    if (colon == std::string::npos || s.size() > 21) return false;
    const std::string portText = s.substr(colon + 1);
    if (portText.empty() || portText.size() > 5) return false;
    uint32_t port = 0;
    for (char c : portText) {
        if (c < '0' || c > '9') return false;
        port = port * 10 + uint32_t(c - '0');
    }
    if (port < 1 || port > 65535) return false;
    uint32_t ip = 0;
    if (!parseIP(s.substr(0, colon), ip)) return false;
    out.ip = ip;
    out.port = uint16_t(port);
    return true;
}

std::string NetAddress::ipString() const {
    return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 0xFF) + "." +
           std::to_string((ip >> 8) & 0xFF) + "." + std::to_string(ip & 0xFF);
}

std::string NetAddress::toString() const { return ipString() + ":" + std::to_string(port); }

bool NetAddress::isLoopback() const { return inNet(ip, ipv4(127, 0, 0, 0), 8); }

bool NetAddress::isPrivate() const {
    return inNet(ip, ipv4(10, 0, 0, 0), 8) || inNet(ip, ipv4(172, 16, 0, 0), 12) ||
           inNet(ip, ipv4(192, 168, 0, 0), 16) || inNet(ip, ipv4(100, 64, 0, 0), 10) ||
           inNet(ip, ipv4(169, 254, 0, 0), 16);
}

bool NetAddress::isValid() const {
    return port != 0 && !inNet(ip, 0, 8) && !inNet(ip, ipv4(224, 0, 0, 0), 4) && !inNet(ip, ipv4(240, 0, 0, 0), 4);
}

bool NetAddress::isRoutable() const {
    // Also not the documentation and benchmark ranges.
    return isValid() && !isLoopback() && !isPrivate() && !inNet(ip, ipv4(192, 0, 2, 0), 24) &&
           !inNet(ip, ipv4(198, 51, 100, 0), 24) && !inNet(ip, ipv4(203, 0, 113, 0), 24) &&
           !inNet(ip, ipv4(198, 18, 0, 0), 15);
}

bool NetAddress::acceptableFrom(uint32_t source) const {
    if (!isValid()) return false;
    if (isRoutable()) return true;
    const NetAddress src{source, 1};
    if (isLoopback()) return src.isLoopback();
    if (isPrivate()) return src.isLoopback() || src.isPrivate();
    return false;
}

namespace {

// Retry delay after `attempts` dials without a handshake: 30 s, doubling up to an hour.
int64_t retryDelay(int attempts) {
    if (attempts <= 0) return 0;
    return std::min<int64_t>(int64_t(30) << std::min(attempts - 1, 7), 3600);
}

constexpr int kMaxFailedAttempts = 10;   // an address never reached after this many dials is dropped

} // namespace

AddrMan::AddrMan() : rng_(std::random_device{}()) {}

bool AddrMan::isTerrible(const Entry& e, int64_t now) const {
    if (now - e.lastSeen > 30 * 86400) return true;                          // not heard of for a month
    if (e.lastSuccess == 0 && e.attempts >= 3) return true;                  // never reached
    if (e.attempts >= kMaxFailedAttempts && now - e.lastSuccess > 7 * 86400) return true;
    return false;
}

// Makes room for one more new entry from `sourceGroup`: drops a terrible entry of that source,
// else its oldest one, if the source is at its share; otherwise (the table is full) a terrible
// entry, else the oldest entry of the source with the most entries.
void AddrMan::evictNew(uint32_t sourceGroup, int64_t now) {
    const auto share = newPerSource_.find(sourceGroup);
    const bool sourceFull = share != newPerSource_.end() && share->second >= MAX_NEW_PER_SOURCE_GROUP;
    if (!sourceFull && newCount_ < MAX_NEW) return;
    uint32_t victimSource = sourceGroup;
    if (!sourceFull) {
        size_t most = 0;
        for (const auto& [g, n] : newPerSource_) {
            if (n > most) { most = n; victimSource = g; }
        }
    }
    auto victim = entries_.end();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        const Entry& e = it->second;
        if (e.tried) continue;
        if (sourceFull && e.sourceGroup != sourceGroup) continue;
        if (isTerrible(e, now)) { victim = it; break; }
        if (e.sourceGroup != victimSource) continue;
        if (victim == entries_.end() || e.lastSeen < victim->second.lastSeen) victim = it;
    }
    if (victim == entries_.end()) return;
    if (--newPerSource_[victim->second.sourceGroup] == 0) newPerSource_.erase(victim->second.sourceGroup);
    --newCount_;
    entries_.erase(victim);
}

void AddrMan::evictTried() {
    auto victim = entries_.end();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (!it->second.tried) continue;
        if (victim == entries_.end() || it->second.lastSuccess < victim->second.lastSuccess) victim = it;
    }
    if (victim == entries_.end()) return;
    --triedCount_;
    entries_.erase(victim);
}

bool AddrMan::add(const NetAddress& a, uint32_t sourceGroup, int64_t now) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = entries_.find(a);
    if (it != entries_.end()) {
        it->second.lastSeen = std::max(it->second.lastSeen, now);
        return false;
    }
    evictNew(sourceGroup, now);
    const auto share = newPerSource_.find(sourceGroup);
    if ((share != newPerSource_.end() && share->second >= MAX_NEW_PER_SOURCE_GROUP) || newCount_ >= MAX_NEW)
        return false;
    Entry e;
    e.addr = a;
    e.sourceGroup = sourceGroup;
    e.lastSeen = now;
    entries_.emplace(a, e);
    ++newPerSource_[sourceGroup];
    ++newCount_;
    return true;
}

void AddrMan::attempt(const NetAddress& a, int64_t now) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = entries_.find(a);
    if (it == entries_.end()) return;
    it->second.lastTry = now;
    ++it->second.attempts;
}

void AddrMan::good(const NetAddress& a, int64_t now) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = entries_.find(a);
    const bool wasTried = it != entries_.end() && it->second.tried;
    if (!wasTried && triedCount_ >= MAX_TRIED) evictTried();   // `a` is not tried, so not the victim
    if (it == entries_.end()) {
        Entry e;
        e.addr = a;
        e.sourceGroup = a.group();
        it = entries_.emplace(a, e).first;
    } else if (!wasTried) {
        if (--newPerSource_[it->second.sourceGroup] == 0) newPerSource_.erase(it->second.sourceGroup);
        --newCount_;
    }
    Entry& e = it->second;
    e.lastSeen = e.lastSuccess = e.lastTry = now;
    e.attempts = 0;
    if (!wasTried) {
        e.tried = true;
        ++triedCount_;
    }
}

void AddrMan::remove(const NetAddress& a) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = entries_.find(a);
    if (it == entries_.end()) return;
    if (it->second.tried) {
        --triedCount_;
    } else {
        if (--newPerSource_[it->second.sourceGroup] == 0) newPerSource_.erase(it->second.sourceGroup);
        --newCount_;
    }
    entries_.erase(it);
}

std::optional<NetAddress> AddrMan::select(int64_t now, const std::function<bool(const NetAddress&)>& skip,
                                          const std::function<bool(const NetAddress&)>& prefer) {
    std::lock_guard<std::mutex> lock(mu_);
    // [preferred][tried]
    std::vector<NetAddress> pool[2][2];
    for (auto it = entries_.begin(); it != entries_.end();) {
        const Entry& e = it->second;
        if (!e.tried && e.lastSuccess == 0 && e.attempts >= kMaxFailedAttempts) {
            if (--newPerSource_[e.sourceGroup] == 0) newPerSource_.erase(e.sourceGroup);
            --newCount_;
            it = entries_.erase(it);
            continue;
        }
        if (now - e.lastTry >= retryDelay(e.attempts) && !(skip && skip(e.addr)))
            pool[!prefer || prefer(e.addr)][e.tried].push_back(e.addr);
        ++it;
    }
    for (int p = 1; p >= 0; --p) {
        std::vector<NetAddress>* from = nullptr;
        if (!pool[p][0].empty() && !pool[p][1].empty()) from = &pool[p][rng_() & 1];
        else if (!pool[p][1].empty()) from = &pool[p][1];
        else if (!pool[p][0].empty()) from = &pool[p][0];
        if (from) return (*from)[rng_() % from->size()];
    }
    return std::nullopt;
}

std::vector<NetAddress> AddrMan::sample(size_t max, int64_t now) {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<NetAddress> out;
    for (const auto& [a, e] : entries_) {
        if (!isTerrible(e, now)) out.push_back(a);
    }
    std::shuffle(out.begin(), out.end(), rng_);
    if (out.size() > max) out.resize(max);
    return out;
}

std::optional<AddrMan::Entry> AddrMan::find(const NetAddress& a) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = entries_.find(a);
    if (it == entries_.end()) return std::nullopt;
    return it->second;
}

size_t AddrMan::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return entries_.size();
}

size_t AddrMan::triedCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return triedCount_;
}

std::string AddrMan::serialize() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::ostringstream out;
    out << "bucks-addresses 1\n";
    for (const auto& [a, e] : entries_) {
        out << a.toString() << ' ' << (e.tried ? 1 : 0) << ' ' << e.lastSeen << ' ' << e.lastTry << ' '
            << e.lastSuccess << ' ' << e.attempts << ' ' << e.sourceGroup << '\n';
    }
    return out.str();
}

size_t AddrMan::load(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != "bucks-addresses 1") return 0;
    size_t taken = 0;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string addr;
        Entry e;
        int tried = 0;
        if (!(fields >> addr >> tried >> e.lastSeen >> e.lastTry >> e.lastSuccess >> e.attempts >> e.sourceGroup))
            continue;
        if (!NetAddress::parse(addr, e.addr) || !e.addr.isValid() || e.attempts < 0) continue;
        std::lock_guard<std::mutex> lock(mu_);
        if (entries_.count(e.addr)) continue;
        if (tried == 1 && e.lastSuccess > 0) {
            if (triedCount_ >= MAX_TRIED) continue;
            e.tried = true;
            ++triedCount_;
        } else {
            const auto share = newPerSource_.find(e.sourceGroup);
            if (newCount_ >= MAX_NEW || (share != newPerSource_.end() && share->second >= MAX_NEW_PER_SOURCE_GROUP))
                continue;
            e.tried = false;
            ++newPerSource_[e.sourceGroup];
            ++newCount_;
        }
        entries_.emplace(e.addr, e);
        ++taken;
    }
    return taken;
}

// ------------------------------------------------------------
// Peer implementation
// ------------------------------------------------------------

Peer::Peer(Network& net, int socket, const std::string& ip, uint16_t port, ConnKind kind)
    : network(net), socket_fd(socket), ip(ip), port(port), kind_(kind), connected(true),
      state(PeerState::HANDSHAKE), misbehaviorScore(0), encryptionNegotiated(false) {

#ifndef _WIN32
    // Non-blocking, so sendMsg's deadline holds (macOS ignores MSG_DONTWAIT for send).
    fcntl(socket_fd, F_SETFL, fcntl(socket_fd, F_GETFL, 0) | O_NONBLOCK);
#endif
    connectionTime = std::chrono::steady_clock::now();
    addrTokensAt = connectionTime;
    lastReceived = lastPingSent = connectionTime;
    if (outbound()) listenPort_ = port;
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
        // Always disconnected; banned unless it is a loopback peer (banIP refuses those).
        Bucks::logOut() << "DEBUG: Peer " << ip << " exceeded misbehavior threshold. Disconnecting." << std::endl;
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
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
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
            lastReceived = std::chrono::steady_clock::now();
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

namespace {
constexpr double kAddrTokensPerSecond = 0.1;
constexpr double kMaxAddrTokens = 1000;
constexpr size_t kMaxAddrPerMessage = 1000;
constexpr size_t kGetAddrReplySize = 250;
} // namespace

void Peer::askForAddresses() {
    addrTokens = std::min(addrTokens + kMaxAddrTokens, 2 * kMaxAddrTokens);
    sendMsg(NetMessage(MessageType::GETADDR, {}));
}

// Counts the headers of a batch that we do not know yet and that do not lead to more work than our
// active tip, and refuses the batch once this peer's total would pass lowWorkHeaderBudget. Such
// headers are stored for ever and a fork from genesis can be replayed to every node, so one
// connection may not make us store an unlimited number of them (audit NET-NEW-3). A batch that
// does lead past the tip resets the count: an honest peer on a heavier branch is never limited,
// and an attacker would need more work than the whole chain. Which headers are valid and which
// chain is best do not change; a refused batch is simply not stored.
bool Peer::withinHeaderBudget(const std::vector<BlockHeader>& headers) {
    Node& node = network.getNode();
    const std::optional<U256> parentWork = node.chainWorkOf(headers.front().prevBlockHash);
    if (!parentWork) return true;   // unknown parent: nothing is stored (MISSING_PARENT)
    U256 work = *parentWork;
    uint32_t lastBits = 0;
    U256 proof;
    for (const BlockHeader& h : headers) {
        if (h.bits != lastBits) proof = Pow::blockProof(lastBits = h.bits);   // bits change once per 2016 at most
        work += proof;
    }
    if (work > node.tip().chainWork) {
        lowWorkHeaders_ = 0;
        return true;
    }
    size_t fresh = 0;
    for (const BlockHeader& h : headers)
        if (!node.knowsHeader(h.hash())) ++fresh;
    if (lowWorkHeaders_ + fresh > network.options().lowWorkHeaderBudget) {
        Bucks::logErr() << "DEBUG: Peer " << ip << " sent " << fresh << " more headers that lead nowhere ("
                        << lowWorkHeaders_ << " already); not storing them" << std::endl;
        return false;
    }
    lowWorkHeaders_ += fresh;
    return true;
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

    // Everything except the key exchange itself must be encrypted: a peer that skips ENCINIT is refused
    // instead of being served in plaintext (audit NET-3).
    if (!encryptionNegotiated && msg.type != MessageType::ENCINIT) {
        Bucks::logErr() << "DEBUG: Peer " << ip << " sent a message before the encryption handshake; dropping it" << std::endl;
        network.banIP(ip);
        disconnect();
        return;
    }

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
            if (encryptionNegotiated) {   // a second key exchange is never legitimate
                misbehavior(20);
                break;
            }
            if (decrypted.payload.size() >= 33) {
                 PublicKey peerPub(decrypted.payload.begin(), decrypted.payload.begin() + 33);
                 if (encryption->establishSharedSecret(peerPub)) {
                     encryptionNegotiated = true;
                     Bucks::logOut() << "DEBUG: P2P Encryption established with Peer " << ip << std::endl;

                     // Now send our VERSION message (it will be encrypted): height, the port we
                     // accept connections on (0 = none) and our nonce, which shows a dial to ourselves.
                     Writer w;
                     w.u32(node.height());
                     w.u16(network.listenPort);
                     w.u64(network.localNonce());
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
                if (r.remaining() >= 10 && !versionSeen_) {
                    const uint16_t peerListenPort = r.u16();
                    remoteNonce_ = r.u64();
                    versionSeen_ = true;
                    if (remoteNonce_ == network.localNonce()) {
                        Bucks::logOut() << "DEBUG: Connected to ourselves through " << address() << "; dropping it" << std::endl;
                        if (outbound()) network.markSelf(address());
                        disconnect();
                        break;
                    }
                    // An inbound peer that listens: its address is worth knowing and passing on.
                    if (!outbound() && peerListenPort != 0 && listenPort_ == 0) {
                        listenPort_ = peerListenPort;
                        if (auto a = listenAddress()) network.learnAddress(*a, ip, socket_fd);
                    }
                    if (network.dropDuplicate(*this)) break;
                }
                if (peerHeight > node.height()) requestHeaders();
            } else {
                misbehavior(10); // Penalty for invalid VERSION payload
            }
            sendMsg(NetMessage(MessageType::VERACK, {}));
            break;
        }
        case MessageType::VERACK: {
            if (state == PeerState::ACTIVE) break;
            state = PeerState::ACTIVE;
            Bucks::logOut() << "DEBUG: Peer " << ip << " is now ACTIVE" << std::endl;
            // An address we dialled and shook hands with is worth keeping and passing on; ask it
            // for more (audit NET-NEW-2: before, getaddr went out every 30 minutes only).
            if (outbound()) {
                if (auto a = listenAddress()) network.addresses().good(*a, unixNow());
                askForAddresses();
            }
            break;
        }
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
            if (!withinHeaderBudget(headers)) {
                misbehavior(20);
                break;
            }
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
            // Each item once, and at most a window of blocks per request (we ask for 16 at a time):
            // 1000 copies of one block hash used to be answered with 1000 blocks (audit NET-5).
            std::set<Hash256> seen;
            size_t blocks = 0;
            for (const Inventory& inv : invs) {
                if (!seen.insert(inv.hash).second) continue;
                if (inv.type == InvType::BLOCK) {
                    if (blocks >= MAX_BLOCKS_IN_FLIGHT) continue;
                    if (auto b = node.blockByHash(inv.hash)) {
                        ++blocks;
                        sendMsg(NetMessage(MessageType::BLOCK, b->serialize()));
                    }
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
            // An unrequested block stores its header even when the block itself is dropped.
            if (!requested && !withinHeaderBudget({b.header})) {
                misbehavior(20);
                break;
            }
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
            // A sample of the address table, once per connection and only to peers that dialled
            // us (as Bitcoin Core does), so the table cannot be scraped; the peer's own entry is
            // left out.
            if (outbound() || answeredGetAddr_) break;
            answeredGetAddr_ = true;
            Bucks::logOut() << "DEBUG: Peer " << ip << " requested peer addresses" << std::endl;
            const std::optional<NetAddress> self = listenAddress();
            std::vector<std::string> addresses;
            for (const NetAddress& a : network.addresses().sample(kGetAddrReplySize, unixNow())) {
                if (!self || !(a == *self)) addresses.push_back(a.toString());
            }
            Writer w;
            w.u32(uint32_t(addresses.size()));
            for (const std::string& addr : addresses) {
                w.u16(uint16_t(addr.size()));
                w.bytes(reinterpret_cast<const uint8_t*>(addr.data()), addr.size());
            }
            sendMsg(NetMessage(MessageType::ADDR, w.take()));
            break;
        }
        case MessageType::ADDR: {
            if (decrypted.payload.size() < 4) break;

            uint32_t count = decrypted.payload[0] | (decrypted.payload[1] << 8) |
                            (decrypted.payload[2] << 16) | (decrypted.payload[3] << 24);

            if (count > kMaxAddrPerMessage) {
                misbehavior(20);  // Penalty for excessive addresses
                break;
            }

            // Token bucket: what is beyond this peer's allowance is dropped unread.
            const auto now = std::chrono::steady_clock::now();
            addrTokens = std::min(kMaxAddrTokens * 2, addrTokens + kAddrTokensPerSecond *
                                  std::chrono::duration<double>(now - addrTokensAt).count());
            addrTokensAt = now;

            std::vector<std::string> addresses;
            size_t pos = 4;
            for (uint32_t i = 0; i < count && pos + 1 < decrypted.payload.size() && addrTokens >= 1; ++i) {
                uint16_t addrLen = decrypted.payload[pos] | (decrypted.payload[pos+1] << 8);
                pos += 2;

                if (pos + addrLen > decrypted.payload.size()) break;

                std::string addr(decrypted.payload.begin() + pos,
                                decrypted.payload.begin() + pos + addrLen);
                addresses.push_back(addr);
                addrTokens -= 1;
                pos += addrLen;
            }
            std::vector<NetAddress> fresh;
            network.addAddresses(addresses, ip, &fresh);
            // A short unsolicited ADDR is news (a node announcing itself): pass on what we did
            // not know yet. Known addresses are not relayed again, so this dies out.
            if (count <= 10) {
                for (const NetAddress& a : fresh) network.relayAddress(a, socket_fd);
            }
            break;
        }
        default:
            break;
    }
}

std::string Peer::address() const {
    return ip + ":" + std::to_string(port);
}

std::optional<NetAddress> Peer::listenAddress() const {
    NetAddress a;
    if (listenPort_ == 0 || !NetAddress::parse(ip + ":" + std::to_string(listenPort_), a)) return std::nullopt;
    return a;
}

// ------------------------------------------------------------
// Network implementation
// ------------------------------------------------------------

Network::Network(Node& n, const NetworkOptions& options)
    : listenPort(0), node(n), options_(options), magic_(n.params().netMagic), running(false), listenSocket(-1) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    
    // Initialize peer discovery
    std::random_device rd;
    localNonce_ = uint64_t(rd()) << 32 | rd();
    relayRng_.seed(rd());
    lastMaintain_ = std::chrono::steady_clock::now() - std::chrono::seconds(1);   // first round at once
    for (const std::string& seed : node.params().seedNodes) {
        if (!addSeedNode(seed)) Bucks::logErr() << "WARNING: ignoring malformed seed node '" << seed << "'" << std::endl;
    }
    Bucks::logOut() << "DEBUG: Loaded " << node.params().seedNodes.size() << " seed nodes" << std::endl;

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
    dnsThread_ = std::thread(&Network::resolverLoop, this);
}

void Network::stop() {
    if (running) {
        running = false;
        {
            // The resolver checks `running` under dnsMutex_, so it cannot miss this wake-up.
            std::lock_guard<std::mutex> lock(dnsMutex_);
        }
        dnsCv_.notify_all();
        if (networkThread.joinable()) networkThread.join();
        if (dnsThread_.joinable()) dnsThread_.join();   // waits for a lookup in progress
    }
    saveAddresses();

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

bool isIPv4Literal(const std::string& host) {
    uint32_t ip = 0;
    return NetAddress::parseIP(host, ip);
}

void closeSocket(int s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

// How long a dial may take before it counts as failed. The network thread never waits for it.
constexpr auto kConnectTimeout = std::chrono::seconds(5);
// A host name is looked up again after this long (after a failed lookup: sooner).
constexpr auto kDnsTtl = std::chrono::minutes(10);
constexpr auto kDnsRetry = std::chrono::seconds(30);

} // namespace

bool Network::connectToPeer(const std::string& host, uint16_t port) {
    std::string ip;
    if (!resolveIPv4(host, ip)) return false;   // on the caller's thread, never the network thread
    std::lock_guard<std::mutex> lock(queueMutex);
    dialQueue_.push_back({ip, port, ConnKind::MANUAL});
    return true;
}

// Network thread only. Starts a non-blocking connect; run() watches it in its poll() set and
// completeConnects() finishes it, so a dial to an address that never answers costs the thread
// nothing (audit NET-4, NET-11). Before, each dial waited up to 3 s inline.
bool Network::startConnect(const std::string& ip, uint16_t port, ConnKind kind) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (port == 0 || inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) return false;
    {
        // Outbound connections have their own slots, which inbound peers cannot fill (audit NET-6).
        std::lock_guard<std::mutex> lock(peersMutex);
        size_t outbound = pending_.size();
        for (const auto& [fd, peer] : peers)
            if (peer->outbound()) ++outbound;
        if (outbound >= MAX_OUTBOUND_PEERS + MAX_MANUAL_PEERS) return false;
    }

    int s = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (s == -1) return false;
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        std::lock_guard<std::mutex> lock(peersMutex);
        peers[s] = std::make_unique<Peer>(*this, s, ip, port, kind);
        return true;
    }
#ifdef _WIN32
    const bool inProgress = WSAGetLastError() == WSAEWOULDBLOCK;
#else
    const bool inProgress = errno == EINPROGRESS;
#endif
    if (!inProgress) {
        closeSocket(s);
        return false;
    }
    pending_.push_back({s, ip, port, kind, std::chrono::steady_clock::now() + kConnectTimeout});
    return true;
}

// Network thread only, right after poll(): `revents` holds the poll results of pending_, in order.
void Network::completeConnects(const std::vector<short>& revents) {
    const auto now = std::chrono::steady_clock::now();
    std::vector<PendingConnect> still;
    for (size_t i = 0; i < pending_.size(); ++i) {
        const PendingConnect& pc = pending_[i];
        const short ev = i < revents.size() ? revents[i] : 0;
        if (!(ev & (POLLOUT | POLLERR | POLLHUP))) {
            if (now < pc.deadline) {
                still.push_back(pc);
                continue;
            }
            Bucks::logOut() << "DEBUG: cannot reach " << pc.ip << ":" << pc.port << " (timeout)" << std::endl;
            closeSocket(pc.fd);
            continue;
        }
        int soErr = 0;
        socklen_t soLen = sizeof(soErr);
        if (getsockopt(pc.fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &soLen) != 0 || soErr != 0) {
            Bucks::logOut() << "DEBUG: cannot reach " << pc.ip << ":" << pc.port << ": " << strerror(soErr) << std::endl;
            closeSocket(pc.fd);
            continue;
        }
        std::lock_guard<std::mutex> lock(peersMutex);
        peers[pc.fd] = std::make_unique<Peer>(*this, pc.fd, pc.ip, pc.port, pc.kind);
    }
    pending_.swap(still);
}

// Non-blocking: an IPv4 literal, or the last answer of the resolver thread. A name that has no
// answer yet (or an old one) is queued for it; getaddrinfo never runs on the network thread or
// under peersMutex (audit NET-4).
bool Network::lookupHost(const std::string& host, std::string& ip) {
    if (isIPv4Literal(host)) {
        ip = host;
        return true;
    }
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(dnsMutex_);
    DnsEntry& e = dnsCache_[host];
    const bool fresh = e.asked && now - e.at < (e.ok ? kDnsTtl : kDnsRetry);
    if (!fresh && !e.queued) {
        e.queued = true;
        dnsQueue_.push_back(host);
        dnsCv_.notify_one();
    }
    if (e.ip.empty()) return false;
    ip = e.ip;   // possibly stale while a new lookup runs
    return true;
}

void Network::resolverLoop() {
    std::unique_lock<std::mutex> lock(dnsMutex_);
    while (true) {
        dnsCv_.wait(lock, [&] { return !running || !dnsQueue_.empty(); });
        if (!running) return;
        const std::string host = dnsQueue_.front();
        dnsQueue_.erase(dnsQueue_.begin());
        lock.unlock();
        std::string ip;
        const bool ok = resolveIPv4(host, ip);   // may take seconds; no lock held
        lock.lock();
        DnsEntry& e = dnsCache_[host];
        e.asked = true;
        e.queued = false;
        e.ok = ok;
        e.at = std::chrono::steady_clock::now();
        if (ok) e.ip = ip;                       // a failed lookup keeps the last good answer
        else Bucks::logOut() << "DEBUG: cannot resolve " << host << std::endl;
    }
}

namespace {

// First retry delay. A seed starts above the 5 s per-address accept throttle, so its first
// retry is not refused for coming too soon after the first try.
int initialBackoffSeconds(ConnKind kind) { return kind == ConnKind::SEED ? 6 : 5; }
constexpr int kMaxBackoffSeconds = 300;
// A connection that lasted this long resets the backoff, so its loss is redialled at once.
constexpr auto kStableConnection = std::chrono::seconds(60);

} // namespace

void Network::addDialTarget(const std::string& host, uint16_t port, ConnKind kind) {
    std::lock_guard<std::mutex> lock(peersMutex);
    for (const auto& t : dialTargets_)
        if (t.host == host && t.port == port) return;
    dialTargets_.push_back({host, port, kind, std::chrono::steady_clock::now(), initialBackoffSeconds(kind)});
}

void Network::addPersistentPeer(const std::string& host, uint16_t port) {
    addDialTarget(host, port, ConnKind::MANUAL);
}

bool Network::addSeedNode(const std::string& hostPort) {
    const size_t colon = hostPort.rfind(':');
    if (colon == std::string::npos || colon == 0) return false;
    int port = 0;
    try { port = std::stoi(hostPort.substr(colon + 1)); } catch (...) { return false; }
    if (port < 1 || port > 65535) return false;
    addDialTarget(hostPort.substr(0, colon), uint16_t(port), ConnKind::SEED);
    return true;
}

// Called from the network thread about once a second. A --connect peer is dialled whenever it
// is missing; a seed whenever it is missing and outbound slots are free. Before, seeds were tried
// once at start and then every 30 minutes, so a seed restart cut seed-only nodes off for that
// long (audit NET-NEW-1).
void Network::maintainConnections() {
    // Dials other threads asked for (connectToPeer) start at once.
    std::vector<QueuedDial> asked;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        asked.swap(dialQueue_);
    }
    for (const QueuedDial& d : asked) startConnect(d.ip, d.port, d.kind);

    const auto now = std::chrono::steady_clock::now();
    if (now - lastMaintain_ < std::chrono::seconds(1)) return;
    lastMaintain_ = now;

    // Every connection with the address it listens on (port 0: an inbound peer that has not
    // said), so an address is never dialled while it is connected in either direction.
    struct Link { std::string ip; uint16_t port; bool outbound; std::chrono::steady_clock::duration age; };
    std::vector<DialTarget> targets;
    std::vector<Link> links;
    size_t outbound = 0;
    {
        std::lock_guard<std::mutex> lock(peersMutex);
        targets = dialTargets_;
        for (const auto& [fd, peer] : peers) {
            if (peer->getState() == PeerState::DISCONNECTED) continue;
            const std::optional<NetAddress> listen = peer->listenAddress();
            links.push_back({peer->getIP(), listen ? listen->port : uint16_t(0), peer->outbound(),
                             now - peer->getConnectionTime()});
            if (peer->kind() == ConnKind::SEED || peer->kind() == ConnKind::AUTO) ++outbound;
        }
    }
    for (const PendingConnect& pc : pending_) {
        links.push_back({pc.ip, pc.port, true, std::chrono::steady_clock::duration::zero()});
        if (pc.kind == ConnKind::SEED || pc.kind == ConnKind::AUTO) ++outbound;
    }

    for (size_t i = 0; i < targets.size(); ++i) {
        const DialTarget& t = targets[i];
        std::string ip;
        if (!lookupHost(t.host, ip)) continue;   // the resolver thread is on it
        NetAddress target;
        if (NetAddress::parse(ip + ":" + std::to_string(t.port), target) && selfAddresses_.count(target)) continue;
        const Link* link = nullptr;
        for (const Link& l : links) {
            // An inbound peer from that host that has not told its port yet counts too.
            if (l.ip == ip && (l.port == t.port || l.port == 0)) { link = &l; break; }
        }
        if (link) {
            if (link->age >= kStableConnection) {
                std::lock_guard<std::mutex> lock(peersMutex);
                dialTargets_[i].backoffSeconds = initialBackoffSeconds(t.kind);
                dialTargets_[i].nextAttempt = now;
            }
            continue;
        }
        if (now < t.nextAttempt) continue;
        if (t.kind == ConnKind::SEED && outbound >= MAX_OUTBOUND_PEERS) continue;

        if (t.kind == ConnKind::SEED) Bucks::logOut() << "DEBUG: Connecting to seed node " << t.host << ":" << t.port << std::endl;
        const bool started = startConnect(ip, t.port, t.kind);
        if (started) {
            links.push_back({ip, t.port, true, std::chrono::steady_clock::duration::zero()});
            if (t.kind == ConnKind::SEED) ++outbound;
        }
        std::lock_guard<std::mutex> lock(peersMutex);
        auto& dt = dialTargets_[i];
        // Back off even after a connect that succeeds, so a peer that accepts and then drops
        // us is not hammered.
        dt.nextAttempt = now + std::chrono::seconds(dt.backoffSeconds);
        if (!started) Bucks::logOut() << "DEBUG: cannot dial " << t.host << ":" << t.port
                                      << ", retrying in " << dt.backoffSeconds << " s" << std::endl;
        dt.backoffSeconds = std::min(dt.backoffSeconds * 2, kMaxBackoffSeconds);
    }

    std::set<NetAddress> linked(selfAddresses_.begin(), selfAddresses_.end());
    std::map<uint32_t, size_t> outboundGroups;
    for (const Link& l : links) {
        NetAddress a;
        if (!NetAddress::parse(l.ip + ":" + std::to_string(l.port), a)) continue;
        linked.insert(a);
        if (l.outbound) ++outboundGroups[a.group()];
    }
    dialFromAddressTable(outbound, linked, outboundGroups);
}

bool Network::outboundGroupFull(const NetAddress& a, const std::map<uint32_t, size_t>& outboundGroups) {
    if (!a.isRoutable()) return false;
    const auto it = outboundGroups.find(a.group());
    return it != outboundGroups.end() && it->second >= MAX_OUTBOUND_PER_GROUP;
}

namespace {
constexpr size_t kMaxAutoDialsInFlight = 2;
} // namespace

// Fills free outbound slots from the address table, one dial a round (about a second) and at
// most two in flight, so a table full of dead addresses costs little. Addresses from ADDR are
// only ever dialled from here, never as they arrive (audit NET-11).
void Network::dialFromAddressTable(size_t outbound, const std::set<NetAddress>& linked,
                                   const std::map<uint32_t, size_t>& outboundGroups) {
    if (outbound >= MAX_OUTBOUND_PEERS) return;
    size_t inFlight = 0;
    for (const PendingConnect& pc : pending_)
        if (pc.kind == ConnKind::AUTO) ++inFlight;
    if (inFlight >= kMaxAutoDialsInFlight) return;

    const int64_t now = unixNow();
    // Spread outbound peers over networks (audit NET-6): a /16 we have no outbound peer in comes
    // first, and a public /16 never holds more than MAX_OUTBOUND_PER_GROUP of them.
    const std::optional<NetAddress> pick = addrman_.select(
        now,
        [&](const NetAddress& a) {
            return linked.count(a) > 0 || outboundGroupFull(a, outboundGroups) || isIPBanned(a.ipString());
        },
        [&](const NetAddress& a) { return outboundGroups.count(a.group()) == 0; });
    if (!pick) return;
    addrman_.attempt(*pick, now);
    Bucks::logOut() << "DEBUG: Connecting to discovered peer " << pick->toString() << std::endl;
    startConnect(pick->ipString(), pick->port, ConnKind::AUTO);
}

// A peer that was disconnected from inside one of its own handlers (oversized frame, write timeout) still
// has an entry here with a closed descriptor. Remove those before the next poll() (audit NET-1).
void Network::reapDisconnectedPeers() {
    std::lock_guard<std::mutex> lock(peersMutex);
    for (auto it = peers.begin(); it != peers.end();) {
        if (it->second->getState() == PeerState::DISCONNECTED) it = peers.erase(it);
        else ++it;
    }
}

void Network::run() {
    // poll(), not select(): an fd_set holds descriptors below FD_SETSIZE (1024) only, and a node
    // with a raised descriptor limit would write past it (audit NET-NEW-9).
    std::vector<pollfd> fds;
    while (running) {
        reapDisconnectedPeers();
        fds.clear();
        fds.push_back({listenSocket, POLLIN, 0});
        {
            std::lock_guard<std::mutex> lock(peersMutex);
            for (auto const& [fd, peer] : peers) fds.push_back({fd, POLLIN, 0});
        }
        const size_t firstPending = fds.size();
        for (const PendingConnect& pc : pending_) fds.push_back({pc.fd, POLLOUT, 0});

        int activity = ::poll(fds.data(), nfds_t(fds.size()), 100);
        if (activity < 0) {
            if (errno == EINTR) continue;
            // Any other error: clean up and back off, never spin.
            reapDisconnectedPeers();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::vector<short> dialEvents;
        for (size_t i = firstPending; i < fds.size(); ++i) dialEvents.push_back(fds[i].revents);
        completeConnects(dialEvents);
        pruneZombiePeers();
        update();

        if (fds[0].revents & POLLIN) {
            acceptNewConnection();
        }

        handleSocketIO();
        flushQueues();
    }
    for (const PendingConnect& pc : pending_) closeSocket(pc.fd);
    pending_.clear();
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

void Network::flushQueues() {
    std::vector<std::pair<NetMessage, int>> relay;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        relay.swap(relayQueue);
    }
    if (!relay.empty()) {
        std::lock_guard<std::mutex> lock(peersMutex);
        for (const auto& [msg, except] : relay) {
            for (auto const& [fd, peer] : peers) {
                if (fd != except && peer->getState() == PeerState::ACTIVE) peer->sendMsg(msg);
            }
        }
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
        if (isIPBanned(ip)) {
            Bucks::logOut() << "DEBUG: Dropping connection from banned IP " << ip << std::endl;
#ifdef _WIN32
            closesocket(s);
#else
            close(s);
#endif
            return;
        }

        // The throttle slows one remote host's reconnect loop. Loopback connections come from this
        // machine (several local nodes discovering each other) and are not throttled.
        std::lock_guard<std::mutex> lock(securityMutex);
        auto now = std::chrono::steady_clock::now();
        if (lastConnectionTimes.size() > 4096) {   // forget addresses that are no longer throttled
            for (auto it = lastConnectionTimes.begin(); it != lastConnectionTimes.end();)
                it = now - it->second >= std::chrono::seconds(5) ? lastConnectionTimes.erase(it) : std::next(it);
        }
        if (!isLoopbackAddress(ip) && lastConnectionTimes.count(ip) > 0) {
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

    size_t inbound = 0;
    for (auto const& [fd, p] : peers)
        if (!p->outbound()) ++inbound;
    if (inbound >= options_.maxInbound) {
        Bucks::logOut() << "DEBUG: Inbound slots full (" << inbound << "). Dropping connection from " << ip << std::endl;
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

        // Nothing received (not even a pong) for inactivityTimeout: the slot goes to someone else.
        if (now - peer->lastReceived > options_.inactivityTimeout) {
            Bucks::logOut() << "DEBUG: Pruning silent peer " << peer->address() << " (inactivity timeout)" << std::endl;
            it = peers.erase(it);
            continue;
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
    // Local nodes share 127.0.0.1; banning it would cut off the whole local network.
    if (isLoopbackAddress(ip)) {
        Bucks::logOut() << "WARNING: Not banning loopback address " << ip << std::endl;
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(securityMutex);
    // Bans end (before: never, until a restart), and the list is bounded: when full, the ban
    // closest to its end makes room.
    if (bannedUntil_.size() >= kMaxBans && !bannedUntil_.count(ip)) {
        auto soonest = bannedUntil_.begin();
        for (auto it = bannedUntil_.begin(); it != bannedUntil_.end(); ++it)
            if (it->second < soonest->second) soonest = it;
        bannedUntil_.erase(soonest);
    }
    bannedUntil_[ip] = now + options_.banDuration;
    Bucks::logOut() << "DEBUG: IP " << ip << " banned for " << options_.banDuration.count() << " s." << std::endl;
}

bool Network::isIPBanned(const std::string& ip) const {
    std::lock_guard<std::mutex> lock(securityMutex);
    auto it = bannedUntil_.find(ip);
    if (it == bannedUntil_.end()) return false;
    if (std::chrono::steady_clock::now() < it->second) return true;
    bannedUntil_.erase(it);
    return false;
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

// Periodic work of the network thread.
void Network::update() {
    maintainConnections();
    sendPings();
    if (std::chrono::steady_clock::now() - lastAddressSave_ > std::chrono::minutes(15)) saveAddresses();
}

void Network::setAddressFile(const std::string& path) {
    addressFile_ = path;
    lastAddressSave_ = std::chrono::steady_clock::now();
    std::string text;
    if (!readWholeFile(path, text)) return;   // none yet: normal on a first start
    Bucks::logOut() << "DEBUG: Loaded " << addrman_.load(text) << " peer addresses from " << path << std::endl;
}

void Network::saveAddresses() {
    lastAddressSave_ = std::chrono::steady_clock::now();
    if (addressFile_.empty()) return;
    std::string err;
    if (!writePrivateFileAtomic(addressFile_, addrman_.serialize(), &err))
        Bucks::logErr() << "WARNING: cannot write " << addressFile_ << ": " << err << std::endl;
}

// Every active peer gets a ping (8 random bytes) each pingInterval, so a peer that went away
// without closing the connection is noticed by pruneZombiePeers (audit NET-12).
void Network::sendPings() {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(peersMutex);
    for (auto& [fd, peer] : peers) {
        if (peer->getState() != PeerState::ACTIVE || now - peer->lastPingSent < options_.pingInterval) continue;
        peer->lastPingSent = now;
        Writer w;
        w.u64(relayRng_());
        peer->sendMsg(NetMessage(MessageType::PING, w.take()));
    }
}

// Peer Discovery Implementation (Phase 6)

size_t Network::addAddresses(const std::vector<std::string>& addresses, const std::string& sourceIp,
                             std::vector<NetAddress>* fresh) {
    uint32_t source = 0;
    if (!NetAddress::parseIP(sourceIp, source)) return 0;
    const int64_t now = unixNow();
    size_t added = 0;
    for (const std::string& s : addresses) {
        NetAddress a;
        if (!NetAddress::parse(s, a) || !a.acceptableFrom(source) || selfAddresses_.count(a)) continue;
        if (!addrman_.add(a, source >> 16, now)) continue;
        ++added;
        if (fresh) fresh->push_back(a);
    }
    if (!addresses.empty())
        Bucks::logOut() << "DEBUG: " << added << " new of " << addresses.size() << " addresses from " << sourceIp << std::endl;
    return added;
}

void Network::learnAddress(const NetAddress& a, const std::string& sourceIp, int fromFd) {
    std::vector<NetAddress> fresh;
    addAddresses({a.toString()}, sourceIp, &fresh);
    for (const NetAddress& f : fresh) relayAddress(f, fromFd);
}

// Called by message handlers, under peersMutex: the address goes to two random active peers.
void Network::relayAddress(const NetAddress& a, int exceptFd) {
    std::vector<Peer*> to;
    for (auto& [fd, peer] : peers) {
        if (fd != exceptFd && peer->getState() == PeerState::ACTIVE) to.push_back(peer.get());
    }
    std::shuffle(to.begin(), to.end(), relayRng_);
    if (to.size() > 2) to.resize(2);
    if (to.empty()) return;
    const std::string s = a.toString();
    Writer w;
    w.u32(1);
    w.u16(uint16_t(s.size()));
    w.bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    const NetMessage msg(MessageType::ADDR, w.take());
    for (Peer* p : to) p->sendMsg(msg);
}

// Called by the VERSION handler, under peersMutex, once `p` has said who it is. When two nodes dial
// each other at the same moment (both learned the other's address together), each has two
// connections to the other. Both ends keep the one dialled by the node with the larger nonce, so
// they drop the same connection. True if `p` itself was dropped.
bool Network::dropDuplicate(Peer& p) {
    const std::optional<NetAddress> addr = p.listenAddress();
    if (!addr) return false;
    for (auto& [fd, other] : peers) {
        if (other.get() == &p || !other->versionSeen() || other->getState() == PeerState::DISCONNECTED) continue;
        if (other->listenAddress() != addr) continue;
        Peer* drop = &p;   // the same direction twice: the newer one goes
        if (p.outbound() != other->outbound()) {
            Peer* out = p.outbound() ? &p : other.get();
            Peer* in = p.outbound() ? other.get() : &p;
            drop = localNonce_ > p.remoteNonce() ? in : out;
        }
        Bucks::logOut() << "DEBUG: Second connection to " << addr->toString() << "; closing "
                        << (drop->outbound() ? "ours" : "theirs") << std::endl;
        drop->disconnect();
        return drop == &p;
    }
    return false;
}

// Our own address, learned by dialling it: never dialled or passed on again.
void Network::markSelf(const std::string& hostPort) {
    NetAddress a;
    if (!NetAddress::parse(hostPort, a)) return;
    selfAddresses_.insert(a);
    addrman_.remove(a);
}

} // namespace Bucks