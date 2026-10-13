#include "fuzz_common.h"
#include "serialize.h"

using namespace Bucks;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Everything the reader returns for a CompactSize is re-encoded to the same prefix, and
    // varBytes never reads past the data.
    Reader r(data, size);
    try {
        const uint64_t v = r.compactSize(MAX_COMPACT_SIZE);
        Writer w;
        w.compactSize(v);
        if (r.position() != w.data().size()) Fuzz::fail("compactsize: consumed length differs from the minimal encoding");
        for (size_t i = 0; i < w.data().size(); ++i)
            if (data[i] != w.data()[i]) Fuzz::fail("compactsize: non-minimal encoding accepted");
    } catch (const ParseError&) {
    }
    Reader r2(data, size);
    try {
        const std::vector<uint8_t> v = r2.varBytes(1 << 16);
        if (v.size() > r2.position()) Fuzz::fail("varBytes: returned more bytes than consumed");
    } catch (const ParseError&) {
    }
    return 0;
}
