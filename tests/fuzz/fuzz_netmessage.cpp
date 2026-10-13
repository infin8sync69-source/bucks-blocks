#include "fuzz_common.h"
#include "chainparams.h"
#include "network.h"

using namespace Bucks;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static const auto magic = chainParams(NetworkId::REGTEST).netMagic;
    const std::vector<uint8_t> in = Fuzz::bytes(data, size);
    try {
        const NetMessage m = NetMessage::deserialize(in, magic);
        if (m.serialize(magic) != in) Fuzz::fail("frame: accepted a non-canonical encoding");
        if (m.payload.size() > NetMessageHeader::maxPayload(m.type)) Fuzz::fail("frame: payload above its cap");
    } catch (const NetMalformed&) {
    } catch (const NetIncomplete&) {
    }
    return 0;
}
