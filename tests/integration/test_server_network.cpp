#include <gtest/gtest.h>
#include "server.h"
#include "blockchain.h"
#include <thread>
#include <chrono>
#include <filesystem>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

class ServerNetworkTest : public ::testing::Test {
protected:
    std::string testDir = "test_server_data";

    void SetUp() override {
        if (fs::exists(testDir)) fs::remove_all(testDir);
        fs::create_directory(testDir);
    }

    void TearDown() override {
        if (fs::exists(testDir)) {
            // Retry loop for Windows file locking
            for (int i = 0; i < 5; ++i) {
                try {
                    fs::remove_all(testDir);
                    break;
                } catch (const std::exception&) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }
    }
};

// Helper to send HTTP GET
std::string sendHttpRequest(int port, const std::string& path) {
    std::string response;
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
#endif

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) >= 0) {
        std::string req = "GET " + path + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
        send(sock, req.c_str(), req.length(), 0);

        char buffer[4096];
        int valread = recv(sock, buffer, 4096, 0);
        if (valread > 0) {
            response.assign(buffer, valread);
        }
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    return response;
}

TEST_F(ServerNetworkTest, ServerStartupAndApiResponse) {
    Bucks::Blockchain chain(testDir + "/utxo.db", testDir + "/blocks.db");
    Bucks::ServerConfig config;
    config.port = 8091; // Use non-default port
    config.enableLogging = false;

    Bucks::Server server(chain, nullptr, config);
    bool started = server.start();
    ASSERT_TRUE(started);

    // Give it a moment to bind/listen
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Send request to /metrics or /block/height which serves simple JSON
    // We assume default routing handles GET / (static) or api
    std::string resp = sendHttpRequest(8091, "/metrics"); // Assuming /metrics exists or just checking connectivity

    // If /metrics doesn't exist, we expect 404 but still a response
    EXPECT_FALSE(resp.empty());
    if (!resp.empty()) {
        EXPECT_TRUE(resp.find("HTTP/1.1") != std::string::npos);
    }

    server.stop();
}
