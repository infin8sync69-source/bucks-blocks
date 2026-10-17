// The wallet views the web console uses: the fee of a payment before it is sent (ONB-03), the spendable part
// of a balance (ONB-07), one wallet's history (ONB-05) and the recovery-phrase backup flag (ONB-08).
#include <gtest/gtest.h>
#include "server.h"
#include "node.h"
#include "wallet.h"
#include "crypto.h"
#include "httplib.h"
#include "node_harness.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using json = nlohmann::json;
using namespace Bucks;

namespace {

const char* const kToken = "views-token-0123456789abcdef0123456789abcdef";

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The API prints a txid byte for byte.
Hash256 txidOf(const std::string& hex) {
    Hash256 id{};
    for (size_t i = 0; i < id.size() && 2 * i + 1 < hex.size(); ++i)
        id[i] = static_cast<uint8_t>(std::stoul(hex.substr(2 * i, 2), nullptr, 16));
    return id;
}

class WalletViewsTest : public ::testing::Test {
protected:
    TestUtil::NodeHarness h;
    ServerConfig cfg;
    const httplib::Headers auth{{"X-API-Token", kToken}};

    void SetUp() override {
        ::unsetenv("BUCKS_API_TOKEN");
        ASSERT_TRUE(h.node) << h.error;
        cfg.port = 0;
        cfg.enableLogging = false;
        cfg.enableRateLimit = false;
        cfg.dataDir = h.dataDir();
        cfg.webRoot = h.root() + "/web";
        cfg.apiToken = kToken;
    }

    json post(httplib::Client& cli, const std::string& path, const json& body, int expect) {
        auto r = cli.Post(path.c_str(), auth, body.dump(), "application/json");
        EXPECT_TRUE(r) << path;
        if (!r) return json();
        EXPECT_EQ(r->status, expect) << path << " " << r->body;
        return json::parse(r->body, nullptr, false);
    }

    std::string createWallet(httplib::Client& cli) {
        return post(cli, "/api/wallets/create", json::object(), 200)["wallet"]["address"];
    }

    // Mines `n` blocks paying the wallet at `index` (regtest blocks need a couple of hashes).
    void mineTo(Server& server, size_t index, int n) {
        const Script script = server.getWallets().at(index).getP2PKHScript();
        std::atomic<bool> stop{false};
        std::string err;
        for (int i = 0; i < n; ++i) ASSERT_EQ(h.node->mine(script, {}, stop, err), MineResult::FOUND) << err;
    }
};

} // namespace

TEST_F(WalletViewsTest, EstimateGivesTheFeeThatSendPays) {
    Server server(*h.node, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());
    cli.set_read_timeout(60, 0);
    const std::string a = createWallet(cli);
    const std::string b = createWallet(cli);
    mineTo(server, 0, 110);   // coinbases at heights 1..11 are mature at the next height, 111

    // Enough to need eight 50-BUCKS coins: more than six inputs pay more than the minimum fee.
    const uint64_t amount = 360ULL * 100000000ULL;
    const json est = post(cli, "/api/transactions/estimate", {{"from", a}, {"to", b}, {"amount", amount}}, 200);
    ASSERT_TRUE(est.value("success", false)) << est.dump();
    EXPECT_EQ(est["inputCount"], 8u);
    EXPECT_EQ(est["outputCount"], 2u);
    const uint64_t fee = est["fee"];
    EXPECT_GT(fee, 100000u) << "more than six inputs pay more than the minimum fee";
    EXPECT_EQ(est["total"], amount + fee);
    EXPECT_EQ(est["change"], 8 * 50ULL * 100000000ULL - amount - fee);
    EXPECT_EQ(h.node->mempoolSize(), 0u) << "an estimate sends nothing";

    // A fee above what the user approved is refused, and nothing is sent.
    json refused = post(cli, "/api/transactions/send", {{"from", a}, {"to", b}, {"amount", amount}, {"maxFee", fee - 1}}, 409);
    EXPECT_EQ(refused["fee"], fee);
    EXPECT_EQ(h.node->mempoolSize(), 0u);
    post(cli, "/api/transactions/send", {{"from", a}, {"to", b}, {"amount", amount}, {"maxFee", "1000"}}, 400);

    // The approved fee is what is paid, and the reply says so.
    const json sent = post(cli, "/api/transactions/send", {{"from", a}, {"to", b}, {"amount", amount}, {"maxFee", fee}}, 200);
    EXPECT_EQ(sent["transaction"]["fee"], fee);
    ASSERT_EQ(h.node->mempoolSize(), 1u);
    const auto tx = h.node->mempoolTransaction(txidOf(sent["transaction"]["hash"]));
    ASSERT_TRUE(tx.has_value());
    EXPECT_EQ(tx->inputs.size(), 8u);
    EXPECT_EQ(8 * 50ULL * 100000000ULL - tx->totalOutput(), fee) << "inputs minus outputs";

    // Without maxFee the send still works and reports the fee (one coin, the minimum fee).
    const json small = post(cli, "/api/transactions/send", {{"from", a}, {"to", b}, {"amount", 100000000}}, 200);
    EXPECT_EQ(small["transaction"]["fee"], 100000u);
    server.stop();
}

TEST_F(WalletViewsTest, WalletListSplitsBalanceIntoSpendableImmatureAndLocked) {
    Server server(*h.node, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());
    const std::string a = createWallet(cli);
    const std::string b = createWallet(cli);
    const uint64_t coin = 50ULL * 100000000ULL;
    auto walletsNow = [&]() {
        auto r = cli.Get("/api/wallets", auth);
        EXPECT_TRUE(r && r->status == 200);
        return json::parse(r->body)["wallets"];
    };

    mineTo(server, 0, 110);
    json w = walletsNow()[0];
    EXPECT_EQ(w["address"], a);
    EXPECT_EQ(w["balance"], 110 * coin);
    EXPECT_EQ(w["spendable"], 11 * coin) << "rewards of heights 1..11 are mature at height 111";
    EXPECT_EQ(w["immature"], 99 * coin);
    EXPECT_EQ(w["locked"], 0u);
    EXPECT_EQ(w["incoming"], 0u);

    // A pending payment locks the coin it spends; its outputs are incoming for both wallets.
    const json sent = post(cli, "/api/transactions/send", {{"from", a}, {"to", b}, {"amount", 100000000}}, 200);
    const uint64_t fee = sent["transaction"]["fee"];
    json list = walletsNow();
    EXPECT_EQ(list[0]["balance"], 110 * coin) << "confirmed balance is unchanged until the payment is mined";
    EXPECT_EQ(list[0]["locked"], coin);
    EXPECT_EQ(list[0]["spendable"], 10 * coin);
    EXPECT_EQ(list[0]["incoming"], coin - 100000000 - fee) << "the change";
    EXPECT_EQ(list[1]["incoming"], 100000000u);
    EXPECT_EQ(list[1]["balance"], 0u);
    EXPECT_EQ(list[1]["spendable"], 0u);

    // Once mined the lock is gone and the payment is part of the balance.
    mineTo(server, 1, 1);
    list = walletsNow();
    EXPECT_EQ(list[0]["locked"], 0u);
    EXPECT_EQ(list[0]["incoming"], 0u);
    EXPECT_EQ(list[0]["balance"], 109 * coin + coin - 100000000 - fee);
    EXPECT_EQ(list[1]["balance"], 100000000 + coin + fee) << "the payment plus the new reward and its fee";
    EXPECT_EQ(list[1]["spendable"], 100000000u) << "the reward itself is immature";
    server.stop();
}

TEST_F(WalletViewsTest, HistoryListsSentReceivedAndPendingPaymentsOfOneWallet) {
    Server server(*h.node, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());
    const std::string a = createWallet(cli);
    const std::string b = createWallet(cli);
    auto history = [&](const std::string& addr, const std::string& query = "", int expect = 200) {
        auto r = cli.Get(("/api/wallets/" + addr + "/history" + query).c_str(), auth);
        EXPECT_TRUE(r);
        if (!r) return json();
        EXPECT_EQ(r->status, expect) << r->body;
        return json::parse(r->body, nullptr, false);
    };

    // Nothing yet: the whole (one-block) chain was scanned.
    json hb = history(b);
    EXPECT_TRUE(hb["activity"].empty());
    EXPECT_TRUE(hb["complete"].get<bool>());
    EXPECT_EQ(hb["address"], b);

    mineTo(server, 0, 110);
    const json sent = post(cli, "/api/transactions/send", {{"from", a}, {"to", b}, {"amount", 100000000}}, 200);
    const std::string txid = sent["transaction"]["hash"];
    const uint64_t fee = sent["transaction"]["fee"];

    // Pending: listed first for both sides, with the fee for the payer.
    json ha = history(a);
    ASSERT_FALSE(ha["activity"].empty());
    json first = ha["activity"][0];
    EXPECT_EQ(first["txid"], txid);
    EXPECT_EQ(first["status"], "pending");
    EXPECT_EQ(first["kind"], "sent");
    EXPECT_EQ(first["amount"], 100000000u);
    EXPECT_EQ(first["to"], b);
    EXPECT_EQ(first["fee"], fee);
    EXPECT_EQ(ha["locked"], 50ULL * 100000000ULL) << "the summary rides along";
    hb = history(b);
    ASSERT_EQ(hb["activity"].size(), 1u);
    EXPECT_EQ(hb["activity"][0]["kind"], "received");
    EXPECT_EQ(hb["activity"][0]["status"], "pending");

    // Confirmed: in block 111 with one confirmation; the rest of A's list is its mining rewards.
    mineTo(server, 1, 1);
    ha = history(a);
    first = ha["activity"][0];
    EXPECT_EQ(first["txid"], txid);
    EXPECT_EQ(first["status"], "confirmed");
    EXPECT_EQ(first["height"], 111u);
    EXPECT_EQ(first["confirmations"], 1u);
    EXPECT_GT(first["time"].get<uint64_t>(), 0u);
    EXPECT_EQ(ha["activity"].size(), 50u) << "default limit";
    EXPECT_EQ(ha["activity"][1]["kind"], "mined");
    EXPECT_EQ(ha["activity"][1]["height"], 110u);
    EXPECT_FALSE(ha["complete"].get<bool>());
    hb = history(b);
    ASSERT_EQ(hb["activity"].size(), 2u);
    EXPECT_EQ(hb["activity"][0]["kind"], "received");
    EXPECT_EQ(hb["activity"][1]["kind"], "mined");
    EXPECT_TRUE(hb["complete"].get<bool>());
    EXPECT_EQ(hb["scannedFrom"], 0u);
    EXPECT_EQ(hb["scannedBlocks"], 112u);

    // The scan is bounded and says so.
    ha = history(a, "?depth=5&limit=500");
    EXPECT_EQ(ha["scannedBlocks"], 5u);
    EXPECT_EQ(ha["scannedFrom"], 107u);
    EXPECT_FALSE(ha["complete"].get<bool>());
    EXPECT_EQ(ha["activity"].size(), 5u) << "the payment and four rewards";

    // A payment to itself.
    post(cli, "/api/transactions/send", {{"from", a}, {"to", a}, {"amount", 100000000}}, 200);
    EXPECT_EQ(history(a)["activity"][0]["kind"], "self");

    history(a, "?limit=0", 400);
    history(a, "?depth=abc", 400);
    history(a, "?limit=501", 400);
    history("tUnknownAddress1111111111111", "", 404);
    auto r = cli.Get(("/api/wallets/" + a + "/history").c_str());
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401);
    server.stop();
}

TEST_F(WalletViewsTest, CreatedWalletIsNotBackedUpUntilThePhraseIsConfirmed) {
    std::string a, b, phraseB, restored;
    {
        Server server(*h.node, nullptr, cfg);
        ASSERT_TRUE(server.start());
        httplib::Client cli("127.0.0.1", server.port());
        auto backedUp = [&](const std::string& addr) {
            auto r = cli.Get("/api/wallets", auth);
            EXPECT_TRUE(r && r->status == 200);
            const json list = json::parse(r->body);
            for (const auto& w : list["wallets"])
                if (w["address"] == addr) return w["backedUp"].get<bool>();
            ADD_FAILURE() << "no wallet " << addr;
            return true;
        };

        a = createWallet(cli);
        const json createdB = post(cli, "/api/wallets/create", json::object(), 200);
        b = createdB["wallet"]["address"];
        phraseB = createdB["mnemonic"];
        EXPECT_FALSE(backedUp(a));
        EXPECT_FALSE(backedUp(b));
        json onDisk = json::parse(readFile(server.walletFile()));
        EXPECT_EQ(onDisk["wallets"][0]["backupConfirmed"], false);

        // A restored wallet was typed in from its phrase: it is backed up.
        std::string otherPhrase;
        Wallet other = Wallet::createNew(&otherPhrase);
        restored = post(cli, "/api/wallets/restore", {{"mnemonic", otherPhrase}}, 200)["wallet"]["address"];
        EXPECT_TRUE(backedUp(restored));

        // The console confirms after its word check.
        const json ok = post(cli, "/api/wallets/backup", {{"address", a}}, 200);
        EXPECT_TRUE(ok["backedUp"].get<bool>());
        EXPECT_TRUE(ok["persisted"].get<bool>());
        EXPECT_TRUE(backedUp(a));
        EXPECT_FALSE(backedUp(b));
        onDisk = json::parse(readFile(server.walletFile()));
        EXPECT_EQ(onDisk["wallets"][0]["backupConfirmed"], true);

        // Proving the phrase: a phrase of another wallet is refused, B's own phrase confirms B.
        json wrong = post(cli, "/api/wallets/backup", {{"address", b}, {"mnemonic", otherPhrase}}, 400);
        EXPECT_EQ(wrong["error"], "That phrase belongs to a different wallet");
        EXPECT_EQ(wrong.dump().find(otherPhrase.substr(0, 6)), std::string::npos) << "the reply never echoes the phrase";
        post(cli, "/api/wallets/backup", {{"address", b}, {"mnemonic", "not a phrase"}}, 400);
        EXPECT_FALSE(backedUp(b));

        post(cli, "/api/wallets/backup", {{"address", "tNoSuchWallet111111111111"}}, 404);
        post(cli, "/api/wallets/backup", {{"wallet", a}}, 400);
        post(cli, "/api/wallets/backup", {{"address", a}, {"mnemonic", 7}}, 400);
        auto r = cli.Post("/api/wallets/backup", json{{"address", a}}.dump(), "application/json");
        ASSERT_TRUE(r);
        EXPECT_EQ(r->status, 401);
        server.stop();
    }
    // The flags survive a restart; restoring B from its phrase also confirms it.
    Server server(*h.node, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());
    auto r = cli.Get("/api/wallets", auth);
    ASSERT_TRUE(r && r->status == 200);
    const json list = json::parse(r->body)["wallets"];
    ASSERT_EQ(list.size(), 3u);
    EXPECT_TRUE(list[0]["backedUp"].get<bool>());
    EXPECT_FALSE(list[1]["backedUp"].get<bool>());
    EXPECT_TRUE(list[2]["backedUp"].get<bool>());
    const json again = post(cli, "/api/wallets/restore", {{"mnemonic", phraseB}}, 200);
    EXPECT_TRUE(again["existing"].get<bool>());
    r = cli.Get("/api/wallets", auth);
    EXPECT_TRUE(json::parse(r->body)["wallets"][1]["backedUp"].get<bool>());
    server.stop();
}

TEST_F(WalletViewsTest, EstimateValidatesLikeSend) {
    Server server(*h.node, nullptr, cfg);
    ASSERT_TRUE(server.start());
    httplib::Client cli("127.0.0.1", server.port());
    const std::string a = createWallet(cli);
    const std::string b = createWallet(cli);

    post(cli, "/api/transactions/estimate", {{"from", a}, {"to", b}, {"amount", 545}}, 400);          // dust
    post(cli, "/api/transactions/estimate", {{"from", a}, {"to", b}, {"amount", -1}}, 400);
    post(cli, "/api/transactions/estimate", {{"from", a}, {"to", b}}, 400);
    post(cli, "/api/transactions/estimate", {{"from", b + "x"}, {"to", a}, {"amount", 1000}}, 404);
    json none = post(cli, "/api/transactions/estimate", {{"from", a}, {"to", b}, {"amount", 1000}}, 400);
    EXPECT_NE(none["error"].get<std::string>().find("No spendable coins"), std::string::npos) << none.dump();
    auto r = cli.Post("/api/transactions/estimate", auth, "not json", "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 400);
    r = cli.Post("/api/transactions/estimate", json{{"from", a}, {"to", b}, {"amount", 1000}}.dump(), "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401) << "wallet views need the token";
    server.stop();
}
