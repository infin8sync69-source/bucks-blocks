#include <gtest/gtest.h>

#include "consensus.h"
#include "sighash.h"
#include "tx_vector.h"

using namespace Bucks;
using namespace TxVector;

namespace {

Transaction vectorTx() { return Transaction::deserialize(parseHex(fullTxHex())); }

std::string hex(const Hash256& h) { return hexStr(std::vector<uint8_t>(h.begin(), h.end())); }

SpendError verify(const Transaction& tx, uint32_t i, const std::vector<SpentOutput>& spent,
                  const Hash256& genesis) {
    SigHashCache cache(tx, spent);
    return verifyP2PKHInput(tx, i, cache, spent[i], genesis);
}

} // namespace

TEST(Sighash, TagConstantAndKeys) {
    std::string tag = "Bucks/SigHash/v1";
    EXPECT_EQ(hex(sha256(std::vector<uint8_t>(tag.begin(), tag.end()))),
              "08072aea4347c380bf052ff17e2ea8ffd5d3310ef262d660be6c9a4f3ee1213f");
    PrivateKey k1 = privKey(1);
    EXPECT_EQ(hexStr(std::vector<uint8_t>(k1.begin(), k1.end())),
              "a4647b6f567b1eaf074395801d6a55eb2f879f7db66e168bf6047f0a30787b67");
    EXPECT_EQ(hexStr(pubKey(1)),
              "022234063b5187e027c46ef631d200ac1ee5d43d75b8829a5048ff8983016579cf");
    EXPECT_EQ(hexStr(pubKey(2)),
              "02c4af81bcbf6616ba60679b7afc46be28a54e83fcef067ff91a6b36dfced2fc4d");
}

TEST(Sighash, Vector) {
    // SIG-1
    Transaction tx = vectorTx();
    SigHashCache cache(tx, spentOutputs());
    EXPECT_EQ(hex(cache.sigHash(0, regtestGenesis())),
              "249b5cb1729c49206fa542cb31ba35436357cb29b2e598bd522b22c8cc66242e");
    EXPECT_EQ(hex(cache.sigHash(1, regtestGenesis())),
              "a57df31365c35160c1cdff343169bd8278b128297df8ca44319d859c978e53db");
    EXPECT_EQ(hex(cache.sigHash(0, Hash256{})),
              "1294ce50e0bc49400af27097cfa899499f1fb11dcd1689206bdbc74142952f3e");
    auto spent = spentOutputs();
    spent[0].amount += 1;
    EXPECT_EQ(hex(SigHashCache(tx, spent).sigHash(0, regtestGenesis())),
              "8ed3413f3b0362bc54e2dba1e572f26882e3d4fa1405bc248b81d38c1913e8f1");
    EXPECT_THROW(cache.sigHash(2, regtestGenesis()), std::out_of_range);
    EXPECT_THROW(SigHashCache(tx, {spentOutputs()[0]}), std::invalid_argument);
}

TEST(Sighash, VectorSignaturesVerify) {
    // SIG-2
    Transaction tx = vectorTx();
    auto spent = spentOutputs();
    EXPECT_EQ(verify(tx, 0, spent, regtestGenesis()), SpendError::OK);
    EXPECT_EQ(verify(tx, 1, spent, regtestGenesis()), SpendError::OK);
}

TEST(Sighash, CommitsToEverything) {
    // SIG-3: any change to what the signature covers breaks input 0.
    const Hash256 g = regtestGenesis();
    const Transaction base = vectorTx();
    const auto baseSpent = spentOutputs();

    auto expectBad = [&](const char* what, const Transaction& tx,
                         const std::vector<SpentOutput>& spent, const Hash256& genesis) {
        EXPECT_EQ(verify(tx, 0, spent, genesis), SpendError::BAD_SIGNATURE) << what;
    };
    {
        auto s = baseSpent; s[0].amount += 1;
        expectBad("spent amount", base, s, g);
    }
    {
        auto s = baseSpent; s[1].amount += 1;
        expectBad("other input's amount", base, s, g);
    }
    {
        auto s = baseSpent; s[1].scriptPubKey = makeP2PKH(hash160(pubKey(2)));
        expectBad("other input's spent script", base, s, g);
    }
    {
        Transaction t = base; t.inputs[1].prevIndex ^= 1;
        expectBad("prevout", t, baseSpent, g);
    }
    {
        Transaction t = base; t.inputs[1].sequence = 0xffffffff;
        expectBad("sequence", t, baseSpent, g);
    }
    {
        Transaction t = base; t.outputs[0].amount -= 1;
        expectBad("output amount", t, baseSpent, g);
    }
    {
        Transaction t = base; t.outputs[0].scriptPubKey = makeP2PKH(hash160(pubKey(1)));
        expectBad("output script", t, baseSpent, g);
    }
    {
        Transaction t = base; t.version = 3;
        expectBad("version", t, baseSpent, g);
    }
    {
        Transaction t = base; t.lockTime = 1;
        expectBad("lockTime", t, baseSpent, g);
    }
    expectBad("genesis hash", base, baseSpent, Hash256{});

    // Each input has its own digest (the old digest ignored the index).
    SigHashCache cache(base, baseSpent);
    EXPECT_NE(cache.sigHash(0, g), cache.sigHash(1, g));
}

TEST(Sighash, SignerRoundTrip) {
    Transaction tx = vectorTx();
    for (auto& in : tx.inputs) in.scriptSig.clear();
    auto spent = spentOutputs();
    const Hash256 g = regtestGenesis();
    // Signatures use a random nonce, so bytes differ from the vector but verify.
    for (uint32_t i = 0; i < 2; ++i)
        ASSERT_TRUE(signP2PKHInput(tx, i, spent, privKey(1), g));
    EXPECT_EQ(verify(tx, 0, spent, g), SpendError::OK);
    EXPECT_EQ(verify(tx, 1, spent, g), SpendError::OK);
    EXPECT_EQ(Transaction::deserialize(tx.serialize()).txid(), tx.txid());

    // A key that does not own the coin cannot sign it.
    Transaction other = vectorTx();
    EXPECT_FALSE(signP2PKHInput(other, 0, spent, privKey(2), g));
    EXPECT_FALSE(signP2PKHInput(other, 5, spent, privKey(1), g));
    EXPECT_FALSE(signP2PKHInput(other, 0, {spent[0]}, privKey(1), g));
}

TEST(Sighash, WrongKey) {
    // SIG-5: signature by key 2 with key 1's pubkey in the scriptSig.
    Transaction tx = vectorTx();
    auto spent = spentOutputs();
    SigHashCache cache(tx, spent);
    std::vector<uint8_t> der;
    ASSERT_TRUE(signECDSA(cache.sigHash(0, regtestGenesis()), privKey(2), der));
    CompressedPubKey k1;
    PublicKey p1 = pubKey(1);
    std::copy(p1.begin(), p1.end(), k1.begin());
    ASSERT_TRUE(makeP2PKHUnlock(der, k1, tx.inputs[0].scriptSig));
    EXPECT_EQ(verify(tx, 0, spent, regtestGenesis()), SpendError::BAD_SIGNATURE);
}

TEST(Sighash, WrongOwner) {
    // SIG-6: key 2 with a valid signature of its own for a coin owned by key 1.
    Transaction tx = vectorTx();
    auto spent = spentOutputs();
    SigHashCache cache(tx, spent);
    std::vector<uint8_t> der;
    ASSERT_TRUE(signECDSA(cache.sigHash(0, regtestGenesis()), privKey(2), der));
    CompressedPubKey k2;
    PublicKey p2 = pubKey(2);
    std::copy(p2.begin(), p2.end(), k2.begin());
    ASSERT_TRUE(makeP2PKHUnlock(der, k2, tx.inputs[0].scriptSig));
    EXPECT_EQ(verify(tx, 0, spent, regtestGenesis()), SpendError::BAD_PUBKEY_HASH);
}

TEST(Sighash, CrossNetworkReplay) {
    // SIG-7: the regtest signatures are worthless with another genesis hash.
    Transaction tx = vectorTx();
    Hash256 other = regtestGenesis();
    other[0] ^= 1;
    EXPECT_EQ(verify(tx, 0, spentOutputs(), other), SpendError::BAD_SIGNATURE);
}

TEST(Sighash, OffCurveKeyRejected) {
    // SIG-8 (verification level): a 33-byte key that is not on the curve.
    Transaction tx = vectorTx();
    auto spent = spentOutputs();
    CompressedPubKey bad{};
    bad[0] = 0x02;
    for (size_t i = 1; i < 33; ++i) bad[i] = 0xFF;  // x >= p
    std::vector<uint8_t> der(tx.inputs[0].scriptSig.begin() + 1, tx.inputs[0].scriptSig.begin() + 71);
    ASSERT_TRUE(makeP2PKHUnlock(der, bad, tx.inputs[0].scriptSig));
    EXPECT_EQ(verify(tx, 0, spent, regtestGenesis()), SpendError::BAD_PUBKEY);
}

TEST(Sighash, MalleationRejected) {
    // SIG-4: every third-party variation of the unlock script is rejected.
    const Transaction base = vectorTx();
    auto spent = spentOutputs();
    const Hash256 g = regtestGenesis();
    const Script good = base.inputs[0].scriptSig;
    ASSERT_EQ(good.size(), 106u);  // L = 71, so L + 35

    auto check = [&](const char* what, Script s, SpendError want) {
        Transaction t = base;
        t.inputs[0].scriptSig = std::move(s);
        EXPECT_EQ(verify(t, 0, spent, g), want) << what;
    };
    {
        Script s = good; s.back() ^= 0;  // unchanged control
        check("control", s, SpendError::OK);
    }
    {
        Script s = good; s[good[0]] = 0x02;
        check("sighash type 02", s, SpendError::BAD_UNLOCK_FORMAT);
    }
    {
        Script s = good; s[good[0]] = 0x81;
        check("sighash type 81", s, SpendError::BAD_UNLOCK_FORMAT);
    }
    {
        Script s = good; s.push_back(0);
        check("extra byte", s, SpendError::BAD_UNLOCK_FORMAT);
    }
    {
        // 65-byte (uncompressed-length) key push.
        Script s(good.begin(), good.begin() + good[0] + 1);
        s.push_back(0x41);
        s.insert(s.end(), 65, 0x04);
        check("65-byte pubkey", s, SpendError::BAD_UNLOCK_FORMAT);
    }
    {
        // DER with an extra leading zero in r: a different encoding of the same r,s.
        Script s = good;
        std::vector<uint8_t> der(good.begin() + 1, good.begin() + good[0]);
        ASSERT_EQ(der[0], 0x30);
        der[1] += 1;
        der.insert(der.begin() + 4, 0x00);
        der[3] += 1;
        CompressedPubKey k;
        std::copy(good.end() - 33, good.end(), k.begin());
        ASSERT_TRUE(makeP2PKHUnlock(der, k, s));
        check("non-canonical DER", s, SpendError::BAD_SIGNATURE);
    }
}
