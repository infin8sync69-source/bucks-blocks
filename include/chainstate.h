#ifndef BUCKS_CHAINSTATE_H
#define BUCKS_CHAINSTATE_H

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
class ChainState {
public:
    using Clock = std::function<int64_t()>;
    using DisconnectHook = std::function<void(const Block&)>;

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

    // Headers in a batch must link (the first entry's parent is known).
    ValidationState ProcessNewBlockHeaders(const std::vector<BlockHeader>& headers);

    // requested = the block was asked for (or mined locally).
    ValidationState ProcessNewBlock(const Block& block, bool requested);

    // Operator approval of one deep reorg (CS-9). Only the named tip is allowed;
    // chain selection runs at once.
    bool approveReorg(const Hash256& tipHash, std::string& err);

    // Called with every block removed from the active chain, after commit.
    void setDisconnectHook(DisconnectHook hook) { onDisconnect_ = std::move(hook); }

    const BlockIndex* tip() const { return chain_.tip(); }
    const BlockIndex* find(const Hash256& hash) const { return index_.find(hash); }
    const Chain& activeChain() const { return chain_; }
    const CoinsView& coins() const { return store_.coins(); }
    uint64_t tipSequence() const { return tipSequence_; }
    const ForkAlert& forkAlert() const { return alert_; }
    const ChainParams& params() const { return params_; }

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

    const ChainParams& params_;
    ChainStore& store_;
    Clock clock_;
    BlockIndexMap index_;
    Chain chain_;
    uint64_t sequence_ = 0;
    uint64_t tipSequence_ = 0;
    std::set<Hash256> skipped_;        // candidates parked by a fork alert
    std::set<Hash256> approved_;       // deep reorgs the operator allowed
    std::map<Hash256, ValidationState> rejects_;
    ForkAlert alert_;
    DisconnectHook onDisconnect_;
};

} // namespace Bucks

#endif // BUCKS_CHAINSTATE_H
