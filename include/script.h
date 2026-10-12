#ifndef SCRIPT_H
#define SCRIPT_H

#include <array>
#include <cstdint>
#include <vector>

namespace Bucks {

using Script = std::vector<uint8_t>;
using Hash160 = std::array<uint8_t, 20>;
using CompressedPubKey = std::array<uint8_t, 33>;

enum OpCode : uint8_t {
    OP_DUP         = 0x76,
    OP_HASH160     = 0xa9,
    OP_EQUALVERIFY = 0x88,
    OP_CHECKSIG    = 0xac,
    OP_RETURN      = 0x6a,
    OP_PUSHDATA1   = 0x4c,
    OP_PUSHDATA2   = 0x4d
};

// There is no script engine: exactly two output types exist (P2PKH and
// NULL_DATA) and one input layout (P2PKH unlock). See
// docs/design/consensus-validation.md section 4.2.

constexpr size_t P2PKH_SCRIPT_SIZE = 25;
constexpr size_t UNLOCK_DER_MIN = 8;    // L = DER length + 1, 9 <= L <= 73
constexpr size_t UNLOCK_DER_MAX = 72;

// 76 a9 14 <hash160> 88 ac
Script makeP2PKH(const Hash160& keyHash);
bool isP2PKH(const Script& s, Hash160* keyHashOut = nullptr);

// Canonical OP_RETURN + one minimal push. Returns false if the payload is
// longer than Consensus::MAX_NULLDATA_PAYLOAD.
bool makeNullData(const std::vector<uint8_t>& payload, Script& out);
// Accepts only the single canonical encoding for the payload length.
bool parseNullData(const Script& s, std::vector<uint8_t>* payloadOut = nullptr);

// True for exactly the two allowed output types.
bool isStandardOutputScript(const Script& s);

// <L> <DER sig> 01 21 <33-byte compressed pubkey>. The sighash type byte is
// always SIGHASH_ALL. Returns false if the DER length is out of range.
bool makeP2PKHUnlock(const std::vector<uint8_t>& derSig,
                     const CompressedPubKey& pubKey, Script& out);
// Strict layout check; DER and key validity are checked by the crypto layer.
bool parseP2PKHUnlock(const Script& s, std::vector<uint8_t>& derSigOut,
                      CompressedPubKey& pubKeyOut);

} // namespace Bucks

#endif
