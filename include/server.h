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
#include <functional>

#include "node.h"
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
    size_t trackedKeys() const;

private:
    size_t maxRequests;
    std::chrono::seconds timeWindow;
    std::unordered_map<
        std::string,
        std::deque<std::chrono::steady_clock::time_point>
    > history;
    size_t callsSinceCleanup = 0;

    mutable std::mutex mutex;
};

// ------------------------------------------------------------
// Server configuration (NON-CONSENSUS)
// ------------------------------------------------------------
struct ServerConfig {
    uint16_t port = 8080;                 // 0 = any free port (see Server::port())
    size_t maxWorkers = 32;               // worker threads (0 = httplib default); slow clients are cut off by the read timeout
    size_t maxRequestSize = 1024 * 1024;  // request body limit (bytes)
    bool enableRateLimit = true;
    size_t rateLimitRequests = 300;
    std::chrono::seconds rateLimitWindow{60};
    bool enableLogging = true;
    std::string webRoot = "web";
    std::string bindAddress = "127.0.0.1";

    // ---- Data directory (wallet file, API cookie) ----
    // "" = Server::defaultDataDir() (~/.bucks). Created with mode 0700.
    std::string dataDir;
    // Pre-datadir wallet file. Read once, never written (see loadWallets()).
    std::string legacyWalletFile;           // empty: never read a wallets.json from the working directory (audit WAL-1); --import-legacy-wallets sets it

    // ---- Authentication ----
    // Token precedence: apiToken (argv) > apiTokenFile > $BUCKS_API_TOKEN >
    // a random token generated at start and written to <dataDir>/.cookie.
    std::string apiToken;
    std::string apiTokenFile;
    std::chrono::seconds sessionLifetime{12 * 3600};
    // Exact origins (scheme://host[:port]) that may call the API cross-origin.
    // Empty = no Access-Control-Allow-Origin header at all.
    std::vector<std::string> corsOrigins;
    // Extra Host header values accepted besides loopback and bindAddress.
    std::vector<std::string> allowedHosts;
    // Behind a reverse proxy on this host: believe X-Forwarded-For (rate limit key) and
    // X-Forwarded-Proto, but only on connections from loopback, where the proxy runs.
    bool trustProxy = false;
    // The browser reaches the node over TLS (a proxy in front): the session cookie is always
    // Secure and only https origins pass the CSRF check. Also on per request when a trusted
    // proxy sends X-Forwarded-Proto: https, or when the login comes from an https origin.
    bool secureCookie = false;
    // Sent on every response. The bundled UI in web/ is dependency-free ES
    // modules served by the node itself: no CDN, no inline script or style,
    // no third-party origin.
    std::string contentSecurityPolicy =
        "default-src 'self'; "
        "script-src 'self'; "
        "style-src 'self'; "
        "font-src 'self'; "
        "img-src 'self' data:; "
        "connect-src 'self'; "
        "frame-ancestors 'none'; base-uri 'none'; form-action 'self'; object-src 'none'";

    // IPFS Integration
    bool enableIPFS = false;
    uint16_t ipfsPort = 5001;
};

/**
 * Domain-separated hash for /api/wallets/sign:
 *   doubleSha256(varstr("Bucks Signed Message:\n") || varstr(message))
 * A signature over this hash can never be mistaken for a transaction
 * signature (those are over serialized transactions).
 */
Hash256 signedMessageHash(const std::string& message);

// ------------------------------------------------------------
// HTTP / REST / WebSocket Server
// ------------------------------------------------------------
class Network; // Forward declaration
class Server {
public:
    explicit Server(Node& node,
                    Network* network = nullptr,
                    const ServerConfig& cfg = {});

    ~Server();

    bool start();
    bool start(int port);
    void stop();
    bool running() const;

    // Port actually bound (differs from config.port when that is 0).
    int port() const { return boundPort; }

    // ~/.bucks (POSIX) or %APPDATA%\Bucks (Windows).
    static std::string defaultDataDir();
    const std::string& dataDir() const { return dataDirPath; }
    const std::string& walletFile() const { return walletFilePath; }
    // Path of the generated API token, "" when the token came from config/env.
    const std::string& cookieFile() const { return cookieFilePath; }

    // False when wallets.json could not be read; the file is then never
    // rewritten and every wallet route answers 503 with walletError().
    bool walletsEnabled() const;
    std::string walletError() const;

    // Optional wallet management. addWallet returns false (and keeps the
    // wallet in memory only) if the file could not be written. `meta` fields
    // (e.g. the word list a phrase needs) are stored in the wallet's entry.
    bool addWallet(const Wallet& wallet);
    bool addWallet(const Wallet& wallet, const nlohmann::json& meta);
    std::vector<Wallet> getWallets() const;

    // WebSocket Notifications
    void broadcast(const std::string& type, const nlohmann::json& data);

private:
    // ---- Core references (read-heavy, write-light) ----
    Node& node;
    Network* network_ = nullptr;
    std::chrono::steady_clock::time_point startedAt_ = std::chrono::steady_clock::now();
    size_t tipListenerId = 0;
    size_t txListenerId = 0;
    ServerConfig config;

    // ---- Wallets (optional module) ----
    std::vector<Wallet> wallets;
    mutable std::mutex walletMutex;
    std::string dataDirPath;
    std::string walletFilePath;
    nlohmann::json walletDoc;          // on-disk document; unknown fields kept
    bool walletOk = false;
    std::string walletErr;
    bool walletFromLegacy = false;     // loaded from config.legacyWalletFile
    bool walletBackupDone = false;     // one .bak per process before a rewrite

    // ---- Authentication ----
    std::string apiToken;
    bool tokenGenerated = false;
    std::string cookieFilePath;
    std::string startupError;          // set in the constructor, fails start()
    struct Session {
        std::chrono::steady_clock::time_point expiresAt;
    };
    std::unordered_map<std::string, Session> sessions;
    std::mutex sessionMutex;

    // ---- Server state ----
    std::atomic<bool> isRunning;
    std::unique_ptr<std::thread> serverThread;
    int boundPort = -1;

    // ---- Auto-mining ----
    std::atomic<bool> autoMining{false};
    std::atomic<bool> autoMineStop{false};
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
    int mineBlock(const std::string& body, nlohmann::json& out);
    void handleEconomyStatus(std::function<void(const std::string&)> response);

    // ---- Auth helpers ----
    enum class AuthKind { None, Token, Cookie };
    bool resolveApiToken();
    bool tokenMatches(const std::string& presented) const;
    static bool isPublicRoute(const std::string& path, const std::string& method);
    AuthKind authenticate(const std::string& authorization,
                          const std::string& apiTokenHeader,
                          const std::string& cookieHeader);
    bool hostAllowed(const std::string& hostHeader) const;
    bool forwardedHeadersTrusted(const std::string& remoteAddr) const;
    bool browserUsesTls(const std::string& remoteAddr, const std::string& forwardedProto) const;
    bool originAllowed(const std::string& origin, const std::string& hostHeader, bool tls) const;
    std::string sessionCookie(const std::string& value, long long maxAge, bool secure) const;
    bool corsOriginAllowed(const std::string& origin) const;
    std::string createSession();
    bool validateSession(const std::string& id);
    void destroySession(const std::string& id);

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
    bool saveWalletsLocked();          // caller holds walletMutex
    void loadWallets();
    bool loadWalletDocument(const std::string& path, bool legacy);
};

} // namespace Bucks

#endif // SERVER_H
