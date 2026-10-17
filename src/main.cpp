#include <iostream>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <thread>
#include <string>
#include <memory>
#include <vector>
#include <filesystem>

#include "bip_8192.h"
#include "fsutil.h"
#include "node.h"
#include "log.h"
#include "chainparams.h"
#include "genesis.h"
#include "network.h"
#include "server.h"
#include "version.h"

using namespace Bucks;

static std::atomic<bool> running{true};   // lock-free: safe to store from a signal handler
static std::atomic<bool> hangup{false};

void handleSignal(int) {
    running.store(false);
}

void handleHangup(int) {
    hangup.store(true);
}

// A whole decimal number in [lo, hi]. Signs, spaces, trailing characters and overflow are refused, so a
// typo cannot abort the process or wrap a port around (audit OPS-13).
static bool parseNumber(const std::string& text, uint64_t lo, uint64_t hi, uint64_t& out) {
    if (text.empty() || text.size() > 20) return false;
    uint64_t v = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (v > (UINT64_MAX - digit) / 10) return false;
        v = v * 10 + digit;
    }
    if (v < lo || v > hi) return false;
    out = v;
    return true;
}

static void printUsage() {
    Bucks::logOut() <<
        "Usage: bucks [options]\n"
        "  --network <name>          main (default), test or regtest; sets magic, ports and genesis\n"
        "  --server                  start the REST API / web UI\n"
        "  --port <n>                API port (default: per network, 8080 on main)\n"
        "  --p2p-port <n>            P2P port (default: per network, 8733 on main)\n"
        "  --connect <host:port>     keep a connection to this peer (repeatable)\n"
        "  --seed <host:port>        seed node, dialled while there are fewer than 3 peers (repeatable)\n"
        "  --web-root <dir>          static UI directory (default: the source tree's web/, else\n"
        "                            <exe dir>/../share/bucks/web)\n"
        "  --wordlist-dir <dir>      mnemonic word lists (default: $BUCKS_WORDLIST_DIR, the source tree's\n"
        "                            wordlists/, else <exe dir>/../share/bucks/wordlists)\n"
        "  --import-legacy-wallets <file>  read an old wallets.json once (never written)\n"
        "  --datadir <dir>           wallets, API cookie and <network>/chain.sqlite (default ~/.bucks)\n"
        "  --checkblocks <n>         verify the last n blocks at start (default 24, 0 = all)\n"
        "  --checkdb[=full]          heavier database checks at start\n"
        "  --reindex-chainstate      rebuild coins, undo and statistics from the stored blocks\n"
        "  --reindex                 rebuild the block index from the stored block bytes\n"
        "  --api-bind <addr>         API bind address (default 127.0.0.1)\n"
        "  --api-token-file <file>   read the API token from a file (0600)\n"
        "  --api-token <token>       API token on the command line (visible to other users!)\n"
        "  --api-cors-origin <o>     allow this exact origin cross-origin (repeatable)\n"
        "  --api-allowed-host <h>    accept this Host header (repeatable)\n"
        "  --trust-proxy             a reverse proxy on this host: believe X-Forwarded-For (rate limit)\n"
        "                            and X-Forwarded-Proto: https (Secure cookie) from loopback peers\n"
        "  --api-secure-cookie       the console is served over TLS: Secure session cookie, https origins only\n"
        "  --ipfs-enable, --ipfs-port <n>, --auto-mine\n"
        "Signals: SIGINT and SIGTERM stop the node cleanly. SIGHUP is logged and ignored, so closing\n"
        "the terminal does not stop it (there is no configuration to reload).\n"
        "Without --api-token/--api-token-file/$BUCKS_API_TOKEN a random token is\n"
        "written to <datadir>/.cookie at start; the web UI asks for it.\n";
}

int main(int argc, char* argv[]) {
    Bucks::logOut() << "=== Bucks Node " << BUCKS_VERSION << " ===\n";

    // Handle shutdown signals
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);   // a peer closing mid-send is an error return, not a crash
#endif
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
#ifdef SIGHUP
    // The conventional daemon answer: a hangup (closed SSH session) does not stop the node (audit OPS-12).
    std::signal(SIGHUP, handleHangup);
#endif

    // Parse command line arguments
    bool startServer = false;
    NetworkId networkId = NetworkId::MAIN;
    uint16_t port = 0;      // 0 = the network default
    uint16_t p2pPort = 0;
    std::vector<std::string> connectPeers;
    std::vector<std::string> seedPeers;
    std::string webRoot;        // "" = the bundled UI (see below)
    std::string wordlistDir;    // "" = $BUCKS_WORDLIST_DIR or the bundled lists
    bool enableIPFS = false;
    uint16_t ipfsPort = 5001;
    bool autoMine = false;
    std::string apiBind = "127.0.0.1";
    std::string apiToken;
    std::string apiTokenFile;
    std::string dataDir;
    std::string legacyWallets;      // --import-legacy-wallets <file>: read-only import of an old wallets.json
    std::vector<std::string> corsOrigins;
    std::vector<std::string> allowedHosts;
    bool trustProxy = false;
    bool secureCookie = false;
    LoadOptions loadOptions;

    // Reads the value after option `name`; false (after saying why) unless it is a number in [lo, hi].
    auto numberArg = [&](int& i, const std::string& name, uint64_t lo, uint64_t hi, uint64_t& out) {
        const std::string text = argv[++i];
        if (parseNumber(text, lo, hi, out)) return true;
        Bucks::logErr() << "✗ " << name << " expects a whole number from " << lo << " to " << hi
                        << ", got '" << text << "'\n";
        return false;
    };

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        uint64_t number = 0;
        std::string networkName;
        if (arg == "--network" && i + 1 < argc) {
            networkName = argv[++i];
        } else if (arg.rfind("--network=", 0) == 0) {
            networkName = arg.substr(10);
        }
        if (!networkName.empty()) {
            if (!parseNetworkName(networkName, networkId)) {
                Bucks::logErr() << "✗ Unknown network: " << networkName << " (use main, test or regtest)\n";
                return 2;
            }
        } else if (arg == "--server") {
            startServer = true;
        } else if (arg == "--port" && i + 1 < argc) {
            if (!numberArg(i, arg, 1, 65535, number)) return 2;
            port = static_cast<uint16_t>(number);
        } else if (arg == "--p2p-port" && i + 1 < argc) {
            if (!numberArg(i, arg, 1, 65535, number)) return 2;
            p2pPort = static_cast<uint16_t>(number);
        } else if (arg == "--connect" && i + 1 < argc) {
            connectPeers.push_back(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            seedPeers.push_back(argv[++i]);
        } else if (arg == "--web-root" && i + 1 < argc) {
             webRoot = argv[++i];
        } else if (arg == "--wordlist-dir" && i + 1 < argc) {
             wordlistDir = argv[++i];
        } else if (arg == "--ipfs-enable") {
             enableIPFS = true;
        } else if (arg == "--ipfs-port" && i + 1 < argc) {
             if (!numberArg(i, arg, 1, 65535, number)) return 2;
             ipfsPort = static_cast<uint16_t>(number);
        } else if (arg == "--auto-mine") {
             autoMine = true;
             startServer = true; // auto-mine implies server mode
        } else if (arg == "--api-bind" && i + 1 < argc) {
             apiBind = argv[++i];
        } else if (arg == "--api-token" && i + 1 < argc) {
             apiToken = argv[++i];
             // The token stays visible in `ps` for every local user (API-13).
             Bucks::logErr() << "⚠ --api-token puts the secret on the command line, where other users can read it; "
                          "prefer --api-token-file or the generated <datadir>/.cookie\n";
        } else if (arg == "--api-token-file" && i + 1 < argc) {
             apiTokenFile = argv[++i];
        } else if (arg == "--datadir" && i + 1 < argc) {
             dataDir = argv[++i];
        } else if (arg == "--import-legacy-wallets" && i + 1 < argc) {
            legacyWallets = argv[++i];
        } else if (arg == "--api-cors-origin" && i + 1 < argc) {
             corsOrigins.push_back(argv[++i]);
        } else if (arg == "--api-allowed-host" && i + 1 < argc) {
             allowedHosts.push_back(argv[++i]);
        } else if (arg == "--trust-proxy") {
             trustProxy = true;
        } else if (arg == "--api-secure-cookie") {
             secureCookie = true;
        } else if (arg == "--checkblocks" && i + 1 < argc) {
             if (!numberArg(i, arg, 0, UINT32_MAX, number)) return 2;
             loadOptions.checkBlocks = static_cast<uint32_t>(number);
        } else if (arg == "--checkdb") {
             loadOptions.checkTier = 2;
        } else if (arg == "--checkdb=full") {
             loadOptions.checkTier = 3;
        } else if (arg == "--reindex-chainstate") {
             loadOptions.reindexChainstate = true;
        } else if (arg == "--reindex") {
             loadOptions.reindex = true;
        } else if (arg == "--help" || arg == "-h") {
             printUsage();
             return 0;
        } else {
             Bucks::logErr() << "✗ Unknown or incomplete option: " << arg << "\n";
             printUsage();
             return 2;
        }
    }

    // Network parameters and genesis are verified before anything touches disk.
    // Exit code 3 means the build or datadir does not match the chosen network.
    const ChainParams& params = chainParams(networkId);
    if (port == 0) port = params.defaultApiPort;
    if (p2pPort == 0) p2pPort = params.defaultP2PPort;
    Bucks::logOut() << "✓ Network: " << params.name << "\n";
    {
        std::string err;
        if (!checkChainParams(params, err) || !Genesis::check(params, err)) {
            Bucks::logErr() << "FATAL: " << err << "\n";
            if (networkId == NetworkId::MAIN)
                Bucks::logErr() << "       (use --network=test or --network=regtest until mainnet launches)\n";
            return 3;
        }
    }

    // Web UI and word lists: an explicit option wins, then the source tree the binary was built from, then
    // the install layout <prefix>/share/bucks/ (audit OPS-06). Creating or restoring a wallet needs the lists,
    // so a server whose lists are missing or do not match their pins stops here, before touching the chain.
    if (!wordlistDir.empty()) setWordlistDir(wordlistDir);
    if (startServer) {
        if (webRoot.empty()) {
            const std::vector<std::string> candidates = bundledDirCandidates("web");
            std::error_code ec;
            for (const std::string& c : candidates) {
                if (std::filesystem::is_regular_file(std::filesystem::path(c) / "index.html", ec)) { webRoot = c; break; }
            }
            if (webRoot.empty()) {
                std::string tried;
                for (const std::string& c : candidates) tried += (tried.empty() ? "" : ", ") + c;
                Bucks::logErr() << "⚠ No web UI found (looked in: " << tried << "); pass --web-root <dir>\n";
                webRoot = candidates.empty() ? "web" : candidates.back();
            }
        }
        std::string err;
        if (!loadWordlists(&err)) {
            Bucks::logErr() << "FATAL: " << err << "\n"
                            << "       (pass --wordlist-dir <dir>; an installed node has them in <prefix>/share/bucks/wordlists)\n";
            return 1;
        }
        Bucks::logOut() << "✓ Word lists: " << resolveWordlistDir() << "\n";
    }

    // Open the chain: <datadir>/<network>/chain.sqlite. Exit codes (storage spec section 4):
    // 3 parameters/genesis/network mismatch, 4 storage failure, 6 database corrupt, 7 unsupported version.
    if (dataDir.empty()) dataDir = Server::defaultDataDir();
    const std::string networkDir = dataDir + "/" + params.name;
    std::string openError;
    int openCode = 0;
    std::unique_ptr<Node> nodePtr = Node::open(networkDir, params, loadOptions, openError, openCode);
    if (!nodePtr) {
        Bucks::logErr() << "FATAL: " << openError << "\n";
        return openCode ? openCode : 4;
    }
    Node& node = *nodePtr;
    Bucks::logOut() << "✓ Chain opened at " << networkDir << "\n";
    Bucks::logOut() << "✓ Current height: " << node.height() << "\n";

    // Start P2P network
    Network network(node);
    try {
        network.start(p2pPort);
    } catch (const std::exception& e) {
        Bucks::logErr() << "FATAL: " << e.what() << "\n";
        node.shutdown();
        return 5;
    }
    Bucks::logOut() << "✓ P2P network started on port " << p2pPort << "\n";

    auto splitHostPort = [](const std::string& hp, std::string& host, uint16_t& port) {
        const size_t colon = hp.rfind(':');
        if (colon == std::string::npos || colon == 0) return false;
        uint64_t p = 0;
        if (!parseNumber(hp.substr(colon + 1), 1, 65535, p)) return false;
        port = static_cast<uint16_t>(p);
        host = hp.substr(0, colon);
        return true;
    };
    for (const std::string& hp : seedPeers) {
        std::string host;
        uint16_t p = 0;
        if (!splitHostPort(hp, host, p)) {
            Bucks::logErr() << "FATAL: --seed expects host:port, got '" << hp << "'\n";
            network.stop();
            return 2;
        }
        network.addSeedNode(hp);
    }
    for (const std::string& hp : connectPeers) {
        std::string host;
        uint16_t p = 0;
        if (!splitHostPort(hp, host, p)) {
            Bucks::logErr() << "FATAL: --connect expects host:port, got '" << hp << "'\n";
            network.stop();
            return 2;
        }
        network.addPersistentPeer(host, p);
        Bucks::logOut() << "✓ Keeping a connection to peer " << hp << "\n";
    }

    Server* server = nullptr;
    if (startServer) {
        // Start HTTP / RPC server
        ServerConfig serverConfig;
        serverConfig.webRoot = webRoot;
        serverConfig.enableIPFS = enableIPFS;
        serverConfig.ipfsPort = ipfsPort;
        serverConfig.bindAddress = apiBind;
        serverConfig.apiToken = apiToken;
        serverConfig.apiTokenFile = apiTokenFile;
        serverConfig.dataDir = dataDir;
        serverConfig.legacyWalletFile = legacyWallets;
        serverConfig.corsOrigins = corsOrigins;
        serverConfig.allowedHosts = allowedHosts;
        serverConfig.trustProxy = trustProxy;
        serverConfig.secureCookie = secureCookie;
        server = new Server(node, &network, serverConfig);
        if (!server->start(port)) {
            Bucks::logErr() << "✗ Failed to start server\n";
            delete server;
            network.stop();
            return 1;
        }
        Bucks::logOut() << "✓ API server started on port " << server->port() << "\n";
        Bucks::logOut() << "✓ Data directory: " << server->dataDir() << "\n";
        if (!server->cookieFile().empty()) {
            Bucks::logOut() << "✓ API token file: " << server->cookieFile() << "\n";
        }
        if (!server->walletsEnabled()) {
            Bucks::logErr() << "⚠ Wallet subsystem disabled: " << server->walletError() << "\n";
        }

        if (autoMine) {
            Bucks::logOut() << "✓ Node configured for mining (rewards → primary wallet)\n";
            server->startAutoMine();
        }
    }

    // Main loop
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (hangup.exchange(false))
            Bucks::logOut() << "SIGHUP received and ignored; stop the node with SIGINT or SIGTERM\n";
    }

    Bucks::logOut() << "\nShutting down...\n";
    if (server) {
        server->stop();
        delete server;
    }
    network.stop();
    node.shutdown();

    Bucks::logOut() << "✓ Node stopped cleanly\n";
    return 0;
}
