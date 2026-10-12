#include "script.h"

#include <algorithm>

#include "consensus.h"

namespace Bucks {

Script makeP2PKH(const Hash160& keyHash) {
    Script s;
    s.reserve(P2PKH_SCRIPT_SIZE);
    s.push_back(OP_DUP);
    s.push_back(OP_HASH160);
    s.push_back(0x14);
    s.insert(s.end(), keyHash.begin(), keyHash.end());
    s.push_back(OP_EQUALVERIFY);
    s.push_back(OP_CHECKSIG);
    return s;
}

bool isP2PKH(const Script& s, Hash160* keyHashOut) {
    if (s.size() != P2PKH_SCRIPT_SIZE || s[0] != OP_DUP || s[1] != OP_HASH160 ||
        s[2] != 0x14 || s[23] != OP_EQUALVERIFY || s[24] != OP_CHECKSIG)
        return false;
    if (keyHashOut) std::copy(s.begin() + 3, s.begin() + 23, keyHashOut->begin());
    return true;
}

bool makeNullData(const std::vector<uint8_t>& payload, Script& out) {
    const size_t n = payload.size();
    if (n > Consensus::MAX_NULLDATA_PAYLOAD) return false;
    out.clear();
    out.push_back(OP_RETURN);
    if (n == 0) return true;
    if (n <= 75) {
        out.push_back(uint8_t(n));
    } else if (n <= 255) {
        out.push_back(OP_PUSHDATA1);
        out.push_back(uint8_t(n));
    } else {
        out.push_back(OP_PUSHDATA2);
        out.push_back(uint8_t(n & 0xFF));
        out.push_back(uint8_t(n >> 8));
    }
    out.insert(out.end(), payload.begin(), payload.end());
    return true;
}

bool parseNullData(const Script& s, std::vector<uint8_t>* payloadOut) {
    if (s.empty() || s[0] != OP_RETURN) return false;
    size_t n, header;
    if (s.size() == 1) {
        n = 0;
        header = 1;
    } else if (s[1] >= 1 && s[1] <= 75) {
        n = s[1];
        header = 2;
    } else if (s[1] == OP_PUSHDATA1) {
        if (s.size() < 3) return false;
        n = s[2];
        header = 3;
        if (n < 76) return false;  // not minimal
    } else if (s[1] == OP_PUSHDATA2) {
        if (s.size() < 4) return false;
        n = size_t(s[2]) | (size_t(s[3]) << 8);
        header = 4;
        if (n < 256) return false;  // not minimal
    } else {
        return false;
    }
    if (n > Consensus::MAX_NULLDATA_PAYLOAD) return false;
    if (s.size() != header + n) return false;
    if (payloadOut) payloadOut->assign(s.begin() + header, s.end());
    return true;
}

bool isStandardOutputScript(const Script& s) {
    return isP2PKH(s) || parseNullData(s);
}

bool makeP2PKHUnlock(const std::vector<uint8_t>& derSig,
                     const CompressedPubKey& pubKey, Script& out) {
    if (derSig.size() < UNLOCK_DER_MIN || derSig.size() > UNLOCK_DER_MAX) return false;
    out.clear();
    out.push_back(uint8_t(derSig.size() + 1));
    out.insert(out.end(), derSig.begin(), derSig.end());
    out.push_back(Consensus::SIGHASH_ALL);
    out.push_back(0x21);
    out.insert(out.end(), pubKey.begin(), pubKey.end());
    return true;
}

bool parseP2PKHUnlock(const Script& s, std::vector<uint8_t>& derSigOut,
                      CompressedPubKey& pubKeyOut) {
    if (s.empty()) return false;
    const size_t L = s[0];
    if (L < UNLOCK_DER_MIN + 1 || L > UNLOCK_DER_MAX + 1) return false;
    if (s.size() != L + 35) return false;
    if (s[L] != Consensus::SIGHASH_ALL) return false;
    if (s[L + 1] != 0x21) return false;
    if (s[L + 2] != 0x02 && s[L + 2] != 0x03) return false;
    derSigOut.assign(s.begin() + 1, s.begin() + L);
    std::copy(s.begin() + L + 2, s.end(), pubKeyOut.begin());
    return true;
}

} // namespace Bucks
