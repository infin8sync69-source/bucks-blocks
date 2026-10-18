#include "wallet.h"
#include "crypto.h"
#include "node.h"
#include "policy.h"
#include "transaction.h"
#include "script.h"
#include "consensus.h"
#include "bip_8192.h"
#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <stdexcept>
#include <algorithm>

namespace Bucks {

namespace {

void cleanse(std::vector<uint8_t>& v) {
    if (!v.empty()) OPENSSL_cleanse(v.data(), v.size());
}

} // namespace

// ------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------

Wallet::Wallet() = default;   // empty until a factory or the key constructor fills it

Wallet::Wallet(const std::string& privHex, const std::string& pubHex) {
    auto priv = parseHex(privHex);
    auto pub = parseHex(pubHex);

    if (priv.size() != 32) throw std::runtime_error("Invalid private key size");
    std::copy(priv.begin(), priv.end(), privKey.begin());
    cleanse(priv);
    if (!isValidPrivateKey(privKey)) {
        OPENSSL_cleanse(privKey.data(), privKey.size());
        throw std::runtime_error("Invalid private key");
    }

    // The stored public key must be exactly the one this private key gives;
    // never trust a saved (pub, priv) pair that does not agree.
    PublicKey derived;
    if (!derivePublicKey(privKey, derived) || derived != pub) {
        OPENSSL_cleanse(privKey.data(), privKey.size());
        throw std::runtime_error("Public key does not match private key");
    }
    pubKey = derived;
    initialised = true;
}

Wallet Wallet::createNew(std::string* mnemonicOut, const std::string& lang, size_t entropyBits) {
    if (entropyBits % 8 != 0 || !isSupportedMnemonicEntropySize(entropyBits / 8))
        throw std::invalid_argument("Unsupported mnemonic entropy size");

    // CRY-13 / OPS-17: never continue with zero or stale entropy.
    std::vector<uint8_t> entropy(entropyBits / 8);
    if (RAND_bytes(entropy.data(), static_cast<int>(entropy.size())) != 1) {
        cleanse(entropy);
        throw std::runtime_error("Secure random number generator unavailable");
    }

    MnemonicError err = MnemonicError::OK;
    std::string phrase = mnemonicFromEntropy(entropy, lang, &err);
    if (err != MnemonicError::OK || phrase.empty()) {
        cleanse(entropy);
        throw std::runtime_error(std::string("Cannot create mnemonic: ") + mnemonicErrorString(err));
    }

    // Self-check: the phrase must come back (auto-detected) as the same entropy,
    // otherwise the user could never restore this wallet.
    std::vector<uint8_t> check;
    err = mnemonicToEntropy(phrase, check);
    bool roundTrip = (err == MnemonicError::OK && check == entropy);
    cleanse(check);
    if (!roundTrip) {
        cleanse(entropy);
        throw std::runtime_error(std::string("Mnemonic self-check failed: ") + mnemonicErrorString(err));
    }

    std::vector<uint8_t> seed = seedFromEntropy(entropy, "");
    cleanse(entropy);
    if (seed.size() != MNEMONIC_SEED_BYTES)
        throw std::runtime_error("Seed derivation failed");

    Wallet w;
    w.initFromSeed(seed);
    w.mnemonic = phrase;
    if (mnemonicOut) *mnemonicOut = phrase;
    return w;
}

Wallet Wallet::fromMnemonic(const std::string& phrase, const std::string& passphrase, const std::string& wantLang) {
    // CRY-11 / API-11: words and checksum are validated before anything is
    // derived, and the phrase itself is never part of an error message.
    std::vector<uint8_t> entropy;
    std::string lang;
    MnemonicError err = mnemonicToEntropy(phrase, entropy, &lang, wantLang);
    if (err != MnemonicError::OK)
        throw std::runtime_error(std::string("Invalid mnemonic: ") + mnemonicErrorString(err));

    std::string canonical = mnemonicFromEntropy(entropy, lang, &err);
    if (err != MnemonicError::OK || canonical.empty()) {
        cleanse(entropy);
        throw std::runtime_error(std::string("Invalid mnemonic: ") + mnemonicErrorString(err));
    }

    std::vector<uint8_t> seed = seedFromEntropy(entropy, passphrase);
    cleanse(entropy);
    if (seed.size() != MNEMONIC_SEED_BYTES)
        throw std::runtime_error("Seed derivation failed");

    Wallet w;
    w.initFromSeed(seed);
    w.mnemonic = canonical;
    return w;
}

void Wallet::initFromSeed(std::vector<uint8_t>& seed) {
    masterKey = HD8192::createMaster(seed);
    cleanse(seed);
    if (masterKey.key.empty())
        throw std::runtime_error("Master key derivation failed");
    deriveAddress(defaultReceivePath());
    initialised = true;
}

void Wallet::generateKeys() {
    *this = createNew(nullptr);
}

void Wallet::restoreFromMnemonic(const std::string& m, const std::string& passphrase) {
    *this = fromMnemonic(m, passphrase);
}

void Wallet::requireKeys() const {
    if (!initialised)
        throw std::logic_error("Wallet has no keys (use Wallet::createNew or Wallet::fromMnemonic)");
}

// ------------------------------------------------------------
// Accessors
// ------------------------------------------------------------

const PrivateKey& Wallet::getPrivateKey() const {
    requireKeys();
    return privKey;
}

const PublicKey& Wallet::getPublicKey() const {
    requireKeys();
    return pubKey;
}

std::string Wallet::getPrivateKeyHex() const {
    requireKeys();
    std::vector<uint8_t> data(privKey.begin(), privKey.end());
    std::string hex = hexStr(data);
    cleanse(data);
    return hex;
}

std::string Wallet::getPublicKeyHex() const {
    requireKeys();
    return hexStr(pubKey);
}

// ------------------------------------------------------------
// Address (P2PKH)
// ------------------------------------------------------------

std::string Wallet::getAddress() const {
    requireKeys();
    Hash160 pubKeyHash = hash160(pubKey);
    std::vector<uint8_t> payload(pubKeyHash.begin(), pubKeyHash.end());
    return base58CheckEncode(addressVersion_, payload);
}

// ------------------------------------------------------------
// ScriptPubKey (P2PKH)
// OP_DUP OP_HASH160 <pubKeyHash> OP_EQUALVERIFY OP_CHECKSIG
// ------------------------------------------------------------

std::vector<uint8_t> Wallet::getP2PKHScript() const {
    requireKeys();
    return makeP2PKH(hash160(pubKey));
}

std::vector<uint8_t> Wallet::addressToP2PKHScript(const std::string& address, uint8_t version) {
    // Decode Base58Check: returns (version || hash160) or empty on checksum failure
    std::vector<uint8_t> decoded = base58CheckDecode(address);
    if (decoded.size() != 21 || decoded[0] != version) {
        throw std::runtime_error("Invalid Bucks address (bad checksum or wrong network)");
    }

    // Extract hash160 (bytes 1..20, skip version byte at index 0)
    Hash160 h160;
    std::copy(decoded.begin() + 1, decoded.end(), h160.begin());

    return makeP2PKH(h160);
}

// ------------------------------------------------------------
// Transaction helpers
// ------------------------------------------------------------

Transaction Wallet::createTransaction(const std::string& to, uint64_t amount, Node& node) const {
    requireKeys();
    // Mature coins that no pending transaction already spends.
    std::vector<CoinRow> coins = node.spendableCoins(hash160(pubKey), 20000);
    if (coins.empty())
        throw std::runtime_error("No spendable coins available for this address");

    uint64_t totalAvailable = 0;
    for (const auto& c : coins) totalAvailable += c.coin.amount;
    if (totalAvailable < amount)
        throw std::runtime_error("Insufficient balance");

    // The recipient first, so a wrong-network address fails before anything else.
    const std::vector<uint8_t> recipientScript = addressToP2PKHScript(to, addressVersion_);

    // Largest coins first: fewer inputs, smaller transaction, lower fee.
    std::sort(coins.begin(), coins.end(), [](const CoinRow& a, const CoinRow& b) {
        return a.coin.amount > b.coin.amount;
    });

    // Fee: max(100000, size * 100), estimated for the transaction with a change output.
    auto buildFor = [&](size_t nInputs, uint64_t change, uint64_t& fee) {
        std::vector<TransactionInput> in;
        for (size_t i = 0; i < nInputs; ++i) in.emplace_back(coins[i].op.txid, coins[i].op.index);
        std::vector<TransactionOutput> out;
        out.emplace_back(amount, recipientScript);
        if (change) out.emplace_back(change, getP2PKHScript());
        Transaction t(in, out);
        // Signed inputs add about 107 bytes of unlock script each.
        const uint64_t size = t.serialize().size() + 107 * nInputs;
        fee = std::max<uint64_t>(100000ULL, size * 100ULL);
        return t;
    };

    size_t n = 0;
    uint64_t accumulated = 0, fee = 0;
    for (;;) {
        if (n == coins.size()) throw std::runtime_error("Insufficient funds for fee");
        accumulated += coins[n++].coin.amount;
        buildFor(n, 1, fee);    // upper estimate
        if (accumulated >= amount + fee) break;
    }

    // Change below the dust limit is not worth an output: it goes to the miner.
    uint64_t change = accumulated - amount - fee;
    if (change < Policy::DUST_LIMIT) change = 0;
    Transaction tx = buildFor(n, change, fee);

    std::vector<SpentOutput> spent;
    for (size_t i = 0; i < n; ++i) spent.push_back(SpentOutput{coins[i].coin.amount, coins[i].coin.scriptPubKey});

    // Sign all inputs; never return a transaction with missing signatures
    if (!signTransaction(tx, spent, node.params().genesisHash))
        throw std::runtime_error("Failed to sign transaction");

    return tx;
}

uint64_t Wallet::getBalance(Node& node) const {
    requireKeys();
    return node.balance(hash160(pubKey));
}

bool Wallet::signTransaction(Transaction& tx, const std::vector<SpentOutput>& spent,
                             const Hash256& genesisHash) const {
    if (!initialised) return false;
    if (spent.size() != tx.inputs.size()) return false;
    for (size_t i = 0; i < tx.inputs.size(); ++i) {
        if (!signP2PKHInput(tx, uint32_t(i), spent, privKey, genesisHash))
            return false;
    }
    return true;
}

void Wallet::deriveAddress(const std::string& path) {
    if (masterKey.key.empty())
        throw std::runtime_error("Wallet has no master key (not created from a mnemonic)");
    derivedCurrent = HD8192::derivePath(masterKey, path);
    // Fail closed: never fall back to an empty or invalid key (CRY-12)
    if (derivedCurrent.key.empty())
        throw std::runtime_error("Key derivation failed for path " + path);
    if (derivedCurrent.isPrivate) {
        std::copy(derivedCurrent.key.begin(), derivedCurrent.key.end(), privKey.begin());
        if (!derivePublicKey(privKey, pubKey))
            throw std::runtime_error("Invalid derived private key for path " + path);
    } else {
        pubKey = derivedCurrent.key;
        // private key stays same or is cleared?
        // In this implementation, we usually only derive private addresses.
    }
}

} // namespace Bucks
