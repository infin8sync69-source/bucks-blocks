#include <iostream>
#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>
#include <string>
#include <memory>
#include <vector>

#include "crypto.h"
#include "node.h"
#include "log.h"
#include "chainparams.h"
#include "genesis.h"
#include "network.h"
#include "server.h"

using namespace Bucks;

static std::atomic<bool> running{true};   // lock-free: safe to store from a signal handler

void handleSignal(int) {
    running.store(false);
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
        "  --web-root <dir>          static UI directory (default: web/ next to the sources)\n"
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
        "  --trust-proxy             rate-limit on X-Forwarded-For (behind a trusted proxy only)\n"
        "  --auto-mine               mine from the start (implies --server); needs --mine-address or a wallet\n"
        "  --mine-address <addr>     pay every mined block to this address: no wallet or key on this node\n"
        "  --mine-threads <n>        hashing threads (default 0 = one per CPU core)\n"
        "  --ipfs-enable, --ipfs-port <n>\n"
        "Without --api-token/--api-token-file/$BUCKS_API_TOKEN a random token is\n"
        "written to <datadir>/.cookie at start; the web UI asks for it.\n";
}

int main(int argc, char* argv[]) {
    Bucks::logOut() << "=== Bucks Node (PATH-B) ===\n";

    // Handle shutdown signals
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);   // a peer closing mid-send is an error return, not a crash
#endif
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    // Parse command line arguments
    bool startServer = false;
    NetworkId networkId = NetworkId::MAIN;
    uint16_t port = 0;      // 0 = the network default
    uint16_t p2pPort = 0;
    std::vector<std::string> connectPeers;
    std::vector<std::string> seedPeers;
#ifdef BUCKS_SOURCE_DIR
    std::string webRoot = std::string(BUCKS_SOURCE_DIR) + "/web";
#else
    std::string webRoot = "web";
#endif
    bool enableIPFS = false;
    uint16_t ipfsPort = 5001;
    bool autoMine = false;
    std::string mineAddress;        // --mine-address: coinbase payout, no wallet needed
    unsigned mineThreads = 0;       // --mine-threads: 0 = one per CPU
    std::string apiBind = "127.0.0.1";
    std::string apiToken;
    std::string apiTokenFile;
    std::string dataDir;
    std::string legacyWallets;      // --import-legacy-wallets <file>: read-only import of an old wallets.json
    std::vector<std::string> corsOrigins;
    std::vector<std::string> allowedHosts;
    bool trustProxy = false;
    LoadOptions loadOptions;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
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
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--p2p-port" && i + 1 < argc) {
            p2pPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--connect" && i + 1 < argc) {
            connectPeers.push_back(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            seedPeers.push_back(argv[++i]);
        } else if (arg == "--web-root" && i + 1 < argc) {
             webRoot = argv[++i];
        } else if (arg == "--ipfs-enable") {
             enableIPFS = true;
        } else if (arg == "--ipfs-port" && i + 1 < argc) {
             ipfsPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--auto-mine") {
             autoMine = true;
             startServer = true; // auto-mine implies server mode
        } else if (arg == "--mine-address" && i + 1 < argc) {
             mineAddress = argv[++i];
        } else if (arg == "--mine-threads" && i + 1 < argc) {
             const std::string v = argv[++i];
             if (v.empty() || v.size() > 4 || v.find_first_not_of("0123456789") != std::string::npos ||
                 std::stoul(v) > 1024) {
                 Bucks::logErr() << "✗ --mine-threads expects 0 (one per CPU) to 1024, got '" << v << "'\n";
                 return 2;
             }
             mineThreads = static_cast<unsigned>(std::stoul(v));
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
        } else if (arg == "--checkblocks" && i + 1 < argc) {
             loadOptions.checkBlocks = static_cast<uint32_t>(std::stoul(argv[++i]));
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

    // The hash and signature primitives are consensus: a broken crypto library must not get near the chain
    // (audit NEW-4). Exit code 3 like a genesis mismatch: this build cannot follow the network.
    {
        std::string err;
        if (!cryptoSelfTest(err)) {
            Bucks::logErr() << "FATAL: cryptographic self-test failed: " << err
                            << " (check the OpenSSL library this binary runs with)\n";
            return 3;
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
    if (!mineAddress.empty()) {
        try {
            Wallet::addressToP2PKHScript(mineAddress, params.p2pkhAddressVersion);
        } catch (const std::exception&) {
            Bucks::logErr() << "✗ --mine-address " << mineAddress << " is not a valid " << params.name
                            << " address (a typo, or an address of another network)\n";
            return 2;
        }
    }
    if (!startServer && (!mineAddress.empty() || mineThreads))
        Bucks::logErr() << "⚠ --mine-address and --mine-threads only apply with --server or --auto-mine\n";

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
        try {
            const int p = std::stoi(hp.substr(colon + 1));
            if (p < 1 || p > 65535) return false;
            port = static_cast<uint16_t>(p);
        } catch (...) {
            return false;
        }
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
        serverConfig.mineAddress = mineAddress;
        serverConfig.mineThreads = mineThreads;
        server = new Server(node, &network, serverConfig);
        // --auto-mine with nowhere to pay the rewards is a configuration error: refuse to start
        // instead of running a node that silently does not mine (audit OPS-03).
        std::string mineErr;
        if (autoMine && !server->minerPayoutReady(mineErr)) {
            Bucks::logErr() << "✗ --auto-mine: " << mineErr << "\n"
                            << "  (on a server prefer --mine-address <address>: the reward key then never sits on it)\n";
            delete server;
            network.stop();
            node.shutdown();
            return 2;
        }
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

        if (autoMine && !server->startAutoMine(mineErr)) {
            Bucks::logErr() << "✗ --auto-mine: " << mineErr << "\n";
            server->stop();
            delete server;
            network.stop();
            node.shutdown();
            return 1;
        }
    }

    // Main loop. A fatal chain-state error (a failed write, stored data that cannot be read back) stops the
    // node: a miner, API and peers working on a chain that can no longer move would be misled (audit CON-4).
    int exitCode = 0;
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::string why;
        if ((exitCode = node.fatalError(why)) != 0) {
            Bucks::logErr() << "FATAL: " << why << "; stopping with exit code " << exitCode
                            << " (the next start runs the crash checks)\n";
            break;
        }
    }

    Bucks::logOut() << "\nShutting down...\n";
    if (server) {
        server->stop();
        delete server;
    }
    network.stop();
    node.shutdown();

    if (exitCode) return exitCode;
    Bucks::logOut() << "✓ Node stopped cleanly\n";
    return 0;
}
