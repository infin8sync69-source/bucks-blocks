#include <gtest/gtest.h>

#include <algorithm>
#include <random>

#include "chaindb.h"
#include "chainstate.h"
#include "consensus.h"
#include "pow.h"
#include "test_chain.h"

using namespace Bucks;
using namespace Bucks::TestUtil;

// Spec: docs/design/consensus-validation.md section 15.4 (CS-1 ... CS-14, MRK-2).
// CS-4 (restart from disk) needs the SQLite store (plan step 15).

namespace {

ChainParams regtestWithDepth(uint32_t depth) {
    ChainParams p = chainParams(NetworkId::REGTEST);
    p.maxAutoReorgDepth = depth;
    return p;
}

// Extends `from` by `count` blocks, each with its own coinbase tag, and
// returns them in order. Blocks are submitted and must be accepted.
std::vector<Block> branch(TestChain& t, const BlockIndex* from, int count, uint8_t tag) {
    std::vector<Block> out;
    const BlockIndex* parent = from;
    for (int i = 0; i < count; ++i) {
        Block b = t.build(parent, {}, tag);
        ValidationState st = t.submit(b);
        EXPECT_TRUE(st.valid()) << st.code;
        out.push_back(b);
        parent = t.state().find(b.getHash());
    }
    return out;
}

// Reads the block of the active chain at `h` back from the store.
Block mainBlock(TestChain& t, uint32_t h) {
    std::vector<uint8_t> bytes;
    EXPECT_TRUE(t.store().getBlock(t.state().activeChain()[h]->hash, bytes));
    return Block::deserialize(bytes);
}

} // namespace

// CS-1: a heavier branch wins, and the coins match the branch.
TEST(ChainState, HeavierBranchWins) {
    TestChain t;
    ASSERT_TRUE(t.ok());
    branch(t, t.state().tip(), 5, 0);  // main chain to height 5
    const BlockIndex* fork = t.state().activeChain()[3];
    ASSERT_NE(fork, nullptr);

    const Transaction replaced = mainBlock(t, 5).transactions[0];  // read before the reorg
    std::vector<Block> side = branch(t, fork, 3, 1);  // heights 4..6 on the side
    EXPECT_EQ(t.state().tip()->hash, side.back().getHash());
    EXPECT_EQ(t.state().tip()->height, 6u);

    // Coins of the winning branch are in the set; the replaced height-5 main
    // coinbase is not.
    EXPECT_TRUE(t.state().coins().haveCoin(OutPoint{side.back().transactions[0].txid(), 0}));
    EXPECT_TRUE(t.state().coins().haveCoin(OutPoint{side[1].transactions[0].txid(), 0}));
    EXPECT_FALSE(t.state().coins().haveCoin(OutPoint{replaced.txid(), 0}));
}

// CS-2: equal work keeps the first tip seen.
TEST(ChainState, EqualWorkKeepsFirst) {
    TestChain t;
    branch(t, t.state().tip(), 5, 0);
    const BlockIndex* tip5 = t.state().tip();
    Block a = t.build(tip5, {}, 1);
    Block b = t.build(tip5, {}, 2);
    ASSERT_TRUE(t.submit(a).valid());
    ASSERT_TRUE(t.submit(b).valid());
    EXPECT_EQ(t.state().tip()->hash, a.getHash());
}

// CS-3: headers with no valid proof of work are rejected at stage A, and
// nothing is stored.
TEST(ChainState, ZeroPowAttack) {
    TestChain t;
    branch(t, t.state().tip(), 5, 0);
    const BlockIndex* tip = t.state().tip();
    const Hash256 tipHash = tip->hash;

    // B1: parent is one block below the tip, nonce chosen to fail the target.
    Block b1 = t.build(tip->prev, {}, 7);
    for (uint32_t n = 0;; ++n) {
        b1.header.nonce = n;
        if (!Pow::checkProofOfWork(b1.header.hash(), b1.header.bits, t.params().powLimit)) break;
    }
    // B2: tiny target (hard bits) and an unlimited coinbase.
    Block b2 = t.build(tip, {}, 8, 600, Consensus::MAX_MONEY);
    b2.header.bits = 0x1c00ffff;
    b2.header.nonce = 0;

    ValidationState s1 = t.submit(b1);
    ValidationState s2 = t.submit(b2);
    EXPECT_EQ(s1.result, ValidationResult::INVALID);
    EXPECT_EQ(s1.code, "high-hash");
    EXPECT_EQ(s2.result, ValidationResult::INVALID);
    EXPECT_EQ(s2.code, "high-hash");
    EXPECT_EQ(t.state().find(b1.getHash()), nullptr);
    EXPECT_EQ(t.state().find(b2.getHash()), nullptr);
    EXPECT_EQ(t.state().tip()->hash, tipHash);
}

// CS-5: a branch with an invalid middle block. The middle block and its child
// are marked; the tip does not move; the UTXO statistics do not change.
TEST(ChainState, InvalidBlockInBranch) {
    TestChain t;
    branch(t, t.state().tip(), 4, 0);  // main chain to height 4
    const BlockIndex* fork = t.state().activeChain()[3];
    const UtxoStats before = t.store().mutableCoins().stats();
    const Hash256 tipHash = t.state().tip()->hash;

    Block b4 = t.build(fork, {}, 1);                     // valid, same work as the tip
    ASSERT_TRUE(t.submit(b4).valid());
    Block b5 = t.build(t.state().find(b4.getHash()), {}, 1, 600, Rules::Subsidy(5, t.params()) + 1);
    ValidationState s5 = t.submit(b5);                   // overtakes the tip; subsidy + 1 fails
    EXPECT_EQ(s5.result, ValidationResult::INVALID);
    EXPECT_EQ(s5.code, "bad-cb-amount");

    const BlockIndex* middle = t.state().find(b5.getHash());
    ASSERT_NE(middle, nullptr);
    EXPECT_NE(middle->status & FAILED_VALID, 0u);
    Block b6 = t.build(middle, {}, 1);                   // a child of the invalid block
    EXPECT_EQ(t.submit(b6).code, "bad-prevblk");
    EXPECT_EQ(t.state().find(b6.getHash()), nullptr);    // never stored
    EXPECT_EQ(t.state().tip()->hash, tipHash);
    EXPECT_EQ(t.store().mutableCoins().stats(), before);
}

// CS-6: a commit that fails leaves the old tip and the coins untouched.
TEST(ChainState, ReorgAtomic) {
    TestChain t;
    branch(t, t.state().tip(), 5, 0);
    const BlockIndex* fork = t.state().activeChain()[2];
    const Hash256 tipHash = t.state().tip()->hash;
    const UtxoStats before = t.store().mutableCoins().stats();

    std::vector<Block> side = branch(t, fork, 3, 1);  // heights 3..5, no reorg yet
    t.store().failNextCommit = true;
    Block last = t.build(t.state().find(side.back().getHash()), {}, 1);
    ValidationState st = t.submit(last);  // would reorg 3 blocks: the commit fails
    EXPECT_EQ(st.result, ValidationResult::INTERNAL);
    EXPECT_EQ(t.state().tip()->hash, tipHash);
    EXPECT_EQ(t.store().mutableCoins().stats(), before);
    EXPECT_TRUE(t.state().coins().haveCoin(OutPoint{mainBlock(t, 5).transactions[0].txid(), 0}));
}

// Audit CON-4: a fatal error halts the chain state for good. Nothing more is taken (not even a block that would
// now commit fine), mining stops on every network, and the reason and exit code are there for the node to stop.
TEST(ChainState, FatalErrorHaltsTheChainState) {
    TestChain t;
    branch(t, t.state().tip(), 3, 0);
    const Hash256 tipHash = t.state().tip()->hash;
    EXPECT_EQ(t.state().fatalExitCode(), 0);

    t.store().failNextCommit = true;
    ValidationState st = t.submit(t.build(t.state().tip()));
    EXPECT_EQ(st.result, ValidationResult::INTERNAL);
    EXPECT_EQ(st.code, "fatal-chainstate");
    EXPECT_EQ(t.state().fatalExitCode(), EXIT_STORAGE_FAILURE);
    EXPECT_NE(t.state().fatalError().find("commit"), std::string::npos) << t.state().fatalError();

    const Block other = t.build(t.state().tip(), {}, 1);
    st = t.submit(other);
    EXPECT_EQ(st.result, ValidationResult::INTERNAL);
    EXPECT_EQ(st.code, "chainstate-halted");
    EXPECT_EQ(t.state().ProcessNewBlockHeaders({other.header}).result, ValidationResult::INTERNAL);
    EXPECT_EQ(t.state().find(other.getHash()), nullptr);
    EXPECT_EQ(t.state().tip()->hash, tipHash);
    EXPECT_NE(t.state().miningBlockedReason().find("halted"), std::string::npos);   // regtest too
    std::string err;
    EXPECT_FALSE(t.state().approveReorg(tipHash, err));
    EXPECT_NE(err.find("halted"), std::string::npos) << err;
}

// CS-7: a branch block spends an output created only on the branch.
TEST(ChainState, ForkUsesForkState) {
    TestChain t;
    branch(t, t.state().tip(), 101, 0);  // main chain to 101; coinbase of height 1 is mature
    const BlockIndex* fork = t.state().activeChain()[100];
    const Transaction cb1 = mainBlock(t, 1).transactions[0];

    const uint64_t sub1 = Rules::Subsidy(1, t.params());
    Transaction first = spendTx(OutPoint{cb1.txid(), 0}, sub1, 3, sub1 - 1000, 4, t.params());
    Transaction second = spendTx(OutPoint{first.txid(), 0}, sub1 - 1000, 4, sub1 - 2000, 5, t.params());

    std::vector<Block> side;
    side.push_back(t.build(fork, {first}, 1));
    ASSERT_TRUE(t.submit(side[0]).valid());
    side.push_back(t.build(t.state().find(side[0].getHash()), {second}, 1));
    ValidationState st = t.submit(side[1]);
    EXPECT_TRUE(st.valid()) << st.code;
    EXPECT_EQ(t.state().tip()->hash, side[1].getHash());
    EXPECT_TRUE(t.state().coins().haveCoin(OutPoint{second.txid(), 0}));
}

// CS-8: a transaction confirmed on the disconnected branch reaches the hook.
TEST(ChainState, DisconnectedTxsReturn) {
    TestChain t;
    branch(t, t.state().tip(), 100, 0);
    const BlockIndex* fork = t.state().activeChain()[100];

    // Confirm a spend of the height-1 coinbase at height 101 on the main chain.
    const Transaction cb1 = mainBlock(t, 1).transactions[0];
    const uint64_t sub1 = Rules::Subsidy(1, t.params());
    Transaction spend = spendTx(OutPoint{cb1.txid(), 0}, sub1, 3, sub1 - 1000, 4, t.params());
    Block main101 = t.build(fork, {spend}, 0);
    ASSERT_TRUE(t.submit(main101).valid());
    ASSERT_EQ(t.state().tip()->hash, main101.getHash());

    std::vector<Block> seen;
    t.state().setDisconnectHook([&](const Block& b) { seen.push_back(b); });

    // A side branch without the spend, two blocks deep: it overtakes the tip.
    std::vector<Block> side = branch(t, fork, 2, 9);
    ASSERT_EQ(t.state().tip()->hash, side.back().getHash());
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0].getHash(), main101.getHash());
    bool found = false;
    for (const Transaction& tx : seen[0].transactions) found = found || tx.txid() == spend.txid();
    EXPECT_TRUE(found);  // the caller can put it back in the mempool
}

// CS-9: a reorg deeper than the limit is refused, raises a fork alert, and
// happens after the operator approves it.
TEST(ChainState, DeepReorgRefused) {
    TestChain t(regtestWithDepth(5));
    branch(t, t.state().tip(), 10, 0);  // tip at 10
    const BlockIndex* fork = t.state().activeChain()[4];

    std::vector<Block> side = branch(t, fork, 7, 1);  // heights 5..11, forks 6 deep
    EXPECT_NE(t.state().tip()->hash, side.back().getHash());
    EXPECT_EQ(t.state().tip()->height, 10u);
    EXPECT_TRUE(t.state().forkAlert().active);
    EXPECT_EQ(t.state().forkAlert().forkHeight, 4u);

    std::string err;
    ASSERT_TRUE(t.state().approveReorg(side.back().getHash(), err)) << err;
    EXPECT_EQ(t.state().tip()->hash, side.back().getHash());
    EXPECT_FALSE(t.state().forkAlert().active);
}

// Audit CON-7: after a long partition the other branch needs more than 2 * limit + 32 blocks to overtake the
// tip. The approval used to be answered with success while the node stayed where it was.
TEST(ChainState, ApprovedDeepReorgHappensEvenWhenLong) {
    TestChain t(regtestWithDepth(3));   // limit 3: a step of more than 38 blocks counts as deep
    const BlockIndex* base = t.state().tip();
    branch(t, base, 40, 0);
    std::vector<Block> side = branch(t, base, 41, 1);
    ASSERT_TRUE(t.state().forkAlert().active);
    EXPECT_EQ(t.state().tip()->height, 40u);

    std::string err;
    ASSERT_TRUE(t.state().approveReorg(side.back().getHash(), err)) << err;
    EXPECT_EQ(t.state().tip()->hash, side.back().getHash());
    EXPECT_EQ(t.state().tip()->height, 41u);
    EXPECT_FALSE(t.state().forkAlert().active);
}

// The approval covers the branch, also after it has grown; once the branch is ours a new deep fork needs a new
// approval.
TEST(ChainState, ApprovalCoversTheGrownBranchOnly) {
    TestChain t(regtestWithDepth(5));
    branch(t, t.state().tip(), 10, 0);
    const BlockIndex* fork = t.state().activeChain()[4];
    std::vector<Block> side = branch(t, fork, 7, 1);   // parked
    const Hash256 parked = side.back().getHash();
    ASSERT_EQ(t.state().forkAlert().otherTip, parked);
    std::vector<Block> more = branch(t, t.state().find(parked), 2, 1);   // the parked branch grows, parked again
    EXPECT_EQ(t.state().tip()->height, 10u);

    std::string err;
    ASSERT_TRUE(t.state().approveReorg(parked, err)) << err;   // the operator approves what the alert showed
    EXPECT_EQ(t.state().tip()->hash, more.back().getHash());
    EXPECT_FALSE(t.state().forkAlert().active);

    // Later, a fork six blocks deep above the approved block (so on the approved branch) is parked again.
    std::vector<Block> ours = branch(t, t.state().tip(), 10, 0);
    const BlockIndex* again = t.state().activeChain()[t.state().tip()->height - 6];
    ASSERT_GT(again->height, t.state().find(parked)->height);
    std::vector<Block> other = branch(t, again, 7, 2);
    EXPECT_EQ(t.state().tip()->hash, ours.back().getHash());
    EXPECT_TRUE(t.state().forkAlert().active);
    EXPECT_EQ(t.state().forkAlert().otherTip, other.back().getHash());
}

// approveReorg says so when the approved branch turns out invalid, and the node stays on its chain.
TEST(ChainState, ApprovingAnInvalidBranchFails) {
    TestChain t(regtestWithDepth(5));
    branch(t, t.state().tip(), 10, 0);
    const Hash256 ourTip = t.state().tip()->hash;
    const BlockIndex* parent = t.state().activeChain()[4];
    std::vector<Block> side;
    for (int i = 0; i < 7; ++i) {
        // The third block's coinbase claims one unit too much: only ConnectBlock can see it.
        const uint64_t amount = i == 2 ? Rules::Subsidy(parent->height + 1, t.params()) + 1 : 0;
        Block b = t.build(parent, {}, 1, 600, amount);
        ASSERT_TRUE(t.submit(b).valid()) << i;   // stored, not connected
        side.push_back(b);
        parent = t.state().find(b.getHash());
    }
    ASSERT_TRUE(t.state().forkAlert().active);

    std::string err;
    EXPECT_FALSE(t.state().approveReorg(side.back().getHash(), err));
    EXPECT_NE(err.find("invalid"), std::string::npos) << err;
    EXPECT_EQ(t.state().tip()->hash, ourTip);
    EXPECT_TRUE(t.state().find(side[2].getHash())->failed());
    EXPECT_FALSE(t.state().forkAlert().active);   // nothing left to decide

    // An unknown tip is refused as before.
    EXPECT_FALSE(t.state().approveReorg(hashWith(7), err));
    EXPECT_EQ(err, "no parked branch with that tip");
}

// CS-10: an unrequested block far above the tip is not stored.
TEST(ChainState, UnrequestedFarBlock) {
    TestChain source;
    std::vector<Block> chain = branch(source, source.state().tip(), 300, 0);

    TestChain t;
    std::vector<BlockHeader> headers;
    for (const Block& b : chain) headers.push_back(b.header);
    // Headers alone (stage A and B) are fine to store.
    ValidationState hs = t.state().ProcessNewBlockHeaders(
        std::vector<BlockHeader>(headers.begin(), headers.begin() + 300));
    ASSERT_TRUE(hs.valid()) << hs.code;

    ValidationState st = t.submit(chain.back(), /*requested=*/false);
    EXPECT_EQ(st.result, ValidationResult::DUPLICATE);
    EXPECT_EQ(st.code, "unrequested-too-far");
    const BlockIndex* e = t.state().find(chain.back().getHash());
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->status & HAVE_DATA, 0u);
}

// CS-11: after an invalid block, resending it and building on it fail fast.
TEST(ChainState, KnownInvalidFast) {
    TestChain t;
    branch(t, t.state().tip(), 4, 0);
    const BlockIndex* fork = t.state().activeChain()[3];
    Block b4 = t.build(fork, {}, 1);
    ASSERT_TRUE(t.submit(b4).valid());
    Block bad = t.build(t.state().find(b4.getHash()), {}, 1, 600, Rules::Subsidy(5, t.params()) + 1);
    EXPECT_EQ(t.submit(bad).code, "bad-cb-amount");
    Block child = t.build(t.state().find(bad.getHash()), {}, 1);
    EXPECT_EQ(t.submit(child).code, "bad-prevblk");
    EXPECT_EQ(t.submit(bad).code, "duplicate-invalid");
}

// CS-12: a block with a spend built on node A is valid on node B with the same history.
TEST(ChainState, PubKeyTravels) {
    TestChain a;
    std::vector<Block> chainA = branch(a, a.state().tip(), 101, 0);

    TestChain b;
    for (const Block& blk : chainA) ASSERT_TRUE(b.submit(blk).valid());

    std::vector<uint8_t> bytes;
    a.store().getBlock(a.state().activeChain()[1]->hash, bytes);
    const Transaction cb1 = Block::deserialize(bytes).transactions[0];
    const uint64_t sub1 = Rules::Subsidy(1, a.params());
    Transaction spend = spendTx(OutPoint{cb1.txid(), 0}, sub1, 3, sub1 - 1000, 4, a.params());
    Block block = a.build(a.tip(), {spend}, 0);
    ASSERT_TRUE(a.submit(block).valid());

    // The bytes that travel over the wire.
    Block received = Block::deserialize(block.serialize());
    ValidationState st = b.submit(received);
    EXPECT_TRUE(st.valid()) << st.code;
    EXPECT_EQ(a.tip()->hash, b.tip()->hash);
}

// CS-13: delivery order does not matter once everything is requested.
TEST(ChainState, OrderIndependent) {
    TestChain a;
    std::vector<Block> chain = branch(a, a.state().tip(), 10, 0);

    std::mt19937 rng(12345);
    std::vector<Block> pending = chain;
    std::shuffle(pending.begin(), pending.end(), rng);

    TestChain b;
    // Retry a block while its parent is missing (a real peer would be asked again).
    for (int round = 0; round < 20 && !pending.empty(); ++round) {
        std::vector<Block> next;
        for (const Block& blk : pending) {
            ValidationState st = b.submit(blk);
            if (st.result == ValidationResult::MISSING_PARENT) next.push_back(blk);
            else EXPECT_TRUE(st.valid()) << st.code;
        }
        pending = next;
    }
    EXPECT_TRUE(pending.empty());
    EXPECT_EQ(b.tip()->hash, a.tip()->hash);
}

// CS-14: an unknown parent is not an error that stores anything.
TEST(ChainState, MissingParentNoPenalty) {
    TestChain a;
    std::vector<Block> chain = branch(a, a.state().tip(), 2, 0);

    TestChain b;
    ValidationState st = b.submit(chain[1]);
    EXPECT_EQ(st.result, ValidationResult::MISSING_PARENT);
    EXPECT_EQ(st.code, "prev-blk-not-found");
    EXPECT_EQ(b.state().find(chain[1].getHash()), nullptr);
}

// MRK-2: a mutated copy of a block does not mark the block as failed.
TEST(ChainState, MutatedBlockNotBlacklisted) {
    TestChain t;
    branch(t, t.state().tip(), 1, 0);

    Block good = t.build(t.state().tip(), {}, 3);
    Block mutated = good;
    mutated.transactions.push_back(mutated.transactions[0]);  // duplicate tx, header unchanged

    ValidationState m = t.submit(mutated);
    EXPECT_EQ(m.result, ValidationResult::MUTATED);
    const BlockIndex* e = t.state().find(good.getHash());
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->status & FAILED_MASK, 0u);

    ValidationState ok = t.submit(good);
    EXPECT_TRUE(ok.valid()) << ok.code;
    EXPECT_EQ(t.tip()->hash, good.getHash());
}
