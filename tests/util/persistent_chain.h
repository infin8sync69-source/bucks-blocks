#ifndef BUCKS_TESTS_PERSISTENT_CHAIN_H
#define BUCKS_TESTS_PERSISTENT_CHAIN_H

#include <filesystem>
#include <memory>
#include <string>

#include <sys/stat.h>

#include "chaindb.h"
#include "chainstate.h"
#include "test_chain.h"

namespace Bucks {
namespace TestUtil {

// A chain on a SQLite store that can be closed and opened again, like a node
// restart. The datadir lives under the system temp directory and is removed
// with the object.
class PersistentChain {
public:
    explicit PersistentChain(const ChainParams& params = chainParams(NetworkId::REGTEST))
        : params_(params) {
        static int counter = 0;
        dir_ = (std::filesystem::temp_directory_path() /
                ("bucks-test-chain-" + std::to_string(getpid()) + "-" + std::to_string(counter++))).string();
        std::filesystem::remove_all(dir_);
        now_ = int64_t(params_.genesisTime) + 10'000'000;
    }
    ~PersistentChain() {
        close(true);
        std::filesystem::remove_all(dir_);
    }

    // Opens (or creates) the database and runs ChainState::load.
    LoadResult open(const LoadOptions& options = {}) {
        close(false);
        std::string err;
        int code = 0;
        store_ = SqliteChainStore::open(dir_, params_, err, code);
        if (!store_) {
            LoadResult r;
            r.exitCode = code;
            r.error = err;
            return r;
        }
        state_ = std::make_unique<ChainState>(params_, *store_, [this] { return now_; });
        return state_->load(options);
    }

    // Orderly shutdown (clean flag) or a simulated crash (clean = false).
    void close(bool clean) {
        if (state_ && clean) state_->shutdown();
        state_.reset();
        store_.reset();
    }

    ChainState& state() { return *state_; }
    SqliteChainStore& store() { return *store_; }
    const ChainParams& params() const { return params_; }
    const std::string& dir() const { return dir_; }
    std::string dbPath() const { return dir_ + "/chain.sqlite"; }
    int64_t& now() { return now_; }
    bool isOpen() const { return state_ != nullptr; }

    Block build(const BlockIndex* parent, const std::vector<Transaction>& extra = {}, uint8_t tag = 0,
                uint32_t spacing = 600) const {
        return buildTestBlock(params_, parent, extra, tag, spacing);
    }
    ValidationState submit(const Block& block) { return state_->ProcessNewBlock(block, true); }
    Block extend(const std::vector<Transaction>& extra = {}, uint8_t tag = 0) {
        Block b = build(state_->tip(), extra, tag);
        submit(b);
        return b;
    }

private:
    ChainParams params_;
    std::string dir_;
    int64_t now_ = 0;
    std::unique_ptr<SqliteChainStore> store_;
    std::unique_ptr<ChainState> state_;
};

} // namespace TestUtil
} // namespace Bucks

#endif
