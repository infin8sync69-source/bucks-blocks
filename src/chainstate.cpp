#include "chainstate.h"

#include <algorithm>
#include <ctime>

#include "connect_block.h"
#include "genesis.h"

namespace Bucks {

namespace {

constexpr size_t MAX_HEADERS_PER_MESSAGE = 2000;
constexpr int64_t MAX_UNREQUESTED_AHEAD = 288;
constexpr size_t MAX_REJECT_STATES = 1000;
constexpr uint32_t MAX_STEP_CONNECTS = 32;

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

    if (!store_.putBlock(e->hash, g.serialize()) || !store_.putIndexRow(e->hash, e->header, e->status)) {
        err = "cannot store genesis";
        return false;
    }
    // Genesis creates no coins; it only sets the best block marker.
    CoinsViewCache view(&store_.coins());
    view.setBestBlock(e->hash);
    StepCommit commit;
    commit.coins = view.changes();
    commit.gold = store_.gold();
    if (!store_.commitStep(commit)) {
        err = "cannot commit genesis state";
        return false;
    }
    chain_.setTip(e);
    return true;
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

    if (!store_.putBlock(e->hash, block.serialize())) return internalError("store-block-failed", "");
    e->status |= HAVE_DATA | DATA_CHECKED;
    e->txCount = uint32_t(block.transactions.size());
    e->sequenceId = ++sequence_;
    persistStatus(e);
    propagateHaveChainData(e);
    return ValidationState{};
}

ValidationState ChainState::ProcessNewBlock(const Block& block, bool requested) {
    BlockIndex* e = nullptr;
    ValidationState st = acceptBlock(block, requested, e);
    if (st.result != ValidationResult::VALID) return st;

    std::string err;
    if (!activateBestChain(err)) return internalError("fatal-chainstate", err);

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

bool ChainState::approveReorg(const Hash256& tipHash, std::string& err) {
    approved_.insert(tipHash);
    skipped_.erase(tipHash);
    return activateBestChain(err);
}

bool ChainState::rejectState(const Hash256& hash, ValidationState& out) const {
    auto it = rejects_.find(hash);
    if (it == rejects_.end()) return false;
    out = it->second;
    return true;
}

// ---------------------------------------------------------------- status

void ChainState::persistStatus(const BlockIndex* e) {
    store_.putIndexRow(e->hash, e->header, e->status);
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
        if (depth > params_.maxAutoReorgDepth && !approved_.count(best->hash)) {
            alert_ = ForkAlert{true, fork->height, tip->hash, best->hash, best->chainWork - tip->chainWork};
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
            if (stepLast > 2 * params_.maxAutoReorgDepth + 32) {
                alert_ = ForkAlert{true, fork->height, tip->hash, best->hash, best->chainWork - tip->chainWork};
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
            return false;
        }

        // COMMIT succeeded: only now change the in-memory state.
        for (BlockIndex* b : connected) {
            b->status |= CONNECTED_VALID | HAVE_UNDO | IN_MAIN_CHAIN;
        }
        for (auto& d : disconnected) d.first->status &= ~uint32_t(IN_MAIN_CHAIN);
        chain_.setTip(stepEnd);
        tipSequence_++;
        if (onDisconnect_) {
            for (auto& d : disconnected) onDisconnect_(d.second);
        }
        if (alert_.active && approved_.count(best->hash)) alert_ = ForkAlert{};
    }
}

} // namespace Bucks
