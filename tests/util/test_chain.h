#ifndef BUCKS_TESTS_TEST_CHAIN_H
#define BUCKS_TESTS_TEST_CHAIN_H

#include <string>
#include <vector>

#include "block.h"
#include "chainstate.h"
#include "consensus.h"
#include "genesis.h"
#include "pow.h"
#include "script.h"
#include "sighash.h"
#include "memory_chainstore.h"
#include "validation_rules.h"

namespace Bucks {
namespace TestUtil {

// Deterministic test keys (same scheme as the unit tests).
inline PrivateKey privKey(int n) {
    std::string s = "bucks-test-key-" + std::to_string(n);
    return sha256(std::vector<uint8_t>(s.begin(), s.end()));
}

inline PublicKey pubKey(int n) {
    PublicKey p;
    derivePublicKey(privKey(n), p);
    return p;
}

inline Script ownedBy(int key) { return makeP2PKH(hash160(pubKey(key))); }

inline Hash256 hashWith(uint8_t first) {
    Hash256 h{};
    h[0] = first;
    return h;
}

// Coinbase paying the full subsidy to key 3. `tag` makes competing blocks at
// the same height produce different coinbases.
inline Transaction coinbaseTx(uint32_t height, uint64_t amount, uint8_t tag = 0) {
    Transaction tx;
    tx.version = Consensus::TX_VERSION;
    TransactionInput in;
    in.prevTxid = Hash256{};
    in.prevIndex = 0xFFFFFFFF;
    in.sequence = Consensus::SEQUENCE_FINAL;
    in.scriptSig = {uint8_t(height), uint8_t(height >> 8), uint8_t(height >> 16), uint8_t(height >> 24), tag};
    tx.inputs.push_back(in);
    tx.outputs.emplace_back(amount, ownedBy(3));
    return tx;
}

// Spends `prev` (owned by `ownerKey`, amount `prevAmount`) into one output for
// `destKey`. Signed after the outputs are fixed.
inline Transaction spendTx(const OutPoint& prev, uint64_t prevAmount, int ownerKey,
                           uint64_t toAmount, int destKey, const ChainParams& params) {
    Transaction tx;
    tx.version = Consensus::TX_VERSION;
    TransactionInput in(prev.txid, prev.index);
    in.sequence = Consensus::SEQUENCE_FINAL;
    tx.inputs.push_back(in);
    tx.outputs.emplace_back(toAmount, ownedBy(destKey));
    const std::vector<SpentOutput> spent = {SpentOutput{prevAmount, ownedBy(ownerKey)}};
    signP2PKHInput(tx, 0, spent, privKey(ownerKey), params.genesisHash);
    return tx;
}

// Builds a valid block on `parent`. The header is mined; the coinbase claims
// exactly the subsidy unless cbAmount is given. `tag` makes competing blocks
// at the same height produce different coinbases.
inline Block buildTestBlock(const ChainParams& params, const BlockIndex* parent,
                            const std::vector<Transaction>& extra = {}, uint8_t tag = 0,
                            uint32_t spacing = 600, uint64_t cbAmount = 0) {
    const uint32_t height = parent->height + 1;
    Block block;
    const uint64_t amount = cbAmount ? cbAmount : Rules::Subsidy(height, params);
    block.transactions.push_back(coinbaseTx(height, amount, tag));
    for (const Transaction& t : extra) block.transactions.push_back(t);

    std::vector<Hash256> ids;
    for (const Transaction& t : block.transactions) ids.push_back(t.txid());
    bool mutated = false;
    block.header.version = Consensus::BLOCK_VERSION;
    block.header.height = height;
    block.header.prevBlockHash = parent->hash;
    block.header.merkleRoot = merkleRoot(ids, &mutated);
    block.header.timestamp = parent->header.timestamp + spacing;
    block.header.bits = Rules::GetNextWorkRequired(parent, params);
    for (uint32_t n = 0;; ++n) {
        block.header.nonce = n;
        if (Pow::checkProofOfWork(block.header.hash(), block.header.bits, params.powLimit)) break;
    }
    return block;
}

// A chain on its own store, with regtest parameters by default. The clock is
// set far enough ahead that test blocks are never "in the future".
class TestChain {
public:
    explicit TestChain(const ChainParams& params = chainParams(NetworkId::REGTEST))
        : params_(params), state_(params_, store_, [this] { return now_; }) {
        now_ = int64_t(params_.genesisTime) + 10'000'000;
        std::string err;
        ok_ = state_.init(err);
        err_ = err;
    }

    bool ok() const { return ok_; }
    const std::string& error() const { return err_; }
    const ChainParams& params() const { return params_; }
    ChainState& state() { return state_; }
    MemoryChainStore& store() { return store_; }
    int64_t& now() { return now_; }

    // Builds a valid block on `parent` (see buildTestBlock).
    Block build(const BlockIndex* parent, const std::vector<Transaction>& extra = {},
                uint8_t tag = 0, uint32_t spacing = 600, uint64_t cbAmount = 0) const {
        return buildTestBlock(params_, parent, extra, tag, spacing, cbAmount);
    }

    const BlockIndex* tip() const { return state_.tip(); }

    // Feeds a block through ProcessNewBlock.
    ValidationState submit(const Block& block, bool requested = true) {
        return state_.ProcessNewBlock(block, requested);
    }

    // Mines and submits a block on the tip; returns it.
    Block extend(const std::vector<Transaction>& extra = {}, uint8_t tag = 0) {
        Block b = build(state_.tip(), extra, tag);
        submit(b);
        return b;
    }

private:
    ChainParams params_;
    MemoryChainStore store_;
    int64_t now_ = 0;
    ChainState state_;
    bool ok_ = false;
    std::string err_;
};

} // namespace TestUtil
} // namespace Bucks

#endif // BUCKS_TESTS_TEST_CHAIN_H
