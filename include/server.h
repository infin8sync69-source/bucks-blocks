#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <deque>
#include <chrono>
#include <unordered_map>
#include <memory>
#include <fstream>

#include "blockchain.h"
#include "wallet.h"

#include <nlohmann/json.hpp>
#include <set>

namespace Bucks {

// ------------------------------------------------------------
// Rate limiter (API protection only)
// ------------------------------------------------------------
class RateLimiter {
public:
    RateLimiter(size_t maxRequests,
                std::chrono::seconds window);

    bool allowRequest(const std::string& key);
    void cleanup();

private:
    size_t maxRequests;
    std::chrono::seconds timeWindow;
    std::unordered_map<
        std::string,
        std::deque<std::chrono::steady_clock::time_point>
    > history;

    std::mutex mutex;
};

// ------------------------------------------------------------
// Server configuration (NON-CONSENSUS)
// ------------------------------------------------------------
struct ServerConfig {
    uint16_t port = 8080;
    size_t maxWorkers = 0;
    size_t maxRequestSize = 1024 * 1024;
    bool enableRateLimit = true;
    size_t rateLimitRequests = 100;
    std::chrono::seconds rateLimitWindow{60};
    bool enableCORS = true;
    bool enableLogging = true;
    std::string webRoot = "web";
    
    // IPFS Integration
    bool enableIPFS = false;
    uint16_t ipfsPort = 5001;
};

// ------------------------------------------------------------
// HTTP / REST / WebSocket Server
// ------------------------------------------------------------
class Network; // Forward declaration
class Server {
public:
    explicit Server(Blockchain& chain,
                    Network* network = nullptr,
                    const ServerConfig& cfg = {});

    ~Server();

    bool start();
    bool start(int port);
    void stop();
    bool running() const;

    // Optional wallet management
    void addWallet(const Wallet& wallet);
    std::vector<Wallet> getWallets() const;

    // WebSocket Notifications
    void broadcast(const std::string& type, const nlohmann::json& data);

private:
    // ---- Core references (read-heavy, write-light) ----
    Blockchain& chain;
    Network* network;
    ServerConfig config;

    // ---- Wallets (optional module) ----
    std::vector<Wallet> wallets;
    mutable std::mutex walletMutex;

    // ---- Server state ----
    std::atomic<bool> isRunning;
    std::unique_ptr<std::thread> serverThread;

    // ---- Auto-mining ----
    std::atomic<bool> autoMining{false};
    std::thread autoMinerThread;
    std::atomic<uint64_t> blocksFound{0};
    std::atomic<uint64_t> hashesPerSecond{0};
    std::string minerAddress;

    // httplib server instance (pointer to avoid including implementation in header if possible, 
    // but httplib is header-only so we can't easily forward declare 'Server' without the header.
    // For simplicity in this single-header plan, we'll use void* and cast it in cpp, 
    // OR include httplib.h here. 
    // Better approach to keep header clean: Pimpl idiom or just void* since it's an internal detail.
    // However, to avoid complexity with void* casting everywhere, let's include httplib.h in the .cpp 
    // and use a unique_ptr to a wrapper struct or just void*.
    // actually, let's just use void* for the server instance to avoid including the massive httplib.h in server.h
    void* svr; 

    // ---- WebSocket ----
    std::mutex wsMutex;
    // We need to store connection handles. httplib uses void* for connection IDs usually or shared_ptr.
    // We will maintain a set of active connection pointers (casted to void* for storage here).
    std::set<void*> wsConnections;

    // ---- Rate limiting ----
    std::unique_ptr<RateLimiter> rateLimiter;

    // ---- Anchoring ----
    std::vector<std::string> pendingCIDs;
    mutable std::mutex anchorMutex;

    // ---- API endpoints (Implementation helpers) ----
    void setupRoutes();
    void mineBlock(const std::string& body, std::function<void(const std::string&, const std::string&)> response);
    void handleEconomyStatus(std::function<void(const std::string&)> response);

    // ---- Auto-mining control ----
public:
    void startAutoMine();
    void stopAutoMine();
private:
    void autoMineLoop();

    // IPFS Helper
    bool checkIPFSStatus(std::string& peerId);
    bool pinCID(const std::string& cid);

    // ---- JSON conversion helpers ----
    nlohmann::json blockToJSON(const Block& b) const;
    nlohmann::json txToJSON(const Transaction& tx) const;

    // ---- Wallet persistence ----
    void saveWallets() const;
    void loadWallets();
};

} // namespace Bucks

#endif // SERVER_H
