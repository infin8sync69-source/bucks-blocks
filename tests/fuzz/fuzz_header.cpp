#include "fuzz_common.h"
#include "blockheader.h"
#include "serialize.h"

using namespace Bucks;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::vector<uint8_t> in = Fuzz::bytes(data, size);
    try {
        const BlockHeader h = BlockHeader::deserialize(in);
        if (h.serialize() != in) Fuzz::fail("header: accepted a non-canonical encoding");
        (void)h.hash();
    } catch (const ParseError&) {
    }
    return 0;
}
