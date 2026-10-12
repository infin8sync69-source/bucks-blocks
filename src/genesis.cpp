#include "genesis.h"

#include "consensus.h"
#include "pow.h"
#include "script.h"

namespace Bucks {
namespace Genesis {

Block build(const ChainParams& params) {
    TransactionInput in;
    in.prevTxid.fill(0);
    in.prevIndex = 0xFFFFFFFF;
    in.sequence = Consensus::SEQUENCE_FINAL;
    in.scriptSig = {0, 0, 0, 0};  // LE32(0): the height
    in.scriptSig.insert(in.scriptSig.end(), params.genesisInscription.begin(),
                        params.genesisInscription.end());

    Transaction coinbase;
    coinbase.inputs.push_back(in);
    Script nullData;
    makeNullData({}, nullData);  // empty payload: a single 0x6a
    coinbase.outputs.emplace_back(0, nullData);

    BlockHeader header;
    header.version = Consensus::BLOCK_VERSION;
    header.height = 0;
    header.prevBlockHash.fill(0);
    header.timestamp = params.genesisTime;
    header.bits = params.powLimitBits;
    header.nonce = params.genesisNonce;

    Block genesis(header, {coinbase});
    genesis.header.merkleRoot = genesis.computeMerkleRoot();
    return genesis;
}

static std::string hex(const Hash256& h) {
    return hexStr(std::vector<uint8_t>(h.begin(), h.end()));
}

bool check(const ChainParams& params, std::string& err) {
    if (!params.genesisConfigured()) {
        err = std::string(params.name) + " genesis is not configured in this build";
        return false;
    }
    Block g = build(params);
    if (g.header.merkleRoot != params.genesisMerkleRoot) {
        err = std::string("genesis merkle mismatch on ") + params.name + ": expected " +
              hex(params.genesisMerkleRoot) + ", computed " + hex(g.header.merkleRoot);
        return false;
    }
    Hash256 hash = g.getHash();
    if (hash != params.genesisHash) {
        err = std::string("genesis mismatch on ") + params.name + ": expected " +
              hex(params.genesisHash) + ", computed " + hex(hash);
        return false;
    }
    if (!Pow::checkProofOfWork(hash, g.header.bits, params.powLimit)) {
        err = std::string("genesis does not satisfy its proof of work on ") + params.name;
        return false;
    }
    return true;
}

} // namespace Genesis
} // namespace Bucks
