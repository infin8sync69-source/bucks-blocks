#include "transaction.h"

#include <algorithm>
#include <limits>

#include "consensus.h"
#include "serialize.h"

namespace Bucks {

// Smallest possible serialized input / output (spec section 11.1, rule R2).
static constexpr size_t MIN_INPUT_SIZE = 32 + 4 + 1 + 4;
static constexpr size_t MIN_OUTPUT_SIZE = 8 + 1;

TransactionInput::TransactionInput()
    : prevTxid{}, prevIndex(0), sequence(Consensus::SEQUENCE_FINAL) {}

TransactionInput::TransactionInput(const Hash256& prevTx, uint32_t idx)
    : prevTxid(prevTx), prevIndex(idx), sequence(Consensus::SEQUENCE_FINAL) {}

TransactionOutput::TransactionOutput() : amount(0) {}

TransactionOutput::TransactionOutput(uint64_t amt, const Script& spk)
    : amount(amt), scriptPubKey(spk) {}

TransactionOutput::TransactionOutput(uint64_t amt, const std::string& scriptStr)
    : amount(amt), scriptPubKey(scriptStr.begin(), scriptStr.end()) {}

Transaction::Transaction() : version(Consensus::TX_VERSION), lockTime(0) {}

Transaction::Transaction(const std::vector<TransactionInput>& in,
                         const std::vector<TransactionOutput>& out)
    : version(Consensus::TX_VERSION), inputs(in), outputs(out), lockTime(0) {}

// ============================================================================
// Serialization (CONSENSUS-CRITICAL)
// ============================================================================

Transaction Transaction::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() > Consensus::MAX_TX_SIZE) throw ParseError("transaction too large");
    Reader r(data);
    Transaction tx = parse(r);
    r.requireEnd();
    return tx;
}

Transaction Transaction::parse(Reader& r) {
    const size_t start = r.position();
    Transaction tx;

    tx.version = r.u32();

    uint64_t inCount = r.compactSize(Consensus::MAX_TX_INPUTS);
    if (inCount > r.remaining() / MIN_INPUT_SIZE) throw ParseError("input count exceeds data");
    tx.inputs.reserve(size_t(inCount));
    for (uint64_t i = 0; i < inCount; ++i) {
        TransactionInput in;
        in.prevTxid = r.hash256();
        in.prevIndex = r.u32();
        in.scriptSig = r.varBytes(Consensus::MAX_TX_SIZE);
        in.sequence = r.u32();
        tx.inputs.push_back(std::move(in));
    }

    uint64_t outCount = r.compactSize(Consensus::MAX_TX_OUTPUTS);
    if (outCount > r.remaining() / MIN_OUTPUT_SIZE) throw ParseError("output count exceeds data");
    tx.outputs.reserve(size_t(outCount));
    for (uint64_t i = 0; i < outCount; ++i) {
        TransactionOutput out;
        out.amount = r.u64();
        out.scriptPubKey = r.varBytes(Consensus::MAX_TX_SIZE);
        tx.outputs.push_back(std::move(out));
    }

    tx.lockTime = r.u32();
    if (r.position() - start > Consensus::MAX_TX_SIZE) throw ParseError("transaction too large");
    return tx;
}

std::vector<uint8_t> Transaction::serialize() const {
    Writer w;
    w.u32(version);
    w.compactSize(inputs.size());
    for (const auto& in : inputs) {
        w.hash256(in.prevTxid);
        w.u32(in.prevIndex);
        w.varBytes(in.scriptSig);
        w.u32(in.sequence);
    }
    w.compactSize(outputs.size());
    for (const auto& out : outputs) {
        w.u64(out.amount);
        w.varBytes(out.scriptPubKey);
    }
    w.u32(lockTime);
    return w.take();
}

Hash256 Transaction::txid() const {
    return doubleSha256(serialize());
}

bool Transaction::isCoinbase() const {
    return inputs.size() == 1 && inputs[0].prevIndex == 0xFFFFFFFFu &&
           std::all_of(inputs[0].prevTxid.begin(), inputs[0].prevTxid.end(),
                       [](uint8_t b) { return b == 0; });
}

uint64_t Transaction::totalOutput() const {
    uint64_t sum = 0;
    for (const auto& out : outputs) {
        if (out.amount > std::numeric_limits<uint64_t>::max() - sum)
            return std::numeric_limits<uint64_t>::max();
        sum += out.amount;
    }
    return sum;
}

} // namespace Bucks
