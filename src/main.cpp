#include <iostream>
#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>
#include <string>
#include <memory>
#include <vector>

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
        "  --connect <ip:port>       connect to a peer\n"
        "  --web-root <dir>          static UI directory (default frontend/dist)\n"
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
        "  --ipfs-enable, --ipfs-port <n>, --auto-mine\n"
        "Without --api-token/--api-token-file/$BUCKS_API_TOKEN a random token is\n"
        "written to <datadir>/.cookie at start; the web UI asks for it.\n";
}

int main(int argc, char* argv[]) {
    Bucks::logOut() << "=== Bucks Node (PATH-B) ===\n";

    // Handle shutdown signals
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    // Parse command line arguments
    bool startServer = false;
    NetworkId networkId = NetworkId::MAIN;
    uint16_t port = 0;      // 0 = the network default
    uint16_t p2pPort = 0;
    std::string connectPeer = "";
    std::string webRoot = "frontend/dist";
    bool enableIPFS = false;
    uint16_t ipfsPort = 5001;
    bool autoMine = false;
    std::string apiBind = "127.0.0.1";
    std::string apiToken;
    std::string apiTokenFile;
    std::string dataDir;
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
            connectPeer = argv[++i];
        } else if (arg == "--web-root" && i + 1 < argc) {
             webRoot = argv[++i];
        } else if (arg == "--ipfs-enable") {
             enableIPFS = true;
        } else if (arg == "--ipfs-port" && i + 1 < argc) {
             ipfsPort = static_cast<uint16_t>(std::stoi(argv[++i]));
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
    network.start(p2pPort);
    Bucks::logOut() << "✓ P2P network started on port " << p2pPort << "\n";

    if (!connectPeer.empty()) {
        size_t colon = connectPeer.find(':');
        if (colon != std::string::npos) {
            std::string ip = connectPeer.substr(0, colon);
            uint16_t p = static_cast<uint16_t>(std::stoi(connectPeer.substr(colon + 1)));
            if (network.connectToPeer(ip, p)) {
                Bucks::logOut() << "✓ Connecting to peer " << connectPeer << "...\n";
            }
        }
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
        serverConfig.corsOrigins = corsOrigins;
        serverConfig.allowedHosts = allowedHosts;
        serverConfig.trustProxy = trustProxy;
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
