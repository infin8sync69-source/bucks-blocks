#include "sighash.h"

#include <stdexcept>

#include "consensus.h"
#include "serialize.h"

namespace Bucks {

Hash256 taggedHash(const std::string& tag, const std::vector<uint8_t>& msg) {
    Hash256 t = sha256(std::vector<uint8_t>(tag.begin(), tag.end()));
    std::vector<uint8_t> buf;
    buf.reserve(64 + msg.size());
    buf.insert(buf.end(), t.begin(), t.end());
    buf.insert(buf.end(), t.begin(), t.end());
    buf.insert(buf.end(), msg.begin(), msg.end());
    return sha256(buf);
}

SigHashCache::SigHashCache(const Transaction& tx, const std::vector<SpentOutput>& spent)
    : version_(tx.version), lockTime_(tx.lockTime), inputCount_(tx.inputs.size()) {
    if (spent.size() != tx.inputs.size())
        throw std::invalid_argument("SigHashCache: one spent output per input required");

    Writer prevouts, amounts, scripts, sequences, outputs;
    for (size_t k = 0; k < tx.inputs.size(); ++k) {
        prevouts.hash256(tx.inputs[k].prevTxid).u32(tx.inputs[k].prevIndex);
        amounts.u64(spent[k].amount);
        scripts.varBytes(spent[k].scriptPubKey);
        sequences.u32(tx.inputs[k].sequence);
    }
    for (const auto& out : tx.outputs)
        outputs.u64(out.amount).varBytes(out.scriptPubKey);

    shaPrevouts_ = sha256(prevouts.data());
    shaAmounts_ = sha256(amounts.data());
    shaScriptPubKeys_ = sha256(scripts.data());
    shaSequences_ = sha256(sequences.data());
    shaOutputs_ = sha256(outputs.data());
}

Hash256 SigHashCache::sigHash(uint32_t inputIndex, const Hash256& genesisHash) const {
    if (inputIndex >= inputCount_) throw std::out_of_range("sigHash: input index");
    Writer w;
    w.hash256(genesisHash)
        .u8(Consensus::SIGHASH_ALL)
        .u32(version_)
        .u32(lockTime_)
        .hash256(shaPrevouts_)
        .hash256(shaAmounts_)
        .hash256(shaScriptPubKeys_)
        .hash256(shaSequences_)
        .hash256(shaOutputs_)
        .u32(inputIndex);  // 205 bytes
    return taggedHash("Bucks/SigHash/v1", w.data());
}

const char* spendErrorCode(SpendError e) {
    switch (e) {
        case SpendError::OK: return "";
        case SpendError::BAD_SPENT_SCRIPT: return "bad-spent-script";
        case SpendError::BAD_UNLOCK_FORMAT: return "bad-scriptsig-format";
        case SpendError::BAD_PUBKEY: return "bad-pubkey";
        case SpendError::BAD_PUBKEY_HASH: return "bad-pubkey-hash";
        case SpendError::BAD_SIGNATURE: return "bad-sig";
    }
    return "bad-sig";
}

bool signP2PKHInput(Transaction& tx, uint32_t i, const std::vector<SpentOutput>& spent,
                    const PrivateKey& key, const Hash256& genesisHash) {
    if (i >= tx.inputs.size() || spent.size() != tx.inputs.size()) return false;

    PublicKey pub;
    if (!derivePublicKey(key, pub) || pub.size() != 33) return false;

    Hash160 owner;
    if (!isP2PKH(spent[i].scriptPubKey, &owner) || owner != hash160(pub)) return false;

    // The signature commits to every output and to all spent data, but not to
    // the scriptSigs, so inputs can be signed in any order.
    SigHashCache cache(tx, spent);
    std::vector<uint8_t> der;
    if (!signECDSA(cache.sigHash(i, genesisHash), key, der)) return false;

    CompressedPubKey cpk;
    std::copy(pub.begin(), pub.end(), cpk.begin());
    Script unlock;
    if (!makeP2PKHUnlock(der, cpk, unlock)) return false;
    tx.inputs[i].scriptSig = std::move(unlock);
    return true;
}

SpendError verifyP2PKHInput(const Transaction& tx, uint32_t i, const SigHashCache& cache,
                            const SpentOutput& spent, const Hash256& genesisHash) {
    if (i >= tx.inputs.size()) return SpendError::BAD_UNLOCK_FORMAT;

    Hash160 owner;
    if (!isP2PKH(spent.scriptPubKey, &owner)) return SpendError::BAD_SPENT_SCRIPT;

    std::vector<uint8_t> der;
    CompressedPubKey cpk;
    if (!parseP2PKHUnlock(tx.inputs[i].scriptSig, der, cpk)) return SpendError::BAD_UNLOCK_FORMAT;

    PublicKey pub(cpk.begin(), cpk.end());
    if (!isValidPublicKey(pub)) return SpendError::BAD_PUBKEY;
    if (hash160(pub) != owner) return SpendError::BAD_PUBKEY_HASH;

    if (!verifyECDSA(cache.sigHash(i, genesisHash), der, pub)) return SpendError::BAD_SIGNATURE;
    return SpendError::OK;
}

} // namespace Bucks
