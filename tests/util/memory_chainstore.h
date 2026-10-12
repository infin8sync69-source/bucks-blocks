#ifndef BUCKS_TESTS_MEMORY_CHAINSTORE_H
#define BUCKS_TESTS_MEMORY_CHAINSTORE_H

#include <map>

#include "chainstore.h"

namespace Bucks {

// In-memory ChainStore for tests. commitStep is all-or-nothing by
// construction (the step is applied to copies first). failNextCommit makes
// the next commit fail without changing anything (CS-6).
class MemoryChainStore : public ChainStore {
public:
    bool putBlock(const Hash256& hash, const std::vector<uint8_t>& bytes) override {
        blocks_[hash] = bytes;
        return true;
    }
    bool getBlock(const Hash256& hash, std::vector<uint8_t>& bytes) const override {
        auto it = blocks_.find(hash);
        if (it == blocks_.end()) return false;
        bytes = it->second;
        return true;
    }
    bool getUndo(const Hash256& hash, BlockUndo& undo) const override {
        auto it = undos_.find(hash);
        if (it == undos_.end()) return false;
        undo = it->second;
        return true;
    }
    bool putIndexRow(const Hash256& hash, const BlockHeader& header, uint32_t status) override {
        rows_[hash] = {header, status};
        return true;
    }
    bool commitStep(const StepCommit& step) override {
        if (failNextCommit) {
            failNextCommit = false;
            return false;
        }
        MemoryCoinsView nextCoins = coins_;
        if (!nextCoins.apply(step.coins)) return false;
        coins_ = nextCoins;
        gold_ = step.gold;
        for (const auto& u : step.undos) undos_[u.first] = u.second;
        for (const auto& s : step.statuses) {
            auto it = rows_.find(s.first);
            if (it != rows_.end()) it->second.status = s.second;
        }
        ++commits;
        return true;
    }
    const CoinsView& coins() const override { return coins_; }
    GoldRules::GoldState gold() const override { return gold_; }

    // Read access for tests.
    uint32_t statusOf(const Hash256& hash) const { return rows_.at(hash).status; }
    MemoryCoinsView& mutableCoins() { return coins_; }

    bool failNextCommit = false;
    size_t commits = 0;

private:
    struct Row {
        BlockHeader header;
        uint32_t status;
    };
    std::map<Hash256, std::vector<uint8_t>> blocks_;
    std::map<Hash256, BlockUndo> undos_;
    std::map<Hash256, Row> rows_;
    MemoryCoinsView coins_;
    GoldRules::GoldState gold_;
};

} // namespace Bucks

#endif // BUCKS_TESTS_MEMORY_CHAINSTORE_H
