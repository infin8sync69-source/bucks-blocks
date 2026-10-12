#include "blockchain.h"
#include <iostream>
#include <iomanip>
#include "validation.h"
#include "consensus.h"
#include "genesis.h"

#include <stdexcept>
#include <algorithm>
#include <set>
#include <climits>

namespace Bucks {

// -----------------------------------------------------------------------------
// FIX: Chain-work helpers (integer only) — Bucks v1 security patch
// Used for reorg tie-breaking: accumulated work is correct; block height is not
// because two chains of equal height can have very different total difficulty.
// -----------------------------------------------------------------------------

static uint64_t workForBits(uint32_t bits) {
    uint32_t exp  = bits >> 24;
    uint32_t mant = bits & 0x007fffff;
    if (mant == 0 || exp < 3) return 0;
    // work ∝ 1/target.  target = mant * 256^(exp-3).
    // Approximate with uint64_t: (UINT64_MAX >> shift_bits) / mant
    // where shift_bits = (exp - 3) * 8, capped at 56 to stay in uint64_t.
    uint32_t shift = (exp - 3) * 8u;
    if (shift >= 64u) return 0;           // target larger than any realistic chain
    uint64_t base = UINT64_MAX >> shift;
    return base / mant;
}

static uint64_t accumWork(const std::vector<Block>& blocks, size_t upToIdx) {
    uint64_t total = 0;
    for (size_t i = 0; i <= upToIdx && i < blocks.size(); ++i) {
        uint64_t w = workForBits(blocks[i].header.bits);
        // Saturating add
        total = (total > UINT64_MAX - w) ? UINT64_MAX : total + w;
    }
    return total;
}

// -----------------------------------------------------------------------------
// FIX: 3-of-5 multisig vault authority validator — Bucks v1 security patch
// OP_RETURN payload format after the 15-byte header:
//   [key_idx:1][sig_len:1][sig_bytes:sig_len] ...  (repeat for each signer)
// Message signed: sha256(marker_LE4 || amount_LE8)
// -----------------------------------------------------------------------------

static bool validateVaultMultisig(
    const std::vector<uint8_t>& scriptPubKey,
    uint32_t markerVal,
    uint64_t amount
) {
    // Build message hash the signers committed to
    std::vector<uint8_t> msg(12);
    for (int i = 0; i < 4; ++i) msg[i]     = (markerVal >> (i * 8)) & 0xFF;
    for (int i = 0; i < 8; ++i) msg[4 + i] = (amount    >> (i * 8)) & 0xFF;
    Hash256 msgHash = sha256(msg);

    // Parse vault pubkeys from consensus constants
    std::vector<PublicKey> vaultKeys;
    for (int k = 0; k < Consensus::VAULT_MULTISIG_TOTAL; ++k)
        vaultKeys.push_back(parseHex(Consensus::VAULT_AUTHORITY_PUBKEYS[k]));

    // Signatures start at byte 15: OP_RETURN(1)+push(1)+marker(4)+push(1)+amount(8)
    size_t pos = 15;
    int validSigs = 0;
    std::set<uint8_t> usedIdx;

    while (pos + 2 <= scriptPubKey.size()) {
        uint8_t keyIdx = scriptPubKey[pos++];
        uint8_t sigLen = scriptPubKey[pos++];
        if (keyIdx >= (uint8_t)Consensus::VAULT_MULTISIG_TOTAL) break;
        if (pos + sigLen > scriptPubKey.size())               break;

        std::vector<uint8_t> sig(scriptPubKey.begin() + pos,
                                  scriptPubKey.begin() + pos + sigLen);
        pos += sigLen;

        if (usedIdx.count(keyIdx)) continue;   // each keyholder counted once
        if (verifyECDSA(msgHash, sig, vaultKeys[keyIdx])) {
            ++validSigs;
            usedIdx.insert(keyIdx);
        }
    }

    if (validSigs < Consensus::VAULT_MULTISIG_THRESHOLD) {
        std::cerr << "GOLD_MARKER: insufficient vault signatures (got "
                  << validSigs << ", need "
                  << Consensus::VAULT_MULTISIG_THRESHOLD << ")" << std::endl;
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------

Blockchain::Blockchain(const std::string& utxoPath, const std::string& blockPath,
                       const ChainParams& params)
    : params_(params),
      utxoDB(std::make_unique<UTXODB>(utxoPath)),
      blockDB(std::make_unique<BlockDB>(blockPath)),
      tipIndex(0) {

    std::string genesisError;
    if (!Genesis::check(params_, genesisError)) {
        std::cerr << "FATAL: " << genesisError << std::endl;
        throw std::runtime_error(genesisError);
    }

    if (!utxoDB->init()) {
        std::cerr << "Failed to initialize UTXO database" << std::endl;
        throw std::runtime_error("Failed to initialize UTXO database");
    }

    if (!blockDB->init()) {
        std::cerr << "Failed to initialize Block database" << std::endl;
        throw std::runtime_error("Failed to initialize Block database");
    }

    std::cout << "DEBUG: UTXO DB initialized. Loading blocks from database..." << std::endl;

    // Try to load persisted blocks
    if (loadPersistedBlocks()) {
        if (blocks.empty() || blocks.front().getHash() != params_.genesisHash) {
            std::cerr << "FATAL: datadir belongs to a different network or genesis" << std::endl;
            throw std::runtime_error("datadir belongs to a different network or genesis");
        }
        std::cout << "DEBUG: Blockchain state recovered. Chain height: " << height() << std::endl;
    } else {
        std::cout << "DEBUG: No persisted blocks found, creating Genesis block..." << std::endl;
        // Create and connect genesis block (if chain is empty)
        Block genesis = Genesis::build(params_);
        if (!connectBlock(genesis)) {
            std::cerr << "DEBUG: Failed to connect genesis block!" << std::endl;
        } else {
            std::cout << "DEBUG: Genesis block connected. Chain height: " << height() << std::endl;
        }
    }

    // Load mempool
    if (blockDB) {
        blockDB->loadMempool(mempool);
        std::cout << "DEBUG: Loaded " << mempool.size() << " transactions into mempool" << std::endl;
    }
}

// -----------------------------------------------------------------------------
// Chain state
// -----------------------------------------------------------------------------

uint32_t Blockchain::height() const {
    return blocks.empty() ? 0 : blocks[tipIndex].header.height;
}

const Block* Blockchain::tip() const {
    if (blocks.empty()) return nullptr;
    return &blocks[tipIndex];
}

uint64_t Blockchain::getCirculatingSupply() const {
    // Simple calculation: height * 50 coins (ignoring halvings for prototype simplicity, or loop to be exact)
    // For Phase 1 prototype, we can use the formula.
    uint64_t supply = 0;
    // Iterate blocks to account for halving logic correctly
    for (const auto& block : blocks) {
        // Find coinbase, add reward.
        // Or cleaner: Use formula based on height.
        // Let's use formula for speed.
        uint64_t subsidy = Consensus::INITIAL_SUBSIDY;
        // Subsidy = 50 * 10^8 >> (height / 210000)
        subsidy >>= (block.header.height / Consensus::HALVING_INTERVAL);
        supply += subsidy;
    }
    return supply;
}

// -----------------------------------------------------------------------------
// Block connection
// -----------------------------------------------------------------------------

bool Blockchain::addBlock(const Block& block) {
    Validation::ValidationError error;

    if (blocks.empty()) {
        // Very basic check for genesis
        if (!Validation::checkTransactionBasic(block.transactions[0], error)) return false;
        return connectBlock(block);
    }

    const Block* currentTip = tip();
    if (block.header.prevBlockHash == currentTip->getHash()) {
        // Normal extension: Validate first!
        uint32_t expectedBits = getNextWorkRequired(block.header.height, currentTip->getHash());
        if (!Validation::checkBlock(block, currentTip->header, *utxoDB, block.header.height, getMedianTimePast(), expectedBits, genesisHash(), error)) {
            std::cerr << "DEBUG: Block rejected by checkBlock (Error: " << (int)error << ")" << std::endl;
            return false;
        }
        return connectBlock(block);
    }

    // Potential Reorg: Is the previous block in our index?
    auto it = blockIndex.find(block.header.prevBlockHash);
    if (it == blockIndex.end()) {
        // Not in memory, check DB
        Block prevBlock;
        if (blockDB && blockDB->getBlockByHash(block.header.prevBlockHash, prevBlock)) {
            // It's a fork from a block that was saved to DB but not connected to tip!
            // For a full reorg, we'd need to trace back to common ancestor in 'blocks' vector.
            std::cout << "DEBUG: Fork from DB detected at height " << prevBlock.header.height << std::endl;

            // FIX: Compare accumulated chain work, not block height, for reorg decisions
            // — Bucks v1 security patch
            // Collect branch first so we can compute fork chain work.
            // The actual work comparison happens after branch is built (below).
            if (block.header.height > currentTip->header.height) {  // pre-filter: height still used as fast skip
                // ... (simplified trace back logic remains)
                // For a proper reorg, we should also check the bits for every block in the fork.
                // But for this security audit, we focus on the tip extension and the simulation.
                // Since prevBlock is at ancestorHeight, we need to find where that height branched.
                // In this simplified test logic, we assume the user provides blocks in order.

                // Let's implement a recursive reorg to follow the side-chain back.
                // For this test, we can assume the branch point is known if it connects to the main chain.
                // Actually, let's keep it robust:
                Block currentAncestor = prevBlock;
                std::vector<Block> branch;
                branch.push_back(block);

                while (blockIndex.find(currentAncestor.getHash()) == blockIndex.end()) {
                    branch.push_back(currentAncestor);
                    if (!blockDB->getBlockByHash(currentAncestor.header.prevBlockHash, currentAncestor)) {
                        break; // Orphan or genesis
                    }
                }

                // Now currentAncestor is in blockIndex!
                uint32_t commonHeight = blockIndex[currentAncestor.getHash()];
                size_t   commonIdx    = blockIndex[currentAncestor.getHash()];

                // FIX: Use accumulated chain work, not height, for reorg decision
                // — Bucks v1 security patch
                uint64_t forkWork = accumWork(blocks, commonIdx);
                for (const auto& fb : branch) {
                    uint64_t w = workForBits(fb.header.bits);
                    forkWork = (forkWork > UINT64_MAX - w) ? UINT64_MAX : forkWork + w;
                }
                uint64_t currentWork = accumWork(blocks, tipIndex);
                if (forkWork <= currentWork) {
                    std::cout << "DEBUG: Fork has less/equal chain work — ignoring." << std::endl;
                    return true;
                }

                std::cout << "DEBUG: Reorg triggering! Common ancestor at height " << commonHeight << std::endl;

                while (height() > commonHeight) {
                    if (!disconnectTip()) return false;
                }

                // Connect branch blocks in reverse order (ancestor to new tip)
                for (auto itB = branch.rbegin(); itB != branch.rend(); ++itB) {
                    if (!connectBlock(*itB)) return false;
                }
                return true;
            }
        }
    }

    if (it != blockIndex.end()) {
        // It's a fork from a known block!
        uint32_t ancestorHeight = blocks[it->second].header.height;

        // FIX: Use accumulated chain work, not height, for reorg tie-breaking
        // — Bucks v1 security patch
        uint64_t forkWork    = accumWork(blocks, it->second)
                               + workForBits(block.header.bits);
        uint64_t currentWork = accumWork(blocks, tipIndex);

        if (forkWork > currentWork) {
            std::cout << "DEBUG: Potential reorg detected from height " << ancestorHeight << " to " << block.header.height << std::endl;

            // Validate the new block before reorg!
            uint32_t expectedBits = getNextWorkRequired(block.header.height, block.header.prevBlockHash);
            if (!Validation::checkBlock(block, blocks[it->second].header, *utxoDB, block.header.height, getMedianTimePast(), expectedBits, genesisHash(), error)) {
                 std::cerr << "DEBUG: Fork block rejected (ValidationError=" << static_cast<int>(error)
                           << ", height=" << block.header.height << ")" << std::endl;
                 return false;
            }

            // Disconnect blocks back to ancestor
            while (height() > ancestorHeight) {
                if (!disconnectTip()) {
                    std::cerr << "CRITICAL: Reorg failed during disconnection at height " << height() << std::endl;
                    return false;
                }
            }

            // Connect the new block
            return connectBlock(block);
        } else {
            std::cout << "DEBUG: Side-chain block at height " << block.header.height << " (shorter/equal branch, ignored for now)" << std::endl;
            // Still save to DB so we can reorg to it later if it grows
            if (blockDB) blockDB->saveBlock(block);
            return true;
        }
    }

    return false;
}

bool Blockchain::connectBlock(const Block& block) {
    // Validate + apply to UTXO set
    Validation::ValidationError error;
    if (!Validation::connectBlock(block, *utxoDB, blockDB.get(), genesisHash(), error)) {
        std::cerr << "DEBUG: connectBlock failed for height=" << block.header.height
                  << " (ValidationError=" << static_cast<int>(error) << ")" << std::endl;
        return false;
    }

    // ----------------------------------------------------
    // Phase 3: Gold Injection / Redemption Scan
    // FIX: Use Consensus::GOLD_MARKER / REDEEM_MARKER constants; require 3-of-5
    //      vault multisig for every gold event — Bucks v1 security patch
    // ----------------------------------------------------
    bool goldUpdated = false;
    for (const auto& tx : block.transactions) {
        for (const auto& out : tx.outputs) {
            // Check for OP_RETURN (0x6a)
            if (out.scriptPubKey.size() < 15 || out.scriptPubKey[0] != 0x6a) continue;
            if (out.scriptPubKey[1] != 0x04) continue;

            // Read 4-byte marker (big-endian from bytes [2..5])
            uint32_t marker = ((uint32_t)out.scriptPubKey[2] << 24)
                            | ((uint32_t)out.scriptPubKey[3] << 16)
                            | ((uint32_t)out.scriptPubKey[4] <<  8)
                            | ((uint32_t)out.scriptPubKey[5]);

            bool isGold  = (marker == Consensus::GOLD_MARKER);
            bool isRedeem = (marker == Consensus::REDEEM_MARKER);
            if (!isGold && !isRedeem) continue;

            // Extract amount (uint64_t LE at offset 7)
            if (out.scriptPubKey.size() < 15) continue;
            uint64_t amount = 0;
            for (int k = 0; k < 8; ++k)
                amount |= (uint64_t)out.scriptPubKey[7 + k] << (k * 8);

            // Verify 3-of-5 vault multisig embedded in the OP_RETURN payload
            if (!validateVaultMultisig(out.scriptPubKey, marker, amount)) {
                std::cerr << "WARNING: Gold event rejected — vault multisig invalid" << std::endl;
                continue;
            }

            if (isGold) {
                std::cout << "DEBUG: GOLD INJECTION DETECTED (vault-verified)! Amount: "
                          << amount << "g" << std::endl;
                goldReserve += amount;
                goldUpdated = true;
            } else {
                // REDM: redemption — decrement reserve
                std::cout << "DEBUG: GOLD REDEMPTION DETECTED (vault-verified)! Amount: "
                          << amount << "g" << std::endl;
                if (goldReserve >= amount) {
                    goldReserve -= amount;
                } else {
                    std::cerr << "WARNING: REDM amount " << amount
                              << " exceeds goldReserve " << goldReserve << " — clamping to 0" << std::endl;
                    goldReserve = 0;
                }
                goldUpdated = true;
            }
        }
    }

    // Persist updated goldReserve within the same logical block commit
    if (goldUpdated && blockDB) {
        blockDB->saveGoldReserve(goldReserve);
    }

    // ----------------------------------------------------------------
    // Governance event scanner
    // Scans every OP_RETURN output for governance records tagged with
    // GOV_PREFIX ("BKGV").  Events are persisted as an immutable log.
    // OP_RETURN format:  0x6a [push_len] [GOV_PREFIX LE4] [type:1] [payload...]
    // ----------------------------------------------------------------
    if (blockDB) {
        for (const auto& tx : block.transactions) {
            Hash256 txHash = tx.txid();
            for (const auto& out : tx.outputs) {
                // Minimum: OP_RETURN(1) + push_len(1) + GOV_PREFIX(4) + type(1) = 7 bytes
                if (out.scriptPubKey.size() < 7) continue;
                if (out.scriptPubKey[0] != 0x6a) continue; // must be OP_RETURN

                // byte[1] is the push-data length; actual payload starts at byte[2]
                size_t pos = 2;

                // Read 4-byte GOV_PREFIX — little-endian
                uint32_t prefix =
                    (uint32_t)out.scriptPubKey[pos]
                    | ((uint32_t)out.scriptPubKey[pos + 1] << 8)
                    | ((uint32_t)out.scriptPubKey[pos + 2] << 16)
                    | ((uint32_t)out.scriptPubKey[pos + 3] << 24);
                if (prefix != Consensus::GOV_PREFIX) continue;
                pos += 4;

                if (pos >= out.scriptPubKey.size()) continue;
                uint8_t eventType = out.scriptPubKey[pos++];

                // Remaining bytes are the governance payload
                std::vector<uint8_t> govPayload(
                    out.scriptPubKey.begin() + pos,
                    out.scriptPubKey.end()
                );

                blockDB->saveGovernanceEvent(
                    block.header.height, eventType, govPayload, txHash);

                std::cout << "DEBUG: Governance event type 0x"
                          << std::hex << (int)eventType << std::dec
                          << " at height " << block.header.height << std::endl;
            }
        }
    }

    // Append block
    blocks.push_back(block);
    tipIndex = blocks.size() - 1;

    // Index by hash
    blockIndex[block.getHash()] = tipIndex;

    // Remove transactions from mempool that are now in this block
    for (const auto& tx : block.transactions) {
        if (!tx.isCoinbase()) {
            removeFromMempool(tx.txid());
        }
    }

    // Persist block to database
    if (blockDB && !blockDB->saveBlock(block)) {
        std::cerr << "Warning: Failed to persist block " << block.header.height << " to database" << std::endl;
    }

    return true;
}

// -----------------------------------------------------------------------------
// Block lookup
// -----------------------------------------------------------------------------

const Block* Blockchain::getBlock(const Hash256& hash) const {
    auto it = blockIndex.find(hash);
    if (it == blockIndex.end())
        return nullptr;
    return &blocks[it->second];
}

const Block* Blockchain::getBlockByHeight(uint32_t height) const {
    if (height >= blocks.size())
        return nullptr;
    return &blocks[height];
}

// -----------------------------------------------------------------------------
// Reorg foundation (Phase-1)
// -----------------------------------------------------------------------------

bool Blockchain::disconnectTip() {
    if (blocks.empty())
        return false;

    const Block& block = blocks[tipIndex];

    // FIX: Reverse Gold Injections and Redemptions using marker constants
    //      — Bucks v1 security patch
    bool goldUpdated = false;
    for (const auto& tx : block.transactions) {
        for (const auto& out : tx.outputs) {
            if (out.scriptPubKey.size() < 15 || out.scriptPubKey[0] != 0x6a) continue;
            if (out.scriptPubKey[1] != 0x04) continue;

            uint32_t marker = ((uint32_t)out.scriptPubKey[2] << 24)
                            | ((uint32_t)out.scriptPubKey[3] << 16)
                            | ((uint32_t)out.scriptPubKey[4] <<  8)
                            | ((uint32_t)out.scriptPubKey[5]);

            bool isGold   = (marker == Consensus::GOLD_MARKER);
            bool isRedeem = (marker == Consensus::REDEEM_MARKER);
            if (!isGold && !isRedeem) continue;

            uint64_t amount = 0;
            for (int k = 0; k < 8; ++k)
                amount |= (uint64_t)out.scriptPubKey[7 + k] << (k * 8);

            if (isGold) {
                // Reverse injection: subtract
                if (goldReserve >= amount) {
                    goldReserve -= amount;
                    std::cout << "DEBUG: Reversing Gold Injection: -" << amount << "g" << std::endl;
                } else {
                    goldReserve = 0;
                }
            } else {
                // Reverse redemption: add back
                goldReserve += amount;
                std::cout << "DEBUG: Reversing Gold Redemption: +" << amount << "g" << std::endl;
            }
            goldUpdated = true;
        }
    }

    if (goldUpdated && blockDB) {
        blockDB->saveGoldReserve(goldReserve);
    }

    // NOTE: Governance events are intentionally NOT reversed on reorg.
    // They form an immutable chronological log of on-chain governance signals.
    // Their validity is determined by whether they appeared in a block that
    // became part of the canonical chain, which clients must verify themselves.

    Validation::ValidationError error;
    if (!blockDB || !Validation::disconnectBlock(block, *utxoDB, *blockDB, error)) {
        return false;
    }

    // Remove from index and vector
    blockIndex.erase(block.getHash());
    blocks.pop_back();

    if (blocks.empty()) {
        tipIndex = 0;
    } else {
        tipIndex = blocks.size() - 1;
    }

    return true;
}

bool Blockchain::isValidBlock(const Block& block, const Block& prev) const {
    // Simple check: hash links and basic structure
    if (block.header.prevBlockHash != prev.getHash()) return false;
    if (block.transactions.empty()) return false;
    if (!block.transactions[0].isCoinbase()) return false;
    return true;
}

const UTXODB& Blockchain::utxo() const {
    return *utxoDB;
}

// -----------------------------------------------------------------------------
// Persistence
// -----------------------------------------------------------------------------

bool Blockchain::loadPersistedBlocks() {
    if (!blockDB) {
        return false;
    }

    std::vector<Block> persistedBlocks;
    if (!blockDB->loadBlocks(persistedBlocks) || persistedBlocks.empty()) {
        return false;
    }

    std::cout << "DEBUG: Loaded " << persistedBlocks.size() << " blocks from database" << std::endl;

    // Clear current state
    blocks.clear();
    blockIndex.clear();
    tipIndex = 0;

    // Rebuild blockchain state from persisted blocks
    for (const auto& block : persistedBlocks) {
        blocks.push_back(block);
        blockIndex[block.getHash()] = blocks.size() - 1;
    }

    tipIndex = blocks.empty() ? 0 : blocks.size() - 1;

    // FIX: Restore goldReserve from persistent storage — Bucks v1 security patch
    // Without this, goldReserve resets to 0 on every node restart.
    if (blockDB) {
        blockDB->loadGoldReserve(goldReserve);
        std::cout << "DEBUG: Loaded goldReserve from DB: " << goldReserve << "g" << std::endl;
    }

    std::cout << "DEBUG: Blockchain state rebuilt from disk" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Mempool
// -----------------------------------------------------------------------------

bool Blockchain::addToMempool(const Transaction& tx) {
    Validation::ValidationError error;
    if (!Validation::checkTransactionBasic(tx, error)) return false;
    if (tx.isCoinbase()) return false;
    if (!Validation::checkTransactionInputs(tx, *utxoDB, height() + 1, genesisHash(), error)) return false;

    // Check if already in mempool
    // Check if already in mempool
    for (const auto& mtx : mempool) {
        if (mtx.txid() == tx.txid()) return true;
    }

    // Check for double-spends within the mempool (First-Seen Rule)
    for (const auto& in : tx.inputs) {
        for (const auto& mtx : mempool) {
            for (const auto& min : mtx.inputs) {
                if (in.prevTxid == min.prevTxid && in.prevIndex == min.prevIndex) {
                    std::cerr << "DEBUG: Mempool conflict detected (double-spend attempt of same input)" << std::endl;
                    return false;
                }
            }
        }
    }

    // Round 5: Mempool RAM Hardening
    if (mempool.size() >= MAX_MEMPOOL_COUNT) {
        std::cerr << "DEBUG: Mempool count limit reached (" << MAX_MEMPOOL_COUNT << ")" << std::endl;
        return false;
    }

    size_t totalSize = 0;
    for (const auto& mtx : mempool) {
        totalSize += mtx.serialize().size();
    }

    if (totalSize + tx.serialize().size() > MAX_MEMPOOL_SIZE) {
        std::cerr << "DEBUG: Mempool size limit reached (" << MAX_MEMPOOL_SIZE << " bytes)" << std::endl;
        return false;
    }

    mempool.push_back(tx);
    if (blockDB) {
        blockDB->addMempoolTx(tx);
    }
    return true;
}

void Blockchain::removeFromMempool(const Hash256& txid) {
    auto it = std::remove_if(mempool.begin(), mempool.end(), [&](const Transaction& tx) {
        return tx.txid() == txid;
    });
    if (it != mempool.end()) {
        mempool.erase(it, mempool.end());
        if (blockDB) {
            blockDB->removeMempoolTx(txid);
        }
    }
}

const std::vector<Transaction>& Blockchain::getMempool() const {
    return mempool;
}

void Blockchain::clearMempool() {
    mempool.clear();
    if (blockDB) {
        blockDB->deleteAllMempool();
    }
}

uint32_t Blockchain::getMedianTimePast() const {
    if (blocks.empty()) return 0;

    std::vector<uint32_t> timestamps;
    for (int i = 0; i < 11 && (int)blocks.size() - 1 - i >= 0; ++i) {
        timestamps.push_back(blocks[blocks.size() - 1 - i].header.timestamp);
    }
    std::sort(timestamps.begin(), timestamps.end());
    return timestamps[timestamps.size() / 2];
}


uint32_t Blockchain::getNextWorkRequired(uint32_t height, const Hash256& prevHash) const {
    if (height == 0) return params_.powLimitBits;

    auto it = blockIndex.find(prevHash);
    if (it == blockIndex.end()) return params_.powLimitBits;

    const Block& lastBlock = blocks[it->second];
    if (params_.powNoRetargeting) return lastBlock.header.bits;

    // Retarget every 2016 blocks
    if ((height % Consensus::DIFFICULTY_ADJUSTMENT_INTERVAL) != 0) {
        return lastBlock.header.bits;
    }

    // Find pindexFirst (2016 blocks ago)
    if (height < Consensus::DIFFICULTY_ADJUSTMENT_INTERVAL) return lastBlock.header.bits;

    uint32_t firstBlockHeight = height - Consensus::DIFFICULTY_ADJUSTMENT_INTERVAL;
    const Block& firstBlock = blocks[firstBlockHeight];

    int64_t actualTimespan = lastBlock.header.timestamp - firstBlock.header.timestamp;
    int64_t targetTimespan = (int64_t)Consensus::DIFFICULTY_ADJUSTMENT_INTERVAL * (int64_t)Consensus::TARGET_BLOCK_TIME;

    return adjustBits(lastBlock.header.bits, actualTimespan, targetTimespan);
}

uint32_t Blockchain::adjustBits(uint32_t bits, int64_t actual, int64_t target) {
    // FIX: Integer-only difficulty adjustment — Bucks v1 security patch
    // Floating-point in consensus code causes chain splits across platforms because
    // IEEE-754 rounding behaviour is implementation-defined.  Replaced with
    // __uint128_t integer arithmetic matching the approach in pow.cpp::multiplyTarget.
    if (actual < target / 4) actual = target / 4;
    if (actual > target * 4) actual = target * 4;

    uint32_t exponent = bits >> 24;
    uint32_t mantissa  = bits & 0x007fffff;

    // Scale mantissa: newMantissa = mantissa * actual / target  (no floats)
    __uint128_t m128 = (__uint128_t)mantissa * (uint64_t)actual;
    __uint128_t t128 = (uint64_t)target;
    __uint128_t nm   = m128 / t128;

    // Normalise: keep result in [0x008000, 0x7fffff]
    while (nm >= 0x800000ULL && exponent < 0x20) { nm >>= 8; exponent++; }
    while (nm  < 0x008000ULL && nm > 0 && exponent > 3) { nm <<= 8; exponent--; }

    uint32_t mValue = (uint32_t)nm;
    if (mValue & 0x800000) { mValue >>= 8; exponent++; }   // sign bit must be clear

    uint32_t ret = (exponent << 24) | (mValue & 0x007fffff);
    if (ret > Consensus::POW_LIMIT_BITS) ret = Consensus::POW_LIMIT_BITS;
    return ret;
}

} // namespace Bucks
