// REST API authentication, wallet storage and hardening (Phase 1):
// API-1/OPS-2/CRY-8 (loopback trust, CORS *, Host), API-2 (mnemonic exposure),
// API-3/OPS-4/CRY-7/DB-17 (wallet file), API-4 (atomic writes, corrupt file),
// API-5/OPS-3/CRY-9 (mining overwrote the primary key), API-9 (body limit,
// rate limiter), API-10 (signing oracle), API-11 (restore validation),
// API-13 (constant-time token), API-15 (security headers), Server::start.
#include <gtest/gtest.h>
#include "server.h"
#include "node.h"
#include "wallet.h"
#include "crypto.h"
#include "bip_8192.h"
#include "httplib.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <atomic>
#include <cstdlib>
#include <thread>
#include <chrono>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string uniqueTempDir(const std::string& tag) {
    static std::atomic<int> counter{0};
#ifdef _WIN32
    int pid = 0;
#else
    int pid = static_cast<int>(::getpid());
#endif
    fs::path base = fs::temp_directory_path() /
        ("bucks-" + tag + "-" + std::to_string(pid) + "-" + std::to_string(counter++));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base);
    return base.string();
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void writeFile(const std::string& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

#ifndef _WIN32
unsigned fileMode(const std::string& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) return 0xFFFF;
    return st.st_mode & 0777;
}
#endif

// "bucks_session=abc; Path=/; HttpOnly" -> "bucks_session=abc"
std::string cookiePair(const std::string& setCookie) {
    return setCookie.substr(0, setCookie.find(';'));
}

class ServerAuthTest : public ::testing::Test {
protected:
    std::string dir;
    std::unique_ptr<Bucks::Node> chain;
    Bucks::ServerConfig cfg;
    const std::string token = "test-token-0123456789abcdef0123456789abcdef";
    const httplib::Headers tokenHeader{{"X-API-Token", "test-token-0123456789abcdef0123456789abcdef"}};

    void SetUp() override {
#ifndef _WIN32
        ::unsetenv("BUCKS_API_TOKEN");
#endif
        dir = uniqueTempDir("api");
        std::string chainError;
        int chainCode = 0;
        chain = Bucks::Node::open(dir + "/chain", Bucks::chainParams(Bucks::NetworkId::REGTEST), {},
                                  chainError, chainCode);
        ASSERT_TRUE(chain) << chainError;
        cfg.port = 0;                                   // random free port
        cfg.enableLogging = false;
        cfg.dataDir = dir + "/data";
        cfg.webRoot = dir + "/web";                     // no UI needed
        cfg.legacyWalletFile = dir + "/legacy-wallets.json"; // absent unless a test writes it
        cfg.apiToken = token;
    }

    void TearDown() override {
        chain.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::string origin(const Bucks::Server& s) const {
        return "http://127.0.0.1:" + std::to_string(s.port());
    }

    // Creates a wallet through the API and returns the response JSON.
    json createWallet(httplib::Client& cli) {
        auto res = cli.Post("/api/wallets/create", tokenHeader, "{}", "application/json");
        EXPECT_TRUE(res) << "no response";
        EXPECT_EQ(res->status, 200) << res->body;
        return json::parse(res->body);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Authentication and origin checks
// ---------------------------------------------------------------------------

TEST_F(ServerAuthTest, LoopbackWithoutTokenIsRejected) {
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());

    // Cross-origin "simple" POSTs (text/plain, no preflight) from a web page
    httplib::Headers evil{{"Origin", "https://evil.example"}};
    auto r = cli.Post("/api/wallets/create", evil, "{}", "text/plain");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401);
    r = cli.Post("/api/mining/mine", evil, "{}", "text/plain");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401);
    r = cli.Post("/api/transactions/send", evil, "{\"from\":\"a\",\"to\":\"b\",\"amount\":1}", "text/plain");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401);
    r = cli.Post("/api/wallets/restore", evil, "{\"mnemonic\":\"x\"}", "text/plain");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401);

    // Sensitive reads from loopback with no credential
    r = cli.Get("/api/wallets/primary");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401);
    r = cli.Get("/api/wallets");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401);
    r = cli.Get("/api/transactions");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401);

    // The read-only allowlist still works without a credential
    r = cli.Get("/api/status");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200);
    r = cli.Get("/api/blockchain/info");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200);
    r = cli.Get("/api/economy/status");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200);
    r = cli.Get("/api/mining/status");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200);

    server.stop();
}

TEST_F(ServerAuthTest, HostHeaderAndCorsAreRestricted) {
    cfg.corsOrigins = {"https://console.example"};
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());

    // DNS rebinding: a foreign Host is refused even on public routes
    auto r = cli.Get("/api/status", {{"Host", "evil.example"}});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 403);
    r = cli.Get("/api/status", {{"Host", "localhost:" + std::to_string(server.port())}});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200);
    r = cli.Get("/api/status", {{"Host", "localhost:9"}}); // wrong port
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 403);

    // No ACAO for an unknown origin, on responses and on preflights
    r = cli.Get("/api/status", {{"Origin", "https://evil.example"}});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200);
    EXPECT_FALSE(r->has_header("Access-Control-Allow-Origin"));
    r = cli.Options("/api/wallets/create", {{"Origin", "https://evil.example"},
                                            {"Access-Control-Request-Method", "POST"}});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 403);
    EXPECT_FALSE(r->has_header("Access-Control-Allow-Origin"));

    // Exact configured origin only
    r = cli.Get("/api/status", {{"Origin", "https://console.example"}});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->get_header_value("Access-Control-Allow-Origin"), "https://console.example");
    r = cli.Get("/api/status", {{"Origin", "https://console.example.attacker"}});
    ASSERT_TRUE(r);
    EXPECT_FALSE(r->has_header("Access-Control-Allow-Origin"));

    server.stop();
}

TEST_F(ServerAuthTest, SecurityHeadersOnEveryResponse) {
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());

    auto r = cli.Get("/api/status");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->get_header_value("X-Frame-Options"), "DENY");
    EXPECT_EQ(r->get_header_value("X-Content-Type-Options"), "nosniff");
    EXPECT_EQ(r->get_header_value("Referrer-Policy"), "no-referrer");
    EXPECT_NE(r->get_header_value("Content-Security-Policy").find("frame-ancestors 'none'"), std::string::npos);
    EXPECT_EQ(r->get_header_value("Cache-Control"), "no-store");

    // httplib's own 404 gets them too
    r = cli.Get("/no-such-page");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 404);
    EXPECT_EQ(r->get_header_value("X-Frame-Options"), "DENY");

    // Handler exceptions do not leak e.what() in a header
    r = cli.Get("/api/wallets", tokenHeader);
    ASSERT_TRUE(r);
    EXPECT_FALSE(r->has_header("EXCEPTION_WHAT"));

    server.stop();
}

TEST_F(ServerAuthTest, HeaderTokenWorks) {
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());

    auto r = cli.Get("/api/wallets", tokenHeader);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200);

    r = cli.Get("/api/wallets", {{"Authorization", "Bearer " + token}});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200);

    r = cli.Get("/api/wallets", {{"X-API-Token", token + "x"}});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401);
    r = cli.Get("/api/wallets", {{"X-API-Token", token.substr(0, token.size() - 1)}});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401);
    r = cli.Get("/api/wallets", {{"Authorization", "Basic " + token}});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401);

    // Header-token POSTs need neither the CSRF header nor an Origin
    r = cli.Post("/api/wallets/create", tokenHeader, "{}", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200);

    // ... but the body must be JSON
    r = cli.Post("/api/wallets/create", tokenHeader, "{}", "text/plain");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 415);

    server.stop();
}

TEST_F(ServerAuthTest, LoginCookieRequiresCsrfHeaderAndOrigin) {
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());

    // Login without a credential is allowed; a wrong token is refused
    auto r = cli.Post("/api/auth/login", json{{"token", "wrong"}}.dump(), "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401);
    EXPECT_FALSE(r->has_header("Set-Cookie"));

    r = cli.Post("/api/auth/login", json{{"token", token}}.dump(), "application/json");
    ASSERT_TRUE(r);
    ASSERT_EQ(r->status, 200) << r->body;
    std::string setCookie = r->get_header_value("Set-Cookie");
    EXPECT_NE(setCookie.find("bucks_session="), std::string::npos);
    EXPECT_NE(setCookie.find("HttpOnly"), std::string::npos);
    EXPECT_NE(setCookie.find("SameSite=Strict"), std::string::npos);
    EXPECT_NE(setCookie.find("Path=/"), std::string::npos);
    std::string cookie = cookiePair(setCookie);
    EXPECT_EQ(cookie.size(), std::string("bucks_session=").size() + 64); // 32 random bytes

    // The cookie authenticates reads
    r = cli.Get("/api/wallets", {{"Cookie", cookie}});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200);
    r = cli.Get("/api/auth/status", {{"Cookie", cookie}});
    ASSERT_TRUE(r);
    EXPECT_TRUE(json::parse(r->body)["authenticated"].get<bool>());
    r = cli.Get("/api/auth/status");
    ASSERT_TRUE(r);
    EXPECT_FALSE(json::parse(r->body)["authenticated"].get<bool>());

    // A state change needs X-Bucks-CSRF: 1 AND a matching Origin
    r = cli.Post("/api/wallets/create", {{"Cookie", cookie}}, "{}", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 403);
    r = cli.Post("/api/wallets/create", {{"Cookie", cookie}, {"X-Bucks-CSRF", "1"}}, "{}", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 403);
    r = cli.Post("/api/wallets/create",
                 {{"Cookie", cookie}, {"X-Bucks-CSRF", "1"}, {"Origin", "https://evil.example"}},
                 "{}", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 403);
    r = cli.Post("/api/wallets/create",
                 {{"Cookie", cookie}, {"Origin", origin(server)}},
                 "{}", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 403);
    r = cli.Post("/api/wallets/create",
                 {{"Cookie", cookie}, {"X-Bucks-CSRF", "1"}, {"Origin", origin(server)}},
                 "{}", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200) << r->body;
    // Referer is accepted when the browser sends no Origin
    r = cli.Post("/api/mining/stop",
                 {{"Cookie", cookie}, {"X-Bucks-CSRF", "1"}, {"Referer", origin(server) + "/index.html"}},
                 "", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200) << r->body;

    // Logout invalidates the session
    r = cli.Post("/api/auth/logout",
                 {{"Cookie", cookie}, {"X-Bucks-CSRF", "1"}, {"Origin", origin(server)}},
                 "", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200);
    r = cli.Get("/api/wallets", {{"Cookie", cookie}});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401);

    // A forged session id never validates
    r = cli.Get("/api/wallets", {{"Cookie", "bucks_session=" + std::string(64, 'a')}});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401);

    server.stop();
}

TEST_F(ServerAuthTest, CookieFileIsWrittenWhenNoTokenConfigured) {
    cfg.apiToken.clear();
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    ASSERT_FALSE(server.cookieFile().empty());
    EXPECT_EQ(server.cookieFile(), cfg.dataDir + "/.cookie");
    ASSERT_TRUE(fs::exists(server.cookieFile()));
#ifndef _WIN32
    EXPECT_EQ(fileMode(server.cookieFile()), 0600u);
    EXPECT_EQ(fileMode(cfg.dataDir), 0700u);
#endif
    std::string generated = readFile(server.cookieFile());
    ASSERT_EQ(generated.size(), 65u); // 64 hex chars + newline
    generated.pop_back();
    for (char c : generated) EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(c)));

    httplib::Client cli("127.0.0.1", server.port());
    auto r = cli.Get("/api/wallets", {{"X-API-Token", generated}});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200);
    r = cli.Get("/api/wallets", tokenHeader); // the fixture token is not valid here
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401);

    // Where the token is stored is told only to an authenticated caller (audit WAL-7), and never the token itself.
    r = cli.Get("/api/auth/status");
    ASSERT_TRUE(r);
    EXPECT_FALSE(json::parse(r->body).contains("cookieFile"));
    EXPECT_EQ(r->body.find(generated), std::string::npos);
    r = cli.Get("/api/auth/status", {{"X-API-Token", generated}});
    ASSERT_TRUE(r);
    EXPECT_EQ(json::parse(r->body)["cookieFile"], server.cookieFile());
    EXPECT_EQ(r->body.find(generated), std::string::npos);

    std::string path = server.cookieFile();
    server.stop();
    EXPECT_FALSE(fs::exists(path));
}

TEST_F(ServerAuthTest, MissingTokenFileFailsStart) {
    cfg.apiToken.clear();
    cfg.apiTokenFile = dir + "/no-such-token-file";
    Bucks::Server server(*chain, nullptr, cfg);
    EXPECT_FALSE(server.start());
    EXPECT_FALSE(server.running());
}

TEST_F(ServerAuthTest, StartReturnsFalseWhenPortIsTaken) {
    Bucks::Server first(*chain, nullptr, cfg);
    ASSERT_TRUE(first.start());
    ASSERT_GT(first.port(), 0);

    Bucks::ServerConfig cfg2 = cfg;
    cfg2.port = static_cast<uint16_t>(first.port());
    cfg2.dataDir = dir + "/data2";
    Bucks::Server second(*chain, nullptr, cfg2);
    EXPECT_FALSE(second.start());
    EXPECT_FALSE(second.running());
    EXPECT_TRUE(first.running());

    first.stop();
}

// ---------------------------------------------------------------------------
// Wallet routes
// ---------------------------------------------------------------------------

TEST_F(ServerAuthTest, MnemonicOnlyInCreationResponse) {
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());

    json created = createWallet(cli);
    ASSERT_TRUE(created["success"].get<bool>());
    ASSERT_TRUE(created.contains("mnemonic"));
    std::string phrase = created["mnemonic"];
    EXPECT_EQ(created["wordCount"].get<size_t>(), 13u);
    EXPECT_EQ(Bucks::splitMnemonic(phrase).size(), 13u);
    EXPECT_TRUE(created["persisted"].get<bool>());
    EXPECT_FALSE(created["wallet"]["address"].get<std::string>().empty());
    EXPECT_FALSE(created.contains("privateKey"));
    EXPECT_FALSE(created.contains("privKeyHex"));

    // 25 words on request
    auto r = cli.Post("/api/wallets/create", tokenHeader, "{\"words\":25}", "application/json");
    ASSERT_TRUE(r);
    ASSERT_EQ(r->status, 200) << r->body;
    EXPECT_EQ(json::parse(r->body)["wordCount"].get<size_t>(), 25u);
    r = cli.Post("/api/wallets/create", tokenHeader, "{\"words\":12}", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 400);

    // Every other wallet payload is phrase-free
    r = cli.Get("/api/wallets/primary", tokenHeader);
    ASSERT_TRUE(r);
    ASSERT_EQ(r->status, 200);
    json primary = json::parse(r->body);
    EXPECT_FALSE(primary.contains("mnemonic"));
    EXPECT_EQ(r->body.find(phrase), std::string::npos);
    EXPECT_EQ(primary["address"], created["wallet"]["address"]);
    EXPECT_EQ(r->get_header_value("Cache-Control"), "no-store");

    r = cli.Get("/api/wallets", tokenHeader);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->body.find("mnemonic"), std::string::npos);
    EXPECT_EQ(r->body.find("privKey"), std::string::npos);

    // Not on disk either
    std::string onDisk = readFile(server.walletFile());
    EXPECT_EQ(onDisk.find("mnemonic"), std::string::npos);
    for (const auto& w : Bucks::splitMnemonic(phrase)) EXPECT_EQ(onDisk.find(w), std::string::npos);
    for (const auto& w : server.getWallets()) EXPECT_TRUE(w.getMnemonic().empty());

    // The old server-side generator is gone
    r = cli.Get("/api/wallets/mnemonic/generate", tokenHeader);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 404);

    server.stop();
}

TEST_F(ServerAuthTest, TwoCreatedWalletsDiffer) {
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());

    json a = createWallet(cli);
    json b = createWallet(cli);
    EXPECT_NE(a["wallet"]["address"], b["wallet"]["address"]);
    EXPECT_NE(a["wallet"]["publicKey"], b["wallet"]["publicKey"]);
    EXPECT_NE(a["mnemonic"], b["mnemonic"]);

    auto wallets = server.getWallets();
    ASSERT_EQ(wallets.size(), 2u);
    EXPECT_NE(wallets[0].getPrivateKeyHex(), wallets[1].getPrivateKeyHex());

    server.stop();
}

TEST_F(ServerAuthTest, RestoreValidatesAndDeduplicates) {
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());

    // Garbage is rejected before any key derivation, without echoing it
    auto r = cli.Post("/api/wallets/restore", tokenHeader,
                      json{{"mnemonic", "zzqx zzqy zzqz"}}.dump(), "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 400);
    EXPECT_EQ(r->body.find("zzqx"), std::string::npos);
    EXPECT_EQ(server.getWallets().size(), 0u);

    r = cli.Post("/api/wallets/restore", tokenHeader, "{\"mnemonic\": 5}", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 400);
    r = cli.Post("/api/wallets/restore", tokenHeader, "not json {", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 400);
    EXPECT_EQ(r->body.find("not json"), std::string::npos);

    // A valid phrase restores the same address, and a repeat is not duplicated
    json created = createWallet(cli);
    std::string phrase = created["mnemonic"];
    r = cli.Post("/api/wallets/restore", tokenHeader, json{{"mnemonic", phrase}}.dump(), "application/json");
    ASSERT_TRUE(r);
    ASSERT_EQ(r->status, 200) << r->body;
    json restored = json::parse(r->body);
    EXPECT_TRUE(restored["existing"].get<bool>());
    EXPECT_EQ(restored["wallet"]["address"], created["wallet"]["address"]);
    EXPECT_FALSE(restored.contains("mnemonic"));
    EXPECT_EQ(server.getWallets().size(), 1u);

    // A tampered checksum word is refused
    auto words = Bucks::splitMnemonic(phrase);
    std::swap(words[0], words[1]);
    std::string swapped;
    for (size_t i = 0; i < words.size(); ++i) swapped += (i ? " " : "") + words[i];
    if (swapped != phrase) {
        r = cli.Post("/api/wallets/restore", tokenHeader, json{{"mnemonic", swapped}}.dump(), "application/json");
        ASSERT_TRUE(r);
        EXPECT_EQ(r->status, 400);
        EXPECT_EQ(server.getWallets().size(), 1u);
    }

    server.stop();
}

TEST_F(ServerAuthTest, SignedMessagesAreDomainSeparated) {
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());

    json created = createWallet(cli);
    std::string address = created["wallet"]["address"];
    const std::string message = "login challenge 42";

    auto r = cli.Post("/api/wallets/sign", tokenHeader,
                      json{{"address", address}, {"message", message}}.dump(), "application/json");
    ASSERT_TRUE(r);
    ASSERT_EQ(r->status, 200) << r->body;
    json signed_ = json::parse(r->body);
    EXPECT_EQ(signed_["scheme"], "bucks-signed-message-v1");
    std::vector<uint8_t> sig = Bucks::parseHex(signed_["signature"]);
    Bucks::PublicKey pub = Bucks::parseHex(signed_["publicKey"]);

    // Verifies under the prefixed hash only, never under the raw hash the
    // old endpoint used (which is also the transaction sighash construction).
    EXPECT_TRUE(Bucks::verifyECDSA(Bucks::signedMessageHash(message), sig, pub));
    std::vector<uint8_t> raw(message.begin(), message.end());
    EXPECT_FALSE(Bucks::verifyECDSA(Bucks::doubleSha256(raw), sig, pub));
    EXPECT_FALSE(Bucks::verifyECDSA(Bucks::signedMessageHash(message + "x"), sig, pub));

    r = cli.Post("/api/wallets/sign", tokenHeader,
                 json{{"address", address}, {"message", std::string(5000, 'm')}}.dump(), "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 400);
    r = cli.Post("/api/wallets/sign", tokenHeader,
                 json{{"address", "1NoSuchAddress"}, {"message", message}}.dump(), "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 404);

    server.stop();
}

TEST_F(ServerAuthTest, SignedMessageHashUsesCompactSizePrefix) {
    std::vector<uint8_t> expect;
    const std::string prefix = "Bucks Signed Message:\n";
    expect.push_back(static_cast<uint8_t>(prefix.size()));
    expect.insert(expect.end(), prefix.begin(), prefix.end());
    expect.push_back(2);
    expect.push_back('h');
    expect.push_back('i');
    EXPECT_EQ(Bucks::signedMessageHash("hi"), Bucks::doubleSha256(expect));
    EXPECT_NE(Bucks::signedMessageHash("hi"), Bucks::signedMessageHash("hj"));
}

// ---------------------------------------------------------------------------
// Mining
// ---------------------------------------------------------------------------

TEST_F(ServerAuthTest, MiningWithIndexDoesNotTouchPrimaryWallet) {
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());
    cli.set_read_timeout(120, 0);

    // No wallet yet: refuse instead of silently creating an unbacked one
    auto r = cli.Post("/api/mining/mine", tokenHeader, "{}", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 400);
    EXPECT_EQ(server.getWallets().size(), 0u);

    json a = createWallet(cli);
    json b = createWallet(cli);
    auto before = server.getWallets();
    ASSERT_EQ(before.size(), 2u);
    const std::string primaryPriv = before[0].getPrivateKeyHex();
    const std::string primaryAddr = before[0].getAddress();
    const std::string secondPriv = before[1].getPrivateKeyHex();
    ASSERT_NE(primaryPriv, secondPriv);
    const uint32_t heightBefore = chain->height();

    r = cli.Post("/api/mining/mine", tokenHeader, "{\"minerWalletIndex\":1}", "application/json");
    ASSERT_TRUE(r);
    ASSERT_EQ(r->status, 200) << r->body;
    json mined = json::parse(r->body);
    EXPECT_TRUE(mined["success"].get<bool>());
    EXPECT_EQ(mined["minerAddress"], b["wallet"]["address"]);
    EXPECT_EQ(chain->height(), heightBefore + 1);

    // The reward went to wallet 1 ...
    auto tip = chain->blockByHeight(chain->height());
    ASSERT_TRUE(tip.has_value());
    ASSERT_FALSE(tip->transactions.empty());
    EXPECT_EQ(tip->transactions[0].outputs[0].scriptPubKey, before[1].getP2PKHScript());

    // ... and wallet 0 is exactly what it was, in memory and on disk
    auto after = server.getWallets();
    ASSERT_EQ(after.size(), 2u);
    EXPECT_EQ(after[0].getPrivateKeyHex(), primaryPriv);
    EXPECT_EQ(after[0].getAddress(), primaryAddr);
    EXPECT_EQ(after[1].getPrivateKeyHex(), secondPriv);
    json onDisk = json::parse(readFile(server.walletFile()));
    ASSERT_EQ(onDisk["wallets"].size(), 2u);
    EXPECT_EQ(onDisk["wallets"][0]["privKeyHex"], primaryPriv);
    EXPECT_EQ(onDisk["wallets"][1]["privKeyHex"], secondPriv);

    r = cli.Post("/api/mining/mine", tokenHeader, "{\"minerWalletIndex\":7}", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 400);
    r = cli.Post("/api/mining/mine", tokenHeader, "{\"minerWalletIndex\":-1}", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 400);

    server.stop();
}

// ---------------------------------------------------------------------------
// Wallet file
// ---------------------------------------------------------------------------

TEST_F(ServerAuthTest, WalletFileIsPrivateAndCorruptFileIsNeverOverwritten) {
    std::string walletPath;
    std::string firstAddress;
    {
        Bucks::Server server(*chain, nullptr, cfg);
        ASSERT_TRUE(server.start());
        httplib::Client cli("127.0.0.1", server.port());
        firstAddress = createWallet(cli)["wallet"]["address"];
        walletPath = server.walletFile();
        EXPECT_EQ(walletPath, cfg.dataDir + "/regtest/wallets.json");
        ASSERT_TRUE(fs::exists(walletPath));
#ifndef _WIN32
        EXPECT_EQ(fileMode(walletPath), 0600u);
        EXPECT_EQ(fileMode(cfg.dataDir), 0700u);
#endif
        EXPECT_FALSE(fs::exists(walletPath + ".tmp"));
        json doc = json::parse(readFile(walletPath));
        EXPECT_EQ(doc["version"], 2);
        ASSERT_EQ(doc["wallets"].size(), 1u);
        EXPECT_EQ(doc["wallets"][0]["address"], firstAddress);
        server.stop();
    }

    // A second run on a good file makes a .bak before the first rewrite
    {
        Bucks::Server server(*chain, nullptr, cfg);
        ASSERT_TRUE(server.walletsEnabled());
        ASSERT_EQ(server.getWallets().size(), 1u);
        std::string before = readFile(walletPath);
        EXPECT_FALSE(fs::exists(walletPath + ".bak"));
        Bucks::Wallet extra = Bucks::Wallet::createNew(nullptr);
        ASSERT_TRUE(server.addWallet(extra));
        ASSERT_TRUE(fs::exists(walletPath + ".bak"));
        EXPECT_EQ(readFile(walletPath + ".bak"), before);
#ifndef _WIN32
        EXPECT_EQ(fileMode(walletPath + ".bak"), 0600u);
#endif
        EXPECT_EQ(json::parse(readFile(walletPath))["wallets"].size(), 2u);
    }

    // Corrupt the file: the subsystem is disabled and the bytes stay untouched
    const std::string garbage = "{ this is not json";
    writeFile(walletPath, garbage);
    {
        Bucks::Server server(*chain, nullptr, cfg);
        EXPECT_FALSE(server.walletsEnabled());
        EXPECT_NE(server.walletError().find("wallets.json"), std::string::npos);
        EXPECT_EQ(server.getWallets().size(), 0u);
        ASSERT_TRUE(server.start());
        httplib::Client cli("127.0.0.1", server.port());

        auto r = cli.Post("/api/wallets/create", tokenHeader, "{}", "application/json");
        ASSERT_TRUE(r);
        EXPECT_EQ(r->status, 503);
        r = cli.Get("/api/wallets/primary", tokenHeader);
        ASSERT_TRUE(r);
        EXPECT_EQ(r->status, 503);
        r = cli.Get("/api/auth/status");
        ASSERT_TRUE(r);
        EXPECT_FALSE(json::parse(r->body)["walletsEnabled"].get<bool>());

        Bucks::Wallet extra = Bucks::Wallet::createNew(nullptr);
        EXPECT_FALSE(server.addWallet(extra));
        server.stop();
    }
    EXPECT_EQ(readFile(walletPath), garbage);
}

TEST_F(ServerAuthTest, UnknownFieldsSurviveRewriteAndMnemonicsAreDropped) {
    fs::create_directories(cfg.dataDir);
    std::string walletPath = cfg.dataDir + "/regtest/wallets.json";
    Bucks::Wallet w1 = Bucks::Wallet::createNew(nullptr);
    json doc = {
        {"version", 2},
        {"note", "keep me"},
        {"wallets", json::array({
            {{"privKeyHex", w1.getPrivateKeyHex()}, {"pubKeyHex", w1.getPublicKeyHex()},
             {"label", "savings"}, {"mnemonic", "never persisted again"}},
            {{"privKeyHex", "00"}, {"pubKeyHex", "00"}, {"comment", "unreadable but preserved"}}
        })}
    };
    writeFile(walletPath, doc.dump());

    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.walletsEnabled());
    ASSERT_EQ(server.getWallets().size(), 1u); // the bad entry is skipped, not deleted
    EXPECT_EQ(server.getWallets()[0].getAddress(), w1.getAddress());
    EXPECT_TRUE(server.getWallets()[0].getMnemonic().empty());

    Bucks::Wallet w2 = Bucks::Wallet::createNew(nullptr);
    ASSERT_TRUE(server.addWallet(w2));

    json after = json::parse(readFile(walletPath));
    EXPECT_EQ(after["note"], "keep me");
    EXPECT_EQ(after["version"], 2);
    ASSERT_EQ(after["wallets"].size(), 3u);
    EXPECT_EQ(after["wallets"][0]["label"], "savings");
    EXPECT_EQ(after["wallets"][0]["address"], w1.getAddress());
    EXPECT_EQ(after["wallets"][1]["address"], w2.getAddress());
    EXPECT_EQ(after["wallets"][2]["comment"], "unreadable but preserved");
    EXPECT_EQ(readFile(walletPath).find("mnemonic"), std::string::npos);
}

TEST_F(ServerAuthTest, TamperedPublicKeyIsNotLoaded) {
    fs::create_directories(cfg.dataDir);
    Bucks::Wallet victim = Bucks::Wallet::createNew(nullptr);
    Bucks::Wallet attacker = Bucks::Wallet::createNew(nullptr);
    json doc = {{"version", 2}, {"wallets", json::array({
        {{"privKeyHex", victim.getPrivateKeyHex()}, {"pubKeyHex", attacker.getPublicKeyHex()}}
    })}};
    writeFile(cfg.dataDir + "/regtest/wallets.json", doc.dump());

    Bucks::Server server(*chain, nullptr, cfg);
    EXPECT_TRUE(server.walletsEnabled());
    EXPECT_EQ(server.getWallets().size(), 0u);
}

TEST_F(ServerAuthTest, LegacyWalletFileIsReadOnly) {
    Bucks::Wallet legacyWallet = Bucks::Wallet::createNew(nullptr);
    json legacy = json::array({
        {{"privKeyHex", legacyWallet.getPrivateKeyHex()},
         {"pubKeyHex", legacyWallet.getPublicKeyHex()},
         {"mnemonic", "phrase from the old format"}}
    });
    writeFile(cfg.legacyWalletFile, legacy.dump(4));
    const std::string legacyBytes = readFile(cfg.legacyWalletFile);

    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.walletsEnabled());
    ASSERT_EQ(server.getWallets().size(), 1u);
    EXPECT_EQ(server.getWallets()[0].getAddress(), legacyWallet.getAddress());
    EXPECT_FALSE(fs::exists(server.walletFile())); // loading alone writes nothing

    Bucks::Wallet fresh = Bucks::Wallet::createNew(nullptr);
    ASSERT_TRUE(server.addWallet(fresh));

    // Saves go to the data directory only; the legacy file is byte-identical
    EXPECT_EQ(readFile(cfg.legacyWalletFile), legacyBytes);
    ASSERT_TRUE(fs::exists(server.walletFile()));
    json saved = json::parse(readFile(server.walletFile()));
    ASSERT_EQ(saved["wallets"].size(), 2u);
    EXPECT_EQ(saved["wallets"][0]["address"], legacyWallet.getAddress());
    EXPECT_EQ(saved["wallets"][1]["address"], fresh.getAddress());
    EXPECT_EQ(readFile(server.walletFile()).find("mnemonic"), std::string::npos);

    // Once the datadir file exists it wins over the legacy one
    Bucks::Server again(*chain, nullptr, cfg);
    EXPECT_EQ(again.getWallets().size(), 2u);
}

// ---------------------------------------------------------------------------
// Resource limits
// ---------------------------------------------------------------------------

TEST_F(ServerAuthTest, RequestBodyLimitIsApplied) {
    cfg.maxRequestSize = 1024;
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());

    std::string big = "{\"cid\":\"" + std::string(4096, 'Q') + "\"}";
    auto r = cli.Post("/api/anchor", tokenHeader, big, "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 413);

    r = cli.Post("/api/anchor", tokenHeader, "{\"cid\":\"QmSmall\"}", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200);

    server.stop();
}

TEST_F(ServerAuthTest, RateLimiterKeysOnPeerNotForwardedFor) {
    cfg.rateLimitRequests = 3;
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());

    for (int i = 0; i < 3; ++i) {
        auto r = cli.Get("/api/status", {{"X-Forwarded-For", "10.0.0." + std::to_string(i)}});
        ASSERT_TRUE(r);
        EXPECT_EQ(r->status, 200);
    }
    auto r = cli.Get("/api/status", {{"X-Forwarded-For", "10.0.0.99"}});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 429);
    EXPECT_TRUE(r->has_header("Retry-After"));

    // Static files are not metered by the API limiter
    r = cli.Get("/");
    ASSERT_TRUE(r);
    EXPECT_NE(r->status, 429);

    server.stop();
}

TEST(RateLimiterTest, EvictsIdleKeys) {
    Bucks::RateLimiter rl(2, std::chrono::seconds(1));
    EXPECT_TRUE(rl.allowRequest("a"));
    EXPECT_TRUE(rl.allowRequest("a"));
    EXPECT_FALSE(rl.allowRequest("a"));
    EXPECT_TRUE(rl.allowRequest("b"));
    EXPECT_EQ(rl.trackedKeys(), 2u);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    EXPECT_TRUE(rl.allowRequest("a"));
    rl.cleanup();
    EXPECT_EQ(rl.trackedKeys(), 1u);

    // Automatic eviction without an explicit cleanup() call
    Bucks::RateLimiter many(1000, std::chrono::seconds(1));
    for (int i = 0; i < 600; ++i) EXPECT_TRUE(many.allowRequest("k" + std::to_string(i)));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    EXPECT_TRUE(many.allowRequest("fresh"));
    EXPECT_LE(many.trackedKeys(), 601u);
    for (int i = 0; i < 512; ++i) many.allowRequest("fresh");
    EXPECT_LE(many.trackedKeys(), 2u);
}

// ---------------------------------------------------------------------------
// Audit findings WAL-1, 3, 4, 9, 10 (reports/audit-2026-10-14/wallet-api-web.md)
// ---------------------------------------------------------------------------

// WAL-1: a wallets.json in the working directory is never read unless the operator asks for it.
TEST_F(ServerAuthTest, DefaultConfigNeverReadsAWalletsJsonFromTheWorkingDirectory) {
    Bucks::ServerConfig fresh;
    EXPECT_TRUE(fresh.legacyWalletFile.empty());
}

// WAL-9: a guessable token is refused at start.
TEST_F(ServerAuthTest, ShortApiTokenIsRefused) {
    cfg.apiToken = "short";
    Bucks::Server server(*chain, nullptr, cfg);
    EXPECT_FALSE(server.start(0));
}

// WAL-3 and WAL-4: no language means a shipped list, not the placeholder; a bad flag is refused BEFORE the wallet is saved.
TEST_F(ServerAuthTest, CreateWalletDefaultsAndValidatesBeforeSaving) {
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start(0));
    httplib::Client cli("127.0.0.1", server.port());

    auto r = cli.Post("/api/wallets/create", tokenHeader, R"({"includeCanonical":"yes"})", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 400);
    r = cli.Get("/api/wallets", tokenHeader);
    ASSERT_TRUE(r);
    EXPECT_EQ(json::parse(r->body)["wallets"].size(), 0u) << "a wallet was saved although the request was refused";

    json made = createWallet(cli);
    EXPECT_NE(made["language"], "ar-dev");
    server.stop();
}

// WAL-10: only a branch the node actually parked can be approved.
TEST_F(ServerAuthTest, ApproveReorgRefusesAnUnknownTip) {
    Bucks::Server server(*chain, nullptr, cfg);
    ASSERT_TRUE(server.start(0));
    httplib::Client cli("127.0.0.1", server.port());
    auto r = cli.Post("/api/node/approve-reorg", tokenHeader, "{\"tipHash\":\"" + std::string(64, 'a') + "\"}", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 409) << r->body;
    server.stop();
}
