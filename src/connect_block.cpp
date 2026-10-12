#include "connect_block.h"

#include "consensus.h"
#include "script.h"
#include "sighash.h"

namespace Bucks {

namespace {

using Consensus::MAX_MONEY;

bool addMoney(uint64_t& sum, uint64_t amount) {
    if (amount > MAX_MONEY || sum > MAX_MONEY - amount) return false;
    sum += amount;
    return true;
}

} // namespace

bool ConnectBlock(const Block& block, const std::vector<Hash256>& txids, const BlockIndex& index,
                  CoinsViewCache& view, GoldRules::GoldState& gold, const ChainParams& params,
                  BlockUndo& undoOut, ValidationState& state) {
    using R = ValidationResult;

    if (!index.prev) return state.invalid(R::INTERNAL, "connect-genesis", "genesis is not connected");
    if (view.bestBlock() != index.prev->hash)
        return state.invalid(R::INTERNAL, "connect-bad-tip", "view is not at the parent");
    if (txids.size() != block.transactions.size())
        return state.invalid(R::INTERNAL, "connect-txid-count", "txids do not match the block");

    undoOut = BlockUndo{};
    undoOut.blockHash = index.hash;
    GoldRules::GoldUndoBuilder goldUndo = GoldRules::beginBlock(gold);
    uint64_t fees = 0;

    for (size_t t = 0; t < block.transactions.size(); ++t) {
        const Transaction& tx = block.transactions[t];

        if (t > 0) {
            std::vector<Coin> spent;
            std::vector<SpentOutput> spentOut;
            spent.reserve(tx.inputs.size());
            spentOut.reserve(tx.inputs.size());
            uint64_t inSum = 0;

            for (const TransactionInput& in : tx.inputs) {
                const OutPoint op{in.prevTxid, in.prevIndex};
                Coin coin;
                if (!view.getCoin(op, coin))
                    return state.invalid(R::INVALID, "bad-txns-inputs-missingorspent");
                // Spendable at height h + COINBASE_MATURITY, so this is a strict "<".
                if (coin.isCoinbase &&
                    int64_t(index.height) - int64_t(coin.height) < int64_t(Consensus::COINBASE_MATURITY))
                    return state.invalid(R::INVALID, "bad-txns-premature-spend-of-coinbase");
                if (!addMoney(inSum, coin.amount))
                    return state.invalid(R::INVALID, "bad-txns-inputvalues-outofrange");
                spentOut.push_back(SpentOutput{coin.amount, coin.scriptPubKey});
                spent.push_back(std::move(coin));
            }

            uint64_t outSum = 0;
            for (const TransactionOutput& out : tx.outputs) {
                if (!addMoney(outSum, out.amount))
                    return state.invalid(R::INVALID, "bad-txns-txouttotal-toolarge");
            }
            if (inSum < outSum) return state.invalid(R::INVALID, "bad-txns-in-belowout");

            const uint64_t fee = inSum - outSum;
            if (!addMoney(fees, fee)) return state.invalid(R::INVALID, "bad-txns-fee-outofrange");

            // C-SIG: every input of every non-coinbase transaction, with the
            // coins it spends (SIG-4 ... SIG-7).
            const SigHashCache cache(tx, spentOut);
            for (size_t k = 0; k < tx.inputs.size(); ++k) {
                const SpendError e = verifyP2PKHInput(tx, uint32_t(k), cache, spentOut[k], params.genesisHash);
                if (e != SpendError::OK) return state.invalid(R::INVALID, spendErrorCode(e));
            }

            for (size_t k = 0; k < tx.inputs.size(); ++k) {
                if (!view.spendCoin(OutPoint{tx.inputs[k].prevTxid, tx.inputs[k].prevIndex}))
                    return state.invalid(R::INTERNAL, "connect-spend-failed");
                undoOut.spent.push_back(spent[k]);
            }
        }

        for (uint32_t j = 0; j < tx.outputs.size(); ++j) {
            const TransactionOutput& out = tx.outputs[j];
            if (isP2PKH(out.scriptPubKey)) {
                const OutPoint op{txids[t], j};
                if (view.haveCoin(op)) return state.invalid(R::INVALID, "bad-txns-BIP30");
                Coin coin;
                coin.amount = out.amount;
                coin.scriptPubKey = out.scriptPubKey;
                coin.height = index.height;
                coin.isCoinbase = (t == 0);
                if (!view.addCoin(op, coin))
                    return state.invalid(R::INTERNAL, "connect-add-failed");
            } else {
                // NULL_DATA never enters the coin set. The gold hook decides
                // whether the payload is valid (stub: always).
                if (!GoldRules::connectOutput(tx, uint32_t(t), j, txids[t], index.height, gold,
                                              goldUndo, state))
                    return false;
            }
        }
    }

    // C-CB-AMOUNT: the coinbase may claim at most subsidy + fees.
    uint64_t cbOut = 0;
    for (const TransactionOutput& out : block.transactions[0].outputs) {
        if (!addMoney(cbOut, out.amount)) return state.invalid(R::INVALID, "bad-cb-amount");
    }
    const uint64_t allowed = Rules::Subsidy(index.height, params) + fees;
    if (cbOut > allowed) return state.invalid(R::INVALID, "bad-cb-amount");

    undoOut.goldUndo = goldUndo.finish();
    view.setBestBlock(index.hash);
    return true;
}

DisconnectResult DisconnectBlock(const Block& block, const std::vector<Hash256>& txids,
                                 const BlockIndex& index, CoinsViewCache& view,
                                 GoldRules::GoldState& gold, const BlockUndo& undo) {
    if (!index.prev) return DisconnectResult::CORRUPT;
    if (view.bestBlock() != index.hash) return DisconnectResult::CORRUPT;
    if (undo.blockHash != index.hash) return DisconnectResult::CORRUPT;
    if (txids.size() != block.transactions.size()) return DisconnectResult::CORRUPT;

    size_t spendCount = 0;
    for (size_t t = 1; t < block.transactions.size(); ++t) spendCount += block.transactions[t].inputs.size();
    if (undo.spent.size() != spendCount) return DisconnectResult::CORRUPT;

    if (!GoldRules::disconnectBlock(gold, undo.goldUndo)) return DisconnectResult::CORRUPT;

    // Reverse order: outputs are removed before the inputs they spent come
    // back, so an output created and spent in this block does not reappear.
    size_t pos = undo.spent.size();
    for (size_t t = block.transactions.size(); t-- > 0;) {
        const Transaction& tx = block.transactions[t];

        for (uint32_t j = uint32_t(tx.outputs.size()); j-- > 0;) {
            const TransactionOutput& out = tx.outputs[j];
            if (!isP2PKH(out.scriptPubKey)) continue;
            const OutPoint op{txids[t], j};
            Coin coin;
            if (!view.getCoin(op, coin)) return DisconnectResult::CORRUPT;
            if (coin.amount != out.amount || coin.scriptPubKey != out.scriptPubKey ||
                coin.height != index.height || coin.isCoinbase != (t == 0))
                return DisconnectResult::CORRUPT;
            if (!view.spendCoin(op)) return DisconnectResult::CORRUPT;
        }

        if (t > 0) {
            for (size_t k = tx.inputs.size(); k-- > 0;) {
                if (pos == 0) return DisconnectResult::CORRUPT;
                --pos;
                const OutPoint op{tx.inputs[k].prevTxid, tx.inputs[k].prevIndex};
                if (view.haveCoin(op)) return DisconnectResult::CORRUPT;
                // Restores the original height and coinbase flag from the undo record.
                if (!view.addCoin(op, undo.spent[pos])) return DisconnectResult::CORRUPT;
            }
        }
    }
    if (pos != 0) return DisconnectResult::CORRUPT;

    view.setBestBlock(index.prev->hash);
    return DisconnectResult::OK;
}

} // namespace Bucks
