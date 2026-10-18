#ifndef BUCKS_MINER_H
#define BUCKS_MINER_H

#include <openssl/sha.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "block.h"
#include "chainparams.h"
#include "chainstate.h"
#include "mempool.h"
#include "script.h"
#include "u256.h"

namespace Bucks {

// Block template builder and miner loop (docs/design/consensus-validation.md
// section 10). The builder works on copies and never holds a lock while
// hashing; the node serializes access to ChainState and Mempool (plan step 16).

struct AssemblerOptions {
    std::string minerTag = "Bucks";           // at most 88 ASCII bytes
    std::vector<uint8_t> extraPayload;        // optional coinbase NULL_DATA, at most 80 bytes
};

struct BlockTemplate {
    Block block;                // header.nonce is 0, proof of work not yet done
    uint64_t tipSequence = 0;   // ChainState::tipSequence() the template was built on
    uint64_t fees = 0;
    int64_t minTime = 0;        // lowest header time that still passes stage B
    uint64_t extraNonce = 0;
};

class BlockAssembler {
public:
    using Clock = std::function<int64_t()>;

    BlockAssembler(const ChainParams& params, const ChainState& chain, Mempool& mempool,
                   Clock clock = {});

    // Fails with `err` set: "node is syncing", "local clock is behind the chain",
    // bad miner script or options, or "template invalid: <code>".
    bool createNewBlock(const Script& minerScript, const AssemblerOptions& options,
                        BlockTemplate& out, std::string& err);

    // Rewrites the extra nonce in the coinbase and recomputes the merkle root.
    static void setExtraNonce(BlockTemplate& t, uint64_t extraNonce);

    // Stages A (without proof of work), B, C, D and E on a scratch view; nothing is committed.
    bool testBlockValidity(const Block& block, ValidationState& state) const;

private:
    bool build(const Script& minerScript, const AssemblerOptions& options,
               const std::vector<Hash256>& skipTxids, BlockTemplate& out, std::string& err);

    const ChainParams& params_;
    const ChainState& chain_;
    Mempool& mempool_;
    Clock clock_;
};

// SHA256d of an 84-byte block header for the proof-of-work search. The first
// 64 bytes (version, height, previous hash, merkle root bytes 0..23) are hashed
// once; each nonce hashes only the 20-byte tail and the second round. The
// result equals BlockHeader::hash(). It uses OpenSSL's low-level SHA-256: the
// one-shot EVP calls behind doubleSha256 cost four times as much per header
// and barely scale across threads (audit ML-2).
class HeaderHasher {
public:
    explicit HeaderHasher(const BlockHeader& header);
    void setTimestamp(uint32_t timestamp);           // the timestamp is in the tail
    Hash256 hash(uint32_t nonce) const;
    // hash(nonce), read big-endian, <= target: Pow::checkProofOfWork with the
    // target decoded once.
    bool meetsTarget(uint32_t nonce, const U256& target) const;

private:
    SHA256_CTX mid_;
    uint8_t tail_[20];
};

enum class MineResult : uint8_t {
    FOUND,     // a block was found and accepted by ProcessNewBlock
    STOPPED,   // stop was requested
    FAILED     // no template could be built, or the node rejected our block
};

class Miner {
public:
    using Clock = std::function<int64_t()>;

    Miner(ChainState& chain, BlockAssembler& assembler, Clock clock = {});

    // Section 10.2. Returns after one block is found (and submitted with
    // ProcessNewBlock, requested = true) or on stop/failure. With several
    // threads each one searches its own coinbase extra nonce (thread index in
    // the top 16 bits), so no two threads ever hash the same header.
    MineResult mineOne(const Script& minerScript, const AssemblerOptions& options,
                       const std::atomic<bool>& stop, std::string& err);

    // The node's chain lock. When set, template building and block submission
    // hold it; the hash loop never does. The tip sequence is read lock-free.
    void setChainLock(std::mutex* lock) { lock_ = lock; }
    // Gives up (STOPPED) after this many seconds of hashing; 0 = no limit.
    void setTimeLimitSeconds(int64_t s) { timeLimit_ = s; }
    // Hashing threads for the next mineOne; the calling thread is one of them.
    void setThreads(unsigned n) { threads_ = n ? n : 1; }
    uint64_t hashes() const { return hashes_.load(); }
    // Hashes per second over the last two seconds or more of hashing, all
    // threads together; 0 until the first sample of a run (audit ML-3).
    uint64_t hashRate() const { return rate_.load(); }

    // How often (in nonces) the loop looks at the tip and the clock; default 65,536.
    void setCheckInterval(uint64_t nonces) { checkInterval_ = nonces ? nonces : 1; }
    // Seconds after which a template is rebuilt for new mempool transactions; default 60.
    // Test hooks.
    void setNonceSpaceForTesting(uint64_t n) { nonceSpace_ = n; }
    void setBatchHookForTesting(std::function<void()> f) { batchHook_ = std::move(f); }

    uint64_t extraNonceRollovers() const { return rollovers_; }
    uint64_t templatesBuilt() const { return templates_; }
    uint64_t staleRestarts() const { return staleRestarts_; }
    const Hash256& lastBlockHash() const { return lastHash_; }

private:
    struct Round;
    void search(BlockTemplate& t, unsigned index, const U256& target, int64_t began, int64_t started,
                const std::atomic<bool>& stop, Round& round);
    void sampleRate(bool restart);

    ChainState& chain_;
    BlockAssembler& assembler_;
    Clock clock_;
    uint64_t checkInterval_ = 65'536;
    int64_t refreshSeconds_ = 60;
    uint64_t nonceSpace_ = 0x100000000ull;
    std::function<void()> batchHook_;
    std::atomic<uint64_t> rollovers_{0}, templates_{0}, staleRestarts_{0};
    std::mutex* lock_ = nullptr;
    int64_t timeLimit_ = 0;
    unsigned threads_ = 1;
    std::atomic<uint64_t> hashes_{0};
    std::atomic<uint64_t> rate_{0};
    // Rate window; written only by thread 0 of the running mineOne.
    std::chrono::steady_clock::time_point rateAt_{};
    uint64_t rateHashes_ = 0;
    Hash256 lastHash_{};
};

} // namespace Bucks

#endif // BUCKS_MINER_H
