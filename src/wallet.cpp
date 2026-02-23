#include "wallet.h"
#include "crypto.h"
#include "blockchain.h"
#include "utxodb.h"
#include "transaction.h"
#include "utxo.h"
#include "script.h"
#include "bip_8192.h"
#include <openssl/rand.h>

#include <stdexcept>
#include <algorithm>

namespace Bucks {

// ------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------

Wallet::Wallet() {
    generateKeys();
}

Wallet::Wallet(const std::string& privHex, const std::string& pubHex) {
    auto priv = parseHex(privHex);
    auto pub = parseHex(pubHex);
    
    if (priv.size() != 32) throw std::runtime_error("Invalid private key size");
    std::copy(priv.begin(), priv.end(), privKey.begin());
    pubKey = pub;
}

void Wallet::generateKeys() {
    std::vector<uint8_t> entropy(16); // 128 bits
    RAND_bytes(entropy.data(), 16);
    
    // Convert entropy to mnemonic using default wordlist
    mnemonic = bip8192MnemonicFromEntropy(entropy, "wordlists/bip_8192.txt");
    
    // Derive seed from mnemonic
    auto seed = bip8192MnemonicToSeed(mnemonic, "");
    masterKey = HD8192::createMaster(seed);
    deriveAddress("m/0");
}

void Wallet::restoreFromMnemonic(const std::string& m, const std::string& passphrase) {
    auto seed = bip8192MnemonicToSeed(m, passphrase);
    if (seed.size() < 64) throw std::runtime_error("Failed to derive full seed from mnemonic");
    
    masterKey = HD8192::createMaster(seed);
    mnemonic = m;
    deriveAddress("m/0"); // Default initial address
}

// ... (existing accessors)


// ------------------------------------------------------------
// Accessors
// ------------------------------------------------------------

const PrivateKey& Wallet::getPrivateKey() const {
    return privKey;
}

const PublicKey& Wallet::getPublicKey() const {
    return pubKey;
}

std::string Wallet::getPrivateKeyHex() const {
    std::vector<uint8_t> data(privKey.begin(), privKey.end());
    return hexStr(data);
}

std::string Wallet::getPublicKeyHex() const {
    return hexStr(pubKey);
}

// ------------------------------------------------------------
// Address (P2PKH)
// ------------------------------------------------------------

std::string Wallet::getAddress() const {
    Hash160 pubKeyHash = hash160(pubKey);
    std::vector<uint8_t> payload(pubKeyHash.begin(), pubKeyHash.end());
    return base58CheckEncode(0x00, payload); // mainnet prefix
}

// ------------------------------------------------------------
// ScriptPubKey (P2PKH)
// OP_DUP OP_HASH160 <pubKeyHash> OP_EQUALVERIFY OP_CHECKSIG
// ------------------------------------------------------------

std::vector<uint8_t> Wallet::getP2PKHScript() const {
    Hash160 h160 = hash160(pubKey);

    std::vector<uint8_t> script;
    script.push_back(0x76); // OP_DUP
    script.push_back(0xa9); // OP_HASH160
    script.push_back(0x14); // push 20 bytes
    script.insert(script.end(), h160.begin(), h160.end());
    script.push_back(0x88); // OP_EQUALVERIFY
    script.push_back(0xac); // OP_CHECKSIG

    return script;
}

std::vector<uint8_t> Wallet::addressToP2PKHScript(const std::string& address) {
    // Since base58CheckEncode returns hex(version + hash160), parse it back
    std::vector<uint8_t> decoded = parseHex(address);
    if (decoded.size() != 21 || decoded[0] != 0x00) {
        throw std::runtime_error("Invalid address format");
    }
    
    // Extract hash160 (skip version byte)
    Hash160 h160;
    std::copy(decoded.begin() + 1, decoded.end(), h160.begin());

    std::vector<uint8_t> script;
    script.push_back(0x76); // OP_DUP
    script.push_back(0xa9); // OP_HASH160
    script.push_back(0x14); // push 20 bytes
    script.insert(script.end(), h160.begin(), h160.end());
    script.push_back(0x88); // OP_EQUALVERIFY
    script.push_back(0xac); // OP_CHECKSIG

    return script;
}

// ------------------------------------------------------------
// Transaction helpers
// ------------------------------------------------------------

Transaction Wallet::createTransaction(const std::string& to, uint64_t amount, Blockchain& chain) const {
    // Fetch UTXOs for this wallet address
    std::vector<UTXO> utxos = chain.utxo().getUTXOsByAddress(getAddress());

    if (utxos.empty())
        throw std::runtime_error("No UTXOs available for this address");

    uint64_t totalAvailable = 0;
    for (const auto& u : utxos) totalAvailable += u.amount;

    if (totalAvailable < amount)
        throw std::runtime_error("Insufficient balance");

    // Build inputs
    std::vector<TransactionInput> inputs;
    uint64_t accumulated = 0;
    for (const auto& u : utxos) {
        if (accumulated >= amount) break;
        inputs.emplace_back(u.outpoint.txid, u.outpoint.index);
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

    // Sign all inputs
    signTransaction(tx);

    return tx;
}

uint64_t Wallet::getBalance(Blockchain& chain) const {
    uint64_t total = 0;
    auto utxos = chain.utxo().getUTXOsByAddress(getAddress());
    for (const auto& u : utxos) total += u.amount;
    return total;
}

bool Wallet::signTransaction(Transaction& tx) const {
    for (size_t i = 0; i < tx.inputs.size(); ++i) {
        if (!tx.signInput(i, privKey, pubKey))
            return false;
    }
    return true;
}

void Wallet::deriveAddress(const std::string& path) {
    derivedCurrent = HD8192::derivePath(masterKey, path);
    if (derivedCurrent.isPrivate) {
        std::copy(derivedCurrent.key.begin(), derivedCurrent.key.end(), privKey.begin());
        derivePublicKey(privKey, pubKey);
    } else {
        pubKey = derivedCurrent.key;
        // private key stays same or is cleared? 
        // In this implementation, we usually only derive private addresses.
    }
}

} // namespace Bucks
