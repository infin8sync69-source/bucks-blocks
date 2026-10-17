#include "chainstate.h"
#include "log.h"

#include <algorithm>
#include <ctime>
#include <iostream>

#include "chaindb.h"
#include "connect_block.h"
#include "genesis.h"

namespace Bucks {

namespace {

constexpr size_t MAX_HEADERS_PER_MESSAGE = 2000;
constexpr int64_t MAX_UNREQUESTED_AHEAD = 288;
constexpr size_t MAX_REJECT_STATES = 1000;
constexpr uint32_t MAX_STEP_CONNECTS = 32;

std::string describeHash(const Hash256& h) {
    return hexStr(std::vector<uint8_t>(h.begin(), h.end()));
}

ValidationState internalError(const std::string& code, const std::string& detail) {
    ValidationState st;
    st.result = ValidationResult::INTERNAL;
    st.code = code;
    st.detail = detail;
    return st;
}

} // namespace

ChainState::ChainState(const ChainParams& params, ChainStore& store, Clock clock)
    : params_(params), store_(store), clock_(clock ? std::move(clock) : Clock([] { return int64_t(std::time(nullptr)); })) {}

bool ChainState::init(std::string& err) {
    if (!Genesis::check(params_, err)) return false;
    const Block g = Genesis::build(params_);
    BlockIndex* e = index_.insert(g.header, nullptr);
    e->status = HEADER_VALID | HAVE_DATA | DATA_CHECKED | CONNECTED_VALID | IN_MAIN_CHAIN;
    e->txCount = uint32_t(g.transactions.size());
    e->haveChainData = true;
    e->sequenceId = 0;

    if (!store_.putIndexRow(e->hash, e->header, e->status) || !store_.putBlock(e->hash, g.serialize())) {
        err = "cannot store genesis";
        return false;
    }
    // Genesis creates no coins; it only sets the best block marker.
    CoinsViewCache view(&store_.coins());
    view.setBestBlock(e->hash);
    StepCommit commit;
    commit.expectedOldBest = store_.coins().bestBlock();
    commit.coins = view.changes();
    commit.gold = store_.gold();
    if (!store_.commitStep(commit)) {
        err = "cannot commit genesis state";
        return false;
    }
    chain_.setTip(e);
    return true;
}

std::vector<Hash256> ChainState::locator() const {
    return chain_.locator();
}

std::vector<BlockHeader> ChainState::headersAfter(const std::vector<Hash256>& locator, const Hash256& stop,
                                                  size_t max) const {
    std::vector<BlockHeader> out;
    const BlockIndex* fork = findForkByLocator(index_, chain_, locator);
    if (!fork) return out;
    for (const BlockIndex* e = chain_.next(fork); e && out.size() < max; e = chain_.next(e)) {
        out.push_back(e->header);
        if (e->hash == stop) break;
    }
    return out;
}

std::vector<Hash256> ChainState::missingBlocks(const Hash256& towards, size_t max) const {
    // The branch to a block a peer announced, unless it is failed or under a fork alert; its
    // ancestors without data, oldest first. Asking only the announcing peer means a header whose
    // block nobody has does not get honest peers dropped as stalling (audit CON-1).
    std::vector<Hash256> out;
    const BlockIndex* best = index_.find(towards);
    if (!best || best->failed() || skipped_.count(best->hash)) return out;
    if (!(best->chainWork > chain_.tip()->chainWork)) return out;
    std::vector<const BlockIndex*> path;
    for (const BlockIndex* e = best; e && !chain_.contains(e); e = e->prev) path.push_back(e);
    for (auto it = path.rbegin(); it != path.rend() && out.size() < max; ++it) {
        if (!((*it)->status & HAVE_DATA)) out.push_back((*it)->hash);
    }
    return out;
}

bool ChainState::hasMoreWork(const Hash256& a, const Hash256& b) const {
    const BlockIndex* ea = index_.find(a);
    if (!ea) return false;
    const BlockIndex* eb = index_.find(b);
    return !eb || ea->chainWork > eb->chainWork;
}

std::string ChainState::miningBlockedReason() const {
    if (fatalExitCode_) return "the chain state has halted: " + fatalError_;
    if (params_.id == NetworkId::REGTEST) return {};
    const BlockIndex* tip = chain_.tip();
    if (!tip) return "no chain tip";
    if (tip->chainWork < params_.minimumChainWork) return "node is syncing";

    // Initial block download: the tip is more than a day old and still moving. Only connected blocks move it, so
    // nothing a peer sends without the blocks below it counts; and a tip that stopped moving never blocks, so the
    // launch from an old genesis and a restart after a day without blocks can mine (section 8.5).
    constexpr int64_t kOldTipSeconds = 86'400;
    constexpr int64_t kSyncProgressSeconds = 600;
    const int64_t now = clock_();
    if (int64_t(tip->header.timestamp) < now - kOldTipSeconds && lastTipMove_ != 0 &&
        now - lastTipMove_ <= kSyncProgressSeconds)
        return "node is syncing";

    // A block that can connect now (its data and all its ancestors' are stored) is waiting. A header, or a body
    // whose parent never arrives, does not count: one of each used to stop mining for good (audit CON-1).
    std::vector<const BlockIndex*> parked;
    for (const Hash256& h : skipped_) {
        if (const BlockIndex* e = index_.find(h)) parked.push_back(e);
    }
    bool waiting = false;
    index_.forEach([&](const BlockIndex& e) {
        if (waiting || !e.haveChainData || e.failed() || !(e.chainWork > tip->chainWork)) return;
        for (const BlockIndex* s : parked) {
            const BlockIndex* lca = lastCommonAncestor(&e, s);
            if (lca == &e || lca == s) return;   // on (or beyond) a branch under fork alert
        }
        waiting = true;
    });
    return waiting ? "node is syncing" : std::string();
}

// ---------------------------------------------------------------- headers

BlockIndex* ChainState::acceptHeader(const BlockHeader& header, ValidationState& st) {
    const Hash256 hash = header.hash();
    if (BlockIndex* known = index_.find(hash)) {
        if (known->failed()) {
            st.invalid(ValidationResult::INVALID, "duplicate-invalid");
            return nullptr;
        }
        st.result = ValidationResult::DUPLICATE;
        st.code = "duplicate";
        return known;
    }
    // Stage A, then stage B. Nothing is stored unless both pass.
    if (!Rules::CheckBlockHeader(header, params_, st)) return nullptr;
    BlockIndex* parent = index_.find(header.prevBlockHash);
    if (!Rules::ContextualCheckBlockHeader(header, parent, clock_(), params_, st, &index_))
        return nullptr;

    BlockIndex* e = index_.insert(header, parent);
    e->status = HEADER_VALID;
    persistStatus(e);
    return e;
}

ValidationState ChainState::ProcessNewBlockHeaders(const std::vector<BlockHeader>& headers) {
    ValidationState st;
    if (fatalExitCode_) return internalError("chainstate-halted", fatalError_);
    if (headers.size() > MAX_HEADERS_PER_MESSAGE) {
        st.invalid(ValidationResult::MALFORMED, "too-many-headers");
        return st;
    }
    for (size_t i = 1; i < headers.size(); ++i) {
        if (headers[i].prevBlockHash != headers[i - 1].hash()) {
            st.invalid(ValidationResult::MALFORMED, "headers-do-not-connect");
            return st;
        }
    }
    for (const BlockHeader& h : headers) {
        ValidationState hs;
        if (!acceptHeader(h, hs) && hs.result != ValidationResult::DUPLICATE) return hs;
        if (fatalExitCode_) return internalError("store-write-failed", fatalError_);
    }
    return st;  // VALID
}

// ---------------------------------------------------------------- blocks

ValidationState ChainState::acceptBlock(const Block& block, bool requested, BlockIndex*& entry) {
    entry = nullptr;
    ValidationState st;
    BlockIndex* e = acceptHeader(block.header, st);
    if (!e) return st;
    entry = e;
    if (e->status & HAVE_DATA) {
        st.result = ValidationResult::DUPLICATE;
        st.code = "duplicate";
        return st;
    }
    const BlockIndex* tip = chain_.tip();
    if (!requested) {
        if (!(e->chainWork > tip->chainWork)) {
            st.result = ValidationResult::DUPLICATE;
            st.code = "unrequested-low-work";
            return st;
        }
        if (int64_t(e->height) > int64_t(tip->height) + MAX_UNREQUESTED_AHEAD) {
            st.result = ValidationResult::DUPLICATE;
            st.code = "unrequested-too-far";
            return st;
        }
    }

    // Stage C. MUTATED and MALFORMED never mark the block (the header may be fine).
    std::vector<Hash256> txids;
    ValidationState cst;
    if (!Rules::CheckBlock(block, params_, cst, &txids)) {
        if (cst.result == ValidationResult::INVALID) markFailed(e, cst);
        return cst;
    }
    // Stage D.
    ValidationState dst;
    if (!Rules::ContextualCheckBlock(block, e->height, e->prev->medianTimePast(), dst)) {
        markFailed(e, dst);
        return dst;
    }

    if (!store_.putBlock(e->hash, block.serialize())) {
        fail(EXIT_STORAGE_FAILURE, "cannot store the data of block " + describeHash(e->hash));
        return internalError("store-block-failed", fatalError_);
    }
    e->status |= HAVE_DATA | DATA_CHECKED;
    e->txCount = uint32_t(block.transactions.size());
    e->sequenceId = ++sequence_;
    persistStatus(e);
    propagateHaveChainData(e);
    return ValidationState{};
}

ValidationState ChainState::ProcessNewBlock(const Block& block, bool requested) {
    if (fatalExitCode_) return internalError("chainstate-halted", fatalError_);
    BlockIndex* e = nullptr;
    ValidationState st = acceptBlock(block, requested, e);
    if (fatalExitCode_) return internalError("store-write-failed", fatalError_);
    if (st.result != ValidationResult::VALID) return st;

    std::string err;
    if (!activateBestChain(err)) {
        fail(EXIT_DATABASE_CORRUPT, err);
        return internalError("fatal-chainstate", fatalError_);
    }

    if (e->failed()) {
        ValidationState reject;
        if (rejectState(e->hash, reject)) return reject;
        // A descendant of an invalid block: the reject state belongs to the ancestor.
        ValidationState child;
        child.invalid(ValidationResult::INVALID, "bad-prevblk");
        return child;
    }
    return ValidationState{};
}

void ChainState::raiseForkAlert(const ForkAlert& a, uint64_t depth) {
    const bool isNew = !alert_.active || alert_.otherTip != a.otherTip;
    alert_ = a;
    if (!isNew) return;
    logErr() << "⚠ FORK ALERT: a branch with more work forks " << depth << " blocks below our tip (at height "
             << a.forkHeight << "), deeper than the automatic reorg limit of " << params_.maxAutoReorgDepth
             << ". This node stays on its chain. After checking with other operators which chain is right, "
                "approve it with POST /api/node/approve-reorg {\"tipHash\": \"<hex>\"}.\n";
}

bool ChainState::approveReorg(const Hash256& tipHash, std::string& err) {
    if (fatalExitCode_) {
        err = "the chain state has halted: " + fatalError_;
        return false;
    }
    // Only a branch the node actually parked behind a fork alert can be approved (audit WAL-10).
    if (!skipped_.count(tipHash) && !(alert_.active && alert_.otherTip == tipHash)) {
        err = "no parked branch with that tip";
        return false;
    }
    const BlockIndex* approved = index_.find(tipHash);
    if (!approved) {
        err = "no parked branch with that tip";
        return false;
    }
    approved_.insert(tipHash);
    // Unpark the tip and whatever was parked above it since: they are the same branch.
    for (auto it = skipped_.begin(); it != skipped_.end();) {
        const BlockIndex* s = index_.find(*it);
        if (s && s->height >= approved->height && s->getAncestor(approved->height) == approved)
            it = skipped_.erase(it);
        else
            ++it;
    }
    if (!activateBestChain(err)) {
        fail(EXIT_DATABASE_CORRUPT, err);
        err = fatalError_;
        return false;
    }
    // Report what happened, not what was asked (audit CON-7).
    if (!chain_.contains(approved)) {
        if (approved->failed()) {
            if (alert_.active && alert_.otherTip == tipHash) alert_ = ForkAlert{};   // nothing left to decide
            err = "the approved branch is invalid; this node stays on its chain";
        } else {
            err = "the approved branch did not become the active chain (no more work than the tip?)";
        }
        return false;
    }
    return true;
}

// The operator's approval covers the reorg that brings the approved block into the active chain, also after its
// branch has grown. Once it is in, a later deep reorg needs a new approval.
bool ChainState::approvedReorgTo(const BlockIndex* best) const {
    for (const Hash256& h : approved_) {
        const BlockIndex* a = index_.find(h);
        if (a && !chain_.contains(a) && best->height >= a->height && best->getAncestor(a->height) == a) return true;
    }
    return false;
}

bool ChainState::rejectState(const Hash256& hash, ValidationState& out) const {
    auto it = rejects_.find(hash);
    if (it == rejects_.end()) return false;
    out = it->second;
    return true;
}

// ---------------------------------------------------------------- status

void ChainState::persistStatus(const BlockIndex* e) {
    if (!store_.putIndexRow(e->hash, e->header, e->status))
        fail(EXIT_STORAGE_FAILURE, "cannot write the index row of block " + describeHash(e->hash));
}

// Continuing after a failed write or unreadable stored data would serve a chain that can no longer move, or a
// wrong one (audit CON-4). The first reason wins; the node polls fatalExitCode() and stops.
void ChainState::fail(int exitCode, const std::string& why) {
    if (fatalExitCode_) return;
    fatalExitCode_ = exitCode;
    fatalError_ = why;
    logErr() << "FATAL: chain state halted: " << why << "\n";
}

void ChainState::markFailed(BlockIndex* e, const ValidationState& reason) {
    e->status |= FAILED_VALID;
    persistStatus(e);
    if (rejects_.size() >= MAX_REJECT_STATES) rejects_.erase(rejects_.begin());
    rejects_[e->hash] = reason;
    index_.forEach([&](BlockIndex& d) {
        if (&d != e && d.height > e->height && d.getAncestor(e->height) == e) {
            d.status |= FAILED_CHILD;
            persistStatus(&d);
        }
    });
}

void ChainState::propagateHaveChainData(BlockIndex* e) {
    if (e->prev && !e->prev->haveChainData) return;
    e->haveChainData = true;
    // Descendants that already have their data become usable when every
    // block between them and e has data.
    index_.forEach([&](BlockIndex& d) {
        if (&d == e || d.height <= e->height || !(d.status & HAVE_DATA) || d.haveChainData) return;
        if (d.getAncestor(e->height) != e) return;
        for (const BlockIndex* p = &d; p != e; p = p->prev) {
            if (!(p->status & HAVE_DATA)) return;
        }
        d.haveChainData = true;
    });
}

// ---------------------------------------------------------------- selection

BlockIndex* ChainState::pickCandidate() {
    const BlockIndex* tip = chain_.tip();
    BlockIndex* best = nullptr;
    index_.forEach([&](BlockIndex& d) {
        if (!d.haveChainData || d.failed() || skipped_.count(d.hash)) return;
        if (!(d.chainWork > tip->chainWork)) return;
        if (!best || d.chainWork > best->chainWork ||
            (d.chainWork == best->chainWork && d.sequenceId < best->sequenceId))
            best = &d;
    });
    return best;
}

bool ChainState::loadBlock(const BlockIndex* e, Block& out) const {
    std::vector<uint8_t> bytes;
    if (!store_.getBlock(e->hash, bytes)) return false;
    try {
        out = Block::deserialize(bytes);
    } catch (const std::exception&) {
        return false;
    }
    return out.getHash() == e->hash;
}

bool ChainState::loadUndo(const BlockIndex* e, BlockUndo& out) const {
    return store_.getUndo(e->hash, out);
}

bool ChainState::activateBestChain(std::string& err) {
    while (true) {
        BlockIndex* best = pickCandidate();
        if (!best) return true;

        BlockIndex* tip = const_cast<BlockIndex*>(chain_.tip());
        BlockIndex* fork = chain_.findFork(best);
        if (!fork) {
            err = "no common ancestor with the active chain";
            return false;
        }

        const uint64_t depth = uint64_t(tip->height - fork->height);
        const bool approved = approvedReorgTo(best);
        if (depth > params_.maxAutoReorgDepth && !approved) {
            raiseForkAlert(ForkAlert{true, fork->height, tip->hash, best->hash, best->chainWork - tip->chainWork}, depth);
            skipped_.insert(best->hash);
            continue;
        }

        // Path from the fork up to the best candidate, oldest first.
        std::vector<BlockIndex*> path;
        for (BlockIndex* b = best; b != fork; b = b->prev) path.push_back(b);
        std::reverse(path.begin(), path.end());

        // One step: a reorg stops at the first block that overtakes the tip;
        // an extension is capped at MAX_STEP_CONNECTS blocks.
        size_t stepLast = 0;
        if (fork != tip) {
            bool found = false;
            for (size_t i = 0; i < path.size(); ++i) {
                if (path[i]->chainWork > tip->chainWork) {
                    stepLast = i;
                    found = true;
                    break;
                }
            }
            if (!found) stepLast = path.size() - 1;
            // A long way to overtake the tip is a deep reorg too, unless the operator approved it.
            if (stepLast > 2 * params_.maxAutoReorgDepth + 32 && !approved) {
                raiseForkAlert(ForkAlert{true, fork->height, tip->hash, best->hash, best->chainWork - tip->chainWork}, depth);
                skipped_.insert(best->hash);
                continue;
            }
        } else {
            stepLast = std::min<size_t>(MAX_STEP_CONNECTS, path.size()) - 1;
        }
        BlockIndex* stepEnd = path[stepLast];

        // Work on a copy of the committed state; nothing is written until COMMIT.
        CoinsViewCache view(&store_.coins());
        GoldRules::GoldState gold = store_.gold();
        StepCommit commit;
        commit.expectedOldBest = store_.coins().bestBlock();

        std::vector<std::pair<BlockIndex*, Block>> disconnected;  // oldest first
        for (BlockIndex* b = tip; b != fork; b = b->prev) {
            Block block;
            BlockUndo undo;
            if (!loadBlock(b, block) || !loadUndo(b, undo)) {
                err = "cannot read block or undo data";
                return false;
            }
            std::vector<Hash256> ids;
            for (const Transaction& t : block.transactions) ids.push_back(t.txid());
            if (DisconnectBlock(block, ids, *b, view, gold, undo) != DisconnectResult::OK) {
                err = "undo data does not match block";
                return false;
            }
            disconnected.insert(disconnected.begin(), {b, block});
        }

        bool stepFailed = false;
        std::vector<BlockIndex*> connected;
        std::vector<Block> connectedBlocks;
        for (size_t i = 0; i <= stepLast; ++i) {
            BlockIndex* b = path[i];
            Block block;
            if (!loadBlock(b, block)) {
                err = "cannot read block data";
                return false;
            }
            ValidationState cst;
            std::vector<Hash256> txids;
            if (!Rules::CheckBlock(block, params_, cst, &txids)) {
                err = "stored block fails stage C: " + cst.code;
                return false;
            }
            BlockUndo undo;
            ValidationState est;
            if (!ConnectBlock(block, txids, *b, view, gold, params_, undo, est)) {
                if (est.result == ValidationResult::INTERNAL) {
                    err = est.code;
                    return false;
                }
                // Nothing was written: drop the view and look for the next candidate.
                markFailed(b, est);
                stepFailed = true;
                break;
            }
            commit.undos.push_back({b->hash, undo});
            connected.push_back(b);
            connectedBlocks.push_back(block);
        }
        if (stepFailed) continue;

        commit.coins = view.changes();
        commit.gold = gold;
        for (const auto& d : disconnected) {
            commit.statuses.push_back({d.first->hash, d.first->status & ~uint32_t(IN_MAIN_CHAIN)});
        }
        for (BlockIndex* b : connected) {
            commit.statuses.push_back({b->hash, b->status | CONNECTED_VALID | HAVE_UNDO | IN_MAIN_CHAIN});
        }
        if (!store_.commitStep(commit)) {
            err = "commit of an activation step failed";
            fail(EXIT_STORAGE_FAILURE, err);   // a write, not corrupt data
            return false;
        }

        // COMMIT succeeded: only now change the in-memory state.
        for (BlockIndex* b : connected) {
            b->status |= CONNECTED_VALID | HAVE_UNDO | IN_MAIN_CHAIN;
        }
        for (auto& d : disconnected) d.first->status &= ~uint32_t(IN_MAIN_CHAIN);
        chain_.setTip(stepEnd);
        tipSequence_.fetch_add(1, std::memory_order_release);
        lastTipMove_ = clock_();
        if (onDisconnect_) {
            for (auto& d : disconnected) onDisconnect_(d.second);
        }
        if (onStep_) {
            std::vector<Block> gone;
            gone.reserve(disconnected.size());
            for (auto& d : disconnected) gone.push_back(d.second);
            onStep_(gone, connectedBlocks);
        }
        if (alert_.active) {   // the branch the alert was about is now ours
            const BlockIndex* other = index_.find(alert_.otherTip);
            if (other && chain_.contains(other)) alert_ = ForkAlert{};
        }
    }
}

// ---------------------------------------------------------------- startup (section 8.7)

namespace {

constexpr int64_t kNoClock = INT64_MAX / 4;   // stage B without H-FUTURE
constexpr uint64_t kFullScanLimit = 5'000'000;

std::string describe(const BlockIndex* e) {
    return hexStr(std::vector<uint8_t>(e->hash.begin(), e->hash.end())) + " at height " + std::to_string(e->height);
}

LoadResult failure(int code, const std::string& msg) {
    LoadResult r;
    r.ok = false;
    r.exitCode = code;
    r.error = msg;
    return r;
}

} // namespace

void ChainState::shutdown() {
    if (fatalExitCode_) return;   // write nothing more; the next start runs the crash checks
    store_.setCleanShutdown(true);
}

bool ChainState::loadIndexFromStore(const StoredState& state, int tier, std::string& err) {
    (void)tier;
    std::vector<StoredIndexRow> rows;
    if (!store_.loadIndex(rows, err)) return false;
    std::sort(rows.begin(), rows.end(), [](const StoredIndexRow& a, const StoredIndexRow& b) {
        return a.header.height < b.header.height;
    });

    index_ = BlockIndexMap();
    chain_ = Chain();
    skipped_.clear();
    std::vector<std::pair<BlockIndex*, const StoredIndexRow*>> loaded;
    loaded.reserve(rows.size());

    // Pass 1: every row against the rules, with statuses still at HEADER_VALID
    // (a failed parent would otherwise make its children look corrupt).
    for (const StoredIndexRow& row : rows) {
        const std::string where = "block index corrupt at " + hexStr(std::vector<uint8_t>(row.hash.begin(), row.hash.end()));
        auto corrupt = [&](const std::string& why) {
            err = where + ": " + why + "; restart with --reindex";
            return false;
        };
        if (row.header.hash() != row.hash) return corrupt("stored header does not hash to its key");
        ValidationState st;
        if (!Rules::CheckBlockHeader(row.header, params_, st)) return corrupt(st.code);

        BlockIndex* parent = nullptr;
        if (row.header.height == 0) {
            if (row.hash != params_.genesisHash) return corrupt("a second genesis");
            if (index_.size() != 0) return corrupt("genesis is not the first row");
        } else {
            parent = index_.find(row.header.prevBlockHash);
            if (!parent) return corrupt("parent not found");
            if (parent->height + 1 != row.header.height) return corrupt("height is not parent height + 1");
            ValidationState bst;
            if (!Rules::ContextualCheckBlockHeader(row.header, parent, kNoClock, params_, bst, &index_))
                return corrupt(bst.code);
        }
        BlockIndex* e = index_.insert(row.header, parent);
        if (e->chainWork != row.chainWork) return corrupt("stored chain work differs from the recomputed value");
        loaded.emplace_back(e, &row);
    }
    if (loaded.empty() || loaded.front().first->hash != params_.genesisHash) {
        err = "block index corrupt: no genesis row; restart with --reindex";
        return false;
    }

    // Pass 2: statuses and the invariants I1 ... I4.
    size_t mainCount = 0;
    for (auto& [e, row] : loaded) {
        e->status = row->status;
        e->txCount = row->txCount;
        e->sequenceId = 0;
        if (!(e->status & HEADER_VALID)) {
            err = "block index corrupt at " + describe(e) + ": missing HEADER_VALID; restart with --reindex";
            return false;
        }
        if (e->prev && e->prev->failed() && !e->failed()) {
            err = "block index corrupt at " + describe(e) + ": child of a failed block is not marked; restart with --reindex";
            return false;
        }
        e->haveChainData = (e->status & HAVE_DATA) && (!e->prev || e->prev->haveChainData);
        if (e->status & IN_MAIN_CHAIN) ++mainCount;
    }

    // The tip: connected, in the main chain, equal to the stored best block, with
    // the main-chain flag on exactly the path to genesis.
    BlockIndex* tip = index_.find(state.best);
    if (!tip) {
        err = "best block is not in the block index; restart with --reindex-chainstate";
        return false;
    }
    if (!(tip->status & CONNECTED_VALID) || !(tip->status & IN_MAIN_CHAIN)) {
        err = "best block " + describe(tip) + " is not marked connected; restart with --reindex-chainstate";
        return false;
    }
    size_t pathCount = 0;
    for (const BlockIndex* p = tip; p; p = p->prev) {
        if (!(p->status & IN_MAIN_CHAIN)) {
            err = "main-chain flag missing on " + describe(p) + "; restart with --reindex-chainstate";
            return false;
        }
        ++pathCount;
    }
    if (pathCount != mainCount) {
        err = "main-chain flag set on blocks outside the path to the tip; restart with --reindex-chainstate";
        return false;
    }
    if (tip->height != state.bestHeight) {
        err = "stored best height differs from the index; restart with --reindex-chainstate";
        return false;
    }
    if (store_.coins().bestBlock() != tip->hash) {
        err = "coin set best-block marker is not the tip; restart with --reindex-chainstate";
        return false;
    }
    chain_.setTip(tip);
    return true;
}

bool ChainState::verifyDb(uint32_t depth, std::string& err) {
    const BlockIndex* tip = chain_.tip();
    const uint32_t n = depth == 0 ? tip->height : std::min<uint32_t>(depth, tip->height);
    if (n == 0) return true;

    struct Loaded {
        Block block;
        std::vector<Hash256> ids;
        BlockUndo undo;
        std::vector<uint8_t> undoBytes;
    };
    auto read = [&](const BlockIndex* e, Loaded& out) {
        std::vector<uint8_t> bytes;
        if (!store_.getBlock(e->hash, bytes)) {
            err = "cannot read block " + describe(e) + "; restart with --reindex";
            return false;
        }
        try {
            out.block = Block::deserialize(bytes);
        } catch (const std::exception&) {
            err = "block bytes of " + describe(e) + " do not parse; restart with --reindex";
            return false;
        }
        if (out.block.getHash() != e->hash) {
            err = "block bytes of " + describe(e) + " hash to another block; restart with --reindex";
            return false;
        }
        ValidationState st;
        if (!Rules::CheckBlock(out.block, params_, st, &out.ids)) {
            err = "stored block " + describe(e) + " fails stage C: " + st.code + "; restart with --reindex";
            return false;
        }
        if (!Rules::ContextualCheckBlock(out.block, e->height, e->prev->medianTimePast(), st)) {
            err = "stored block " + describe(e) + " fails stage D: " + st.code + "; restart with --reindex";
            return false;
        }
        if (!store_.getUndo(e->hash, out.undo)) {
            err = "undo record of " + describe(e) + " is missing or corrupt; restart with --reindex-chainstate";
            return false;
        }
        out.undoBytes = out.undo.serialize();
        return true;
    };

    CoinsViewCache view(&store_.coins());
    GoldRules::GoldState gold = store_.gold();

    // Down: disconnect the last n blocks.
    for (const BlockIndex* e = tip; e->height > tip->height - n; e = e->prev) {
        Loaded l;
        if (!read(e, l)) return false;
        if (DisconnectBlock(l.block, l.ids, *e, view, gold, l.undo) != DisconnectResult::OK) {
            err = "undo data of " + describe(e) + " does not match the coin set; restart with --reindex-chainstate";
            return false;
        }
    }
    // Up: connect them again with every signature checked.
    std::vector<const BlockIndex*> path;
    for (const BlockIndex* e = tip; e->height > tip->height - n; e = e->prev) path.push_back(e);
    std::reverse(path.begin(), path.end());
    for (const BlockIndex* e : path) {
        Loaded l;
        if (!read(e, l)) return false;
        BlockUndo again;
        ValidationState st;
        if (!ConnectBlock(l.block, l.ids, *e, view, gold, params_, again, st)) {
            err = "stored block " + describe(e) + " fails to connect: " + st.code + "; restart with --reindex-chainstate";
            return false;
        }
        if (again.serialize() != l.undoBytes) {
            err = "undo record of " + describe(e) + " differs from the recomputed one; restart with --reindex-chainstate";
            return false;
        }
    }
    // Round trip: the scratch view must equal the committed state.
    const CoinChanges net = view.changes();
    if (!net.removed.empty() || !net.added.empty() || view.bestBlock() != tip->hash || gold != store_.gold()) {
        err = "disconnect and reconnect of the last " + std::to_string(n) +
              " blocks does not restore the coin set; restart with --reindex-chainstate";
        return false;
    }
    return true;
}

bool ChainState::reindexFromBlocks(std::string& err) {
    const Block genesis = Genesis::build(params_);
    if (!store_.reindexBegin(genesis.header, params_.genesisHash, err)) return false;

    // Rebuild the index in memory and in the store from the stored block bytes.
    index_ = BlockIndexMap();
    chain_ = Chain();
    skipped_.clear();
    BlockIndex* g = index_.insert(genesis.header, nullptr);
    g->status = HEADER_VALID | HAVE_DATA | DATA_CHECKED | CONNECTED_VALID | IN_MAIN_CHAIN;
    g->haveChainData = true;
    chain_.setTip(g);

    std::vector<std::pair<Hash256, BlockHeader>> stored;
    if (!store_.listStoredBlocks(stored, err)) return false;
    std::sort(stored.begin(), stored.end(), [](const auto& a, const auto& b) {
        return a.second.height < b.second.height;
    });

    for (const auto& [hash, header] : stored) {
        if (hash == params_.genesisHash) continue;
        bool good = false;
        Block block;
        std::vector<uint8_t> bytes;
        if (store_.getBlock(hash, bytes)) {
            try {
                block = Block::deserialize(bytes);
                good = block.getHash() == hash;
            } catch (const std::exception&) {
            }
        }
        BlockIndex* e = nullptr;
        if (good) {
            ValidationState st;
            e = acceptHeader(block.header, st);
            good = e != nullptr;
            if (good) {
                std::vector<Hash256> txids;
                ValidationState cst, dst;
                good = Rules::CheckBlock(block, params_, cst, &txids) &&
                       Rules::ContextualCheckBlock(block, e->height, e->prev->medianTimePast(), dst);
            }
        }
        if (good) {
            e->status |= HAVE_DATA | DATA_CHECKED;
            e->txCount = uint32_t(block.transactions.size());
            persistStatus(e);
            propagateHaveChainData(e);
        } else {
            Bucks::logErr() << "reindex: dropping unusable block " << hexStr(std::vector<uint8_t>(hash.begin(), hash.end())) << "\n";
            store_.deleteBlockData(hash);
        }
        if (fatalExitCode_) {
            err = "a write to the store failed during the reindex";
            return false;
        }
    }
    if (!store_.reindexFinish(err)) return false;
    // The normal load path now reads everything back from the store.
    index_ = BlockIndexMap();
    chain_ = Chain();
    return true;
}

LoadResult ChainState::load(const LoadOptions& opt) {
    std::string err;
    if (!checkChainParams(params_, err) || !Genesis::check(params_, err))
        return failure(EXIT_PARAMS_MISMATCH, err);

    if (store_.isEmpty()) {
        if (!init(err)) return failure(EXIT_STORAGE_FAILURE, err);
        store_.setCleanShutdown(false);
        LoadResult ok;
        ok.ok = true;
        return ok;
    }

    StoredState state;
    if (!store_.loadState(state, err)) return failure(EXIT_DATABASE_CORRUPT, err);
    if (state.network != params_.name || state.genesis != params_.genesisHash)
        return failure(EXIT_PARAMS_MISMATCH, "datadir belongs to network " + state.network + " with another genesis");
    if (state.reindexInProgress && !opt.reindex)
        return failure(EXIT_DATABASE_CORRUPT, "an earlier --reindex did not finish; run again with --reindex");

    const bool unclean = !state.cleanShutdown;
    int tier = opt.checkTier ? opt.checkTier : (unclean ? 2 : 1);
    uint32_t depth = opt.checkBlocks;
    if (unclean && depth != 0) depth = std::max<uint32_t>(depth, 288);

    // From here on a crash is detectable.
    if (!store_.setCleanShutdown(false)) return failure(EXIT_STORAGE_FAILURE, "cannot write the shutdown flag");

    if (opt.reindex) {
        if (!reindexFromBlocks(err)) return failure(EXIT_DATABASE_CORRUPT, err);
        if (!store_.loadState(state, err)) return failure(EXIT_DATABASE_CORRUPT, err);
    } else if (opt.reindexChainstate) {
        if (!store_.resetChainState(err)) return failure(EXIT_DATABASE_CORRUPT, err);
        if (!store_.loadState(state, err)) return failure(EXIT_DATABASE_CORRUPT, err);
    }

    if (!store_.selfCheck(tier, err)) return failure(EXIT_DATABASE_CORRUPT, err + "; restart with --reindex");
    if (!loadIndexFromStore(state, tier, err)) return failure(EXIT_DATABASE_CORRUPT, err);
    if (!verifyDb(depth, err)) return failure(EXIT_DATABASE_CORRUPT, err);

    // Coin statistics: recomputed by a scan when the set is small or after a crash.
    if (tier >= 2 || state.stats.count <= kFullScanLimit) {
        UtxoStats scanned;
        if (!store_.scanStats(scanned, err)) return failure(EXIT_DATABASE_CORRUPT, err);
        if (scanned != state.stats)
            return failure(EXIT_DATABASE_CORRUPT,
                           "UTXO statistics differ from a scan of the coin set; restart with --reindex-chainstate");
    }
    {   // The coins cannot hold more than every subsidy ever paid.
        uint64_t issued = 0;
        for (uint32_t h = 1; h <= chain_.tip()->height; ++h) issued += Rules::Subsidy(h, params_);
        if (state.stats.totalAmount > issued)
            return failure(EXIT_DATABASE_CORRUPT, "the coin set holds more than all subsidies paid; restart with --reindex-chainstate");
    }

    // Blocks that were stored but not connected before shutdown.
    if (!activateBestChain(err)) return failure(fatalExitCode_ ? fatalExitCode_ : EXIT_DATABASE_CORRUPT, err);

    LoadResult ok;
    ok.ok = true;
    return ok;
}

} // namespace Bucks
