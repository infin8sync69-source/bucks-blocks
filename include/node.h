#ifndef BUCKS_NODE_H
#define BUCKS_NODE_H

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "block.h"
#include "chaindb.h"
#include "chainparams.h"
#include "chainstate.h"
#include "mempool.h"
#include "miner.h"

namespace Bucks {

// The running chain: store, chain state, mempool, block assembler and miner
// behind one lock (docs/design/consensus-validation.md section 8.8, "chain
// lock"). The server, the network code and the wallet talk only to this class
// and only get copies, never pointers into the chain state. The proof-of-work
// loop runs without the lock.
class Node {
public:
    using Clock = std::function<int64_t()>;

    struct TipInfo {
        Hash256 hash{};
        uint32_t height = 0;
        uint32_t time = 0;
        uint32_t bits = 0;
        U256 chainWork;
        uint64_t sequence = 0;
        UtxoStats stats;                // totalAmount is the circulating supply
        bool initialBlockDownload = false;
        bool forkAlert = false;
        uint32_t forkHeight = 0;
        Hash256 forkOtherTip{};
    };

    // Opens <networkDir>/chain.sqlite, runs the startup checks of section 8.7
    // and returns the node. On failure returns nullptr; exitCode is one of the
    // StorageExit values (3 parameters, 4 storage, 6 corrupt, 7 version).
    static std::unique_ptr<Node> open(const std::string& networkDir, const ChainParams& params,
                                      const LoadOptions& load, std::string& err, int& exitCode,
                                      Clock clock = {});
    ~Node();

    const ChainParams& params() const { return params_; }

    // ---- Chain (copies) ----
    TipInfo tip() const;
    uint32_t height() const;
    uint64_t tipSequence() const { return state_->tipSequence(); }
    std::optional<Block> blockByHeight(uint32_t height) const;
    std::optional<Block> blockByHash(const Hash256& hash) const;
    bool haveBlockData(const Hash256& hash) const;     // block bytes stored
    bool knowsHeader(const Hash256& hash) const;
    // Cumulative work of the chain that ends at a known header; empty if it is unknown.
    std::optional<U256> chainWorkOf(const Hash256& hash) const;
    std::vector<Hash256> locator() const;
    std::vector<BlockHeader> headersAfter(const std::vector<Hash256>& locator, const Hash256& stop,
                                          size_t max) const;
    // Blocks whose data we still lack on the way to `peerBest`, the most-work block a peer
    // announced (at most max): what to download from that peer.
    std::vector<Hash256> missingBlocksTowards(const Hash256& peerBest, size_t max) const;
    // A peer announced or sent `hash`: it becomes the peer's best block if it is known and
    // has more work than `peerBest`.
    void noteBlockAvailable(const Hash256& hash, Hash256& peerBest) const;
    uint64_t circulatingSupply() const;
    uint64_t goldReserve() const { return 0; }        // the gold module is a later phase

    // ---- Processing ----
    ValidationState submitBlock(const Block& block, bool requested);
    ValidationState submitHeaders(const std::vector<BlockHeader>& headers);
    ValidationState submitTransaction(const Transaction& tx, const AcceptOptions& options = {});
    bool approveReorg(const Hash256& tipHash, std::string& err);

    // ---- Coins and mempool ----
    std::vector<CoinRow> coinsByHash160(const Hash160& key, size_t limit) const;
    // Unspent coins that a new transaction can spend now: mature, and not already
    // spent by a mempool transaction.
    std::vector<CoinRow> spendableCoins(const Hash160& key, size_t limit) const;
    uint64_t balance(const Hash160& key) const;
    bool coinCount(const Hash160& key, uint64_t& count) const;
    size_t mempoolSize() const;
    std::vector<Transaction> mempoolTransactions(size_t max) const;
    std::optional<Transaction> mempoolTransaction(const Hash256& txid) const;

    // ---- Mining ----
    // Builds a template and hashes on the calling thread plus threads - 1 helpers; returns
    // when a block was found and accepted, on stop/timeout, or on failure. The hash of the
    // block is stored in *found.
    MineResult mine(const Script& minerScript, const AssemblerOptions& options,
                    const std::atomic<bool>& stop, std::string& err, Hash256* found = nullptr,
                    int64_t timeLimitSeconds = 0, unsigned threads = 1);
    uint64_t minerHashes() const { return miner_->hashes(); }
    uint64_t minerHashRate() const { return miner_->hashRate(); }   // all threads, recent seconds

    struct MiningInfo {
        std::string blockedReason;   // why no block template can be built now ("" = it can)
        uint32_t bits = 0;           // proof of work required of the next block
    };
    MiningInfo miningInfo() const;

    // ---- Notifications (called without the chain lock held) ----
    using TipListener = std::function<void(const Hash256& hash, uint32_t height)>;
    using TxListener = std::function<void(const Transaction&)>;
    // Listeners are removed by id; whoever registers one removes it before it goes away.
    size_t addTipListener(TipListener f);
    void removeTipListener(size_t id);
    size_t addTxListener(TxListener f);
    void removeTxListener(size_t id);

    // Orderly shutdown: marks the store clean. Called by the destructor too.
    // After a fatal chain-state error nothing more is written.
    void shutdown();

    // 0 while the chain state is healthy; otherwise the exit code the process must
    // stop with (4 storage failure, 6 corrupt) and `why`. The caller stops the node.
    int fatalError(std::string& why) const;

private:
    Node() = default;
    TipInfo tipLocked() const;
    bool initialBlockDownloadLocked() const;
    void notifyTip(uint64_t sequenceBefore);

    ChainParams params_;
    std::string networkDir_;
    void loadMempoolFile();
    void saveMempoolFile();
    Clock clock_;
    std::unique_ptr<SqliteChainStore> store_;
    std::unique_ptr<ChainState> state_;
    std::unique_ptr<Mempool> mempool_;
    std::unique_ptr<BlockAssembler> assembler_;
    std::unique_ptr<Miner> miner_;

    mutable std::mutex mu_;                  // the chain lock
    std::mutex mineMu_;                      // one mine() at a time: the Miner object is not reentrant
    mutable bool ibdDone_ = false;           // once false, stays false until restart
    bool shutDown_ = false;

    std::mutex listenerMu_;
    size_t nextListenerId_ = 1;
    std::map<size_t, TipListener> tipListeners_;
    std::map<size_t, TxListener> txListeners_;
};

} // namespace Bucks

#endif // BUCKS_NODE_H
