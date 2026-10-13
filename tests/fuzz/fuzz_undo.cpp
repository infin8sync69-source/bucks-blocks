#include "fuzz_common.h"
#include "serialize.h"
#include "undo.h"

using namespace Bucks;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // The first byte is a mode switch, the rest is the record. The checksum rejects almost every
    // random mutation before any field is read, so when the low bit is set the checksum is
    // recomputed over everything but the last 32 bytes and the field parsers are reached too.
    if (size == 0) return 0;
    std::vector<uint8_t> in = Fuzz::bytes(data + 1, size - 1);
    if ((data[0] & 1) && in.size() >= 32) {
        std::vector<uint8_t> body(in.begin(), in.end() - 32);
        const Hash256 c = doubleSha256(body);
        in = body;
        in.insert(in.end(), c.begin(), c.end());
    }
    try {
        const BlockUndo u = BlockUndo::deserialize(in);
        if (u.serialize() != in) Fuzz::fail("undo: accepted a non-canonical encoding");
    } catch (const ParseError&) {
    }
    return 0;
}
