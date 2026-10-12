#include <gtest/gtest.h>

#include <filesystem>

#include "blockchain.h"
#include "chainparams.h"

namespace fs = std::filesystem;
using namespace Bucks;

class NetworkGuardTest : public ::testing::Test {
protected:
    std::string dir = (fs::temp_directory_path() / "bucks-test-network-guard").string();
    void SetUp() override {
        fs::remove_all(dir);
        fs::create_directory(dir);
    }
    void TearDown() override { fs::remove_all(dir); }
};

TEST_F(NetworkGuardTest, UnconfiguredMainnetRefusesToStart) {
    EXPECT_THROW(Blockchain(dir + "/u.db", dir + "/b.db", chainParams(NetworkId::MAIN)),
                 std::runtime_error);
}

TEST_F(NetworkGuardTest, DatadirOfAnotherNetworkIsRefused) {
    {
        Blockchain regtest(dir + "/u.db", dir + "/b.db", chainParams(NetworkId::REGTEST));
        EXPECT_EQ(regtest.height(), 0u);
        EXPECT_EQ(regtest.genesisHash(), chainParams(NetworkId::REGTEST).genesisHash);
    }
    // Reopening with the same network works; with another one it must not.
    EXPECT_NO_THROW(Blockchain(dir + "/u.db", dir + "/b.db", chainParams(NetworkId::REGTEST)));
    EXPECT_THROW(Blockchain(dir + "/u.db", dir + "/b.db", chainParams(NetworkId::TEST)),
                 std::runtime_error);
}
