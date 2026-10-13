#include "fuzz_common.h"
#include "chainparams.h"
#include "policy.h"
#include "serialize.h"
#include "transaction.h"
#include "validation_rules.h"

using namespace Bucks;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::vector<uint8_t> in = Fuzz::bytes(data, size);
    try {
        const Transaction tx = Transaction::deserialize(in);
        if (tx.serialize() != in) Fuzz::fail("transaction: accepted a non-canonical encoding");
        // The stateless rules and policy must cope with anything the parser accepts.
        ValidationState st;
        if (Rules::CheckTransaction(tx, st)) {
            (void)Rules::TxSigOps(tx);
            (void)Rules::IsFinal(tx, 1000, 1'700'000'000);
            ValidationState pol;
            (void)Policy::IsStandardTx(tx, pol);
        }
        (void)tx.txid();
        (void)tx.isCoinbase();
        (void)tx.totalOutput();
    } catch (const ParseError&) {
    }
    return 0;
}
