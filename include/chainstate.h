#ifndef BUCKS_CHAINSTATE_H
#define BUCKS_CHAINSTATE_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "block.h"
#include "chain.h"
#include "chainparams.h"
#include "chainstore.h"
#include "validation_rules.h"

namespace Bucks {

// Block index, chain selection and atomic reorganisation (consensus spec
// sections 8.2 ... 8.5). Single-threaded for now: the chain lock and the
// thread model arrive with plan step 16.
struct LoadOptions {
    uint32_t checkBlocks = 24;        // VerifyDB depth; 0 = the whole chain. Raised to 288 after an unclean shutdown.
    int checkTier = 0;                // 0 = automatic (1 after a clean shutdown, 2 after a crash), 1..3 as in storage spec 9.2
    bool reindexChainstate = false;   // drop coins, undo and gold state; reconnect every stored block
    bool reindex = false;             // rebuild the block index from stored block bytes first
};

struct LoadResult {
    bool ok = false;
    int exitCode = 0;                 // storage spec section 4: 3 params/genesis, 4 storage, 6 corrupt
    std::string error;
};

class ChainState {
public:
    using Clock = std::function<int64_t()>;
    using DisconnectHook = std::function<void(const Block&)>;
    // Called once per committed step, after the tip changed: the blocks that
    // left the active chain (oldest first) and the blocks that joined it (in
    // order). The mempool updates itself from this (spec section 9.6).
    using StepHook = std::function<void(const std::vector<Block>& disconnected,
                                        const std::vector<Block>& connected)>;

    struct ForkAlert {
        bool active = false;
        uint32_t forkHeight = 0;
        Hash256 ourTip{};
        Hash256 otherTip{};
        U256 workDifference;
    };

    ChainState(const ChainParams& params, ChainStore& store, Clock clock = {});

    // Creates the genesis entry and its stored rows. The store must be empty.
    bool init(std::string& err);

    // Startup (consensus section 8.7): checks the parameters and the genesis,
    // initialises an empty store, validates the stored index, the tip and the
    // recent blocks, checks the coin statistics, then connects any stored
    // block that has more work than the tip. Nothing is repaired silently.
    LoadResult load(const LoadOptions& options = {});

    // Orderly shutdown: marks the store clean so the next start runs the light checks.
    void shutdown();

    // Headers in a batch must link (the first entry's parent is known).
    ValidationState ProcessNewBlockHeaders(const std::vector<BlockHeader>& headers);

    // requested = the block was asked for (or mined locally).
    ValidationState ProcessNewBlock(const Block& block, bool requested);

    // Operator approval of one deep reorg (CS-9). Only the named tip is allowed;
    // chain selection runs at once.
    bool approveReorg(const Hash256& tipHash, std::string& err);

    // Called with every block removed from the active chain, after commit.
    void setDisconnectHook(DisconnectHook hook) { onDisconnect_ = std::move(hook); }
    void setStepHook(StepHook hook) { onStep_ = std::move(hook); }

    const BlockIndex* tip() const { return chain_.tip(); }
    const BlockIndex* find(const Hash256& hash) const { return index_.find(hash); }
    const Chain& activeChain() const { return chain_; }
    const CoinsView& coins() const { return store_.coins(); }
    GoldRules::GoldState gold() const { return store_.gold(); }
    uint64_t tipSequence() const { return tipSequence_.load(std::memory_order_acquire); }
    const ForkAlert& forkAlert() const { return alert_; }
    void raiseForkAlert(const ForkAlert& a, uint64_t depth);   // logs once per competing tip

    // Section 8.5, "when the miner may work". Empty when mining is allowed,
    // otherwise the reason ("node is syncing"). Regtest has no restriction.
    // A branch parked by a fork alert does not block mining.
    std::string miningBlockedReason() const;
    const ChainParams& params() const { return params_; }

    // Newest-first hashes of the active chain for a getheaders request (spec 8.9).
    std::vector<Hash256> locator() const;
    // Up to `max` headers that follow the first locator hash on the active chain,
    // stopping after `stop` (all zero = no stop). Copies.
    std::vector<BlockHeader> headersAfter(const std::vector<Hash256>& locator, const Hash256& stop,
                                          size_t max) const;

    // Hashes of blocks on the best header chain whose data we do not have yet
    // (closest to the tip first, at most `max`). Used to decide what to download.
    std::vector<Hash256> missingBlocks(size_t max) const;

    // The reject state recorded for a block that failed validation.
    bool rejectState(const Hash256& hash, ValidationState& out) const;

private:
    // Stage A-B for a header; stores the entry on success. Returns the entry
    // for VALID and DUPLICATE, nullptr otherwise (st says why).
    BlockIndex* acceptHeader(const BlockHeader& header, ValidationState& st);
    ValidationState acceptBlock(const Block& block, bool requested, BlockIndex*& entry);

    void markFailed(BlockIndex* e, const ValidationState& reason);
    void propagateHaveChainData(BlockIndex* e);
    BlockIndex* pickCandidate();
    bool activateBestChain(std::string& err);
    bool loadBlock(const BlockIndex* e, Block& out) const;
    bool loadUndo(const BlockIndex* e, BlockUndo& out) const;
    void persistStatus(const BlockIndex* e);
    bool loadIndexFromStore(const StoredState& state, int tier, std::string& err);
    bool verifyDb(uint32_t depth, std::string& err);
    bool reindexFromBlocks(std::string& err);

    const ChainParams& params_;
    ChainStore& store_;
    Clock clock_;
    BlockIndexMap index_;
    Chain chain_;
    uint64_t sequence_ = 0;
    std::atomic<uint64_t> tipSequence_{0};
    std::set<Hash256> skipped_;        // candidates parked by a fork alert
    std::set<Hash256> approved_;       // deep reorgs the operator allowed
    std::map<Hash256, ValidationState> rejects_;
    ForkAlert alert_;
    bool storeFailed_ = false;         // a write to the store failed: the chain state is not trustworthy
    DisconnectHook onDisconnect_;
    StepHook onStep_;
};

} // namespace Bucks

#endif // BUCKS_CHAINSTATE_H
