#include <gtest/gtest.h>
#include "server.h"
#include "wallet.h"
#include "blockchain.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class WalletPersistenceTest : public ::testing::Test {
protected:
    // Temp dir, never the CWD (the owner's wallets.json may live there).
    std::string testDir = (fs::temp_directory_path() / "bucks-test-wallet-db").string();

    void SetUp() override {
        if (fs::exists(testDir)) {
            fs::remove_all(testDir);
        }
        fs::create_directory(testDir);
    }

    void TearDown() override {
        // Cleanup handled by filesystem, but proper shutdown needed if server running
        if (fs::exists(testDir)) {
            fs::remove_all(testDir);
        }
    }
};

TEST_F(WalletPersistenceTest, SaveAndLoadWallets) {
    // 1. Setup minimal dependencies
    Bucks::Blockchain chain(testDir + "/utxo.db", testDir + "/blocks.db");
    // Server config: wallets live in the data directory, not the CWD
    Bucks::ServerConfig config;
    config.port = 0;
    config.enableLogging = false;
    config.dataDir = testDir + "/data";
    config.legacyWalletFile.clear();
    config.apiToken = "wallet-db-test-token";
    const std::string walletFile = config.dataDir + "/wallets.json";
    std::string address;

    {
        Bucks::Server server(chain, nullptr, config);
        EXPECT_EQ(server.walletFile(), walletFile);

        // 2. Create and add a wallet (saved immediately, atomically)
        Bucks::Wallet w1;
        w1.generateKeys();
        address = w1.getAddress();
        EXPECT_TRUE(server.addWallet(w1));

        // 3. Verify it's in memory
        auto wallets = server.getWallets();
        ASSERT_EQ(wallets.size(), 1);
        EXPECT_EQ(wallets[0].getAddress(), w1.getAddress());

        server.stop();
    }

    // 5. Verify file exists in the data directory
    EXPECT_TRUE(fs::exists(walletFile));

    // 6. Reload
    {
        Bucks::Server server2(chain, nullptr, config);
        // Constructor loads
        auto wallets = server2.getWallets();
        ASSERT_EQ(wallets.size(), 1);
        EXPECT_EQ(wallets[0].getAddress(), address);
    }
}
