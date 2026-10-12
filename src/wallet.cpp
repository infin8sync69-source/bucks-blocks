#include "wallet.h"
#include "crypto.h"
#include "blockchain.h"
#include "utxodb.h"
#include "transaction.h"
#include "utxo.h"
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

Wallet Wallet::fromMnemonic(const std::string& phrase, const std::string& passphrase) {
    // CRY-11 / API-11: words and checksum are validated before anything is
    // derived, and the phrase itself is never part of an error message.
    std::vector<uint8_t> entropy;
    std::string lang;
    MnemonicError err = mnemonicToEntropy(phrase, entropy, &lang);
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
    return base58CheckEncode(0x00, payload); // mainnet prefix
}

// ------------------------------------------------------------
// ScriptPubKey (P2PKH)
// OP_DUP OP_HASH160 <pubKeyHash> OP_EQUALVERIFY OP_CHECKSIG
// ------------------------------------------------------------

std::vector<uint8_t> Wallet::getP2PKHScript() const {
    requireKeys();
    return makeP2PKH(hash160(pubKey));
}

std::vector<uint8_t> Wallet::addressToP2PKHScript(const std::string& address) {
    // Decode Base58Check: returns (version || hash160) or empty on checksum failure
    std::vector<uint8_t> decoded = base58CheckDecode(address);
    if (decoded.size() != 21 || decoded[0] != 0x00) {
        // decoded[0] == 0x00: Bucks mainnet P2PKH version byte
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

Transaction Wallet::createTransaction(const std::string& to, uint64_t amount, Blockchain& chain) const {
    requireKeys();
    // Fetch UTXOs for this wallet address
    std::vector<UTXO> utxos = chain.utxo().getUTXOsByAddress(getAddress());

    // Skip coins that cannot be spent in the next block: immature coinbase
    // outputs and outputs already spent by a pending transaction.
    {
        const uint64_t spendHeight = uint64_t(chain.height()) + 1;
        std::vector<OutPoint> pending;
        for (const auto& mtx : chain.getMempool())
            for (const auto& in : mtx.inputs) pending.push_back({in.prevTxid, in.prevIndex});
        utxos.erase(std::remove_if(utxos.begin(), utxos.end(), [&](const UTXO& u) {
            if (u.isCoinbase && spendHeight < uint64_t(u.height) + Consensus::COINBASE_MATURITY)
                return true;
            return std::find(pending.begin(), pending.end(), u.outpoint) != pending.end();
        }), utxos.end());
    }

    if (utxos.empty())
        throw std::runtime_error("No UTXOs available for this address");

    uint64_t totalAvailable = 0;
    for (const auto& u : utxos) totalAvailable += u.amount;

    if (totalAvailable < amount)
        throw std::runtime_error("Insufficient balance");

    // Build inputs
    std::vector<TransactionInput> inputs;
    std::vector<SpentOutput> spent;
    uint64_t accumulated = 0;
    for (const auto& u : utxos) {
        if (accumulated >= amount) break;
        inputs.emplace_back(u.outpoint.txid, u.outpoint.index);
        spent.push_back(SpentOutput{u.amount, u.scriptPubKey});
        accumulated += u.amount;
    }

    // Build outputs: recipient + change if any
    std::vector<TransactionOutput> outputs;
    // Create proper P2PKH script for recipient
    std::vector<uint8_t> recipientScript = addressToP2PKHScript(to);
    outputs.emplace_back(amount, recipientScript);

    if (accumulated > amount) {
        uint64_t change = accumulated - amount;
        std::vector<uint8_t> changeScript = getP2PKHScript();
        outputs.emplace_back(change, changeScript);
    }

    Transaction tx(inputs, outputs);

    // Simple fee: max(100000, size * 100)
    uint64_t fee = std::max<uint64_t>(100000ULL, tx.serialize().size() * 100ULL);
    // If accumulated covers amount + fee adjust change
    if (accumulated < amount + fee) {
        throw std::runtime_error("Insufficient funds for fee");
    }

    if (outputs.size() > 1) {
        uint64_t newChange = accumulated - amount - fee;
        if (newChange == 0) outputs.pop_back();
        else outputs.back() = TransactionOutput(newChange, getP2PKHScript());
        tx = Transaction(inputs, outputs);
    }

    // Sign all inputs; never return a transaction with missing signatures
    if (!signTransaction(tx, spent, chain.genesisHash()))
        throw std::runtime_error("Failed to sign transaction");

    return tx;
}

uint64_t Wallet::getBalance(Blockchain& chain) const {
    requireKeys();
    uint64_t total = 0;
    auto utxos = chain.utxo().getUTXOsByAddress(getAddress());
    for (const auto& u : utxos) total += u.amount;
    return total;
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
