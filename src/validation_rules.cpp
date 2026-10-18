#include "validation_rules.h"

#include <set>
#include <unordered_set>

#include "coins.h"
#include "consensus.h"
#include "gold_rules.h"
#include "pow.h"
#include "script.h"

namespace Bucks {
namespace Rules {

namespace {

using Consensus::MAX_MONEY;

// Checked addition for amounts: returns false instead of wrapping.
bool addMoney(uint64_t& sum, uint64_t amount) {
    if (amount > MAX_MONEY || sum > MAX_MONEY - amount) return false;
    sum += amount;
    return true;
}

uint32_t readLE32(const std::vector<uint8_t>& v) {
    return uint32_t(v[0]) | (uint32_t(v[1]) << 8) | (uint32_t(v[2]) << 16) | (uint32_t(v[3]) << 24);
}

} // namespace

// ---------------------------------------------------------------- stage A

bool CheckBlockHeader(const BlockHeader& header, const ChainParams& params, ValidationState& state) {
    if (header.version != Consensus::BLOCK_VERSION)
        return state.invalid(ValidationResult::INVALID, "bad-version");
    if (!Pow::bitsValid(header.bits, params.powLimit))
        return state.invalid(ValidationResult::INVALID, "bad-diffbits-encoding");
    if (!Pow::checkProofOfWork(header.hash(), header.bits, params.powLimit))
        return state.invalid(ValidationResult::INVALID, "high-hash");
    return true;
}

// ---------------------------------------------------------------- stage B

bool ContextualCheckBlockHeader(const BlockHeader& header, const BlockIndex* parent,
                                int64_t now, const ChainParams& params, ValidationState& state,
                                const BlockIndexMap* index) {
    if (!parent)
        return state.invalid(ValidationResult::MISSING_PARENT, "prev-blk-not-found");
    if (parent->failed())
        return state.invalid(ValidationResult::INVALID, "bad-prevblk");

    const int64_t height = int64_t(parent->height) + 1;
    if (int64_t(header.height) != height)
        return state.invalid(ValidationResult::INVALID, "bad-height");

    if (header.bits != GetNextWorkRequired(parent, params))
        return state.invalid(ValidationResult::INVALID, "bad-diffbits");

    const int64_t time = int64_t(header.timestamp);
    if (time <= parent->medianTimePast())
        return state.invalid(ValidationResult::INVALID, "time-too-old");

    // H-TW1 and H-TW2 protect the 2016-block window of the Bitcoin retarget.
    // ASERT has no window: a late timestamp lowers only the next block's
    // difficulty, by at most 2^(7200 / halfLife) with H-FUTURE, and an early
    // one raises it, so H-MTP and H-FUTURE are its time rules.
    if (params.powAlgorithm == PowAlgorithm::BITCOIN) {
        // H-TW1: the first block of a retarget period may not be more than 600 s
        // earlier than its parent (BIP94 style timewarp fix).
        if (height % int64_t(Consensus::RETARGET_INTERVAL) == 0 &&
            time < int64_t(parent->header.timestamp) - int64_t(Consensus::TIMEWARP_GRACE))
            return state.invalid(ValidationResult::INVALID, "time-timewarp-attack");

        // H-TW2: the last block of a period may not be earlier than the first.
        if (height % int64_t(Consensus::RETARGET_INTERVAL) == Consensus::RETARGET_INTERVAL - 1) {
            const BlockIndex* first = parent->getAncestor(uint32_t(height - (Consensus::RETARGET_INTERVAL - 1)));
            if (first && time < int64_t(first->header.timestamp))
                return state.invalid(ValidationResult::INVALID, "time-period-negative");
        }
    }

    // H-CKPT(a): the checkpoint height must carry the checkpoint hash.
    const Hash256 hash = header.hash();
    for (const Checkpoint& cp : params.checkpoints) {
        if (uint64_t(cp.height) == uint64_t(height) && hash != cp.hash)
            return state.invalid(ValidationResult::INVALID, "checkpoint-mismatch");
    }
    // H-CKPT(b): no fork below a checkpoint that is already in the index.
    if (index) {
        for (const Checkpoint& cp : params.checkpoints) {
            if (int64_t(cp.height) > height && index->find(cp.hash))
                return state.invalid(ValidationResult::INVALID, "bad-fork-prior-to-checkpoint");
        }
    }

    // H-FUTURE: checked last, because it is the only rule that depends on the
    // local clock. The caller must not cache a TIME_TOO_NEW result.
    if (time > now + int64_t(Consensus::MAX_FUTURE_BLOCK_TIME))
        return state.invalid(ValidationResult::TIME_TOO_NEW, "time-too-new");
    return true;
}

// ---------------------------------------------------------------- difficulty

uint32_t CalculateNextWorkRequired(uint32_t lastBits, int64_t firstTime, int64_t lastTime,
                                   const ChainParams& params) {
    return Pow::calculateNextWorkRequired(lastBits, firstTime, lastTime, params.powLimit,
                                          Consensus::TARGET_TIMESPAN);
}

uint32_t GetNextASERTWorkRequired(const BlockIndex* parent, const ChainParams& params) {
    // Anchor = genesis (height 0, its own bits). The specification uses the
    // anchor's parent time; genesis has none, so it is taken as genesis.time -
    // 600, a parent on schedule. Block 1 then keeps the genesis bits and the
    // schedule is genesis.time + 600 * height. Equivalently, in the
    // specification's terms: anchor = block 1 (bits = powLimitBits), anchor
    // parent time = genesis.time.
    const BlockIndex* genesis = parent->getAncestor(0);
    if (!genesis) return parent->header.bits;
    const int64_t anchorParentTime = int64_t(genesis->header.timestamp) - Consensus::TARGET_SPACING;
    const U256 target = Pow::calculateASERT(Pow::decodeCompact(genesis->header.bits).target,
                                            Consensus::TARGET_SPACING,
                                            int64_t(parent->header.timestamp) - anchorParentTime,
                                            int64_t(parent->height), params.powLimit,
                                            params.asertHalfLife);
    return Pow::encodeCompact(target);
}

uint32_t GetNextWorkRequired(const BlockIndex* parent, const ChainParams& params) {
    if (params.powNoRetargeting) return parent->header.bits;
    if (params.powAlgorithm == PowAlgorithm::ASERT) return GetNextASERTWorkRequired(parent, params);
    const uint64_t height = uint64_t(parent->height) + 1;
    if (height % Consensus::RETARGET_INTERVAL != 0) return parent->header.bits;
    // A child at a retarget height always has a window ancestor (height - 2016).
    const BlockIndex* first = parent->getAncestor(uint32_t(height - Consensus::RETARGET_INTERVAL));
    if (!first) return parent->header.bits;
    return CalculateNextWorkRequired(parent->header.bits, int64_t(first->header.timestamp),
                                     int64_t(parent->header.timestamp), params);
}

// ---------------------------------------------------------------- transactions

bool CheckTransaction(const Transaction& tx, ValidationState& state) {
    using R = ValidationResult;

    if (tx.version != Consensus::TX_VERSION)
        return state.invalid(R::INVALID, "bad-txns-version");

    if (tx.inputs.empty()) return state.invalid(R::INVALID, "bad-txns-vin-empty");
    if (tx.inputs.size() > Consensus::MAX_TX_INPUTS)
        return state.invalid(R::INVALID, "bad-txns-vin-toomany");
    if (tx.outputs.empty()) return state.invalid(R::INVALID, "bad-txns-vout-empty");
    if (tx.outputs.size() > Consensus::MAX_TX_OUTPUTS)
        return state.invalid(R::INVALID, "bad-txns-vout-toomany");

    const size_t size = tx.serialize().size();
    if (size > Consensus::MAX_TX_SIZE) return state.invalid(R::INVALID, "bad-txns-oversize");
    if (size == 64) return state.invalid(R::INVALID, "bad-txns-size-64");

    uint64_t outSum = 0;
    for (const TransactionOutput& out : tx.outputs) {
        if (!isStandardOutputScript(out.scriptPubKey))
            return state.invalid(R::INVALID, "bad-txns-output-type");
        if (isP2PKH(out.scriptPubKey)) {
            if (out.amount == 0) return state.invalid(R::INVALID, "bad-txns-vout-zero");
            if (out.amount > MAX_MONEY) return state.invalid(R::INVALID, "bad-txns-vout-toolarge");
        } else {
            // NULL_DATA: zero value, so a marker never creates a spendable coin.
            if (out.amount != 0) return state.invalid(R::INVALID, "bad-txns-nulldata-value");
        }
        if (!addMoney(outSum, out.amount))
            return state.invalid(R::INVALID, "bad-txns-txouttotal-toolarge");
    }

    if (tx.isCoinbase()) {
        const size_t len = tx.inputs[0].scriptSig.size();
        if (len < Consensus::COINBASE_SCRIPTSIG_MIN || len > Consensus::COINBASE_SCRIPTSIG_MAX)
            return state.invalid(R::INVALID, "bad-cb-length");
        if (tx.inputs[0].sequence != Consensus::SEQUENCE_FINAL)
            return state.invalid(R::INVALID, "bad-cb-sequence");
        if (tx.lockTime != 0) return state.invalid(R::INVALID, "bad-cb-locktime");
        return true;
    }

    std::set<OutPoint, OutPointLess> seen;
    for (const TransactionInput& in : tx.inputs) {
        bool nullTxid = true;
        for (uint8_t b : in.prevTxid) nullTxid = nullTxid && b == 0;
        if (nullTxid) return state.invalid(R::INVALID, "bad-txns-prevout-null");
        if (!seen.insert(OutPoint{in.prevTxid, in.prevIndex}).second)
            return state.invalid(R::INVALID, "bad-txns-inputs-duplicate");

        std::vector<uint8_t> der;
        CompressedPubKey pub{};
        if (!parseP2PKHUnlock(in.scriptSig, der, pub))
            return state.invalid(R::INVALID, "bad-scriptsig-format");
    }
    return true;
}

bool IsFinal(const Transaction& tx, uint32_t height, int64_t mtp) {
    if (tx.lockTime == 0) return true;
    const int64_t limit = tx.lockTime < Consensus::LOCKTIME_THRESHOLD ? int64_t(height) : mtp;
    if (int64_t(tx.lockTime) < limit) return true;
    for (const TransactionInput& in : tx.inputs) {
        if (in.sequence != Consensus::SEQUENCE_FINAL) return false;
    }
    return true;
}

uint64_t Subsidy(uint32_t height, const ChainParams& params) {
    const uint64_t halvings = uint64_t(height) / params.halvingInterval;
    if (halvings >= 64) return 0;
    return Consensus::INITIAL_SUBSIDY >> halvings;
}

uint64_t TxSigOps(const Transaction& tx) {
    uint64_t ops = tx.isCoinbase() ? 0 : uint64_t(tx.inputs.size());
    for (const TransactionOutput& out : tx.outputs) {
        std::vector<uint8_t> payload;
        if (!isP2PKH(out.scriptPubKey) && parseNullData(out.scriptPubKey, &payload))
            ops += GoldRules::classify(payload).sigOps;
    }
    return ops;
}

// ---------------------------------------------------------------- stage C

bool CheckBlock(const Block& block, const ChainParams& params, ValidationState& state,
                std::vector<Hash256>* txids) {
    using R = ValidationResult;
    (void)params;
    std::vector<Hash256> ids;
    ids.reserve(block.transactions.size());
    for (const Transaction& tx : block.transactions) ids.push_back(tx.txid());

    bool mutated = false;
    const Hash256 root = merkleRoot(ids, &mutated);
    if (root != block.header.merkleRoot)
        return state.invalid(R::MUTATED, "bad-txnmrklroot");
    if (mutated) return state.invalid(R::MUTATED, "bad-txns-duplicate");

    // A transaction of exactly 64 bytes can be the concatenation of two txids: a forged body, not proof that the
    // block is bad. Treat it as a mutation so the honest block is never marked failed (audit CON-2).
    for (const Transaction& tx : block.transactions)
        if (tx.serialize().size() == 64) return state.invalid(R::MUTATED, "bad-txns-size-64");

    if (block.transactions.empty()) return state.invalid(R::INVALID, "bad-blk-length");
    if (block.serialize().size() > Consensus::MAX_BLOCK_SIZE)
        return state.invalid(R::INVALID, "bad-blk-length");

    if (!block.transactions[0].isCoinbase())
        return state.invalid(R::INVALID, "bad-cb-missing");
    for (size_t i = 1; i < block.transactions.size(); ++i) {
        if (block.transactions[i].isCoinbase())
            return state.invalid(R::INVALID, "bad-cb-multiple");
    }

    for (const Transaction& tx : block.transactions) {
        if (!CheckTransaction(tx, state)) return false;
    }

    std::unordered_set<Hash256, Hash256Hasher> seen;
    seen.reserve(ids.size());
    for (const Hash256& id : ids) {
        if (!seen.insert(id).second) return state.invalid(R::INVALID, "bad-txns-duplicate");
    }

    uint64_t sigops = 0;
    for (const Transaction& tx : block.transactions) sigops += TxSigOps(tx);
    if (sigops > Consensus::MAX_SIGOPS_PER_BLOCK)
        return state.invalid(R::INVALID, "bad-blk-sigops");

    if (txids) *txids = std::move(ids);
    return true;
}

// ---------------------------------------------------------------- stage D

bool ContextualCheckBlock(const Block& block, uint32_t height, int64_t parentMtp,
                          ValidationState& state) {
    const Transaction& cb = block.transactions.at(0);
    // CheckTransaction already guarantees at least 4 bytes of scriptSig.
    if (readLE32(cb.inputs[0].scriptSig) != height)
        return state.invalid(ValidationResult::INVALID, "bad-cb-height");

    for (const Transaction& tx : block.transactions) {
        if (!IsFinal(tx, height, parentMtp))
            return state.invalid(ValidationResult::INVALID, "bad-txns-nonfinal");
    }
    return true;
}

} // namespace Rules
} // namespace Bucks
