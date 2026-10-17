// Mining through the REST API: one race-free mining thread (audit SAN-2, NEW-6), "mine one
// block" as a background search (ML-4, NEW-5) and a live status (ML-3, ONB-14).
#include <gtest/gtest.h>
#include "server.h"
#include "node.h"
#include "consensus.h"
#include "pow.h"
#include "validation_rules.h"
#include "httplib.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace {

std::string uniqueTempDir(const std::string& tag) {
    static std::atomic<int> counter{0};
#ifdef _WIN32
    int pid = 0;
#else
    int pid = static_cast<int>(::getpid());
#endif
    fs::path base = fs::temp_directory_path() /
        ("bucks-" + tag + "-" + std::to_string(pid) + "-" + std::to_string(counter++));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base);
    return base.string();
}

class ServerMiningTest : public ::testing::Test {
protected:
    std::string dir;
    std::unique_ptr<Bucks::Node> chain;
    Bucks::ServerConfig cfg;
    const httplib::Headers tokenHeader{{"X-API-Token", "mining-token-0123456789abcdef0123456789abcdef"}};

    void SetUp() override {
#ifndef _WIN32
        ::unsetenv("BUCKS_API_TOKEN");
#endif
        dir = uniqueTempDir("mining");
        cfg.port = 0;
        cfg.enableLogging = false;
        cfg.enableRateLimit = false;
        cfg.dataDir = dir + "/data";
        cfg.webRoot = dir + "/web";
        cfg.apiToken = "mining-token-0123456789abcdef0123456789abcdef";
        cfg.mineThreads = 2;
    }

    void TearDown() override {
        chain.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    void openChain(const Bucks::ChainParams& params) {
        std::string err;
        int code = 0;
        chain = Bucks::Node::open(dir + "/chain", params, {}, err, code);
        ASSERT_TRUE(chain) << err;
    }

    json createWallet(httplib::Client& cli) {
        auto r = cli.Post("/api/wallets/create", tokenHeader, "{}", "application/json");
        EXPECT_TRUE(r);
        EXPECT_EQ(r->status, 200) << r->body;
        return json::parse(r->body);
    }

    static json status(httplib::Client& cli) {
        auto r = cli.Get("/api/mining/status");
        EXPECT_TRUE(r);
        if (!r || r->status != 200) return json::object();
        return json::parse(r->body);
    }

    // Polls the status until pred holds or the deadline passes; returns the last status.
    template <class Pred>
    static json waitStatus(httplib::Client& cli, Pred pred, std::chrono::seconds limit) {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        json s = status(cli);
        while (!pred(s) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(50ms);
            s = status(cli);
        }
        return s;
    }
};

} // namespace

// SAN-2/NEW-6: overlapping start, stop and "mine one block" calls, from the API and from the
// node itself, once made two threads join and replace the same std::thread (std::terminate)
// and could leave a start hanging behind a loop that never saw its stop.
TEST_F(ServerMiningTest, ConcurrentStartStopAndMineNeverAbort) {
    openChain(Bucks::chainParams(Bucks::NetworkId::REGTEST));
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client setup("127.0.0.1", server.port());
    createWallet(setup);

    std::atomic<int> failures{0};
    std::vector<std::thread> pool;
    for (int t = 0; t < 8; ++t) {
        pool.emplace_back([&, t] {
            httplib::Client cli("127.0.0.1", server.port());
            cli.set_read_timeout(60, 0);
            std::string err;
            for (int i = 0; i < 40; ++i) {
                httplib::Result r;
                switch ((t + i) % 5) {
                    case 0: server.startAutoMine(err); break;
                    case 1: server.stopAutoMine(); break;
                    case 2:
                        r = cli.Post("/api/mining/start", tokenHeader, "{}", "application/json");
                        if (!r || (r->status != 200 && r->status != 409)) ++failures;
                        break;
                    case 3:
                        r = cli.Post("/api/mining/stop", tokenHeader, "{}", "application/json");
                        if (!r || r->status != 200) ++failures;
                        break;
                    default:
                        r = cli.Post("/api/mining/mine", tokenHeader, "{}", "application/json");
                        if (!r || (r->status != 200 && r->status != 202 && r->status != 409)) ++failures;
                        break;
                }
                auto s = cli.Get("/api/mining/status");
                if (!s || s->status != 200) ++failures;
            }
        });
    }
    for (std::thread& t : pool) t.join();
    EXPECT_EQ(failures.load(), 0);

    server.stopAutoMine();
    json s = status(setup);
    EXPECT_FALSE(s["mining"].get<bool>());
    EXPECT_FALSE(s["busy"].get<bool>());

    // Still in order afterwards: a start mines, a stop stops.
    std::string err;
    ASSERT_TRUE(server.startAutoMine(err)) << err;
    const uint32_t h = chain->height();
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (chain->height() < h + 3 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(10ms);
    EXPECT_GE(chain->height(), h + 3);
    server.stopAutoMine();
    const uint32_t stoppedAt = chain->height();
    std::this_thread::sleep_for(200ms);
    EXPECT_EQ(chain->height(), stoppedAt);
    server.stop();
}

// NEW-6: a stop followed at once by a start used to clear the stop flag before the old loop
// had seen it, so the start waited for a loop that never ended.
TEST_F(ServerMiningTest, StopThenStartAnswersAtOnce) {
    openChain(Bucks::chainParams(Bucks::NetworkId::REGTEST));
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());
    cli.set_read_timeout(10, 0);
    createWallet(cli);
    for (int i = 0; i < 20; ++i) {
        const auto began = std::chrono::steady_clock::now();
        auto r = cli.Post("/api/mining/start", tokenHeader, "{}", "application/json");
        ASSERT_TRUE(r);
        ASSERT_EQ(r->status, 200) << r->body;
        r = cli.Post("/api/mining/stop", tokenHeader, "{}", "application/json");
        ASSERT_TRUE(r);
        ASSERT_EQ(r->status, 200);
        EXPECT_LT(std::chrono::steady_clock::now() - began, 5s) << "round " << i;
        EXPECT_FALSE(status(cli)["mining"].get<bool>());
    }
    server.stop();
}

// NEW-5: while continuous mining runs, "mine one block" is refused at once instead of
// waiting for a lock the auto-miner never gives up.
TEST_F(ServerMiningTest, MineOneBlockIsRefusedWhileMiningContinuously) {
    openChain(Bucks::chainParams(Bucks::NetworkId::REGTEST));
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());
    cli.set_read_timeout(10, 0);
    createWallet(cli);
    auto r = cli.Post("/api/mining/start", tokenHeader, "{}", "application/json");
    ASSERT_TRUE(r);
    ASSERT_EQ(r->status, 200);
    const auto began = std::chrono::steady_clock::now();
    r = cli.Post("/api/mining/mine", tokenHeader, "{}", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 409) << r->body;
    EXPECT_LT(std::chrono::steady_clock::now() - began, 2s);
    server.stop();
}

// ML-4: at a real difficulty "mine one block" answers 202 and keeps searching in the
// background with no time limit; the status shows the search and a stop ends it.
TEST_F(ServerMiningTest, MineOneBlockRunsInTheBackground) {
    openChain(Bucks::chainParams(Bucks::NetworkId::TEST));   // about 2^24 hashes per block
    cfg.mineThreads = 1;
    cfg.mineOneWait = 0ms;
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());
    cli.set_read_timeout(10, 0);
    const std::string address = createWallet(cli)["wallet"]["address"];

    auto r = cli.Post("/api/mining/mine", tokenHeader, "{}", "application/json");
    ASSERT_TRUE(r);
    ASSERT_EQ(r->status, 202) << r->body;
    json j = json::parse(r->body);
    EXPECT_TRUE(j["pending"].get<bool>());
    EXPECT_EQ(j["minerAddress"], address);
    ASSERT_TRUE(j["job"].is_object());
    const uint64_t id = j["job"]["id"].get<uint64_t>();

    json s = status(cli);
    ASSERT_TRUE(s["job"].is_object());
    EXPECT_EQ(s["job"]["id"].get<uint64_t>(), id);
    if (s["job"]["state"] == "running") {   // a block within milliseconds is possible, if unlikely
        EXPECT_TRUE(s["busy"].get<bool>());
        EXPECT_FALSE(s["mining"].get<bool>());
        r = cli.Post("/api/mining/mine", tokenHeader, "{}", "application/json");
        ASSERT_TRUE(r);
        EXPECT_EQ(r->status, 409) << r->body;
        r = cli.Post("/api/mining/start", tokenHeader, "{}", "application/json");
        ASSERT_TRUE(r);
        EXPECT_EQ(r->status, 409) << r->body;

        r = cli.Post("/api/mining/stop", tokenHeader, "{}", "application/json");
        ASSERT_TRUE(r);
        EXPECT_EQ(r->status, 200);
        s = status(cli);
        EXPECT_FALSE(s["busy"].get<bool>());
        EXPECT_EQ(s["hashRate"].get<uint64_t>(), 0u);
        EXPECT_TRUE(s["job"]["state"] == "stopped" || s["job"]["state"] == "found") << s.dump();
    }

    // A new search starts after the old one ended.
    r = cli.Post("/api/mining/mine", tokenHeader, "{}", "application/json");
    ASSERT_TRUE(r);
    EXPECT_TRUE(r->status == 202 || r->status == 200) << r->body;
    EXPECT_GT(status(cli)["job"]["id"].get<uint64_t>(), id);
    server.stop();   // ends the search
}

// ML-3: the hash rate is live while mining (it used to stay 0 until a block was found) and
// the status gives the expected time per block at the current difficulty (ONB-11).
TEST_F(ServerMiningTest, StatusReportsALiveHashRate) {
    openChain(Bucks::chainParams(Bucks::NetworkId::TEST));
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());
    cli.set_read_timeout(10, 0);
    createWallet(cli);

    json s = status(cli);
    EXPECT_FALSE(s["busy"].get<bool>());
    EXPECT_EQ(s["hashRate"].get<uint64_t>(), 0u);
    EXPECT_TRUE(s["expectedSecondsPerBlock"].is_null());
    EXPECT_EQ(s["threads"].get<unsigned>(), 2u);
    EXPECT_EQ(s["bits"].get<uint32_t>(), 0x1e00ffffu);
    const double perBlock = s["expectedHashesPerBlock"].get<double>();   // 2^256 / (target + 1)
    EXPECT_GT(perBlock, 16'777'216.0);
    EXPECT_LT(perBlock, 16'777'216.0 * 1.001);

    auto r = cli.Post("/api/mining/start", tokenHeader, "{}", "application/json");
    ASSERT_TRUE(r);
    ASSERT_EQ(r->status, 200) << r->body;
    s = waitStatus(cli, [](const json& x) { return x.value("hashRate", uint64_t(0)) > 0; }, 20s);
    EXPECT_GT(s["hashRate"].get<uint64_t>(), 0u) << s.dump();
    EXPECT_TRUE(s["mining"].get<bool>());
    EXPECT_TRUE(s["busy"].get<bool>());
    ASSERT_TRUE(s["expectedSecondsPerBlock"].is_number());
    EXPECT_NEAR(s["expectedSecondsPerBlock"].get<double>(),
                s["expectedHashesPerBlock"].get<double>() / s["hashRate"].get<double>(), 1.0);

    r = cli.Post("/api/mining/stop", tokenHeader, "{}", "application/json");
    ASSERT_TRUE(r);
    s = status(cli);
    EXPECT_FALSE(s["mining"].get<bool>());
    EXPECT_EQ(s["hashRate"].get<uint64_t>(), 0u);
    server.stop();
}

// ONB-14: "syncing" in /api/blockchain/info is the time-based initial-download flag; whether
// the node can mine is miningBlockedReason, which the mining status now reports.
TEST_F(ServerMiningTest, StatusSaysWhyMiningIsBlocked) {
    openChain(Bucks::chainParams(Bucks::NetworkId::TEST));   // genesis is days old: "syncing"
    {
        Bucks::Server server(*chain, nullptr, cfg);
        ASSERT_TRUE(server.start());
        httplib::Client cli("127.0.0.1", server.port());
        auto info = cli.Get("/api/blockchain/info");
        ASSERT_TRUE(info);
        EXPECT_TRUE(json::parse(info->body)["syncing"].get<bool>());
        EXPECT_EQ(status(cli)["blockedReason"], "");   // but it can mine
    }
    chain.reset();
    fs::remove_all(dir + "/chain");
    fs::remove_all(dir + "/data");

    // A header with more work whose block has not arrived: mining waits for it.
    Bucks::ChainParams p = Bucks::chainParams(Bucks::NetworkId::REGTEST);
    p.id = Bucks::NetworkId::TEST;   // the rule applies to every network but regtest
    openChain(p);
    Bucks::BlockHeader h;
    h.version = Bucks::Consensus::BLOCK_VERSION;
    h.height = 1;
    h.prevBlockHash = p.genesisHash;
    h.merkleRoot[0] = 1;
    h.timestamp = p.genesisTime + 600;
    h.bits = p.powLimitBits;
    while (!Bucks::Pow::checkProofOfWork(h.hash(), h.bits, p.powLimit)) ++h.nonce;
    ASSERT_TRUE(chain->submitHeaders({h}).valid());

    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());
    EXPECT_EQ(status(cli)["blockedReason"], "node is syncing");
    server.stop();
}
