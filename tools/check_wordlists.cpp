// bucks-check-wordlists: validates every list named in wordlists/manifest.json and the set as a
// whole (see checkWordlistSet). Exit 0 when there are no errors; warnings do not fail.
// Every list users may get (anything but a "dev"/"test" placeholder) must carry a "sha256" pin,
// and loading already refuses a list whose bytes differ from its pin.
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
    WordlistCheck c = checkWordlistSet();
    for (const auto& lang : mnemonicLanguages()) {
        auto wl = findWordlist(lang);
        std::cout << lang << ": " << wl->entries.size() << " entries, normalizer " << wl->normalizer
                  << ", status " << wl->status << ", sha256 " << wl->sha256 << (wl->pinned ? " (pinned)" : "") << "\n";
        if (!wl->pinned && wl->status != "dev" && wl->status != "test")
            c.errors.push_back(lang + ": no \"sha256\" pin in manifest.json (add \"sha256\":\"" + wl->sha256 + "\")");
    }
    for (const auto& w : c.warnings) std::cout << "warning: " << w << "\n";
    for (const auto& e : c.errors) std::cerr << "error: " << e << "\n";
    if (!c.ok()) return 1;
    std::cout << "word lists OK\n";
    return 0;
}
