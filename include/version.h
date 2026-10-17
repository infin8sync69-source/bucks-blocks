#ifndef BUCKS_VERSION_H
#define BUCKS_VERSION_H

#include <string>

namespace Bucks {

// The release version, in one place. Bump it in the release commit.
constexpr const char* BUCKS_VERSION = "0.2.0";

// What a node reports: the version plus the network it runs, e.g. "0.2.0-main" (audit ML-11, OPS-14).
inline std::string nodeVersionString(const std::string& network) {
    return std::string(BUCKS_VERSION) + "-" + network;
}

} // namespace Bucks

#endif // BUCKS_VERSION_H
