#ifndef BUCKS_FUZZ_COMMON_H
#define BUCKS_FUZZ_COMMON_H

// Shared by the libFuzzer targets (docs/design/consensus-validation.md section 11.3).
// A target feeds one parser with arbitrary bytes. ParseError is the documented way to
// reject input; any other exception, a crash, a sanitizer report, or an accepted input that
// does not re-encode to the same bytes is a bug and aborts the process.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace Fuzz {

[[noreturn]] inline void fail(const char* what) {
    std::fprintf(stderr, "FUZZ FINDING: %s\n", what);
    std::abort();
}

inline std::vector<uint8_t> bytes(const uint8_t* data, size_t size) {
    return std::vector<uint8_t>(data, data + size);
}

} // namespace Fuzz

#endif
