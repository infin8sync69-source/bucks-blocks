#include "transaction.h"
#include "crypto.h"

#include <ctime>
#include <cstring>
#include <algorithm>
#include <stdexcept>

namespace Bucks {

// ============================================================================
// Constructors
// ============================================================================

TransactionInput::TransactionInput()
    : prevIndex(0), sequence(0xffffffff) {}

TransactionInput::TransactionInput(const Hash256& prevTx, uint32_t idx)
    : prevTxid(prevTx), prevIndex(idx), sequence(0xffffffff) {}

TransactionOutput::TransactionOutput(uint64_t amt, const Script& spk)
    : amount(amt), scriptPubKey(spk) {}

TransactionOutput::TransactionOutput()
    : amount(0) {}

TransactionOutput::TransactionOutput(uint64_t amt, const std::string& scriptStr)
    : amount(amt), scriptPubKey(scriptStr.begin(), scriptStr.end()) {}

Transaction::Transaction()
    : version(1), lockTime(0), timestamp(std::time(nullptr)) {}

Transaction::Transaction(
    const std::vector<TransactionInput>& in,
    const std::vector<TransactionOutput>& out
) : version(1),
    inputs(in),
    outputs(out),
    lockTime(0),
    timestamp(std::time(nullptr)) {}

// ============================================================================
// Serialization helpers
// ============================================================================

static void writeU32(std::vector<uint8_t>& buf, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        buf.push_back((v >> (8 * i)) & 0xff);
}

static void writeU64(std::vector<uint8_t>& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        buf.push_back((v >> (8 * i)) & 0xff);
}

static void writeVarInt(std::vector<uint8_t>& buf, uint64_t v) {
    if (v < 0xfd) {
        buf.push_back(static_cast<uint8_t>(v));
    } else {
        buf.push_back(0xfd);
        buf.push_back(v & 0xff);
        buf.push_back((v >> 8) & 0xff);
    }
}

static void writeVarBytes(std::vector<uint8_t>& buf,
                          const std::vector<uint8_t>& data) {
    writeVarInt(buf, data.size());
    buf.insert(buf.end(), data.begin(), data.end());
}

static uint32_t readU32(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("Unexpected end of data");
    uint32_t v = data[offset] | (data[offset+1] << 8) | (data[offset+2] << 16) | (data[offset+3] << 24);
    offset += 4;
    return v;
}

static uint64_t readU64(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + 8 > data.size()) throw std::runtime_error("Unexpected end of data");
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (static_cast<uint64_t>(data[offset + i]) << (8 * i));
    offset += 8;
    return v;
}

static uint64_t readVarInt(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset >= data.size()) throw std::runtime_error("Unexpected end of data");
    uint8_t first = data[offset++];
    if (first < 0xfd) return first;
    if (first == 0xfd) {
        if (offset + 2 > data.size()) throw std::runtime_error("Unexpected end of data");
        uint64_t v = data[offset] | (data[offset+1] << 8);
        offset += 2;
        return v;
    }
    throw std::runtime_error("Unsupported VarInt size (only < 0xfd and 0xfd supported)");
}

static std::vector<uint8_t> readVarBytes(const std::vector<uint8_t>& data, size_t& offset) {
    uint64_t len = readVarInt(data, offset);
    if (offset + len > data.size()) throw std::runtime_error("Unexpected end of data");
    std::vector<uint8_t> res(data.begin() + offset, data.begin() + offset + len);
    offset += len;
    return res;
}

// ============================================================================
// Serialization (CONSENSUS-CRITICAL)
// ============================================================================

Transaction Transaction::deserialize(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    Transaction tx;

    tx.version = readU32(data, offset);

    // Flag to check if SegWit (00 01)
    bool hasWitness = false;
    if (offset + 2 <= data.size() && data[offset] == 0x00 && data[offset+1] == 0x01) {
        hasWitness = true;
        offset += 2;
    }

    // inputs
    uint64_t inputCount = readVarInt(data, offset);
    for (uint64_t i = 0; i < inputCount; ++i) {
        TransactionInput in;
        if (offset + 32 > data.size()) throw std::runtime_error("Unexpected end of data");
        std::copy(data.begin() + offset, data.begin() + offset + 32, in.prevTxid.begin());
        offset += 32;
        in.prevIndex = readU32(data, offset);
        in.scriptSig = readVarBytes(data, offset);
        in.sequence = readU32(data, offset);
        tx.inputs.push_back(in);
    }

    // outputs
    uint64_t outputCount = readVarInt(data, offset);
    for (uint64_t i = 0; i < outputCount; ++i) {
        TransactionOutput out;
        out.amount = readU64(data, offset);
        out.scriptPubKey = readVarBytes(data, offset);
        tx.outputs.push_back(out);
    }

    // witnesses
    if (hasWitness) {
        for (uint64_t i = 0; i < inputCount; ++i) {
            Witness w;
            uint64_t stackSize = readVarInt(data, offset);
            for (uint64_t j = 0; j < stackSize; ++j) {
                w.stack.push_back(readVarBytes(data, offset));
            }
            tx.witnesses.push_back(w);
        }
    }

    tx.lockTime = readU32(data, offset);

    return tx;
}

std::vector<uint8_t> Transaction::serialize(bool includeWitness) const {
    std::vector<uint8_t> buf;

    bool hasWitness = includeWitness && !witnesses.empty();

    // version
    writeU32(buf, version);

    // SegWit marker + flag
    if (hasWitness) {
        buf.push_back(0x00);
        buf.push_back(0x01);
    }

    // inputs
    writeVarInt(buf, inputs.size());
    for (const auto& in : inputs) {
        buf.insert(buf.end(), in.prevTxid.begin(), in.prevTxid.end());
        writeU32(buf, in.prevIndex);
        writeVarBytes(buf, in.scriptSig);
        writeU32(buf, in.sequence);
    }

    // outputs
    writeVarInt(buf, outputs.size());
    for (const auto& out : outputs) {
        writeU64(buf, out.amount);
        writeVarBytes(buf, out.scriptPubKey);
    }

    // witnesses
    if (hasWitness) {
        for (const auto& w : witnesses) {
            writeVarInt(buf, w.stack.size());
            for (const auto& item : w.stack)
                writeVarBytes(buf, item);
        }
    }

    // locktime
    writeU32(buf, lockTime);

    return buf;
}

// ============================================================================
// TXID / WTXID (SegWit rule)
// ============================================================================
//
// txid  = hash(serialization WITHOUT witness)
// wtxid = hash(serialization WITH witness)
//

Hash256 Transaction::txid() const {
    return doubleSha256(serialize(false));
}

Hash256 Transaction::wtxid() const {
    return doubleSha256(serialize(true));
}

// ============================================================================
// Legacy SIGHASH (Pre-SegWit, SIGHASH_ALL)
// ============================================================================

Hash256 Transaction::legacySignatureHash(size_t inputIndex) const {
    Transaction tmp = *this;

    for (size_t i = 0; i < tmp.inputs.size(); ++i) {
        tmp.inputs[i].scriptSig.clear();
    }

    std::vector<uint8_t> data = tmp.serialize(false);

    uint32_t sighashType = 1; // SIGHASH_ALL
    writeU32(data, sighashType);

    return doubleSha256(data);
}

bool Transaction::signInput(
    size_t inputIndex,
    const PrivateKey& privKey,
    const PublicKey& pubKey
) {
    if (inputIndex >= inputs.size())
        return false;

    Hash256 h = legacySignatureHash(inputIndex);
    std::vector<uint8_t> sig;

    if (!signECDSA(h, privKey, sig))
        return false;

    sig.push_back(0x01); // SIGHASH_ALL

    inputs[inputIndex].scriptSig = sig;
    inputs[inputIndex].pubKey = pubKey;
    return true;
}

bool Transaction::verifyInput(size_t inputIndex) const {
    if (inputIndex >= inputs.size())
        return false;

    const auto& in = inputs[inputIndex];
    if (in.scriptSig.empty() || in.pubKey.empty())
        return false;

    std::vector<uint8_t> sig = in.scriptSig;
    sig.pop_back(); // remove sighash byte

    Hash256 h = legacySignatureHash(inputIndex);
    return verifyECDSA(h, sig, in.pubKey);
}

// ============================================================================
// Helpers
// ============================================================================

bool Transaction::isCoinbase() const {
    return inputs.size() == 1 &&
           std::all_of(inputs[0].prevTxid.begin(),
                       inputs[0].prevTxid.end(),
                       [](uint8_t b) { return b == 0; });
}

uint64_t Transaction::totalOutput() const {
    uint64_t sum = 0;
    for (const auto& out : outputs) sum += out.amount;
    return sum;
}

bool Transaction::verify() const {
    for (size_t i = 0; i < inputs.size(); ++i) {
        if (!verifyInput(i)) return false;
    }
    return true;
}

} // namespace Bucks
