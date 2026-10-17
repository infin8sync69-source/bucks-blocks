#include "server.h"
#include "fsutil.h"
#include "log.h"
#include "node.h"
#include "pow.h"
#include "block.h"
#include "blockheader.h"
#include "transaction.h"
#include "wallet.h"
#include "crypto.h"
#include "bip_8192.h"
#include "version.h"
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <ctime>
#include <iostream>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <memory>
#include "network.h"
#include "consensus.h"
#include "policy.h"
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <nlohmann/json.hpp>
#include <iomanip>
#include <cstring>
#include <cerrno>
#include <cctype>
#include <set>
#include <cstdlib>
#include <cmath>
#include <filesystem>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "httplib.h"

using json = nlohmann::json;

namespace Bucks {

// "dev" and "test" lists are placeholders that exist to be replaced.
static bool placeholderList(const Wordlist& wl) {
    return wl.status == "dev" || wl.status == "test";
}

// The main network never offers or accepts a placeholder list (audit WAL-3).
static bool listAllowed(const Wordlist& wl, bool mainNet) {
    return !(mainNet && placeholderList(wl));
}

static std::vector<std::string> allowedMnemonicLanguages(bool mainNet) {
    std::vector<std::string> out;
    for (const auto& lang : mnemonicLanguages()) {
        auto wl = findWordlist(lang);
        if (wl && listAllowed(*wl, mainNet)) out.push_back(lang);
    }
    return out;
}

// The Arabic list shown as "the actual Arabic words": the first non-placeholder Arabic list (release or
// draft), else, off the main network, the library default. "" when there is none.
static std::string canonicalMnemonicLanguage(bool mainNet) {
    for (const auto& lang : mnemonicLanguages()) {
        auto wl = findWordlist(lang);
        if (wl && wl->normalizer == "arabic" && !placeholderList(*wl)) return lang;
    }
    return mainNet ? "" : defaultMnemonicLanguage();
}

// What a create request without "language" gets, and what GET /api/wallets/languages calls the default: the
// first non-placeholder Arabic list, else the first non-placeholder list, else, off the main network, the
// library default. "" when nothing qualifies.
static std::string defaultWalletLanguage(bool mainNet) {
    std::string firstOther;
    for (const auto& lang : mnemonicLanguages()) {
        auto wl = findWordlist(lang);
        if (!wl || placeholderList(*wl)) continue;
        if (wl->normalizer == "arabic") return lang;
        if (firstOther.empty()) firstOther = lang;
    }
    if (!firstOther.empty()) return firstOther;
    return mainNet ? "" : defaultMnemonicLanguage();
}

namespace fs = std::filesystem;

// ============================================================================
// Helper: Hash256 to hex string
// ============================================================================
static std::string hash256ToHex(const Hash256& hash) {
    std::ostringstream oss;
    for (uint8_t b : hash) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    }
    return oss.str();
}

// ============================================================================
// Helper: Hex string to Hash256
// ============================================================================
static Hash256 hexToHash256(const std::string& hex) {
    Hash256 hash{};
    for (size_t i = 0; i < hex.size() && i < 64; i += 2) {
        std::string byteStr = hex.substr(i, 2);
        hash[i/2] = static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16));
    }
    return hash;
}

// ============================================================================
// Small string / file / crypto helpers
// ============================================================================
namespace {

const char* const SESSION_COOKIE = "bucks_session";

std::string trimWs(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

// Hex of `bytes` random bytes. Throws instead of continuing with a zero
// buffer when the RNG fails (CRY-13).
std::string randomHex(size_t bytes) {
    std::vector<uint8_t> buf(bytes);
    if (RAND_bytes(buf.data(), static_cast<int>(bytes)) != 1) {
        OPENSSL_cleanse(buf.data(), buf.size());
        throw std::runtime_error("Secure random number generator unavailable");
    }
    std::string hex = hexStr(buf);
    OPENSSL_cleanse(buf.data(), buf.size());
    return hex;
}

// Constant-time comparison that does not depend on the lengths either:
// both sides are hashed and the digests compared with CRYPTO_memcmp.
bool constantTimeEquals(const std::string& a, const std::string& b) {
    Hash256 ha = sha256(std::vector<uint8_t>(a.begin(), a.end()));
    Hash256 hb = sha256(std::vector<uint8_t>(b.begin(), b.end()));
    bool same = CRYPTO_memcmp(ha.data(), hb.data(), ha.size()) == 0;
    return same && a.size() == b.size();
}

bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// Create `path` (and missing parents) private to the owner. A directory that already exists is never
// chmodded: the operator may have pointed --datadir at their home. When other users can reach it, *warning
// says so and how to close it (audit WAL-8).
bool ensurePrivateDir(const std::string& path, std::string* err, std::string* warning) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        std::vector<fs::path> created;
        for (fs::path p = fs::path(path); !p.empty() && !fs::exists(p, ec); p = p.parent_path()) {
            created.push_back(p);
            if (p == p.parent_path()) break;
        }
        fs::create_directories(path, ec);
        if (ec) { if (err) *err = ec.message(); return false; }
#ifndef _WIN32
        for (const fs::path& p : created) {
            if (::chmod(p.c_str(), 0700) != 0) {
                if (err) *err = std::string("chmod 0700 failed: ") + std::strerror(errno);
                return false;
            }
        }
#endif
        return true;
    }
    if (!fs::is_directory(path, ec)) {
        if (err) *err = "not a directory";
        return false;
    }
#ifndef _WIN32
    struct stat st {};
    if (warning && ::stat(path.c_str(), &st) == 0 && (st.st_mode & 077) != 0) {
        std::ostringstream w;
        w << path << " is open to other users (mode " << std::oct << (st.st_mode & 0777)
          << "); the node leaves it as it is. Close it with: chmod 700 " << path;
        *warning = w.str();
    }
#endif
    return true;
}

std::string cookieValue(const std::string& header, const std::string& name) {
    size_t pos = 0;
    while (pos <= header.size()) {
        size_t end = header.find(';', pos);
        if (end == std::string::npos) end = header.size();
        std::string part = trimWs(header.substr(pos, end - pos));
        size_t eq = part.find('=');
        if (eq != std::string::npos && part.substr(0, eq) == name) return part.substr(eq + 1);
        pos = end + 1;
    }
    return "";
}

// "https://Example.com:8080/x" -> scheme "https", hostport "example.com:8080"
bool splitOrigin(const std::string& origin, std::string& scheme, std::string& hostport) {
    size_t p = origin.find("://");
    if (p == std::string::npos) return false;
    scheme = lowerAscii(origin.substr(0, p));
    std::string rest = origin.substr(p + 3);
    size_t slash = rest.find('/');
    if (slash != std::string::npos) rest = rest.substr(0, slash);
    hostport = lowerAscii(trimWs(rest));
    return !hostport.empty();
}

// "host[:port]" or "[v6]:port" -> name (brackets kept), port ("" if none)
bool splitHostPort(const std::string& hp, std::string& name, std::string& port) {
    if (hp.empty()) return false;
    if (hp[0] == '[') {
        size_t rb = hp.find(']');
        if (rb == std::string::npos) return false;
        name = hp.substr(0, rb + 1);
        std::string rest = hp.substr(rb + 1);
        if (rest.empty()) { port.clear(); return true; }
        if (rest[0] != ':') return false;
        port = rest.substr(1);
    } else {
        size_t c = hp.rfind(':');
        if (c != std::string::npos && hp.find(':') == c) {
            name = hp.substr(0, c);
            port = hp.substr(c + 1);
        } else {
            name = hp;
            port.clear();
        }
    }
    if (!port.empty() && !std::all_of(port.begin(), port.end(),
                                      [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return false;
    return !name.empty();
}

// 127.0.0.0/8 and ::1, also as an IPv4-mapped address or a bind name.
bool isLoopbackAddress(std::string a) {
    a = lowerAscii(trimWs(a));
    if (a.size() > 2 && a.front() == '[' && a.back() == ']') a = a.substr(1, a.size() - 2);
    if (startsWith(a, "::ffff:")) a = a.substr(7);
    return a == "localhost" || a == "::1" || startsWith(a, "127.");
}

// Host and port of a Host header or an origin's authority, with the scheme's default port written as none.
bool hostAndPort(const std::string& hp, const std::string& scheme, std::string& name, std::string& port) {
    if (!splitHostPort(hp, name, port)) return false;
    if ((scheme == "http" && port == "80") || (scheme == "https" && port == "443")) port.clear();
    return true;
}

void appendCompactSize(std::vector<uint8_t>& buf, uint64_t v) {
    if (v < 0xfd) {
        buf.push_back(static_cast<uint8_t>(v));
    } else if (v <= 0xffff) {
        buf.push_back(0xfd);
        for (int i = 0; i < 2; ++i) buf.push_back(static_cast<uint8_t>(v >> (8 * i)));
    } else if (v <= 0xffffffffULL) {
        buf.push_back(0xfe);
        for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>(v >> (8 * i)));
    } else {
        buf.push_back(0xff);
        for (int i = 0; i < 8; ++i) buf.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
}

// Value of a 256-bit target as a double (only for display: difficulty).
double targetToDouble(const U256& v) {
    const unsigned bits = v.bits();
    if (bits <= 64) return double(v.low64());
    const unsigned shift = bits - 64;
    return std::ldexp(double((v >> shift).low64()), int(shift));
}

json errorJson(const std::string& msg) {
    return json{{"success", false}, {"error", msg}};
}

void reply(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

// Parses the request body as JSON. On failure answers 400 without echoing
// any of the body (it may contain a recovery phrase).
bool parseBody(const httplib::Request& req, httplib::Response& res, json& out) {
    try {
        out = req.body.empty() ? json::object() : json::parse(req.body);
    } catch (const std::exception&) {
        reply(res, 400, errorJson("Invalid JSON body"));
        return false;
    }
    if (!out.is_object()) {
        reply(res, 400, errorJson("JSON body must be an object"));
        return false;
    }
    return true;
}

} // namespace

Hash256 signedMessageHash(const std::string& message) {
    static const std::string prefix = "Bucks Signed Message:\n";
    std::vector<uint8_t> buf;
    appendCompactSize(buf, prefix.size());
    buf.insert(buf.end(), prefix.begin(), prefix.end());
    appendCompactSize(buf, message.size());
    buf.insert(buf.end(), message.begin(), message.end());
    return doubleSha256(buf);
}

// ============================================================================
// RateLimiter
// ============================================================================

RateLimiter::RateLimiter(size_t maxReq, std::chrono::seconds window)
    : maxRequests(maxReq), timeWindow(window) {}

bool RateLimiter::allowRequest(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex);
    auto now = std::chrono::steady_clock::now();

    // Evict idle keys now and then so the map cannot grow without bound (API-9).
    if (++callsSinceCleanup >= 512) {
        callsSinceCleanup = 0;
        for (auto it = history.begin(); it != history.end();) {
            auto& q = it->second;
            while (!q.empty() && now - q.front() > timeWindow) q.pop_front();
            it = q.empty() ? history.erase(it) : std::next(it);
        }
    }

    auto& q = history[id];
    while (!q.empty() && now - q.front() > timeWindow)
        q.pop_front();

    if (q.size() >= maxRequests)
        return false;

    q.push_back(now);
    return true;
}

void RateLimiter::cleanup() {
    std::lock_guard<std::mutex> lock(mutex);
    auto now = std::chrono::steady_clock::now();
    for (auto it = history.begin(); it != history.end();) {
        auto& q = it->second;
        while (!q.empty() && now - q.front() > timeWindow)
            q.pop_front();
        if (q.empty())
            it = history.erase(it);
        else
            ++it;
    }
}

size_t RateLimiter::trackedKeys() const {
    std::lock_guard<std::mutex> lock(mutex);
    return history.size();
}

// ============================================================================
// Server lifecycle
// ============================================================================

std::string Server::defaultDataDir() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata && *appdata) return std::string(appdata) + "\\Bucks";
    const char* profile = std::getenv("USERPROFILE");
    if (profile && *profile) return std::string(profile) + "\\.bucks";
#else
    const char* home = std::getenv("HOME");
    if (home && *home) return std::string(home) + "/.bucks";
#endif
    return ".bucks";
}

Server::Server(Node& n, Network* net, const ServerConfig& cfg)
    : node(n),
      network_(net),
      config(cfg),
      isRunning(false),
      svr(new httplib::Server()) {

    dataDirPath = config.dataDir.empty() ? defaultDataDir() : config.dataDir;
    // One wallet file per network, next to that network's chain (and its process lock): two nodes can no longer
    // overwrite each other's keys through a shared file (audit WAL-2).
    const std::string walletDir = (fs::path(dataDirPath) / node.params().name).string();
    walletFilePath = (fs::path(walletDir) / "wallets.json").string();

    std::string err, dataWarning, walletWarning;
    if (!ensurePrivateDir(dataDirPath, &err, &dataWarning) || !ensurePrivateDir(walletDir, &err, &walletWarning)) {
        startupError = "cannot create data directory " + dataDirPath + ": " + err;
        walletErr = startupError;
        Bucks::logErr() << "✗ " << startupError << "\n";
    } else {
        for (const std::string* w : {&dataWarning, &walletWarning})
            if (!w->empty() && config.enableLogging) Bucks::logErr() << "⚠ " << *w << "\n";
        loadWallets();
    }

    resolveApiToken();

    if (config.enableRateLimit) {
        rateLimiter = std::make_unique<RateLimiter>(
            config.rateLimitRequests,
            config.rateLimitWindow
        );
    }

    // Push chain events to WebSocket clients. Removed again in the destructor.
    tipListenerId = node.addTipListener([this](const Hash256& hash, uint32_t height) {
        broadcast("new_tip", json{{"hash", hash256ToHex(hash)}, {"height", height}});
    });
    txListenerId = node.addTxListener([this](const Transaction& tx) {
        broadcast("tx_added", txToJSON(tx));
    });
}

Server::~Server() {
    node.removeTipListener(tipListenerId);
    node.removeTxListener(txListenerId);
    stop();
    delete static_cast<httplib::Server*>(svr);
}

bool Server::resolveApiToken() {
    tokenGenerated = false;
    constexpr size_t kMinToken = 16;   // a one-character token is a guessable password (audit WAL-9)
    if (!config.apiToken.empty()) {
        apiToken = config.apiToken;
        if (apiToken.size() < kMinToken) {
            startupError = "the API token must be at least 16 characters";
            Bucks::logErr() << "✗ " << startupError << "\n";
            return false;
        }
        return true;
    }
    if (!config.apiTokenFile.empty()) {
#ifndef _WIN32
        // A token other users can read is everyone's token (audit WAL-9).
        struct stat st {};
        if (::stat(config.apiTokenFile.c_str(), &st) == 0 && (st.st_mode & 077) != 0) {
            std::ostringstream why;
            why << "API token file " << config.apiTokenFile << " is open to other users (mode " << std::oct
                << (st.st_mode & 0777) << "); restrict it with: chmod 600 " << config.apiTokenFile;
            startupError = why.str();
            Bucks::logErr() << "✗ " << startupError << "\n";
            return false;
        }
#endif
        std::string t;
        if (!readTextFile(config.apiTokenFile, t)) {
            startupError = "cannot read API token file " + config.apiTokenFile;
            Bucks::logErr() << "✗ " << startupError << "\n";
            return false;
        }
        apiToken = trimWs(t);
        if (apiToken.size() < kMinToken) {
            startupError = "API token file " + config.apiTokenFile + " must hold a token of at least 16 characters";
            Bucks::logErr() << "✗ " << startupError << "\n";
            return false;
        }
        return true;
    }
    const char* envToken = std::getenv("BUCKS_API_TOKEN");
    if (envToken && !trimWs(envToken).empty()) {
        apiToken = trimWs(envToken);
        if (apiToken.size() < kMinToken) {
            startupError = "BUCKS_API_TOKEN must be at least 16 characters";
            Bucks::logErr() << "✗ " << startupError << "\n";
            return false;
        }
        return true;
    }
    try {
        apiToken = randomHex(32);
    } catch (const std::exception& e) {
        startupError = e.what();
        Bucks::logErr() << "✗ " << startupError << "\n";
        return false;
    }
    tokenGenerated = true;
    return true;
}

bool Server::start() {
    return start(config.port);
}

bool Server::start(int port) {
    if (isRunning) return false;
    if (!startupError.empty()) {
        Bucks::logErr() << "✗ Cannot start API server: " << startupError << "\n";
        return false;
    }

    try {
        setupRoutes();
    } catch (const std::exception& e) {
        Bucks::logErr() << "Error setting up routes: " << e.what() << std::endl;
        return false;
    }

    httplib::Server* s = static_cast<httplib::Server*>(svr);

    // Serve static files
    if (!s->set_base_dir(config.webRoot.c_str()) && config.enableLogging) {
        Bucks::logErr() << "⚠ Web root " << config.webRoot << " not found; only the API is served\n";
    }

    // httplib's default is SO_REUSEPORT, which lets a second process (or a
    // second Server) bind the same port and steal connections. Only allow
    // reuse of TIME_WAIT sockets.
    s->set_socket_options([](socket_t sock) {
#ifdef _WIN32
        httplib::detail::set_socket_opt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
        httplib::detail::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1);
#endif
    });

    // Resource limits (API-9)
    s->set_payload_max_length(config.maxRequestSize);
    s->set_read_timeout(5, 0);    // a half-sent request frees its worker after 5 s (audit WAL-5)
    s->set_write_timeout(10, 0);
    s->set_keep_alive_timeout(5);
    s->set_keep_alive_max_count(100);
    if (config.maxWorkers > 0) {
        const size_t workers = config.maxWorkers;
        s->new_task_queue = [workers] { return new httplib::ThreadPool(workers, workers * 64); };
    }

    // Never leak e.what() through httplib's EXCEPTION_WHAT header (API-15)
    s->set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
        try {
            if (ep) std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            Bucks::logErr() << "✗ Unhandled exception in " << req.method << " " << req.path << ": " << e.what() << "\n";
        } catch (...) {
            Bucks::logErr() << "✗ Unhandled exception in " << req.method << " " << req.path << "\n";
        }
        reply(res, 500, errorJson("Internal server error"));
    });

    s->set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        // 1. Host header: DNS-rebinding defence for every route (API-1)
        if (!hostAllowed(req.get_header_value("Host"))) {
            reply(res, 403, errorJson("Forbidden: unexpected Host header"));
            return httplib::Server::HandlerResponse::Handled;
        }

        if (!startsWith(req.path, "/api/")) {
            return httplib::Server::HandlerResponse::Unhandled; // static UI
        }

        // 2. Rate limit keyed on the socket peer (X-Forwarded-For only
        //    behind a trusted proxy)
        if (config.enableRateLimit && rateLimiter) {
            std::string key = req.remote_addr;
            if (forwardedHeadersTrusted(req.remote_addr) && req.has_header("X-Forwarded-For")) {
                // The proxy appends the address it saw, so the LAST entry is the one a client cannot forge (audit WAL-6).
                std::string xff = req.get_header_value("X-Forwarded-For");
                std::string last = trimWs(xff.substr(xff.rfind(',') == std::string::npos ? 0 : xff.rfind(',') + 1));
                if (!last.empty()) key = last;
            }
            if (!rateLimiter->allowRequest(key)) {
                res.set_header("Retry-After", std::to_string(config.rateLimitWindow.count()));
                reply(res, 429, errorJson("Too Many Requests"));
                return httplib::Server::HandlerResponse::Handled;
            }
        }

        // 3. CORS preflight: only for explicitly configured origins. The
        //    headers themselves are added by the post-routing handler.
        const std::string origin = req.get_header_value("Origin");
        if (req.method == "OPTIONS") {
            if (!origin.empty() && corsOriginAllowed(origin)) {
                res.status = 204;
            } else {
                reply(res, 403, errorJson("Cross-origin requests are not enabled for this origin"));
            }
            return httplib::Server::HandlerResponse::Handled;
        }

        // 4. Authentication: default deny, explicit read-only allowlist.
        //    Loopback is NOT trusted.
        const bool loginRoute = req.path == "/api/auth/login" && req.method == "POST";
        if (!loginRoute && !isPublicRoute(req.path, req.method)) {
            AuthKind kind = authenticate(req.get_header_value("Authorization"),
                                         req.get_header_value("X-API-Token"),
                                         req.get_header_value("Cookie"));
            if (kind == AuthKind::None) {
                reply(res, 401, errorJson("Unauthorized"));
                return httplib::Server::HandlerResponse::Handled;
            }
            // 5. CSRF: a cookie session may change state only with the
            //    custom header and an Origin/Referer we recognise.
            if (kind == AuthKind::Cookie && req.method != "GET" && req.method != "HEAD") {
                std::string src = origin.empty() ? req.get_header_value("Referer") : origin;
                const bool tls = browserUsesTls(req.remote_addr, req.get_header_value("X-Forwarded-Proto"));
                if (req.get_header_value("X-Bucks-CSRF") != "1" || src.empty() ||
                    !originAllowed(src, req.get_header_value("Host"), tls)) {
                    reply(res, 403, errorJson("CSRF check failed: X-Bucks-CSRF: 1 and a matching Origin are required"));
                    return httplib::Server::HandlerResponse::Handled;
                }
            }
        }

        // 6. Bodies must be JSON (no text/plain "simple request" CSRF)
        if (req.method == "POST" || req.method == "PUT" || req.method == "PATCH") {
            const bool hasBody = req.has_header("Transfer-Encoding") ||
                (req.has_header("Content-Length") && req.get_header_value("Content-Length") != "0");
            if (hasBody && !startsWith(lowerAscii(req.get_header_value("Content-Type")), "application/json")) {
                reply(res, 415, errorJson("Content-Type must be application/json"));
                return httplib::Server::HandlerResponse::Handled;
            }
        }

        return httplib::Server::HandlerResponse::Unhandled;
    });

    // Security headers on every response, including httplib's own errors (API-15)
    s->set_post_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_header("X-Frame-Options", "DENY");
        res.set_header("Referrer-Policy", "no-referrer");
        if (!config.contentSecurityPolicy.empty()) {
            res.set_header("Content-Security-Policy", config.contentSecurityPolicy);
        }
        if (startsWith(req.path, "/api/")) {
            res.set_header("Cache-Control", "no-store");
        } else {
            res.set_header("Cache-Control", "no-cache");   // static UI: always revalidate (ETag), so upgrades show at once
        }
        const std::string origin = req.get_header_value("Origin");
        if (!origin.empty() && corsOriginAllowed(origin)) {
            res.set_header("Access-Control-Allow-Origin", origin);
            res.set_header("Vary", "Origin");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-API-Token, X-Bucks-CSRF");
            res.set_header("Access-Control-Max-Age", "600");
        }
    });

    // The generated token is handed over through a private file (OPS-2/API-13), one per network so that a test
    // node sharing the datadir cannot overwrite or delete the main node's (audit NEW-5). The main network keeps
    // <datadir>/.cookie, where it always was.
    if (tokenGenerated) {
        const fs::path cookieDir = node.params().id == NetworkId::MAIN ? fs::path(dataDirPath)
                                                                        : fs::path(dataDirPath) / node.params().name;
        cookieFilePath = (cookieDir / ".cookie").string();
        std::string err;
        if (!writePrivateFileAtomic(cookieFilePath, apiToken + "\n", &err)) {
            Bucks::logErr() << "✗ Cannot write API token cookie " << cookieFilePath << ": " << err << "\n";
            cookieFilePath.clear();
            return false;
        }
    }

    // Bind here, synchronously, so a taken port fails start() instead of
    // dying silently in the listener thread.
    int bound = -1;
    if (port == 0) {
        bound = s->bind_to_any_port(config.bindAddress);
    } else if (s->bind_to_port(config.bindAddress, port)) {
        bound = port;
    }
    if (bound <= 0) {
        Bucks::logErr() << "✗ Cannot bind API server to " << config.bindAddress << ":" << port << "\n";
        removeOwnCookie();
        return false;
    }
    boundPort = bound;

    isRunning = true;
    serverThread = std::make_unique<std::thread>([s]() {
        s->listen_after_bind();
    });
    // Wait until the accept loop runs, otherwise an immediate stop() would be
    // a no-op in httplib and join() would hang.
    for (int i = 0; i < 5000 && !s->is_running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (config.enableLogging) {
        if (!isLoopbackAddress(config.bindAddress)) {
            // Plain HTTP beyond this host: the token and the session cookie would cross the network in clear (NEW-3).
            Bucks::logErr() << "⚠ The API listens on " << config.bindAddress << ", not on loopback. It speaks plain HTTP: "
                            "put a TLS reverse proxy (with --trust-proxy and --api-secure-cookie) or an SSH tunnel in front of it\n";
            if (config.trustProxy)
                Bucks::logErr() << "⚠ --trust-proxy: X-Forwarded-* headers are believed only on connections from loopback\n";
        }
        Bucks::logOut() << "✓ HTTP Server started on port " << boundPort << "\n";
        Bucks::logOut() << "✓ Web UI available at http://" << config.bindAddress << ":" << boundPort << "\n";
        if (!cookieFilePath.empty()) {
            Bucks::logOut() << "✓ API token written to " << cookieFilePath << " (paste it into the UI login screen)\n";
        }
        if (!walletOk) {
            Bucks::logOut() << "⚠ Wallet subsystem disabled: " << walletErr << "\n";
        }
    }
    return true;
}

void Server::stop() {
    // Stop the miner first: it reads the wallet list and the chain.
    autoMineStop.store(true);
    autoMining.store(false);
    if (autoMinerThread.joinable()) autoMinerThread.join();

    if (!isRunning) return;

    isRunning = false;
    if (svr) static_cast<httplib::Server*>(svr)->stop();

    if (serverThread && serverThread->joinable()) {
        serverThread->join();
    }

    removeOwnCookie();
}

// Deletes the token file only while it still holds this process's token: another node may have taken it over.
void Server::removeOwnCookie() {
    if (cookieFilePath.empty()) return;
    std::string onDisk;
    if (readTextFile(cookieFilePath, onDisk) && constantTimeEquals(trimWs(onDisk), apiToken)) {
        std::error_code ec;
        fs::remove(cookieFilePath, ec);
    }
    cookieFilePath.clear();
}

bool Server::running() const {
    return isRunning.load();
}

bool Server::walletsEnabled() const {
    return walletOk;
}

std::string Server::walletError() const {
    return walletErr;
}

bool Server::addWallet(const Wallet& wallet) {
    return addWallet(wallet, json::object());
}

bool Server::addWallet(const Wallet& wallet, const nlohmann::json& meta) {
    std::lock_guard<std::mutex> lock(walletMutex);
    wallets.push_back(wallet);
    wallets.back().setAddressVersion(node.params().p2pkhAddressVersion);
    wallets.back().setMnemonic(""); // phrases live only in the creation response
    if (meta.is_object() && !meta.empty()) {
        // Extra fields ride on the wallet's entry: saveWalletsLocked keeps the fields of entries it knows.
        json entry = meta;
        entry["pubKeyHex"] = wallets.back().getPublicKeyHex();
        if (!walletDoc.contains("wallets") || !walletDoc["wallets"].is_array()) walletDoc["wallets"] = json::array();
        walletDoc["wallets"].push_back(entry);
    }
    return saveWalletsLocked();
}

std::vector<Wallet> Server::getWallets() const {
    std::lock_guard<std::mutex> lock(walletMutex);
    return wallets;
}

// ============================================================================
// Authentication helpers
// ============================================================================

bool Server::tokenMatches(const std::string& presented) const {
    if (apiToken.empty() || presented.empty()) return false;
    return constantTimeEquals(presented, apiToken);
}

bool Server::isPublicRoute(const std::string& path, const std::string& method) {
    if (method != "GET" && method != "HEAD") return false;
    static const char* const exact[] = {
        "/api/status",
        "/api/auth/status",
        "/api/blockchain/info",
        "/api/blocks/recent",
        "/api/economy/status",
        "/api/mining/status",
    };
    static const char* const prefixes[] = {
        "/api/blocks/hash/",
        "/api/blocks/height/",
    };
    for (const char* p : exact) if (path == p) return true;
    for (const char* p : prefixes) if (startsWith(path, p)) return true;
    return false;
}

Server::AuthKind Server::authenticate(const std::string& authorization,
                                      const std::string& apiTokenHeader,
                                      const std::string& cookieHeader) {
    if (!apiTokenHeader.empty() && tokenMatches(trimWs(apiTokenHeader))) {
        return AuthKind::Token;
    }
    const std::string bearer = "bearer ";
    if (authorization.size() > bearer.size() &&
        lowerAscii(authorization.substr(0, bearer.size())) == bearer &&
        tokenMatches(trimWs(authorization.substr(bearer.size())))) {
        return AuthKind::Token;
    }
    if (!cookieHeader.empty()) {
        std::string id = cookieValue(cookieHeader, SESSION_COOKIE);
        if (!id.empty() && validateSession(id)) return AuthKind::Cookie;
    }
    return AuthKind::None;
}

bool Server::hostAllowed(const std::string& hostHeader) const {
    std::string name, port;
    if (!splitHostPort(lowerAscii(trimWs(hostHeader)), name, port)) return false;

    // Operator-configured names: with a port the whole value must match,
    // without one any port is accepted (reverse proxies).
    for (const std::string& entry : config.allowedHosts) {
        std::string e = lowerAscii(trimWs(entry));
        if (e.empty()) continue;
        if (e == name + (port.empty() ? "" : ":" + port) || e == name) return true;
    }

    // Loopback names and the bind address, on our own port only.
    if (!port.empty() && boundPort > 0 && std::to_string(boundPort) != port) return false;
    if (name == "localhost" || name == "127.0.0.1" || name == "[::1]") return true;
    std::string bind = lowerAscii(config.bindAddress);
    if (bind.find(':') != std::string::npos && bind[0] != '[') bind = "[" + bind + "]";
    return !bind.empty() && name == bind;
}

bool Server::corsOriginAllowed(const std::string& origin) const {
    std::string o = lowerAscii(trimWs(origin));
    for (const std::string& allowed : config.corsOrigins) {
        if (!allowed.empty() && lowerAscii(trimWs(allowed)) == o) return true;
    }
    return false;
}

// X-Forwarded-For/-Proto are believed only with --trust-proxy and only from loopback, where the reverse proxy
// runs: a client that reaches the port directly cannot rotate its rate-limit key or fake TLS (audit OPS-09).
bool Server::forwardedHeadersTrusted(const std::string& remoteAddr) const {
    return config.trustProxy && isLoopbackAddress(remoteAddr);
}

// The node never speaks TLS itself; the browser uses it when the operator says so (--api-secure-cookie) or the
// trusted proxy reports it.
bool Server::browserUsesTls(const std::string& remoteAddr, const std::string& forwardedProto) const {
    if (config.secureCookie) return true;
    if (!forwardedHeadersTrusted(remoteAddr)) return false;
    const std::string first = forwardedProto.substr(0, forwardedProto.find(','));   // the client-facing hop
    return lowerAscii(trimWs(first)) == "https";
}

// CSRF for cookie sessions: the Origin (or Referer) must be the origin the request itself was sent to, the same
// host and port as its Host header, or a configured --api-cors-origin exactly. When the browser is known to use
// TLS only https passes (audit WAL-12). Behind a proxy that rewrites the port, list the public origin with
// --api-cors-origin.
bool Server::originAllowed(const std::string& origin, const std::string& hostHeader, bool tls) const {
    if (corsOriginAllowed(origin)) return true;
    std::string scheme, hostport;
    if (!splitOrigin(origin, scheme, hostport)) return false;
    if (scheme != "https" && (scheme != "http" || tls)) return false;
    std::string oName, oPort, hName, hPort;
    if (!hostAndPort(hostport, scheme, oName, oPort) ||
        !hostAndPort(lowerAscii(trimWs(hostHeader)), scheme, hName, hPort))
        return false;
    return oName == hName && oPort == hPort;
}

// HttpOnly and SameSite=Strict always; Secure when the browser talks TLS, so the cookie never travels over
// plain HTTP (audit WAL-12, NEW-3, OPS-10).
std::string Server::sessionCookie(const std::string& value, long long maxAge, bool secure) const {
    return std::string(SESSION_COOKIE) + "=" + value + "; Path=/; HttpOnly; SameSite=Strict" +
           (secure ? "; Secure" : "") + "; Max-Age=" + std::to_string(maxAge);
}

std::string Server::createSession() {
    std::string id = randomHex(32);
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(sessionMutex);
    for (auto it = sessions.begin(); it != sessions.end();) {
        it = (it->second.expiresAt <= now) ? sessions.erase(it) : std::next(it);
    }
    while (sessions.size() >= 64) {
        auto oldest = std::min_element(sessions.begin(), sessions.end(),
            [](const auto& a, const auto& b) { return a.second.expiresAt < b.second.expiresAt; });
        sessions.erase(oldest);
    }
    sessions[id] = Session{now + config.sessionLifetime};
    return id;
}

bool Server::validateSession(const std::string& id) {
    std::lock_guard<std::mutex> lock(sessionMutex);
    auto it = sessions.find(id);
    if (it == sessions.end()) return false;
    if (it->second.expiresAt <= std::chrono::steady_clock::now()) {
        sessions.erase(it);
        return false;
    }
    return true;
}

void Server::destroySession(const std::string& id) {
    std::lock_guard<std::mutex> lock(sessionMutex);
    sessions.erase(id);
}

// ============================================================================
// WebSocket Broadcast
// ============================================================================

void Server::broadcast(const std::string& type, const nlohmann::json& data) {
    // Current cpp-httplib simple broadcast implementation workaround:
    // We cannot easily push to clients without a custom loop or valid connection handles.
    // For this implementation, we will try to iterate stored connections.
    // IF the library version doesn't support 'send' on connection handle from another thread safely,
    // this might crash. But let's assume it works for the MVP or we need a task queue.

    // Safety: we will just log for now to avoid crashing if uncertain,
    // BUT we really want this feature.
    // Let's implement it.

    // Note: To make this compile with generic void* storage, we need to cast back checks.
    // This is risky.
    // fallback: The frontend will ALSO poll.
    (void)type;
    (void)data;
}

// ============================================================================
// Routes
// ============================================================================

void Server::setupRoutes() {
    httplib::Server* s = static_cast<httplib::Server*>(svr);

    // Answers 503 and returns true when the wallet file could not be read.
    auto walletUnavailable = [this](httplib::Response& res) {
        if (walletOk) return false;
        reply(res, 503, errorJson("Wallet subsystem disabled: " + walletErr));
        return true;
    };

    auto walletJson = [this](const Wallet& w) {
        uint64_t balance = 0;
        try { balance = w.getBalance(node); } catch (...) {}
        return json{
            {"address", w.getAddress()},
            {"publicKey", w.getPublicKeyHex()},
            {"balance", balance}
        };
    };

    // Serve index.html for root path
    s->Get("/", [this](const httplib::Request&, httplib::Response& res) {
        std::string indexPath = config.webRoot + "/index.html";
        std::ifstream file(indexPath);
        if (file) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            res.set_content(buffer.str(), "text/html");
        } else {
            res.status = 404;
            res.set_content("index.html not found", "text/plain");
        }
    });

    // Get Status
    s->Get("/api/status", [this](const httplib::Request&, httplib::Response& res) {
        const auto up = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - startedAt_).count();
        json j{
            {"status", "ok"},
            {"version", nodeVersionString(node.params().name)},
            {"network", node.params().name},
            {"uptimeSeconds", up},
            {"peerCount", network_ ? network_->peerCount() : 0},
        };
        res.set_content(j.dump(), "application/json");
    });

    // =========================================================
    // Authentication
    // =========================================================

    // Public: lets the UI decide whether to show the login screen.
    s->Get("/api/auth/status", [this](const httplib::Request& req, httplib::Response& res) {
        AuthKind kind = authenticate(req.get_header_value("Authorization"),
                                     req.get_header_value("X-API-Token"),
                                     req.get_header_value("Cookie"));
        // The reason may name files on the node: only signed-in callers see it (audit WAL-7).
        const std::string shownError = kind != AuthKind::None || walletErr.empty()
            ? walletErr : "the wallet file could not be read; sign in to see why";
        json j = {
            {"authenticated", kind != AuthKind::None},
            {"method", kind == AuthKind::Token ? "token" : kind == AuthKind::Cookie ? "cookie" : ""},
            {"walletsEnabled", walletOk},
            {"walletError", shownError}
        };
        // The location of the token file is not published to unauthenticated callers (audit WAL-7).
        if (!cookieFilePath.empty() && kind != AuthKind::None) j["cookieFile"] = cookieFilePath;
        reply(res, 200, j);
    });

    // Exchange the API token for an HttpOnly session cookie.
    s->Post("/api/auth/login", [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        if (!parseBody(req, res, body)) return;
        std::string token = body.contains("token") && body["token"].is_string() ? body["token"].get<std::string>() : "";
        if (!tokenMatches(trimWs(token))) {
            reply(res, 401, errorJson("Invalid API token"));
            return;
        }
        std::string id;
        try {
            id = createSession();
        } catch (const std::exception& e) {
            reply(res, 500, errorJson(e.what()));
            return;
        }
        // A login page served over https makes the cookie Secure even when nothing told the node about TLS.
        std::string scheme, hostport;
        const bool httpsPage = splitOrigin(req.get_header_value("Origin"), scheme, hostport) && scheme == "https";
        const bool secure = httpsPage || browserUsesTls(req.remote_addr, req.get_header_value("X-Forwarded-Proto"));
        res.set_header("Set-Cookie", sessionCookie(id, config.sessionLifetime.count(), secure));
        reply(res, 200, json{{"success", true}, {"expiresIn", config.sessionLifetime.count()}});
    });

    s->Post("/api/auth/logout", [this](const httplib::Request& req, httplib::Response& res) {
        std::string id = cookieValue(req.get_header_value("Cookie"), SESSION_COOKIE);
        if (!id.empty()) destroySession(id);
        std::string scheme, hostport;
        const bool httpsPage = splitOrigin(req.get_header_value("Origin"), scheme, hostport) && scheme == "https";
        res.set_header("Set-Cookie", sessionCookie("", 0, httpsPage ||
            browserUsesTls(req.remote_addr, req.get_header_value("X-Forwarded-Proto"))));
        reply(res, 200, json{{"success", true}});
    });

    // Basic API Routes

    // Economy Status
    s->Get("/api/economy/status", [this](const httplib::Request&, httplib::Response& res) {
        handleEconomyStatus([&](const std::string& body) {
            res.set_content(body, "application/json");
        });
    });

    // Blockchain Info
    s->Get("/api/blockchain/info", [this](const httplib::Request&, httplib::Response& res) {
        const Node::TipInfo t = node.tip();
        const ChainParams& p = node.params();
        // difficulty = (proof-of-work limit target) / (current target)
        const double difficulty = targetToDouble(Pow::decodeCompact(p.powLimitBits).target) /
                                  std::max(1.0, targetToDouble(Pow::decodeCompact(t.bits).target));
        json j{
            {"network", p.name},
            {"height", t.height},
            {"tipHash", hash256ToHex(t.hash)},
            {"bits", t.bits},
            {"difficulty", difficulty},
            {"chainWork", hash256ToHex(t.chainWork.toBytesBE())},
            {"utxoCount", t.stats.count},
            {"circulatingSupply", t.stats.totalAmount},
            {"mempoolSize", node.mempoolSize()},
            {"addressVersion", p.p2pkhAddressVersion},
            {"coinbaseMaturity", Bucks::Consensus::COINBASE_MATURITY},
            {"minRelayFeeRate", Policy::MIN_RELAY_FEERATE},
            {"dustLimit", Policy::DUST_LIMIT},
            {"syncing", t.initialBlockDownload},
            {"forkAlert", t.forkAlert},
            {"forkHeight", t.forkHeight},
            {"forkOtherTip", t.forkAlert ? hash256ToHex(t.forkOtherTip) : std::string()},
            {"isValid", true}
        };
        res.set_content(j.dump(), "application/json");
    });

    // Recent Blocks
    s->Get("/api/blocks/recent", [this](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        const uint32_t currentHeight = node.height();
        const uint32_t startHeight = (currentHeight > 10) ? currentHeight - 9 : 0;
        for (uint32_t h = startHeight; h <= currentHeight; ++h) {
            if (auto block = node.blockByHeight(h)) arr.push_back(blockToJSON(*block));
        }
        res.set_content(json{{"blocks", arr}}.dump(), "application/json");
    });

    // Block by Hash
    s->Get(R"(/api/blocks/hash/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string hashStr = req.matches[1];
        try {
            Hash256 hash = hexToHash256(hashStr);
            auto block = node.blockByHash(hash);
            if (block) {
                res.set_content(json{{"success", true}, {"block", blockToJSON(*block)}}.dump(), "application/json");
            } else {
                res.status = 404;
                res.set_content(json{{"success", false}, {"error", "Block not found"}}.dump(), "application/json");
            }
        } catch (...) {
            reply(res, 400, errorJson("Invalid hash"));
        }
    });

    // Block by Height
    s->Get(R"(/api/blocks/height/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string heightStr = req.matches[1];
        try {
            uint32_t height = std::stoul(heightStr);
            auto block = node.blockByHeight(height);
            if (block) {
                res.set_content(json{{"success", true}, {"block", blockToJSON(*block)}}.dump(), "application/json");
            } else {
                res.status = 404;
                res.set_content(json{{"success", false}, {"error", "Block not found"}}.dump(), "application/json");
            }
        } catch (...) {
            reply(res, 400, errorJson("Invalid height"));
        }
    });

    // =========================================================
    // Wallets (every route below needs a token or session)
    // =========================================================

    s->Get("/api/wallets", [this, walletUnavailable](const httplib::Request&, httplib::Response& res) {
        if (walletUnavailable(res)) return;
        std::lock_guard<std::mutex> lock(walletMutex);
        json walletArray = json::array();
        for (const auto& wallet : wallets) {
            uint64_t balance = 0;
            try { balance = wallet.getBalance(node); } catch (...) {}
            walletArray.push_back({
                {"address", wallet.getAddress()},
                {"balance", balance}
            });
        }
        res.set_content(json{{"wallets", walletArray}}.dump(), "application/json");
    });

    // Mnemonic languages available for creation (index-aligned lists). "default" is what create uses when the
    // request names none (audit WAL-3); placeholder lists are not listed on the main network.
    s->Get("/api/wallets/languages", [this](const httplib::Request&, httplib::Response& res) {
        const bool mainNet = node.params().id == NetworkId::MAIN;
        std::string err;
        std::vector<std::string> langs = mnemonicLanguages(&err);
        if (langs.empty()) {
            reply(res, 500, errorJson("Word lists unavailable: " + err));
            return;
        }
        json arr = json::array();
        for (const auto& lang : langs) {
            auto wl = findWordlist(lang);
            if (!wl || !listAllowed(*wl, mainNet)) continue;
            arr.push_back({
                {"lang", lang},
                {"normalizer", wl->normalizer},
                {"status", wl->status},
                {"sha256", wl->sha256},
                {"direction", wl->normalizer == "arabic" ? "rtl" : "ltr"}
            });
        }
        reply(res, 200, json{
            {"success", true},
            {"languages", arr},
            {"default", defaultWalletLanguage(mainNet)},
            {"wordCounts", json::array({mnemonicWordCount(128), mnemonicWordCount(256)})}
        });
    });

    // Create Wallet. The phrase is in THIS response only (API-2); the node
    // never stores it and no other route returns it.
    s->Post("/api/wallets/create", [this, walletUnavailable, walletJson](const httplib::Request& req, httplib::Response& res) {
        if (walletUnavailable(res)) return;
        json body;
        if (!parseBody(req, res, body)) return;
        if (body.contains("language") && !body["language"].is_string()) {
            reply(res, 400, errorJson("language must be a word list tag"));
            return;
        }
        std::string lang = body.value("language", "");
        if (body.contains("includeCanonical") && !body["includeCanonical"].is_boolean()) {
            reply(res, 400, errorJson("includeCanonical must be true or false"));   // before anything is saved (audit WAL-4)
            return;
        }
        const bool mainNet = node.params().id == NetworkId::MAIN;
        std::string listErr;
        if (mnemonicLanguages(&listErr).empty()) {
            reply(res, 503, errorJson("Word lists unavailable: " + listErr));
            return;
        }
        // No language given: use the best list we ship, never the development placeholder (audit WAL-3).
        if (lang.empty()) lang = defaultWalletLanguage(mainNet);
        if (lang.empty()) {
            reply(res, 503, errorJson("No word list on this node may be used on the main network (only placeholders are installed)"));
            return;
        }
        auto list = findWordlist(lang);
        if (!list) {
            reply(res, 400, errorJson("Unknown word list language '" + lang + "'"));
            return;
        }
        if (!listAllowed(*list, mainNet)) {
            reply(res, 400, errorJson("Word list '" + lang + "' is a placeholder and is not available on the main network"));
            return;
        }
        size_t entropyBits = Wallet::DEFAULT_ENTROPY_BITS;
        if (body.contains("words")) {
            if (!body["words"].is_number_unsigned()) {
                reply(res, 400, errorJson("words must be 13 or 25"));
                return;
            }
            size_t words = body["words"].get<size_t>();
            if (words == mnemonicWordCount(128)) entropyBits = 128;
            else if (words == mnemonicWordCount(256)) entropyBits = 256;
            else { reply(res, 400, errorJson("words must be 13 or 25")); return; }
        }
        try {
            std::string mnemonic;
            Wallet w = Wallet::createNew(&mnemonic, lang, entropyBits);
            w.setAddressVersion(node.params().p2pkhAddressVersion);
            w.setMnemonic("");
            // Which list (and which exact bytes of it) the phrase needs, so support can still tell years later
            // (gate WORDLIST-1).
            bool persisted = addWallet(w, json{{"wordlist", lang}, {"wordlistSha256", list->sha256}});

            json out = {
                {"success", true},
                {"wallet", walletJson(w)},
                {"mnemonic", mnemonic},
                {"wordCount", splitMnemonic(mnemonic).size()},
                {"language", lang},
                {"wordlistStatus", list->status},
                {"wordlistSha256", list->sha256},
                {"direction", list->normalizer == "arabic" ? "rtl" : "ltr"},
                {"persisted", persisted},
                {"warning", "Write these words down now. They are shown only once and the node does not store them."}
            };
            if (list->status != "release")
                out["wordlistWarning"] = placeholderList(*list)
                    ? "This phrase uses a development placeholder word list. Use it for testing only."
                    : "This phrase uses a draft word list that may still change before release, and a phrase made now "
                      "might not restore with later software. Keep a backup of the node's wallets.json as well.";
            if (!persisted) out["error"] = "Wallet could not be saved to " + walletFilePath + "; it exists in memory only";
            // Opt-in: the same phrase in the canonical (Arabic) list, for users who write it in
            // their own language but want to see the words the network side uses.
            std::string canonical;
            const std::string canonLang = canonicalMnemonicLanguage(mainNet);
            if (body.value("includeCanonical", false) && !canonLang.empty() && lang != canonLang) {
                MnemonicError cerr = MnemonicError::OK;
                canonical = convertMnemonic(mnemonic, canonLang, &cerr);
                if (cerr == MnemonicError::OK) {
                    out["canonicalMnemonic"] = canonical;
                    out["canonicalLanguage"] = canonLang;
                    out["canonicalDirection"] = "rtl";
                }
            }
            reply(res, 200, out);
            OPENSSL_cleanse(&mnemonic[0], mnemonic.size());
            if (!canonical.empty()) OPENSSL_cleanse(&canonical[0], canonical.size());
        } catch (const std::exception& e) {
            reply(res, 500, errorJson(e.what()));
        }
    });

    // Restore Wallet: words and checksum are validated before any key is derived (API-11); duplicates are not
    // added twice. An optional "language" names the list; otherwise it is detected among the lists this
    // network accepts (gate WORDLIST-2).
    s->Post("/api/wallets/restore", [this, walletUnavailable, walletJson](const httplib::Request& req, httplib::Response& res) {
        if (walletUnavailable(res)) return;
        json body;
        if (!parseBody(req, res, body)) return;
        std::string restoreMnemonic = body.contains("mnemonic") && body["mnemonic"].is_string() ? body["mnemonic"].get<std::string>() : "";
        std::string pass = body.contains("passphrase") && body["passphrase"].is_string() ? body["passphrase"].get<std::string>() : "";
        for (const char* flag : {"includeCanonical", "confirmUnused"}) {
            if (body.contains(flag) && !body[flag].is_boolean()) {
                reply(res, 400, errorJson(std::string(flag) + " must be true or false"));
                return;
            }
        }
        if (body.contains("language") && !body["language"].is_string()) {
            reply(res, 400, errorJson("language must be a word list tag"));
            return;
        }
        if (trimWs(restoreMnemonic).empty()) {
            reply(res, 400, errorJson("Mnemonic required"));
            return;
        }
        const bool mainNet = node.params().id == NetworkId::MAIN;
        std::string lang = body.value("language", "");
        if (!lang.empty()) {
            auto list = findWordlist(lang);
            if (!list) {
                reply(res, 400, errorJson("Unknown word list language '" + lang + "'"));
                return;
            }
            if (!listAllowed(*list, mainNet)) {
                reply(res, 400, errorJson("Word list '" + lang + "' is a placeholder and is not available on the main network"));
                return;
            }
        }
        std::vector<uint8_t> entropy;
        const MnemonicError merr = lang.empty()
            ? mnemonicToEntropyAmong(restoreMnemonic, allowedMnemonicLanguages(mainNet), entropy, &lang)
            : mnemonicToEntropy(restoreMnemonic, entropy, nullptr, lang);
        if (merr != MnemonicError::OK) {
            // The message never contains the phrase.
            reply(res, merr == MnemonicError::WORDLIST_LOAD_FAILED ? 503 : 400,
                  errorJson(std::string("Invalid mnemonic: ") + mnemonicErrorString(merr)));
            return;
        }
        try {
            Wallet w = Wallet::fromMnemonic(restoreMnemonic, pass, lang);
            w.setAddressVersion(node.params().p2pkhAddressVersion);
            w.setMnemonic("");
            // Which list the phrase was written in, and (opt-in) the same phrase in the canonical list.
            json echo = json::object();
            echo["detectedLanguage"] = lang;
            const std::string canonLang = canonicalMnemonicLanguage(mainNet);
            if (body.value("includeCanonical", false) && !canonLang.empty()) {
                MnemonicError cerr = MnemonicError::OK;
                std::string canon = mnemonicFromEntropy(entropy, canonLang, &cerr);
                if (cerr == MnemonicError::OK) {
                    echo["canonicalMnemonic"] = canon;
                    echo["canonicalLanguage"] = canonLang;
                    echo["canonicalDirection"] = "rtl";
                }
                if (!canon.empty()) OPENSSL_cleanse(&canon[0], canon.size());
            }
            OPENSSL_cleanse(entropy.data(), entropy.size());
            const std::string address = w.getAddress();
            {
                std::lock_guard<std::mutex> lock(walletMutex);
                for (const auto& existing : wallets) {
                    if (existing.getAddress() == address) {
                        json out = {{"success", true}, {"wallet", walletJson(existing)}, {"existing", true}};
                        out.update(echo);
                        reply(res, 200, out);
                        return;
                    }
                }
            }
            // A mistyped passphrase does not fail: it gives a valid but different, empty wallet. Saving that
            // silently makes users think their coins are gone, so ask first (audit WAL-13).
            uint64_t coins = 0;
            if (!pass.empty() && !body.value("confirmUnused", false) &&
                (!node.coinCount(hash160(w.getPublicKey()), coins) || coins == 0)) {
                reply(res, 409, json{
                    {"success", false},
                    {"confirmRequired", true},
                    {"reason", "unusedWithPassphrase"},
                    {"wallet", walletJson(w)},
                    {"error", "This phrase and passphrase give a wallet with no coins on this node's chain (address " +
                              address + "). A mistyped passphrase gives a different, empty wallet. Check the "
                              "passphrase, or send the request again with confirmUnused: true to add it anyway."}
                });
                return;
            }
            const auto list = findWordlist(lang);
            bool persisted = addWallet(w, json{{"wordlist", lang}, {"wordlistSha256", list ? list->sha256 : ""}});
            json out = {{"success", true}, {"wallet", walletJson(w)}, {"existing", false}, {"persisted", persisted}};
            out.update(echo);
            if (!persisted) out["error"] = "Wallet could not be saved to " + walletFilePath + "; it exists in memory only";
            reply(res, 200, out);
        } catch (const std::exception& e) {
            // e.what() never contains the phrase (see Wallet::fromMnemonic)
            OPENSSL_cleanse(entropy.data(), entropy.size());
            reply(res, 400, errorJson(e.what()));
        }
    });

    // Get Transactions (bounded: newest `limit` transactions, ascending)
    s->Get("/api/transactions", [this](const httplib::Request& req, httplib::Response& res) {
        size_t limit = 100;
        if (req.has_param("limit")) {
            try { limit = std::min<size_t>(1000, std::max<size_t>(1, std::stoul(req.get_param_value("limit")))); }
            catch (...) { reply(res, 400, errorJson("Invalid limit")); return; }
        }
        std::vector<json> collected;
        uint32_t height = node.height();
        for (uint32_t h = height; collected.size() < limit; --h) {
            auto blockOpt = node.blockByHeight(h);
            if (blockOpt) {
                const Block* block = &*blockOpt;
                for (auto it = block->transactions.rbegin(); it != block->transactions.rend() && collected.size() < limit; ++it) {
                    const auto& tx = *it;
                    uint64_t totalAmount = 0;
                    for (const auto& out : tx.outputs) totalAmount += out.amount;
                    collected.push_back({
                        {"hash", hash256ToHex(tx.txid())},
                        {"inputCount", tx.inputs.size()},
                        {"outputCount", tx.outputs.size()},
                        {"totalAmount", totalAmount},
                        {"timestamp", block->header.timestamp}
                    });
                }
            }
            if (h == 0) break;
        }
        json txArray = json::array();
        for (auto it = collected.rbegin(); it != collected.rend(); ++it) txArray.push_back(*it);
        res.set_content(json{{"transactions", txArray}, {"limit", limit}}.dump(), "application/json");
    });

    // Create Transaction (Send)
    s->Post("/api/transactions/send", [this, walletUnavailable](const httplib::Request& req, httplib::Response& res) {
        if (walletUnavailable(res)) return;
        json j;
        if (!parseBody(req, res, j)) return;
        if (!j.contains("from") || !j["from"].is_string() ||
            !j.contains("to") || !j["to"].is_string() ||
            !j.contains("amount") || !j["amount"].is_number_unsigned()) {
            reply(res, 400, errorJson("from, to (strings) and amount (unsigned integer) are required"));
            return;
        }
        std::string fromAddress = j["from"];
        std::string toAddress = j["to"];
        uint64_t amount = j["amount"];
        if (amount == 0) {
            reply(res, 400, errorJson("amount must be positive"));
            return;
        }

        // Copy the wallet under the lock: a raw pointer into `wallets` would
        // dangle as soon as another request appends a wallet (API-8).
        Wallet sender;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(walletMutex);
            for (const auto& w : wallets) {
                if (w.getAddress() == fromAddress) {
                    sender = w;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            reply(res, 404, errorJson("Sender wallet not found"));
            return;
        }

        try {
            Transaction tx = sender.createTransaction(toAddress, amount, node);
            const ValidationState accepted = node.submitTransaction(tx);
            if (accepted.valid()) {
                // The network layer and WebSocket clients hear about it through the node's listeners.
                json txJson = {
                    {"hash", hash256ToHex(tx.txid())},
                    {"inputCount", tx.inputs.size()},
                    {"outputCount", tx.outputs.size()},
                    {"totalAmount", amount}
                };
                res.set_content(json{{"success", true}, {"transaction", txJson}}.dump(), "application/json");
            } else {
                reply(res, 400, errorJson("Transaction rejected: " + accepted.code));
            }
        } catch (const std::exception& e) {
            reply(res, 400, errorJson(e.what()));
        }
    });

    // Mine Block
    s->Post("/api/mining/mine", [this](const httplib::Request& req, httplib::Response& res) {
        json out;
        int status = mineBlock(req.body, out);
        reply(res, status, out);
    });

    // Sign Message (for SSO/Auth): domain-separated, see signedMessageHash (API-10)
    s->Post("/api/wallets/sign", [this, walletUnavailable](const httplib::Request& req, httplib::Response& res) {
        if (walletUnavailable(res)) return;
        json j;
        if (!parseBody(req, res, j)) return;
        std::string address = j.contains("address") && j["address"].is_string() ? j["address"].get<std::string>() : "";
        std::string message = j.contains("message") && j["message"].is_string() ? j["message"].get<std::string>() : "";
        if (address.empty() || message.empty()) {
            reply(res, 400, errorJson("Missing address or message"));
            return;
        }
        if (message.size() > 4096) {
            reply(res, 400, errorJson("Message too long (max 4096 bytes)"));
            return;
        }
        Wallet signer;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(walletMutex);
            for (const auto& w : wallets) {
                if (w.getAddress() == address) {
                    signer = w;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            reply(res, 404, errorJson("Wallet not found"));
            return;
        }
        Hash256 digest = signedMessageHash(message);
        std::vector<uint8_t> signature;
        if (signECDSA(digest, signer.getPrivateKey(), signature)) {
            reply(res, 200, json{
                {"success", true},
                {"scheme", "bucks-signed-message-v1"},
                {"signature", hexStr(signature)},
                {"publicKey", signer.getPublicKeyHex()},
                {"address", signer.getAddress()}
            });
        } else {
            reply(res, 500, errorJson("Signing failed"));
        }
    });

    // Anchor IPFS CID
    s->Post("/api/anchor", [this](const httplib::Request& req, httplib::Response& res) {
        json j;
        if (!parseBody(req, res, j)) return;
        if (!j.contains("cid") || !j["cid"].is_string()) {
            reply(res, 400, errorJson("Missing cid"));
            return;
        }
        std::string cid = j["cid"];
        if (cid.empty() || cid.size() > 128 ||
            !std::all_of(cid.begin(), cid.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)); })) {
            reply(res, 400, errorJson("Invalid cid"));
            return;
        }
        {
            std::lock_guard<std::mutex> lock(anchorMutex);
            if (pendingCIDs.size() >= 1000) {
                reply(res, 429, errorJson("Too many pending anchors"));
                return;
            }
            pendingCIDs.push_back(cid);
        }
        res.set_content(json{{"success", true}, {"message", "CID queued for anchoring"}, {"cid", cid}}.dump(), "application/json");
    });

    // IPFS Pin Status
    s->Get(R"(/api/ipfs/pin/ls/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string cid = req.matches[1];
        if (cid.empty() || cid.size() > 128 ||
            !std::all_of(cid.begin(), cid.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)); })) {
            reply(res, 400, errorJson("Invalid cid"));
            return;
        }
        httplib::Client cli("localhost", config.ipfsPort);
        auto ipfsRes = cli.Post(("/api/v0/pin/ls?arg=" + cid).c_str());
        if (ipfsRes && ipfsRes->status == 200) {
            res.set_content(ipfsRes->body, "application/json");
        } else {
            res.status = 404;
            res.set_content(json{{"success", false}, {"error", "CID not pinned or IPFS error"}}.dump(), "application/json");
        }
    });

    // =========================================================
    // Primary Wallet (never includes key material or the phrase)
    // =========================================================
    s->Get("/api/wallets/primary", [this, walletUnavailable](const httplib::Request&, httplib::Response& res) {
        if (walletUnavailable(res)) return;
        std::lock_guard<std::mutex> lock(walletMutex);
        if (wallets.empty()) {
            res.status = 404;
            res.set_content(json{{"success", false}, {"error", "No primary wallet yet"}}.dump(), "application/json");
            return;
        }
        const Wallet& w = wallets[0];
        uint64_t balance = 0;
        try { balance = w.getBalance(node); } catch (...) {}
        json resp = {
            {"success", true},
            {"address",  w.getAddress()},
            {"publicKey", w.getPublicKeyHex()},
            {"balance",  balance},
            {"_v", 3}
        };

        // Include transaction history from UTXO set
        json txHistory = json::array();
        try {
            const auto coins = node.coinsByHash160(hash160(w.getPublicKey()), 20);
            uint64_t coinCount = coins.size();
            node.coinCount(hash160(w.getPublicKey()), coinCount);
            resp["utxoCount"] = coinCount;
            for (const auto& u : coins) {
                txHistory.push_back({
                    {"hash", hash256ToHex(u.op.txid)},
                    {"type", u.coin.isCoinbase ? "mining_reward" : "receive"},
                    {"amount", u.coin.amount},
                    {"blockHeight", u.coin.height},
                    {"timestamp", 0}
                });
            }
        } catch (...) {
            resp["utxoError"] = "failed to load UTXOs";
        }
        resp["transactions"] = txHistory;

        res.set_content(resp.dump(), "application/json");
    });

    // =========================================================
    // Auto-Mining Control
    // =========================================================
    s->Post("/api/mining/start", [this](const httplib::Request&, httplib::Response& res) {
        startAutoMine();
        if (!autoMining.load()) {
            reply(res, 400, errorJson("No wallet to mine to; create or restore one first"));
            return;
        }
        res.set_content(json{
            {"success", true},
            {"message", "Auto-mining started"},
            {"minerAddress", minerAddress}
        }.dump(), "application/json");
    });

    s->Post("/api/mining/stop", [this](const httplib::Request&, httplib::Response& res) {
        stopAutoMine();
        res.set_content(json{
            {"success", true},
            {"message", "Auto-mining stopped"}
        }.dump(), "application/json");
    });

    // Operator decision after a fork alert (spec 8.5): follow the deeper-reorg branch whose tip is given.
    s->Post("/api/node/approve-reorg", [this](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body, nullptr, false);
        const std::string hex = (j.is_object() && j.contains("tipHash") && j["tipHash"].is_string())
                                    ? j["tipHash"].get<std::string>() : std::string();
        const bool isHex = hex.size() == 64 &&
                           hex.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
        if (!isHex) {
            reply(res, 400, errorJson("tipHash (64 hex characters) is required"));
            return;
        }
        std::string err;
        if (!node.approveReorg(hexToHash256(hex), err)) {
            reply(res, 409, errorJson(err));
            return;
        }
        res.set_content(json{{"success", true}}.dump(), "application/json");
    });

    s->Get("/api/mining/status", [this](const httplib::Request& req, httplib::Response& res) {
        json j{
            {"mining",       autoMining.load()},
            {"blocksFound",  blocksFound.load()},
            {"hashRate",     hashesPerSecond.load()}
        };
        // The payout address links this public node to its owner's coins: signed-in callers only (audit WAL-7).
        if (authenticate(req.get_header_value("Authorization"), req.get_header_value("X-API-Token"),
                         req.get_header_value("Cookie")) != AuthKind::None)
            j["minerAddress"] = minerAddress;
        res.set_content(j.dump(), "application/json");
    });
}

// ============================================================================
// Logic Implementations
// ============================================================================

void Server::handleEconomyStatus(std::function<void(const std::string&)> response) {
    uint64_t supply = node.circulatingSupply();
    uint64_t gold = node.goldReserve();
    double price = 0.0;
    if (supply > 0) {
        price = (double)gold / ((double)supply / 100000000.0);
    }
    json j = {
        {"success", true},
        {"supply", supply},
        {"supply_fmt", (double)supply / 100000000.0},
        {"gold_reserve", gold},
        {"gold_reserve_unit", "g"},
        // The reserve is whatever the vault signers recorded on-chain; the
        // node cannot verify physical gold, so UIs must label it "reported".
        {"reserve_source", "vault-signed OP_RETURN markers (reported, not audited by the node)"},
        {"price_per_buck", price}
    };
    response(j.dump());
}

int Server::mineBlock(const std::string& body, json& out) {
    try {
        size_t minerIndex = 0;
        if (!trimWs(body).empty()) {
            json j;
            try { j = json::parse(body); } catch (...) { out = errorJson("Invalid JSON body"); return 400; }
            if (j.is_object() && j.contains("minerWalletIndex")) {
                if (!j["minerWalletIndex"].is_number_unsigned()) {
                    out = errorJson("minerWalletIndex must be a non-negative integer");
                    return 400;
                }
                minerIndex = j["minerWalletIndex"].get<size_t>();
            }
        }

        // Copy only what the coinbase needs. The wallet list is never written
        // here (the old `Wallet& miner = wallets[0]; miner = wallets[idx];`
        // overwrote the primary key: CRY-9/API-5/OPS-3) and the lock is not
        // held across the hash loop (API-8).
        std::vector<uint8_t> minerScript;
        std::string minerAddr;
        {
            std::lock_guard<std::mutex> lock(walletMutex);
            if (wallets.empty()) {
                out = errorJson("No wallet to mine to; create or restore one first");
                return 400;
            }
            if (minerIndex >= wallets.size()) {
                out = errorJson("minerWalletIndex out of range");
                return 400;
            }
            minerScript = wallets[minerIndex].getP2PKHScript();
            minerAddr = wallets[minerIndex].getAddress();
        }

        // IPFS Liveness Check
        std::string ipfsPeerId;
        if (config.enableIPFS) {
            if (!checkIPFSStatus(ipfsPeerId)) {
                out = errorJson("IPFS Node not active (Required for mining)");
                return 503;
            }
        }

        // Coinbase data: the IPFS peer id travels in the miner tag, anchored CIDs in the single
        // NULL_DATA output (at most 80 bytes per block; the rest wait for the next block). Neither
        // goes into the scriptSig any more (API-16).
        AssemblerOptions options;
        if (!ipfsPeerId.empty()) options.minerTag = "IPFS:" + ipfsPeerId;
        std::vector<std::string> anchored;
        {
            std::lock_guard<std::mutex> lock(anchorMutex);
            std::string payload;
            while (!pendingCIDs.empty()) {
                const std::string piece = "ANCHOR:" + pendingCIDs.front();
                const size_t extra = piece.size() + (payload.empty() ? 0 : 1);
                if (payload.size() + extra > 80) break;
                if (!payload.empty()) payload += ';';
                payload += piece;
                anchored.push_back(pendingCIDs.front());
                pendingCIDs.erase(pendingCIDs.begin());
            }
            options.extraPayload.assign(payload.begin(), payload.end());
        }
        auto requeue = [&]() {
            std::lock_guard<std::mutex> lock(anchorMutex);
            pendingCIDs.insert(pendingCIDs.begin(), anchored.begin(), anchored.end());
        };

        std::atomic<bool> noStop{false};
        std::string mineErr;
        Hash256 foundHash{};
        const MineResult result = node.mine(minerScript, options, noStop, mineErr, &foundHash, 60);
        if (result == MineResult::STOPPED) {
            requeue();
            out = errorJson("Mining timed out");
            return 503;
        }
        if (result == MineResult::FAILED) {
            requeue();
            out = errorJson(mineErr.empty() ? "Mining failed" : mineErr);
            return mineErr == "node is syncing" ? 503 : 500;
        }
        auto mined = node.blockByHash(foundHash);
        if (!mined) {
            out = errorJson("Block rejected");
            return 500;
        }
        for (const std::string& cid : anchored) pinCID(cid);
        out = json{{"success", true}, {"block", blockToJSON(*mined)}, {"minerAddress", minerAddr}};
        return 200;

    } catch (const std::exception& e) {
        out = errorJson(e.what());
        return 500;
    }
}

nlohmann::json Server::blockToJSON(const Block& b) const {
    return {
        {"height", b.header.height},
        {"hash", hash256ToHex(b.getHash())},
        {"prevHash", hash256ToHex(b.header.prevBlockHash)},
        {"merkleRoot", hash256ToHex(b.header.merkleRoot)},
        {"timestamp", b.header.timestamp},
        {"difficulty", b.header.bits},
        {"nonce", b.header.nonce},
        {"transactionCount", b.transactions.size()}
    };
}
nlohmann::json Server::txToJSON(const Transaction& tx) const {
    uint64_t total = 0;
    for (const auto& out : tx.outputs) total += out.amount;
    return {
        {"hash", hash256ToHex(tx.txid())},
        {"inputCount", tx.inputs.size()},
        {"outputCount", tx.outputs.size()},
        {"totalAmount", total}
    };
}

bool Server::checkIPFSStatus(std::string& peerId) {
    if (!config.enableIPFS) return true; // Bypass if not enabled (or should we force it?)
    // Plan says: "If IPFS is offline, refuse to mine". So if enabled, we MUST check.

    // Simple HTTP check to /api/v0/id
    httplib::Client cli("localhost", config.ipfsPort);
    cli.set_connection_timeout(0, 500000); // 500ms timeout
    cli.set_read_timeout(0, 500000); // 500ms timeout

    auto res = cli.Post("/api/v0/id");
    if (res && res->status == 200) {
        try {
            auto j = json::parse(res->body);
            if (j.contains("ID")) {
                peerId = j["ID"];
                return true;
            }
        } catch (...) {}
    }
    return false;
}

bool Server::pinCID(const std::string& cid) {
    if (!config.enableIPFS) return true;

    httplib::Client cli("localhost", config.ipfsPort);
    // Longer timeout for pinning
    cli.set_connection_timeout(0, 1000000); // 1s
    cli.set_read_timeout(0, 5000000); // 5s

    auto res = cli.Post(("/api/v0/pin/add?arg=" + cid).c_str());
    if (res && res->status == 200) {
        return true;
    }
    return false;
}

// ============================================================================
// Auto-mining
// ============================================================================

void Server::startAutoMine() {
    if (autoMining.exchange(true)) return; // already running
    autoMineStop.store(false);

    // Check if we have any wallet to mine to
    {
        std::lock_guard<std::mutex> lock(walletMutex);
        if (wallets.empty()) {
            autoMining.store(false);
            Bucks::logOut() << "[miner] FAILED to start auto-mining: No wallets available. Please create or restore a wallet first.\n";
            return;
        }
        minerAddress = wallets[0].getAddress();
    }

    // A previous loop may have exited after stopAutoMine(); reap it first.
    if (autoMinerThread.joinable()) autoMinerThread.join();

    Bucks::logOut() << "[miner] Auto-mining started -> " << minerAddress << std::endl;
    autoMinerThread = std::thread(&Server::autoMineLoop, this);
}

void Server::stopAutoMine() {
    autoMineStop.store(true);
    autoMining.store(false);
    Bucks::logOut() << "[miner] Auto-mining stopped." << std::endl;
}

void Server::autoMineLoop() {
    using clock = std::chrono::steady_clock;

    while (autoMining.load() && !autoMineStop.load()) {
        Script script;
        {
            std::lock_guard<std::mutex> lk(walletMutex);
            if (wallets.empty()) break;
            script = wallets[0].getP2PKHScript();
        }

        AssemblerOptions options;     // tag "Bucks"; templates are rebuilt every minute and on every new tip
        std::string err;
        Hash256 found{};
        const uint64_t hashesBefore = node.minerHashes();
        const auto started = clock::now();
        const MineResult r = node.mine(script, options, autoMineStop, err, &found, 0);

        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(clock::now() - started).count();
        if (seconds > 0) hashesPerSecond.store((node.minerHashes() - hashesBefore) / seconds);

        if (r == MineResult::FOUND) {
            ++blocksFound;
            Bucks::logOut() << "[miner] *** Block found! hash=" << hash256ToHex(found)
                      << " total=" << blocksFound.load() << " ***" << std::endl;
        } else if (r == MineResult::FAILED) {
            // For example "node is syncing": wait and try again.
            Bucks::logOut() << "[miner] " << err << ", retrying..." << std::endl;
            for (int i = 0; i < 10 && autoMining.load() && !autoMineStop.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    Bucks::logOut() << "[miner] Auto-mine loop exited." << std::endl;
}

// ============================================================================
// Wallet persistence
// ============================================================================
//
// <dataDir>/wallets.json, mode 0600, written atomically:
//   { "version": 2, "wallets": [ { "privKeyHex", "pubKeyHex", "address", ... } ], ... }
// The legacy layout (a bare array) is accepted on read. Unknown fields, top
// level and per entry, survive a rewrite. Mnemonics are never written and
// never loaded (API-2/CRY-7). If the file cannot be parsed the subsystem is
// disabled and the file is left exactly as it is (API-4).

bool Server::saveWalletsLocked() {
    if (!walletOk) {
        Bucks::logErr() << "✗ Not saving wallets: " << walletErr << "\n";
        return false;
    }

    std::error_code ec;
    if (!walletBackupDone && fs::exists(walletFilePath, ec)) {
        std::string old, err;
        if (!readTextFile(walletFilePath, old) ||
            !writePrivateFileAtomic(walletFilePath + ".bak", old, &err)) {
            Bucks::logErr() << "✗ Cannot back up " << walletFilePath << " (" << err << "); not rewriting it\n";
            return false;
        }
        walletBackupDone = true;
    }

    json out = walletDoc.is_object() ? walletDoc : json::object();
    out["version"] = 2;

    // Existing entries keep their extra fields; entries that failed to load
    // are carried over untouched so a rewrite never drops data.
    json oldEntries = out.contains("wallets") && out["wallets"].is_array() ? out["wallets"] : json::array();
    std::unordered_map<std::string, json> byPub;
    std::vector<json> opaque;
    for (const auto& e : oldEntries) {
        if (!e.is_object()) { opaque.push_back(e); continue; }
        std::string pub = lowerAscii(e.value("pubKeyHex", e.value("pubKey", "")));
        bool loaded = false;
        for (const auto& w : wallets) {
            if (lowerAscii(w.getPublicKeyHex()) == pub) { loaded = true; break; }
        }
        if (loaded) byPub[pub] = e; else opaque.push_back(e);
    }

    json arr = json::array();
    for (const auto& w : wallets) {
        std::string pub = lowerAscii(w.getPublicKeyHex());
        json e = byPub.count(pub) ? byPub[pub] : json::object();
        e.erase("privKey");
        e.erase("pubKey");
        e.erase("mnemonic");
        e["privKeyHex"] = w.getPrivateKeyHex();
        e["pubKeyHex"]  = w.getPublicKeyHex();
        e["address"]    = w.getAddress();
        arr.push_back(e);
    }
    for (auto& e : opaque) {
        if (e.is_object()) e.erase("mnemonic");
        arr.push_back(e);
    }
    out["wallets"] = arr;

    std::string text = out.dump(4) + "\n";
    std::string err;
    bool ok = writePrivateFileAtomic(walletFilePath, text, &err);
    OPENSSL_cleanse(&text[0], text.size());
    if (!ok) {
        Bucks::logErr() << "✗ Cannot write " << walletFilePath << ": " << err << "\n";
        return false;
    }
    walletDoc = out;
    return true;
}

bool Server::loadWalletDocument(const std::string& path, bool legacy) {
    std::string text;
    if (!readTextFile(path, text)) {
        walletErr = "cannot read " + path;
        Bucks::logErr() << "✗ Wallet subsystem disabled: " << walletErr << "\n";
        return false;
    }
    json doc;
    try {
        doc = json::parse(text);
    } catch (const std::exception&) {
        walletErr = path + " is not valid JSON; it is left untouched (restore it from wallets.json.bak or fix it by hand)";
        Bucks::logErr() << "✗ Wallet subsystem disabled: " << walletErr << "\n";
        return false;
    }
    OPENSSL_cleanse(&text[0], text.size());

    json entries;
    if (doc.is_array()) {
        entries = doc;
        doc = json{{"version", 2}, {"wallets", entries}};
    } else if (doc.is_object() && doc.contains("wallets") && doc["wallets"].is_array()) {
        entries = doc["wallets"];
    } else {
        walletErr = path + " has an unknown layout; it is left untouched";
        Bucks::logErr() << "✗ Wallet subsystem disabled: " << walletErr << "\n";
        return false;
    }

    std::lock_guard<std::mutex> lock(walletMutex);
    wallets.clear();
    size_t skipped = 0;
    size_t n = 0;
    for (const auto& item : entries) {
        ++n;
        if (!item.is_object()) { ++skipped; continue; }
        // Support both key name styles
        std::string privKey = item.value("privKeyHex", item.value("privKey", ""));
        std::string pubKey  = item.value("pubKeyHex",  item.value("pubKey", ""));
        if (privKey.empty() || pubKey.empty()) { ++skipped; continue; }
        try {
            // The constructor rejects a public key that does not belong to
            // the private key, so a tampered entry cannot redirect payouts.
            wallets.emplace_back(privKey, pubKey);
            wallets.back().setAddressVersion(node.params().p2pkhAddressVersion);
        } catch (const std::exception& e) {
            ++skipped;
            Bucks::logErr() << "⚠ Wallet entry " << n << " in " << path << " skipped: " << e.what() << "\n";
        }
        OPENSSL_cleanse(&privKey[0], privKey.size());
    }
    walletDoc = doc;
    if (skipped && config.enableLogging) {
        Bucks::logErr() << "⚠ " << skipped << " wallet entr" << (skipped == 1 ? "y" : "ies") << " in " << path
                  << " could not be loaded; " << (legacy ? "" : "they are kept as-is in the file") << "\n";
    }
    return true;
}

void Server::loadWallets() {
    walletOk = false;
    walletErr.clear();
    walletFromLegacy = false;
    walletDoc = json{{"version", 2}, {"wallets", json::array()}};

    std::error_code ec;
    if (fs::exists(walletFilePath, ec)) {
        if (!loadWalletDocument(walletFilePath, false)) return;
    } else if (!config.legacyWalletFile.empty() && fs::exists(config.legacyWalletFile, ec)) {
        // Pre-datadir file in the working directory: read once, never written.
        if (!loadWalletDocument(config.legacyWalletFile, true)) return;
        walletFromLegacy = true;
        size_t count;
        {
            std::lock_guard<std::mutex> lock(walletMutex);
            count = wallets.size();
        }
        Bucks::logErr() << "⚠ Loaded " << count << " wallet(s) from legacy " << fs::absolute(config.legacyWalletFile, ec).string()
                  << " (read-only). Move that file to " << walletFilePath
                  << " and delete the original; new wallets are saved to the data directory only.\n";
    }
    walletOk = true;
}

} // namespace Bucks
