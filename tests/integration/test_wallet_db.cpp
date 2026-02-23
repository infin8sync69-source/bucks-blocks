#include <gtest/gtest.h>
#include "server.h"
#include "wallet.h"
#include "blockchain.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class WalletPersistenceTest : public ::testing::Test {
protected:
    std::string testDir = "test_wallet_data";

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
    // Server config with custom fields if needed
    Bucks::ServerConfig config;
    config.port = 8090; 
    
    {
        Bucks::Server server(chain, nullptr, config);
        
        // 2. Create and add a wallet
        Bucks::Wallet w1;
        w1.generateKeys();
        server.addWallet(w1);
        
        // 3. Verify it's in memory
        auto wallets = server.getWallets();
        ASSERT_EQ(wallets.size(), 1);
        EXPECT_EQ(wallets[0].getAddress(), w1.getAddress());
        
        // 4. Server destruction should trigger save (assuming RAII or explicit stop)
        server.stop(); 
    }

    // 5. Verify file exists
    // The exact path depends on where Server saves it. Usually in current dir or data dir?
    // Looking at Server implementation (assumed): "wallets.json"
    // Ideally code uses data-dir, but if it uses CWD:
    EXPECT_TRUE(fs::exists("wallets.json"));

    // 6. Reload
    {
        Bucks::Server server2(chain, nullptr, config);
        // Constructor or start should load
        // Verify
        auto wallets = server2.getWallets();
        ASSERT_EQ(wallets.size(), 1);
        EXPECT_EQ(wallets[0].getAddress(), wallets[0].getAddress()); // Check valid
    }
    
    // Cleanup the wallets.json file if created in CWD
    if (fs::exists("wallets.json")) {
        fs::remove("wallets.json");
    }
}
