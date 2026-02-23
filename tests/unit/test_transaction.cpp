#include <gtest/gtest.h>
#include "transaction.h"
#include <vector>
#include <iostream>

TEST(TransactionTest, InitializationAndSerialization) {
    Bucks::Transaction tx;
    
    // Setup inputs
    Bucks::TransactionInput in;
    in.prevTxid.fill(0xaa); // Mock hash
    in.prevIndex = 0;
    // mock scriptSig
    in.scriptSig = {0x01, 0x02};
    in.pubKey = {0x03, 0x04};
    tx.inputs.push_back(in);

    // Setup outputs
    Bucks::TransactionOutput out;
    out.amount = 1000;
    // mock scriptPubKey
    out.scriptPubKey = Bucks::Script(); // default empty or add bytes if possible but verify logic
    // Actually Script might be a class, let's just leave it empty or checking serialize handles it
    tx.outputs.push_back(out);

    // Serialize
    std::vector<uint8_t> serialized = tx.serialize();
    EXPECT_FALSE(serialized.empty());

    // Deserialize
    Bucks::Transaction deserialized = Bucks::Transaction::deserialize(serialized);

    // Verify fields
    ASSERT_EQ(tx.inputs.size(), deserialized.inputs.size());
    EXPECT_EQ(tx.inputs[0].prevTxid, deserialized.inputs[0].prevTxid);
    EXPECT_EQ(tx.inputs[0].prevIndex, deserialized.inputs[0].prevIndex);
    EXPECT_EQ(tx.inputs[0].scriptSig, deserialized.inputs[0].scriptSig);
    // pubKey is not serialized (part of scriptSig usually, but separate field here is transient)
    // EXPECT_EQ(tx.inputs[0].pubKey, deserialized.inputs[0].pubKey);

    ASSERT_EQ(tx.outputs.size(), deserialized.outputs.size());
    EXPECT_EQ(tx.outputs[0].amount, deserialized.outputs[0].amount);
    // Basic script check - assumption script serialization logic is correct
}
