// The bucks executable itself: start-up checks, option parsing, mining options and signals
// (audit OPS-03, OPS-04, OPS-06, OPS-12, OPS-13).
#include <gtest/gtest.h>

#include "chainparams.h"
#include "crypto.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

#ifndef _WIN32

struct RunResult {
    int exitCode = -1;
    std::string output;   // stdout and stderr
};

// Runs the node binary with `args` (already shell-quoted) and waits for it.
RunResult runNode(const std::string& args) {
    RunResult r;
    const std::string cmd = std::string("'") + BUCKS_NODE_BINARY + "' " + args + " 2>&1 </dev/null";
    FILE* p = ::popen(cmd.c_str(), "r");
    if (!p) return r;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) r.output.append(buf, n);
    const int status = ::pclose(p);
    r.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    return r;
}

// A TCP port that was free a moment ago.
uint16_t freePort() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(a);
    uint16_t port = 0;
    if (fd >= 0 && ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0 &&
        ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len) == 0)
        port = ntohs(a.sin_port);
    if (fd >= 0) ::close(fd);
    return port;
}

std::string readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Polls until `done` holds or `seconds` pass.
template <class F>
bool waitFor(F done, int seconds) {
    for (int i = 0; i < seconds * 20; ++i) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return done();
}

// A valid address of `id` that nobody holds the key for.
std::string addressFor(Bucks::NetworkId id) {
    const std::string seed = "cli-test";
    const Bucks::Hash256 h = Bucks::sha256(std::vector<uint8_t>(seed.begin(), seed.end()));
    return Bucks::base58CheckEncode(Bucks::chainParams(id).p2pkhAddressVersion,
                                    std::vector<uint8_t>(h.begin(), h.begin() + 20));
}

class CliTest : public ::testing::Test {
protected:
    std::string dir;

    void SetUp() override {
        static std::atomic<int> counter{0};
        dir = (fs::temp_directory_path() /
               ("bucks-cli-" + std::to_string(::getpid()) + "-" + std::to_string(counter++))).string();
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // Runs the node on a fresh datadir under `dir`.
    RunResult runWithDatadir(const std::string& args) {
        return runNode("--datadir '" + dir + "/data' " + args);
    }
};

#endif

} // namespace

#ifndef _WIN32

// --wordlist-dir is used as given, and a server without usable word lists stops before it opens the chain.
TEST_F(CliTest, ServerWithoutWordListsStopsBeforeTouchingTheChain) {
    fs::create_directories(dir + "/empty");
    const RunResult r = runNode("--network=regtest --server --datadir '" + dir + "/data' --wordlist-dir '" +
                                dir + "/empty'");
    EXPECT_EQ(r.exitCode, 1) << r.output;
    EXPECT_NE(r.output.find("manifest.json"), std::string::npos) << r.output;
    EXPECT_NE(r.output.find("--wordlist-dir"), std::string::npos) << r.output;
    EXPECT_FALSE(fs::exists(dir + "/data/regtest/chain.sqlite"));
}

// OPS-13: a malformed or out-of-range number is a usage error (exit 2) with a message, never an abort or a
// port that silently wraps around.
TEST_F(CliTest, MalformedNumbersAreUsageErrors) {
    const char* const bad[][2] = {
        {"--port", "abc"}, {"--port", "99999"}, {"--port", "-1"}, {"--port", "0"}, {"--port", "80x"},
        {"--port", " 80"}, {"--p2p-port", "65536"}, {"--p2p-port", "18446744073709551617"},
        {"--ipfs-port", "''"}, {"--checkblocks", "-1"}, {"--checkblocks", "4294967296"},
    };
    for (const auto& b : bad) {
        const RunResult r = runNode("--network=regtest --datadir '" + dir + "/data' " + b[0] + " '" + b[1] + "'");
        EXPECT_EQ(r.exitCode, 2) << b[0] << " " << b[1] << ": " << r.output;
        EXPECT_NE(r.output.find(std::string(b[0]) + " expects a whole number"), std::string::npos) << r.output;
        EXPECT_EQ(r.output.find("terminate called"), std::string::npos) << r.output;
    }
    EXPECT_FALSE(fs::exists(dir + "/data/regtest/chain.sqlite"));
    const RunResult r = runNode("--network=regtest --datadir '" + dir + "/data' --p2p-port " +
                                std::to_string(freePort()) + " --connect 127.0.0.1:80x");
    EXPECT_EQ(r.exitCode, 2) << r.output;
    EXPECT_NE(r.output.find("--connect expects host:port"), std::string::npos) << r.output;
}

// OPS-03: --auto-mine with nowhere to pay the rewards stops the node at startup instead of running a node
// that silently does not mine.
TEST_F(CliTest, AutoMineWithNowhereToPayRefusesToStart) {
    const RunResult r = runWithDatadir("--network=regtest --auto-mine --p2p-port " + std::to_string(freePort()) +
                                       " --port " + std::to_string(freePort()));
    EXPECT_EQ(r.exitCode, 2) << r.output;
    EXPECT_NE(r.output.find("--auto-mine: No wallet to mine to"), std::string::npos) << r.output;
    EXPECT_NE(r.output.find("--mine-address"), std::string::npos) << r.output;
}

// OPS-04: a payout address of another network, or no address at all, is refused before anything starts.
TEST_F(CliTest, MineAddressMustBelongToTheNetwork) {
    RunResult r = runWithDatadir("--network=regtest --mine-address " + addressFor(Bucks::NetworkId::TEST));
    EXPECT_EQ(r.exitCode, 2) << r.output;
    EXPECT_NE(r.output.find("is not a valid regtest address"), std::string::npos) << r.output;
    r = runWithDatadir("--network=regtest --mine-address nonsense");
    EXPECT_EQ(r.exitCode, 2) << r.output;
}

TEST_F(CliTest, MineThreadsMustBeANumber) {
    for (const char* bad : {"lots", "-1", "1025", "99999"}) {
        const RunResult r = runWithDatadir(std::string("--network=regtest --mine-threads ") + bad);
        EXPECT_EQ(r.exitCode, 2) << bad << ": " << r.output;
        EXPECT_NE(r.output.find("--mine-threads expects"), std::string::npos) << r.output;
    }
}

// OPS-12: SIGHUP (a closed terminal) is logged and ignored; SIGTERM still stops the node cleanly.
TEST_F(CliTest, HangupIsIgnoredAndTermStopsCleanly) {
    const uint16_t p2p = freePort();
    ASSERT_NE(p2p, 0);
    const std::string log = dir + "/node.log";
    const std::string datadir = dir + "/data";
    const std::string p2pArg = std::to_string(p2p);
    const pid_t pid = ::fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        const int fd = ::open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) ::_exit(127);
        ::dup2(fd, 1);
        ::dup2(fd, 2);
        ::execl(BUCKS_NODE_BINARY, BUCKS_NODE_BINARY, "--network=regtest", "--datadir", datadir.c_str(),
                "--p2p-port", p2pArg.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }
    int status = 0;
    bool reaped = false;
    auto exited = [&] { return reaped || (reaped = ::waitpid(pid, &status, WNOHANG) == pid); };
    // Never leave the node running when an assertion ends the test early.
    struct Reaper {
        pid_t pid;
        bool& reaped;
        ~Reaper() { if (!reaped) { ::kill(pid, SIGKILL); ::waitpid(pid, nullptr, 0); } }
    } reaper{pid, reaped};

    ASSERT_TRUE(waitFor([&] { return readAll(log).find("P2P network started") != std::string::npos || exited(); }, 30))
        << readAll(log);
    ASSERT_EQ(::kill(pid, SIGHUP), 0) << "the node exited early: " << readAll(log);
    EXPECT_TRUE(waitFor([&] { return readAll(log).find("SIGHUP received") != std::string::npos; }, 10)) << readAll(log);
    EXPECT_FALSE(exited()) << "SIGHUP stopped the node: " << readAll(log);

    ::kill(pid, SIGTERM);
    ASSERT_TRUE(waitFor(exited, 30)) << readAll(log);
    EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0) << readAll(log);
    EXPECT_NE(readAll(log).find("Node stopped cleanly"), std::string::npos) << readAll(log);
}

#endif
