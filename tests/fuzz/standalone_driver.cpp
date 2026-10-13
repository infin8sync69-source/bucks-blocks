// Replays files (or every file under a directory) through LLVMFuzzerTestOneInput. Used when the
// compiler has no libFuzzer runtime (Apple clang, gcc): the targets then act as a regression
// test over the seed corpus and any crash reproducers.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

namespace fs = std::filesystem;

static size_t replay(const fs::path& p) {
    size_t n = 0;
    if (fs::is_directory(p)) {
        for (const auto& e : fs::recursive_directory_iterator(p))
            if (e.is_regular_file()) n += replay(e.path());
        return n;
    }
    std::ifstream f(p, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    LLVMFuzzerTestOneInput(data.data(), data.size());
    return 1;
}

int main(int argc, char** argv) {
    size_t n = 0;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') continue;   // libFuzzer-style flags are ignored
        n += replay(argv[i]);
    }
    std::printf("replayed %zu input(s)\n", n);
    return n ? 0 : 1;
}
