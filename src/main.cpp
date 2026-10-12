#include <iostream>
#include <csignal>
#include <string>
#include <memory>
#include <vector>

#include "blockchain.h"
#include "chainparams.h"
#include "genesis.h"
#include "network.h"
#include "server.h"

using namespace Bucks;

static bool running = true;

void handleSignal(int) {
    running = false;
}

static void printUsage() {
    std::cout <<
        "Usage: bucks [options]\n"
        "  --network <name>          main (default), test or regtest; sets magic, ports and genesis\n"
        "  --server                  start the REST API / web UI\n"
        "  --port <n>                API port (default: per network, 8080 on main)\n"
        "  --p2p-port <n>            P2P port (default: per network, 8733 on main)\n"
        "  --connect <ip:port>       connect to a peer\n"
        "  --web-root <dir>          static UI directory (default frontend/dist)\n"
        "  --datadir <dir>           wallet file and API cookie (default ~/.bucks)\n"
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
    std::cout << "=== Bucks Node (PATH-B) ===\n";

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
                std::cerr << "✗ Unknown network: " << networkName << " (use main, test or regtest)\n";
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
             std::cerr << "⚠ --api-token puts the secret on the command line, where other users can read it; "
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
        } else if (arg == "--help" || arg == "-h") {
             printUsage();
             return 0;
        } else {
             std::cerr << "✗ Unknown or incomplete option: " << arg << "\n";
             printUsage();
             return 2;
        }
    }

    // Network parameters and genesis are verified before anything touches disk.
    // Exit code 3 means the build or datadir does not match the chosen network.
    const ChainParams& params = chainParams(networkId);
    if (port == 0) port = params.defaultApiPort;
    if (p2pPort == 0) p2pPort = params.defaultP2PPort;
    std::cout << "✓ Network: " << params.name << "\n";
    {
        std::string err;
        if (!checkChainParams(params, err) || !Genesis::check(params, err)) {
            std::cerr << "FATAL: " << err << "\n";
            if (networkId == NetworkId::MAIN)
                std::cerr << "       (use --network=test or --network=regtest until mainnet launches)\n";
            return 3;
        }
    }

    // Initialize blockchain state
    std::unique_ptr<Blockchain> chainPtr;
    try {
        chainPtr = std::make_unique<Blockchain>("utxodb.sqlite", "blockchain.db", params);
    } catch (const std::exception& e) {
        std::cerr << "FATAL: " << e.what() << "\n";
        return 3;
    }
    Blockchain& chain = *chainPtr;
    std::cout << "✓ Blockchain initialized\n";
    std::cout << "✓ Current height: " << chain.height() << "\n";

    // Start P2P network
    Network network(chain);
    network.start(p2pPort);
    std::cout << "✓ P2P network started on port " << p2pPort << "\n";

    if (!connectPeer.empty()) {
        size_t colon = connectPeer.find(':');
        if (colon != std::string::npos) {
            std::string ip = connectPeer.substr(0, colon);
            uint16_t p = static_cast<uint16_t>(std::stoi(connectPeer.substr(colon + 1)));
            if (network.connectToPeer(ip, p)) {
                std::cout << "✓ Connecting to peer " << connectPeer << "...\n";
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
        server = new Server(chain, &network, serverConfig);
        if (!server->start(port)) {
            std::cerr << "✗ Failed to start server\n";
            delete server;
            network.stop();
            return 1;
        }
        std::cout << "✓ API server started on port " << server->port() << "\n";
        std::cout << "✓ Data directory: " << server->dataDir() << "\n";
        if (!server->cookieFile().empty()) {
            std::cout << "✓ API token file: " << server->cookieFile() << "\n";
        }
        if (!server->walletsEnabled()) {
            std::cerr << "⚠ Wallet subsystem disabled: " << server->walletError() << "\n";
        }

        if (autoMine) {
            std::cout << "✓ Node configured for mining (rewards → primary wallet)\n";
            server->startAutoMine();
        }
    }

    // Main loop
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\nShutting down...\n";
    if (server) {
        server->stop();
        delete server;
    }
    network.stop();

    std::cout << "✓ Node stopped cleanly\n";
    return 0;
}
