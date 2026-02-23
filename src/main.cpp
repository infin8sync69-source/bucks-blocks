#include <iostream>
#include <csignal>
#include <string>

#include "blockchain.h"
#include "network.h"
#include "server.h"

using namespace Bucks;

static bool running = true;

void handleSignal(int) {
    running = false;
}

int main(int argc, char* argv[]) {
    std::cout << "=== Bucks Node (PATH-B) ===\n";

    // Handle shutdown signals
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    // Parse command line arguments
    bool startServer = false;
    uint16_t port = 8080;
    uint16_t p2pPort = 8333;
    std::string connectPeer = "";
    std::string webRoot = "web";
    bool enableIPFS = false;
    uint16_t ipfsPort = 5001;
    bool autoMine = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--server") {
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
        }
    }

    // Initialize blockchain state
    Blockchain chain;
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
        server = new Server(chain, &network, serverConfig);
        if (!server->start(port)) {
            std::cerr << "✗ Failed to start server\n";
            return 1;
        }
        std::cout << "✓ API server started on port " << port << "\n";
        std::cout << "✓ Web UI available at http://localhost:" << port << "\n";

        if (autoMine) {
            std::cout << "✓ Node configured for mining (rewards → primary wallet)\n";
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
