#ifndef BUCKS_TEST_TX_VECTOR_H
#define BUCKS_TEST_TX_VECTOR_H

// Test vector V-TX-1 from docs/design/consensus-validation.md section 5.4.
// Test keys only (derived from public strings); never use them for real coins.

#include <string>
#include <vector>

#include "crypto.h"
#include "script.h"
#include "sighash.h"
#include "transaction.h"

namespace TxVector {

inline std::string fullTxHex() {
    return "0200000002b5bc37193893ea8bd44b15fa740ef4a1b55178c9bd4311346a14767a8d95231e"
           "010000006a47304402202501be07e71d53496ba8eaf91eb75647247b9b1a6fab4c6e40902e239c79d05b"
           "02207ae641960c78565d17ca9257e5337303b3ac56789eea6fe51718a117b386620e0121022234063b51"
           "87e027c46ef631d200ac1ee5d43d75b8829a5048ff8983016579cfffffffffb8081d4d2e3b6439ca8ecf"
           "94856e0a6226ba328e9df210e08d4a41a8ac1bede6000000006a47304402206bbdb2e3d4e972aad2e7b6"
           "f2a436d656f03bef746ac8e18afd39d0c6c98f861d02203f106868c0f5482226cb3d0a09c004d7919087"
           "22d2ca37e62e024312a0cf45f20121022234063b5187e027c46ef631d200ac1ee5d43d75b8829a5048ff"
           "8983016579cffeffffff02008c8647000000001976a914e3a42ed3976ddeecae5a126beee85549f1b112"
           "5788ac0000000000000000076a056275636b7300000000";
}

inline Bucks::Hash256 hash256FromHex(const std::string& hex) {
    auto v = Bucks::parseHex(hex);
    Bucks::Hash256 h;
    std::copy(v.begin(), v.end(), h.begin());
    return h;
}

inline Bucks::Hash256 regtestGenesis() {
    return hash256FromHex("3491115920c29000d16f7d776bbab4a3399e5065abeeaf73fa23a273406bf520");
}

inline Bucks::PrivateKey privKey(int n) {
    std::string s = "bucks-test-key-" + std::to_string(n);
    return Bucks::sha256(std::vector<uint8_t>(s.begin(), s.end()));
}

inline Bucks::PublicKey pubKey(int n) {
    Bucks::PublicKey p;
    Bucks::derivePublicKey(privKey(n), p);
    return p;
}

// The coins spent by the two inputs: 10 BUCKS and 2.5 BUCKS, both to key 1.
inline std::vector<Bucks::SpentOutput> spentOutputs() {
    Bucks::Script s = Bucks::makeP2PKH(Bucks::hash160(pubKey(1)));
    return {Bucks::SpentOutput{1000000000, s}, Bucks::SpentOutput{250000000, s}};
}

} // namespace TxVector

#endif
