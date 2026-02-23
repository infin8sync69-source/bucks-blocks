#include "miner.h"
#include "consensus.h"
#include "validation.h"

#include <chrono>
#include <iostream>

namespace Bucks {

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------

Miner::Miner(Blockchain& bc, Mempool& mp, const ConsensusParams& params)
    : chain(bc),
      mempool(mp),
      consensus(params),
      mining(false) {}

// -----------------------------------------------------------------------------
// Control
// -----------------------------------------------------------------------------

void Miner::start(const Script& coinbaseScript) {
    if (mining) return;
    mining = true;

    minerThread = std::thread(
        &Miner::mineLoop,
        this,
        coinbaseScript
    );
}

void Miner::stop() {
    mining = false;
    if (minerThread.joinable())
        minerThread.join();
}

// -----------------------------------------------------------------------------
// Mining loop
// -----------------------------------------------------------------------------

void Miner::mineLoop(Script coinbaseScript) {
    while (mining) {
        Block block = createBlockTemplate(coinbaseScript);

        std::cout << "[miner] Mining block at height "
                  << block.header.height << std::endl;

        if (mineBlock(block)) {
            if (chain.addBlock(block)) {
                std::cout << "[miner] Block found: "
                          << toHex(block.getHash()) << std::endl;

                // Remove mined txs from mempool
                for (size_t i = 1; i < block.transactions.size(); ++i) {
                    mempool.removeTransaction(block.transactions[i].txid);
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Block template
// -----------------------------------------------------------------------------

Block Miner::createBlockTemplate(const Script& coinbaseScript) {
    Block block;

    const Block* tip = chain.tip();
    uint32_t height = tip ? tip->header.height + 1 : 0;

    // Header
    block.header.version = consensus.blockVersion;
    block.header.height = height;
    block.header.prevBlock = tip ? tip->getHash() : Hash256{};
    block.header.timestamp = currentTimestamp();
    block.header.bits = consensus.getWorkRequired(height);
    block.header.nonce = 0;

    // -------------------------------------------------------------------------
    // Transactions
    // -------------------------------------------------------------------------

    uint64_t fees = 0;

    // Coinbase (must be first)
    Transaction coinbase;
    coinbase.makeCoinbase(
        height,
        consensus.getBlockReward(height),
        coinbaseScript
    );

    block.transactions.push_back(coinbase);

    // Collect mempool txs
    auto txs = mempool.collect(consensus.maxBlockTxs);

    for (const auto& tx : txs) {
        fees += tx.fee();
        block.transactions.push_back(tx);
    }

    // Update coinbase reward with fees
    block.transactions[0].outputs[0].amount += fees;

    // Merkle root
    block.header.merkleRoot = block.computeMerkleRoot();

    return block;
}

// -----------------------------------------------------------------------------
// Proof-of-Work
// -----------------------------------------------------------------------------

bool Miner::mineBlock(Block& block) {
    const Hash256 target =
        consensus.bitsToTarget(block.header.bits);

    while (mining) {
        Hash256 hash = block.getHash();

        if (hash <= target) {
            // Final validation before submission
            if (Validation::validateBlock(block, chain))
                return true;
        }

        ++block.header.nonce;

        // Timestamp update occasionally
        if ((block.header.nonce & 0xFFFF) == 0) {
            block.header.timestamp = currentTimestamp();
        }
    }

    return false;
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

uint32_t Miner::currentTimestamp() const {
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

} // namespace Bucks
