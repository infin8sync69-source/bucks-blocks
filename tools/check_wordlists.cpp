// bucks-check-wordlists: validates every list named in wordlists/manifest.json and the set as a
// whole (see checkWordlistSet). Exit 0 when there are no errors; warnings do not fail.
//
//   bucks-check-wordlists [wordlist-dir]
#include <iostream>

#include "bip_8192.h"

int main(int argc, char** argv) {
    using namespace Bucks;
    if (argc > 1) setWordlistDir(argv[1]);
    std::string err;
    if (!loadWordlists(&err)) {
        std::cerr << "FAIL: " << err << "\n";
        return 1;
    }
    for (const auto& lang : mnemonicLanguages()) {
        auto wl = findWordlist(lang);
        std::cout << lang << ": " << wl->entries.size() << " entries, normalizer " << wl->normalizer
                  << ", status " << wl->status << "\n";
    }
    const WordlistCheck c = checkWordlistSet();
    for (const auto& w : c.warnings) std::cout << "warning: " << w << "\n";
    for (const auto& e : c.errors) std::cerr << "error: " << e << "\n";
    if (!c.ok()) return 1;
    std::cout << "word lists OK\n";
    return 0;
}
