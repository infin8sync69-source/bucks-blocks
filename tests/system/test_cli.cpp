// The bucks binary's mining options: bad values and an --auto-mine with nowhere to pay the
// rewards stop the node at startup with exit code 2 (audit OPS-03, OPS-04).
#include <gtest/gtest.h>
#include "chainparams.h"
#include "crypto.h"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef _WIN32
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

#ifndef _WIN32
struct NodeRun {
    int exitCode = -1;
    std::string output;
};

// Runs the node with `args` and a fresh datadir; returns its exit code and output.
NodeRun runBucks(const std::string& args) {
    static std::atomic<int> counter{0};
    const fs::path dir = fs::temp_directory_path() /
        ("bucks-cli-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string log = (dir / "out.log").string();
    const std::string cmd = std::string("'") + BUCKS_BINARY + "' --datadir '" + (dir / "data").string() + "' " +
                            args + " > '" + log + "' 2>&1 < /dev/null";
    const int status = std::system(cmd.c_str());
    NodeRun r;
    if (status != -1 && WIFEXITED(status)) r.exitCode = WEXITSTATUS(status);
    std::ifstream in(log);
    std::ostringstream ss;
    ss << in.rdbuf();
    r.output = ss.str();
    fs::remove_all(dir);
    return r;
}

// A port that was free a moment ago, so a node that gets as far as P2P can bind it.
int freePort() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof a;
    int port = 0;
    if (fd >= 0 && ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 &&
        ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len) == 0)
        port = ntohs(a.sin_port);
    if (fd >= 0) ::close(fd);
    return port;
}
#endif

std::string addressFor(Bucks::NetworkId id) {
    const std::string seed = "cli-test";
    const Bucks::Hash256 h = Bucks::sha256(std::vector<uint8_t>(seed.begin(), seed.end()));
    return Bucks::base58CheckEncode(Bucks::chainParams(id).p2pkhAddressVersion,
                                    std::vector<uint8_t>(h.begin(), h.begin() + 20));
}

} // namespace

TEST(Cli, AutoMineWithNowhereToPayRefusesToStart) {
#ifdef _WIN32
    GTEST_SKIP() << "POSIX process test";
#else
    const NodeRun r = runBucks("--network=regtest --auto-mine --p2p-port " + std::to_string(freePort()) +
                               " --port " + std::to_string(freePort()));
    EXPECT_EQ(r.exitCode, 2) << r.output;
    EXPECT_NE(r.output.find("--auto-mine: No wallet to mine to"), std::string::npos) << r.output;
    EXPECT_NE(r.output.find("--mine-address"), std::string::npos) << r.output;
#endif
}

TEST(Cli, MineAddressMustBelongToTheNetwork) {
#ifdef _WIN32
    GTEST_SKIP() << "POSIX process test";
#else
    NodeRun r = runBucks("--network=regtest --mine-address " + addressFor(Bucks::NetworkId::TEST));
    EXPECT_EQ(r.exitCode, 2) << r.output;
    EXPECT_NE(r.output.find("is not a valid regtest address"), std::string::npos) << r.output;
    r = runBucks("--network=regtest --mine-address nonsense");
    EXPECT_EQ(r.exitCode, 2) << r.output;
#endif
}

TEST(Cli, MineThreadsMustBeANumber) {
#ifdef _WIN32
    GTEST_SKIP() << "POSIX process test";
#else
    for (const char* bad : {"lots", "-1", "1025", "99999"}) {
        const NodeRun r = runBucks(std::string("--network=regtest --mine-threads ") + bad);
        EXPECT_EQ(r.exitCode, 2) << bad << ": " << r.output;
        EXPECT_NE(r.output.find("--mine-threads expects"), std::string::npos) << r.output;
    }
#endif
}
