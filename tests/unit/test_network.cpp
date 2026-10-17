#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "chainparams.h"
#include "network.h"

using namespace Bucks;

namespace {
const std::array<uint8_t, 4> kMagic = chainParams(NetworkId::REGTEST).netMagic;

// A frame with arbitrary header fields, for the hardening tests.
std::vector<uint8_t> frame(const std::array<uint8_t, 4>& magic, const char* command12, uint32_t length,
                           const std::vector<uint8_t>& payload, bool goodChecksum = true) {
    std::vector<uint8_t> f(magic.begin(), magic.end());
    f.insert(f.end(), command12, command12 + 12);
    for (int i = 0; i < 4; ++i) f.push_back(uint8_t(length >> (8 * i)));
    Hash256 h = doubleSha256(payload);
    for (int i = 0; i < 4; ++i) f.push_back(goodChecksum ? h[i] : uint8_t(h[i] ^ 1));
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}

char cmd12[12];
const char* command(const char* name) {
    memset(cmd12, 0, 12);
    strncpy(cmd12, name, 12);
    return cmd12;
}
} // namespace

TEST(NetworkMessageTest, SerializationRoundTrip) {
    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    NetMessage original(MessageType::VERSION, payload);

    std::vector<uint8_t> serialized = original.serialize(kMagic);
    ASSERT_EQ(serialized.size(), 24u + payload.size());
    EXPECT_EQ(std::vector<uint8_t>(serialized.begin(), serialized.begin() + 4),
              std::vector<uint8_t>(kMagic.begin(), kMagic.end()));

    NetMessage recovered = NetMessage::deserialize(serialized, kMagic);
    EXPECT_EQ(recovered.type, MessageType::VERSION);
    EXPECT_EQ(recovered.payload, payload);
}

TEST(NetworkMessageTest, CommandToTypeMapping) {
    EXPECT_EQ(NetMessageHeader::commandToType(command("version")), MessageType::VERSION);
    EXPECT_EQ(NetMessageHeader::commandToType(command("inv")), MessageType::INV);
    EXPECT_EQ(NetMessageHeader::commandToType(command("getheaders")), MessageType::GETHEADERS);
    EXPECT_EQ(NetMessageHeader::commandToType(command("unknown_cmd")), MessageType::UNKNOWN);
    EXPECT_STREQ(NetMessageHeader::typeToCommand(MessageType::VERSION).c_str(), "version");
    // Every known command round-trips.
    for (uint8_t t = 0; t <= 13; ++t) {
        const MessageType type = MessageType(t);
        EXPECT_EQ(NetMessageHeader::commandToType(command(NetMessageHeader::typeToCommand(type).c_str())), type);
    }
}

// NET-17: the old matcher used strncmp prefixes, so "invalid" was an "inv" and "txx" a "tx".
TEST(NetworkMessageTest, CommandsMatchExactly) {
    for (const char* bad : {"invalid", "txx", "blockchain", "versionx", "getheadersx", "pingpong", "Inv", "IN V",
                            "inv-", "tx\x01"}) {
        EXPECT_EQ(NetMessageHeader::commandToType(command(bad)), MessageType::UNKNOWN) << bad;
    }
    char c[12] = {'i', 'n', 'v', 0, 'x', 0, 0, 0, 0, 0, 0, 0};   // a byte after the terminating zero
    EXPECT_EQ(NetMessageHeader::commandToType(c), MessageType::UNKNOWN);
    char empty[12] = {};
    EXPECT_EQ(NetMessageHeader::commandToType(empty), MessageType::UNKNOWN);
    char full[12] = {'g', 'e', 't', 'h', 'e', 'a', 'd', 'e', 'r', 's', 'x', 'y'};   // 12 chars, no terminator
    EXPECT_EQ(NetMessageHeader::commandToType(full), MessageType::UNKNOWN);
}

// OPS-8: every check lives in deserialize itself.
TEST(NetworkMessageTest, FrameHardening) {
    const std::vector<uint8_t> payload = {1, 2, 3};
    EXPECT_NO_THROW(NetMessage::deserialize(frame(kMagic, command("tx"), 3, payload), kMagic));

    // Wrong network.
    std::array<uint8_t, 4> other = chainParams(NetworkId::TEST).netMagic;
    EXPECT_THROW(NetMessage::deserialize(frame(other, command("tx"), 3, payload), kMagic), NetMalformed);
    // The old Bucks magic is dead.
    std::array<uint8_t, 4> old = {0xF5, 0xB3, 0xB4, 0xD9};
    EXPECT_THROW(NetMessage::deserialize(frame(old, command("tx"), 3, payload), kMagic), NetMalformed);
    // Unknown and prefix commands.
    EXPECT_THROW(NetMessage::deserialize(frame(kMagic, command("txx"), 3, payload), kMagic), NetMalformed);
    // Checksum.
    EXPECT_THROW(NetMessage::deserialize(frame(kMagic, command("tx"), 3, payload, false), kMagic), NetMalformed);
    // Too short for a header; payload not all there.
    EXPECT_THROW(NetMessage::deserialize(std::vector<uint8_t>(10, 0), kMagic), NetIncomplete);
    std::vector<uint8_t> cut = frame(kMagic, command("tx"), 3, payload);
    cut.pop_back();
    EXPECT_THROW(NetMessage::deserialize(cut, kMagic), NetIncomplete);
    // Bytes after the payload.
    std::vector<uint8_t> extra = frame(kMagic, command("tx"), 3, payload);
    extra.push_back(0);
    EXPECT_THROW(NetMessage::deserialize(extra, kMagic), NetMalformed);
}

// A 32-bit `24 + length` wrapped around and then read out of bounds.
TEST(NetworkMessageTest, HugeLengthDoesNotWrap) {
    for (uint32_t len : {0xFFFFFFF0u, 0xFFFFFFE8u, 0xFFFFFFFFu, 0x80000000u}) {
        EXPECT_THROW(NetMessage::deserialize(frame(kMagic, command("ping"), len, {}), kMagic), NetMalformed) << len;
    }
}

TEST(NetworkMessageTest, PerCommandPayloadCaps) {
    EXPECT_GE(NetMessageHeader::maxPayload(MessageType::BLOCK), 1'000'000u);
    EXPECT_LT(NetMessageHeader::maxPayload(MessageType::BLOCK), 1'001'000u);
    EXPECT_GE(NetMessageHeader::maxPayload(MessageType::TX), 100'000u);
    EXPECT_LT(NetMessageHeader::maxPayload(MessageType::TX), 101'000u);
    EXPECT_GE(NetMessageHeader::maxPayload(MessageType::GETHEADERS), 3'269u);
    EXPECT_GE(NetMessageHeader::maxPayload(MessageType::HEADERS), 168'003u);
    // NET-14: small messages are capped near their size (a 4 MiB ping was echoed in full).
    for (MessageType t : {MessageType::PING, MessageType::PONG, MessageType::VERACK, MessageType::GETADDR,
                          MessageType::VERSION}) {
        EXPECT_LE(NetMessageHeader::maxPayload(t), 64u + 256u) << NetMessageHeader::typeToCommand(t);
    }
    EXPECT_GE(NetMessageHeader::maxPayload(MessageType::PING), 8u);
    EXPECT_GE(NetMessageHeader::maxPayload(MessageType::ADDR), 4u + 1000u * 23u);
    EXPECT_GE(NetMessageHeader::maxPayload(MessageType::INV), 4u + 50'000u * 36u);
    std::vector<uint8_t> bigPing(1000, 1);
    EXPECT_THROW(NetMessage::deserialize(frame(kMagic, command("ping"), 1000, bigPing), kMagic), NetMalformed);

    // A tx frame above its cap is refused although it is far below the 4 MiB limit.
    std::vector<uint8_t> big(NetMessageHeader::maxPayload(MessageType::TX) + 1, 7);
    EXPECT_THROW(NetMessage::deserialize(frame(kMagic, command("tx"), uint32_t(big.size()), big), kMagic), NetMalformed);
    std::vector<uint8_t> ok(NetMessageHeader::maxPayload(MessageType::TX), 7);
    EXPECT_NO_THROW(NetMessage::deserialize(frame(kMagic, command("tx"), uint32_t(ok.size()), ok), kMagic));
}

// ---------------------------------------------------------------------------
// Address table (audit NET-NEW-4, NET-11)
// ---------------------------------------------------------------------------

namespace {
NetAddress na(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint16_t port = 8733) {
    return NetAddress{a << 24 | b << 16 | c << 8 | d, port};
}
uint32_t ip(const char* s) {
    uint32_t out = 0;
    EXPECT_TRUE(NetAddress::parseIP(s, out)) << s;
    return out;
}
constexpr int64_t kNow = 1'800'000'000;
} // namespace

TEST(NetAddressTest, ParseIsStrict) {
    NetAddress a;
    ASSERT_TRUE(NetAddress::parse("1.2.3.4:8733", a));
    EXPECT_EQ(a, na(1, 2, 3, 4));
    EXPECT_EQ(a.toString(), "1.2.3.4:8733");
    EXPECT_EQ(a.group(), (1u << 8) | 2u);
    for (const char* bad : {"1.2.3.4", "1.2.3.4:", ":80", "1.2.3.4:0", "1.2.3.4:65536", "1.2.3.4:123456",
                            "1.2.3.4:+80", "1.2.3.4: 80", " 1.2.3.4:80", "1.2.3.4:80 ", "1.2.3:80", "1.2.3.4.5:80",
                            "256.1.1.1:80", "seed.example:80", "[::1]:80", "::1:80", "1.2.3.4:80:80", ""}) {
        EXPECT_FALSE(NetAddress::parse(bad, a)) << bad;
    }
    EXPECT_FALSE(NetAddress::parse(std::string(400, 'a') + ":80", a));
}

// A peer may point us at public addresses only; LAN and loopback addresses only come from peers
// in that scope (a remote peer cannot make us dial services on our own network or host).
TEST(NetAddressTest, WhoMayTellUsWhat) {
    const uint32_t pub = ip("8.8.8.8"), lan = ip("192.168.1.5"), lo = ip("127.0.0.1");
    EXPECT_TRUE(na(9, 9, 9, 9).acceptableFrom(pub));
    for (const NetAddress& a : {na(192, 168, 1, 9), na(10, 0, 0, 1), na(172, 16, 0, 1), na(100, 64, 0, 1),
                                na(169, 254, 1, 1), na(127, 0, 0, 1)}) {
        EXPECT_FALSE(a.acceptableFrom(pub)) << a.toString();
    }
    EXPECT_TRUE(na(10, 0, 0, 1).acceptableFrom(lan));
    EXPECT_FALSE(na(127, 0, 0, 1).acceptableFrom(lan));
    EXPECT_TRUE(na(127, 0, 0, 1).acceptableFrom(lo));
    EXPECT_TRUE(na(192, 168, 1, 9).acceptableFrom(lo));
    for (const NetAddress& a : {na(0, 1, 2, 3), na(224, 0, 0, 1), na(240, 0, 0, 1), na(255, 255, 255, 255),
                                na(1, 2, 3, 4, 0)}) {
        EXPECT_FALSE(a.acceptableFrom(lo)) << a.toString();
    }
    EXPECT_FALSE(na(172, 31, 0, 1).isRoutable());
    EXPECT_TRUE(na(172, 32, 0, 1).isRoutable());
    EXPECT_FALSE(na(203, 0, 113, 7).isRoutable());
}

// One /16 of sources can only churn its own share of the table; the table never grows past its bounds.
TEST(AddrManTest, FloodFromOneSourceTakesOnlyItsShare) {
    AddrMan m;
    const uint32_t honest = na(5, 5, 0, 0).group(), flooder = na(6, 6, 0, 0).group();
    for (uint32_t i = 0; i < 10; ++i) EXPECT_TRUE(m.add(na(9, 9, i, 1), honest, kNow));
    for (uint32_t i = 0; i < 5000; ++i) m.add(na(20 + i / 65536, (i / 256) % 256, i % 256, 1), flooder, kNow + i);
    EXPECT_EQ(m.size(), 10 + AddrMan::MAX_NEW_PER_SOURCE_GROUP);
    for (uint32_t i = 0; i < 10; ++i) EXPECT_TRUE(m.find(na(9, 9, i, 1))) << i;

    // Many sources together: the new entries stop at MAX_NEW.
    for (uint32_t g = 0; g < 100; ++g)
        for (uint32_t i = 0; i < AddrMan::MAX_NEW_PER_SOURCE_GROUP; ++i) m.add(na(30 + g, 1, i, 1), 0x1000 + g, kNow);
    EXPECT_EQ(m.size(), AddrMan::MAX_NEW);
}

TEST(AddrManTest, TriedEntriesSurviveAFlood) {
    AddrMan m;
    const NetAddress good = na(9, 9, 9, 9);
    m.add(good, na(5, 5, 0, 0).group(), kNow);
    m.good(good, kNow);
    EXPECT_EQ(m.triedCount(), 1u);
    for (uint32_t g = 0; g < 200; ++g)
        for (uint32_t i = 0; i < AddrMan::MAX_NEW_PER_SOURCE_GROUP; ++i) m.add(na(30 + g, 1, i, 1), 0x1000 + g, kNow);
    auto e = m.find(good);
    ASSERT_TRUE(e);
    EXPECT_TRUE(e->tried);
    EXPECT_EQ(m.size(), AddrMan::MAX_NEW + 1);

    // The tried entries are bounded too.
    for (uint32_t i = 0; i < AddrMan::MAX_TRIED + 50; ++i) m.good(na(40, 1, i / 256, i % 256), kNow + 1 + i);
    EXPECT_EQ(m.triedCount(), AddrMan::MAX_TRIED);
    EXPECT_FALSE(m.find(good)) << "the oldest tried entry makes room";
}

TEST(AddrManTest, SelectSkipsWaitsAndPrefers) {
    AddrMan m;
    const NetAddress a = na(9, 9, 9, 1), b = na(9, 9, 9, 2), c = na(8, 8, 8, 8);
    const uint32_t src = na(5, 5, 0, 0).group();
    m.add(a, src, kNow);
    m.add(b, src, kNow);
    auto skipA = [&](const NetAddress& x) { return x == a; };
    for (int i = 0; i < 20; ++i) EXPECT_EQ(m.select(kNow, skipA), b);

    m.attempt(b, kNow);                                    // a dial that never completed
    EXPECT_FALSE(m.select(kNow + 1, skipA)) << "b waits before it is tried again";
    EXPECT_EQ(m.select(kNow + 31, skipA), b);
    m.attempt(b, kNow + 31);
    EXPECT_FALSE(m.select(kNow + 31 + 59, skipA));
    EXPECT_EQ(m.select(kNow + 31 + 60, skipA), b);

    // Entries in an unused /16 come first.
    m.add(c, src, kNow);
    auto prefer = [&](const NetAddress& x) { return x.group() != a.group(); };
    for (int i = 0; i < 20; ++i) EXPECT_EQ(m.select(kNow, {}, prefer), c);

    // An address that never answers is dropped after ten dials.
    for (int i = 0; i < 10; ++i) m.attempt(c, kNow);
    EXPECT_FALSE(m.select(kNow + 10 * 86400, skipA).value_or(NetAddress{}) == c);
    EXPECT_FALSE(m.find(c));
}

TEST(AddrManTest, SampleLeavesOutUnreachable) {
    AddrMan m;
    const NetAddress a = na(9, 9, 9, 1), b = na(9, 9, 9, 2);
    m.add(a, 1, kNow);
    m.add(b, 1, kNow);
    for (int i = 0; i < 3; ++i) m.attempt(b, kNow);
    const auto s = m.sample(100, kNow);
    ASSERT_EQ(s.size(), 1u);
    EXPECT_EQ(s[0], a);
    EXPECT_TRUE(m.sample(100, kNow + 31 * 86400).empty()) << "not heard of for a month";
}

// NET-6: at most two automatic outbound peers in one public /16; local addresses are not limited.
TEST(NetworkPolicyTest, OutboundGroupLimit) {
    std::map<uint32_t, size_t> groups;
    EXPECT_FALSE(Network::outboundGroupFull(na(9, 9, 1, 1), groups));
    groups[na(9, 9, 0, 0).group()] = 1;
    EXPECT_FALSE(Network::outboundGroupFull(na(9, 9, 1, 1), groups));
    groups[na(9, 9, 0, 0).group()] = MAX_OUTBOUND_PER_GROUP;
    EXPECT_TRUE(Network::outboundGroupFull(na(9, 9, 200, 7), groups));
    EXPECT_FALSE(Network::outboundGroupFull(na(9, 10, 1, 1), groups));
    groups[na(127, 0, 0, 0).group()] = 50;
    groups[na(192, 168, 0, 0).group()] = 50;
    EXPECT_FALSE(Network::outboundGroupFull(na(127, 0, 0, 1), groups));
    EXPECT_FALSE(Network::outboundGroupFull(na(192, 168, 1, 1), groups));
}

TEST(NetworkPolicyTest, SlotsAddUp) {
    EXPECT_EQ(MAX_INBOUND_PEERS + MAX_OUTBOUND_PEERS + MAX_MANUAL_PEERS, MAX_PEERS);
    EXPECT_LT(MAX_INBOUND_PEERS, MAX_PEERS);
}

TEST(AddrManTest, SaveAndLoad) {
    AddrMan m;
    const NetAddress good = na(9, 9, 9, 9), heard = na(8, 8, 8, 8);
    m.add(heard, 77, kNow);
    m.attempt(heard, kNow + 5);
    m.good(good, kNow + 10);
    const std::string text = m.serialize();

    AddrMan again;
    EXPECT_EQ(again.load(text + "1.2.3.4:5 junk\nnot an address 0 0 0 0 0 0\n0.0.0.0:80 0 1 0 0 0 1\n"), 2u);
    auto g = again.find(good), h = again.find(heard);
    ASSERT_TRUE(g && h);
    EXPECT_TRUE(g->tried);
    EXPECT_EQ(g->lastSuccess, kNow + 10);
    EXPECT_FALSE(h->tried);
    EXPECT_EQ(h->attempts, 1);
    EXPECT_EQ(h->sourceGroup, 77u);
    EXPECT_EQ(again.triedCount(), 1u);
    EXPECT_EQ(again.load("something else\n" + text), 0u);

    // The bounds hold for a file written by hand.
    std::string big = "bucks-addresses 1\n";
    for (uint32_t i = 0; i < 5000; ++i) big += na(20, 1, i / 256, i % 256).toString() + " 0 1 0 0 0 5\n";
    AddrMan bounded;
    EXPECT_EQ(bounded.load(big), AddrMan::MAX_NEW_PER_SOURCE_GROUP);
}
