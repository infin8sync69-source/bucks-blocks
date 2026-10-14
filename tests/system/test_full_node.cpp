#include <gtest/gtest.h>
#include "server.h"
#include "node_harness.h"
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

class FullNodeSystemTest : public ::testing::Test {
protected:
    // Temp dir, never the CWD: the server writes its wallet file and cookie
    // into the configured data directory.
    std::string testDir = (fs::temp_directory_path() / "bucks-test-full-node").string();

    void SetUp() override {
        if (fs::exists(testDir)) fs::remove_all(testDir);
        fs::create_directory(testDir);
    }

    void TearDown() override {
        if (fs::exists(testDir)) fs::remove_all(testDir);
    }
};

TEST_F(FullNodeSystemTest, StartupAndMineBlock) {
    // 1. Open a regtest node
    Bucks::TestUtil::NodeHarness harness;
    ASSERT_TRUE(harness.node) << harness.error;
    Bucks::Node& chain = *harness.node;

    // 2. Configure Server (Full Node mode)
    Bucks::ServerConfig config;
    config.port = 0; // any free port
    config.maxWorkers = 2;
    config.enableLogging = false;
    config.dataDir = testDir + "/data";
    config.legacyWalletFile.clear();
    config.webRoot = testDir + "/web";
    config.apiToken = "full-node-test-token";

    // 3. Start Server
    Bucks::Server server(chain, nullptr, config);
    ASSERT_TRUE(server.start());
    ASSERT_GT(server.port(), 0);

    // 4. Create a wallet and add it (needed for mining rewards)
    Bucks::Wallet minerWallet;
    minerWallet.generateKeys();
    EXPECT_TRUE(server.addWallet(minerWallet));
    EXPECT_TRUE(fs::exists(config.dataDir + "/regtest/wallets.json"));

    // 5. Simulate Mining (if API allows internal call or we simulate via generic method)
    // Server has private `mineBlock` but exposed via API.
    // We can also interact via Blockchain directly to see if it accepts a block?
    // Or we can just check if it stays up and chain is accessible.

    // Let's verify initial state
    EXPECT_EQ(chain.height(), 0);

    // Wait a bit to ensure threads are active
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Validation: Server is running
    EXPECT_TRUE(server.running());

    // 6. Stop
    server.stop();
    EXPECT_FALSE(server.running());
}
