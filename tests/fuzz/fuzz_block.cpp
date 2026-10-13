#include "fuzz_common.h"
#include "block.h"
#include "chainparams.h"
#include "serialize.h"
#include "validation_rules.h"

using namespace Bucks;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::vector<uint8_t> in = Fuzz::bytes(data, size);
    try {
        const Block block = Block::deserialize(in);
        if (block.serialize() != in) Fuzz::fail("block: accepted a non-canonical encoding");
        bool mutated = false;
        (void)block.computeMerkleRoot(&mutated);
        ValidationState st;
        (void)Rules::CheckBlock(block, chainParams(NetworkId::REGTEST), st);
    } catch (const ParseError&) {
    }
    return 0;
}
