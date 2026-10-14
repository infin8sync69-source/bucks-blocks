#include "node.h"

#include <algorithm>
#include <ctime>
#include <chrono>
#include <thread>

#include "consensus.h"
#include "fsutil.h"
#include "log.h"
#include "mempool_file.h"
#include "script.h"

namespace Bucks {

std::unique_ptr<Node> Node::open(const std::string& networkDir, const ChainParams& params,
                                 const LoadOptions& load, std::string& err, int& exitCode, Clock clock) {
    std::unique_ptr<Node> n(new Node());
    n->params_ = params;
    n->networkDir_ = networkDir;
    n->clock_ = clock ? std::move(clock) : Clock([] { return int64_t(std::time(nullptr)); });

    n->store_ = SqliteChainStore::open(networkDir, n->params_, err, exitCode);
    if (!n->store_) return nullptr;

    Clock clk = n->clock_;
    n->state_ = std::make_unique<ChainState>(n->params_, *n->store_, clk);
    n->mempool_ = std::make_unique<Mempool>(n->params_, clk);
    n->assembler_ = std::make_unique<BlockAssembler>(n->params_, *n->state_, *n->mempool_, clk);
    n->miner_ = std::make_unique<Miner>(*n->state_, *n->assembler_, clk);
    n->miner_->setChainLock(&n->mu_);

    Node* raw = n.get();
    n->state_->setStepHook([raw](const std::vector<Block>& gone, const std::vector<Block>& joined) {
        raw->mempool_->updateForStep(gone, joined, *raw->state_);   // runs under the chain lock
    });

    LoadResult r = n->state_->load(load);
    if (!r.ok) {
        err = r.error;
        exitCode = r.exitCode;
        n->shutDown_ = true;     // a node that failed to load is not marked clean
        return nullptr;
    }
    exitCode = EXIT_STORAGE_OK;
    n->loadMempoolFile();
    return n;
}

void Node::loadMempoolFile() {
    const std::string path = networkDir_ + "/mempool.dat";
    std::string bytes;
    if (!readWholeFile(path, bytes)) return;   // none yet: normal on a first start
    std::vector<std::vector<uint8_t>> txs;
    std::string err;
    if (!decodeMempoolFile(bytes, params_.genesisHash, txs, err)) {
        logErr() << "mempool.dat ignored: " << err << "\n";
        return;
    }
    // Everything is re-validated against the current chain. A child can sit before its
    // parent in a damaged file, so retry the leftovers while a pass makes progress.
    size_t loaded = 0;
    std::vector<std::vector<uint8_t>> pending = std::move(txs);
    while (!pending.empty()) {
        std::vector<size_t> dropped;
        const size_t n = mempool_->reloadValidated(pending, *state_, dropped);
        loaded += n;
        if (n == 0) break;
        std::vector<std::vector<uint8_t>> next;
        for (size_t i : dropped) next.push_back(pending[i]);
        pending = std::move(next);
    }
    logOut() << "✓ Mempool restored: " << loaded << " transaction(s), "
             << pending.size() << " dropped\n";
}

void Node::saveMempoolFile() {
    std::string err;
    const std::string data = encodeMempoolFile(mempool_->serializedByArrival(), params_.genesisHash);
    if (!writePrivateFileAtomic(networkDir_ + "/mempool.dat", data, &err))
        logErr() << "could not save mempool.dat: " << err << "\n";
}

Node::~Node() {
    shutdown();
}

void Node::shutdown() {
    std::lock_guard<std::mutex> lock(mu_);
    if (shutDown_ || !state_) return;
    shutDown_ = true;
    saveMempoolFile();
    state_->shutdown();
}

// ---------------------------------------------------------------- chain reads

bool Node::initialBlockDownloadLocked() const {
    if (ibdDone_) return false;
    if (params_.id == NetworkId::REGTEST) return false;
    const BlockIndex* tip = state_->tip();
    const bool behind = tip->chainWork < params_.minimumChainWork ||
                        int64_t(tip->header.timestamp) < clock_() - 86'400;
    if (!behind) ibdDone_ = true;
    return behind;
}

Node::TipInfo Node::tipLocked() const {
    TipInfo t;
    const BlockIndex* tip = state_->tip();
    t.hash = tip->hash;
    t.height = tip->height;
    t.time = tip->header.timestamp;
    t.bits = tip->header.bits;
    t.chainWork = tip->chainWork;
    t.sequence = state_->tipSequence();
    t.stats = store_->stats();
    t.initialBlockDownload = initialBlockDownloadLocked();
    t.forkAlert = state_->forkAlert().active;
    t.forkHeight = state_->forkAlert().forkHeight;
    t.forkOtherTip = state_->forkAlert().otherTip;
    return t;
}

Node::TipInfo Node::tip() const {
    std::lock_guard<std::mutex> lock(mu_);
    return tipLocked();
}

uint32_t Node::height() const {
    std::lock_guard<std::mutex> lock(mu_);
    return state_->tip()->height;
}

std::optional<Block> Node::blockByHeight(uint32_t h) const {
    std::lock_guard<std::mutex> lock(mu_);
    const BlockIndex* e = state_->activeChain()[h];
    if (!e) return std::nullopt;
    std::vector<uint8_t> bytes;
    if (!store_->getBlock(e->hash, bytes)) return std::nullopt;
    try {
        return Block::deserialize(bytes);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<Block> Node::blockByHash(const Hash256& hash) const {
    std::lock_guard<std::mutex> lock(mu_);
    const BlockIndex* e = state_->find(hash);
    if (!e || !(e->status & HAVE_DATA)) return std::nullopt;
    std::vector<uint8_t> bytes;
    if (!store_->getBlock(hash, bytes)) return std::nullopt;
    try {
        return Block::deserialize(bytes);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool Node::haveBlockData(const Hash256& hash) const {
    std::lock_guard<std::mutex> lock(mu_);
    const BlockIndex* e = state_->find(hash);
    return e && (e->status & HAVE_DATA);
}

bool Node::knowsHeader(const Hash256& hash) const {
    std::lock_guard<std::mutex> lock(mu_);
    return state_->find(hash) != nullptr;
}

std::vector<Hash256> Node::locator() const {
    std::lock_guard<std::mutex> lock(mu_);
    return state_->locator();
}

std::vector<BlockHeader> Node::headersAfter(const std::vector<Hash256>& loc, const Hash256& stop,
                                            size_t max) const {
    std::lock_guard<std::mutex> lock(mu_);
    return state_->headersAfter(loc, stop, max);
}

std::vector<Hash256> Node::missingBlocksOnBestHeaderChain(size_t max) const {
    std::lock_guard<std::mutex> lock(mu_);
    return state_->missingBlocks(max);
}

uint64_t Node::circulatingSupply() const {
    std::lock_guard<std::mutex> lock(mu_);
    return store_->stats().totalAmount;
}

// ---------------------------------------------------------------- processing

size_t Node::addTipListener(TipListener f) {
    std::lock_guard<std::mutex> lock(listenerMu_);
    const size_t id = nextListenerId_++;
    tipListeners_[id] = std::move(f);
    return id;
}

void Node::removeTipListener(size_t id) {
    std::lock_guard<std::mutex> lock(listenerMu_);
    tipListeners_.erase(id);
}

size_t Node::addTxListener(TxListener f) {
    std::lock_guard<std::mutex> lock(listenerMu_);
    const size_t id = nextListenerId_++;
    txListeners_[id] = std::move(f);
    return id;
}

void Node::removeTxListener(size_t id) {
    std::lock_guard<std::mutex> lock(listenerMu_);
    txListeners_.erase(id);
}

void Node::notifyTip(uint64_t sequenceBefore) {
    Hash256 hash;
    uint32_t height;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (state_->tipSequence() == sequenceBefore) return;
        hash = state_->tip()->hash;
        height = state_->tip()->height;
    }
    std::vector<TipListener> listeners;
    {
        std::lock_guard<std::mutex> lock(listenerMu_);
        for (auto& kv : tipListeners_) listeners.push_back(kv.second);
    }
    for (auto& f : listeners) f(hash, height);
}

ValidationState Node::submitBlock(const Block& block, bool requested) {
    uint64_t before;
    ValidationState st;
    {
        std::lock_guard<std::mutex> lock(mu_);
        before = state_->tipSequence();
        st = state_->ProcessNewBlock(block, requested);
    }
    notifyTip(before);
    return st;
}

ValidationState Node::submitHeaders(const std::vector<BlockHeader>& headers) {
    std::lock_guard<std::mutex> lock(mu_);
    return state_->ProcessNewBlockHeaders(headers);
}

ValidationState Node::submitTransaction(const Transaction& tx, const AcceptOptions& options) {
    AcceptOptions o = options;
    ValidationState st;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (o.fromPeer) o.inInitialBlockDownload = initialBlockDownloadLocked();
        st = mempool_->accept(tx, *state_, o);
    }
    if (st.valid()) {
        std::vector<TxListener> listeners;
        {
            std::lock_guard<std::mutex> lock(listenerMu_);
            for (auto& kv : txListeners_) listeners.push_back(kv.second);
        }
        for (auto& f : listeners) f(tx);
    }
    return st;
}

bool Node::approveReorg(const Hash256& tipHash, std::string& err) {
    uint64_t before;
    bool ok;
    {
        std::lock_guard<std::mutex> lock(mu_);
        before = state_->tipSequence();
        ok = state_->approveReorg(tipHash, err);
    }
    notifyTip(before);
    return ok;
}

// ---------------------------------------------------------------- coins and mempool

std::vector<CoinRow> Node::coinsByHash160(const Hash160& key, size_t limit) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<CoinRow> rows;
    store_->coinsByHash160(key, limit, rows);
    return rows;
}

std::vector<CoinRow> Node::spendableCoins(const Hash160& key, size_t limit) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<CoinRow> rows;
    if (!store_->coinsByHash160(key, limit, rows)) return {};
    const uint64_t next = uint64_t(state_->tip()->height) + 1;
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const CoinRow& r) {
        if (r.coin.isCoinbase && next < uint64_t(r.coin.height) + Consensus::COINBASE_MATURITY) return true;
        return mempool_->spends(r.op);
    }), rows.end());
    return rows;
}

uint64_t Node::balance(const Hash160& key) const {
    std::lock_guard<std::mutex> lock(mu_);
    uint64_t balance = 0, count = 0;
    store_->summaryByHash160(key, balance, count);
    return balance;
}

bool Node::coinCount(const Hash160& key, uint64_t& count) const {
    std::lock_guard<std::mutex> lock(mu_);
    uint64_t balance = 0;
    return store_->summaryByHash160(key, balance, count);
}

size_t Node::mempoolSize() const {
    std::lock_guard<std::mutex> lock(mu_);
    return mempool_->size();
}

std::vector<Transaction> Node::mempoolTransactions(size_t max) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<Transaction> out;
    for (const MempoolEntry& e : mempool_->byFeeRateDescending()) {
        if (out.size() >= max) break;
        out.push_back(e.tx);
    }
    return out;
}

std::optional<Transaction> Node::mempoolTransaction(const Hash256& txid) const {
    std::lock_guard<std::mutex> lock(mu_);
    const MempoolEntry* e = mempool_->find(txid);
    if (!e) return std::nullopt;
    return e->tx;
}

// ---------------------------------------------------------------- mining

MineResult Node::mine(const Script& minerScript, const AssemblerOptions& options,
                      const std::atomic<bool>& stop, std::string& err, Hash256* found,
                      int64_t timeLimitSeconds) {
    // The API's "mine one block" and the auto-miner share one Miner; serialize them, and give up if asked to stop
    // while waiting (audit CON-5).
    std::unique_lock<std::mutex> mineLock(mineMu_, std::defer_lock);
    while (!mineLock.try_lock()) {
        if (stop.load()) return MineResult::STOPPED;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    uint64_t before;
    {
        std::lock_guard<std::mutex> lock(mu_);
        before = state_->tipSequence();
    }
    miner_->setTimeLimitSeconds(timeLimitSeconds);
    const MineResult r = miner_->mineOne(minerScript, options, stop, err);
    if (r == MineResult::FOUND && found) *found = miner_->lastBlockHash();
    notifyTip(before);
    return r;
}

} // namespace Bucks
