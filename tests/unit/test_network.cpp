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
    EXPECT_EQ(NetMessageHeader::maxPayload(MessageType::PING), MAX_MESSAGE_PAYLOAD);

    // A tx frame above its cap is refused although it is far below the 4 MiB limit.
    std::vector<uint8_t> big(NetMessageHeader::maxPayload(MessageType::TX) + 1, 7);
    EXPECT_THROW(NetMessage::deserialize(frame(kMagic, command("tx"), uint32_t(big.size()), big), kMagic), NetMalformed);
    std::vector<uint8_t> ok(NetMessageHeader::maxPayload(MessageType::TX), 7);
    EXPECT_NO_THROW(NetMessage::deserialize(frame(kMagic, command("tx"), uint32_t(ok.size()), ok), kMagic));
}
