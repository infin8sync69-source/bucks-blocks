// The bucks executable itself: start-up checks and option parsing (audit OPS-06).
#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <string>

#ifndef _WIN32
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

#endif
