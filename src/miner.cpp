#include "miner.h"

#include <algorithm>
#include <ctime>
#include <iostream>

#include "coins.h"
#include "connect_block.h"
#include "consensus.h"
#include "gold_rules.h"
#include "merkle.h"
#include "pow.h"
#include "serialize.h"
#include "validation_rules.h"

namespace Bucks {

namespace {

constexpr size_t MAX_MINER_TAG = 88;
constexpr size_t MAX_EXTRA_PAYLOAD = 80;

} // namespace

BlockAssembler::BlockAssembler(const ChainParams& params, const ChainState& chain,
                               Mempool& mempool, Clock clock)
    : params_(params), chain_(chain), mempool_(mempool), clock_(std::move(clock)) {
    if (!clock_) clock_ = [] { return int64_t(std::time(nullptr)); };
}

bool BlockAssembler::testBlockValidity(const Block& block, ValidationState& state) const {
    const BlockIndex* tip = chain_.tip();
    if (!tip) return state.invalid(ValidationResult::INTERNAL, "no-tip");
    if (block.transactions.empty()) return state.invalid(ValidationResult::INVALID, "bad-blk-length");

    // Stage A without proof of work: the nonce is not mined yet.
    if (block.header.version != Consensus::BLOCK_VERSION)
        return state.invalid(ValidationResult::INVALID, "bad-version");
    if (!Pow::bitsValid(block.header.bits, params_.powLimit))
        return state.invalid(ValidationResult::INVALID, "bad-diffbits-encoding");
    // Stage B.
    if (!Rules::ContextualCheckBlockHeader(block.header, tip, clock_(), params_, state)) return false;
    // Stage C.
    std::vector<Hash256> txids;
    if (!Rules::CheckBlock(block, params_, state, &txids)) return false;
    // Stage D.
    const uint32_t height = tip->height + 1;
    if (!Rules::ContextualCheckBlock(block, height, tip->medianTimePast(), state)) return false;
    // Stage E on a scratch view.
    BlockIndex scratch;
    scratch.prev = const_cast<BlockIndex*>(tip);
    scratch.height = height;
    scratch.header = block.header;
    scratch.hash = block.header.hash();
    CoinsViewCache view(&chain_.coins());
    GoldRules::GoldState gold = chain_.gold();
    BlockUndo undo;
    return ConnectBlock(block, txids, scratch, view, gold, params_, undo, state);
}

void BlockAssembler::setExtraNonce(BlockTemplate& t, uint64_t extraNonce) {
    Transaction& cb = t.block.transactions[0];
    std::vector<uint8_t>& sig = cb.inputs[0].scriptSig;
    for (int i = 0; i < 8; ++i) sig[4 + i] = uint8_t(extraNonce >> (8 * i));
    std::vector<Hash256> ids;
    ids.reserve(t.block.transactions.size());
    for (const Transaction& tx : t.block.transactions) ids.push_back(tx.txid());
    t.block.header.merkleRoot = merkleRoot(ids);
    t.extraNonce = extraNonce;
}

bool BlockAssembler::build(const Script& minerScript, const AssemblerOptions& options,
                           const std::vector<Hash256>& skipTxids, BlockTemplate& out,
                           std::string& err) {
    const BlockIndex* tip = chain_.tip();
    if (!tip) {
        err = "no chain tip";
        return false;
    }
    const uint32_t h = tip->height + 1;
    const int64_t now = clock_();
    const int64_t mtp = tip->medianTimePast();

    BlockHeader header;
    header.version = Consensus::BLOCK_VERSION;
    header.height = h;
    header.prevBlockHash = tip->hash;
    header.bits = Rules::GetNextWorkRequired(tip, params_);

    int64_t minTime = mtp + 1;
    if (h % Consensus::RETARGET_INTERVAL == 0)   // H-TW1
        minTime = std::max<int64_t>(minTime, int64_t(tip->header.timestamp) - int64_t(Consensus::TIMEWARP_GRACE));
    if (h % Consensus::RETARGET_INTERVAL == Consensus::RETARGET_INTERVAL - 1) {   // H-TW2
        if (const BlockIndex* first = tip->getAncestor(h - (Consensus::RETARGET_INTERVAL - 1)))
            minTime = std::max<int64_t>(minTime, int64_t(first->header.timestamp));
    }
    const int64_t time = std::max(now, minTime);
    if (time > now + int64_t(Consensus::MAX_FUTURE_BLOCK_TIME)) {
        err = "local clock is behind the chain";
        return false;
    }
    header.timestamp = uint32_t(time);

    // Transactions: best fee rate first, older first on ties, within the size
    // and sigop budgets. Size and sigops come from the same functions the
    // validation uses, so a template can never exceed the block limits.
    std::vector<MempoolEntry> pool = mempool_.byFeeRateDescending();
    std::stable_sort(pool.begin(), pool.end(), [](const MempoolEntry& a, const MempoolEntry& b) {
        if (a.feeRate != b.feeRate) return a.feeRate > b.feeRate;
        return a.entryTime < b.entryTime;
    });

    int64_t sizeLeft = int64_t(Consensus::MAX_BLOCK_SIZE) - int64_t(Consensus::BLOCK_HEADER_SIZE) - 3 -
                       int64_t(Policy::COINBASE_RESERVED_SIZE);
    int64_t sigopLeft = int64_t(Consensus::MAX_SIGOPS_PER_BLOCK);
    std::vector<Transaction> selected;
    uint64_t fees = 0;
    for (const MempoolEntry& e : pool) {
        if (std::find(skipTxids.begin(), skipTxids.end(), e.txid) != skipTxids.end()) continue;
        if (int64_t(e.size) > sizeLeft || int64_t(e.sigOps) > sigopLeft) continue;
        if (!Rules::IsFinal(e.tx, h, mtp)) continue;
        selected.push_back(e.tx);
        sizeLeft -= e.size;
        sigopLeft -= e.sigOps;
        fees += e.fee;
    }

    // Coinbase: LE32(height) | LE64(extraNonce) | tag, so every one is unique.
    Transaction cb;
    cb.version = Consensus::TX_VERSION;
    TransactionInput in;
    in.prevTxid = Hash256{};
    in.prevIndex = 0xFFFFFFFF;
    in.sequence = Consensus::SEQUENCE_FINAL;
    Writer sig;
    sig.u32(h).u64(0);
    sig.bytes(reinterpret_cast<const uint8_t*>(options.minerTag.data()), options.minerTag.size());
    in.scriptSig = sig.take();
    cb.inputs.push_back(in);

    const uint64_t reward = Rules::Subsidy(h, params_) + fees;
    Script nullData;
    if (reward == 0) {
        makeNullData({}, nullData);   // a zero-value P2PKH output would be invalid
        cb.outputs.emplace_back(0, nullData);
    } else {
        cb.outputs.emplace_back(reward, minerScript);
    }
    if (!options.extraPayload.empty()) {
        makeNullData(options.extraPayload, nullData);
        cb.outputs.emplace_back(0, nullData);
    }

    out = BlockTemplate{};
    out.block.header = header;
    out.block.transactions.push_back(std::move(cb));
    for (Transaction& tx : selected) out.block.transactions.push_back(std::move(tx));
    out.tipSequence = chain_.tipSequence();
    out.fees = fees;
    out.minTime = minTime;
    setExtraNonce(out, 0);
    return true;
}

bool BlockAssembler::createNewBlock(const Script& minerScript, const AssemblerOptions& options,
                                    BlockTemplate& out, std::string& err) {
    if (!isP2PKH(minerScript)) {
        err = "miner script must be P2PKH";
        return false;
    }
    if (options.minerTag.size() > MAX_MINER_TAG) {
        err = "miner tag longer than 88 bytes";
        return false;
    }
    if (options.extraPayload.size() > MAX_EXTRA_PAYLOAD) {
        err = "extra payload longer than 80 bytes";
        return false;
    }
    const std::string blocked = chain_.miningBlockedReason();
    if (!blocked.empty()) {
        err = blocked;
        return false;
    }

    std::vector<Hash256> skip;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!build(minerScript, options, skip, out, err)) return false;
        ValidationState st;
        if (testBlockValidity(out.block, st)) return true;

        std::cerr << "BUG: template invalid: " << st.code << " " << st.detail << "\n";
        err = "template invalid: " + st.code;
        if (attempt == 1 || out.block.transactions.size() < 2) return false;

        // Find the first transaction whose presence breaks the block: the
        // shortest valid-prefix boundary, by bisection. The coinbase reward
        // follows the prefix' fees, so each prefix is rebuilt.
        const std::vector<Transaction> all = out.block.transactions;
        size_t lo = 0, hi = all.size() - 1;   // number of selected txs kept: lo works, hi fails
        auto prefixValid = [&](size_t keep) {
            BlockTemplate t = out;
            t.block.transactions.assign(all.begin(), all.begin() + 1 + keep);
            // the reward follows the kept transactions' fees
            uint64_t f = 0;
            for (size_t i = 1; i <= keep; ++i) {
                const MempoolEntry* e = mempool_.find(all[i].txid());
                if (e) f += e->fee;
            }
            const uint32_t h = chain_.tip()->height + 1;
            t.block.transactions[0].outputs[0].amount = Rules::Subsidy(h, params_) + f;
            setExtraNonce(t, 0);
            ValidationState s;
            return testBlockValidity(t.block, s);
        };
        if (prefixValid(hi)) return false;   // not caused by a transaction: give up
        while (hi - lo > 1) {
            const size_t mid = (lo + hi) / 2;
            if (prefixValid(mid)) lo = mid; else hi = mid;
        }
        const Hash256 bad = all[hi].txid();   // all[0] is the coinbase, so all[hi] is selected tx hi
        std::cerr << "BUG: removing transaction " << hexStr(std::vector<uint8_t>(bad.begin(), bad.end()))
                  << " from the mempool\n";
        mempool_.remove(bad);
        skip.push_back(bad);
    }
    return false;
}

// ---------------------------------------------------------------- miner loop

Miner::Miner(ChainState& chain, BlockAssembler& assembler, Clock clock)
    : chain_(chain), assembler_(assembler), clock_(std::move(clock)) {
    if (!clock_) clock_ = [] { return int64_t(std::time(nullptr)); };
}

MineResult Miner::mineOne(const Script& minerScript, const AssemblerOptions& options,
                          const std::atomic<bool>& stop, std::string& err) {
    const ChainParams& params = chain_.params();
    int64_t began = 0;
    for (;;) {
        if (stop.load()) return MineResult::STOPPED;

        BlockTemplate t;
        {
            std::unique_lock<std::mutex> guard;
            if (lock_) guard = std::unique_lock<std::mutex>(*lock_);
            if (!assembler_.createNewBlock(minerScript, options, t, err)) return MineResult::FAILED;
        }
        ++templates_;
        const int64_t started = clock_();
        if (!began) { began = started; }
        bool restart = false;
        uint64_t extraNonce = 0;

        while (!restart) {
            for (uint64_t n = 0; n < nonceSpace_; ++n) {
                if (n % checkInterval_ == 0) {
                    if (batchHook_) batchHook_();
                    if (stop.load()) return MineResult::STOPPED;
                    if (chain_.tipSequence() != t.tipSequence) {   // the tip moved: this template is stale
                        ++staleRestarts_;
                        restart = true;
                        break;
                    }
                    const int64_t now = clock_();
                    if (timeLimit_ > 0 && now - began >= timeLimit_) return MineResult::STOPPED;
                    hashes_ += (n % checkInterval_ == 0 && n > 0) ? checkInterval_ : 0;
                    if (refreshSeconds_ > 0 && now - started >= refreshSeconds_) {
                        restart = true;   // pick up new mempool transactions
                        break;
                    }
                    // Time may move forward, never backwards and never past the future limit.
                    if (now > int64_t(t.block.header.timestamp) &&
                        now <= clock_() + int64_t(Consensus::MAX_FUTURE_BLOCK_TIME))
                        t.block.header.timestamp = uint32_t(now);
                }
                t.block.header.nonce = uint32_t(n);
                if (Pow::checkProofOfWork(t.block.header.hash(), t.block.header.bits, params.powLimit)) {
                    lastHash_ = t.block.header.hash();
                    std::unique_lock<std::mutex> guard;
                    if (lock_) guard = std::unique_lock<std::mutex>(*lock_);
                    const ValidationState st = chain_.ProcessNewBlock(t.block, true);
                    if (!st.valid()) {
                        err = "node rejected our block: " + st.code;
                        return MineResult::FAILED;
                    }
                    return MineResult::FOUND;
                }
            }
            if (restart) break;
            // The whole nonce space failed: change the coinbase and go again.
            BlockAssembler::setExtraNonce(t, ++extraNonce);
            ++rollovers_;
        }
    }
}

} // namespace Bucks
