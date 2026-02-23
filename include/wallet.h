
#ifndef WALLET_H
#define WALLET_H

#include <string>
#include <vector>
#include <cstdint>

#include "crypto.h"
#include "transaction.h"
#include "blockchain.h"
#include "utxo.h"
#include "hd_8192.h"

namespace Bucks {

class Wallet {
public:
    Wallet();
    explicit Wallet(const std::string& privHex, const std::string& pubHex);

    void generateKeys();
    void restoreFromMnemonic(const std::string& mnemonic, const std::string& passphrase = "");

    const PrivateKey& getPrivateKey() const;
    const PublicKey& getPublicKey() const;
    std::string getAddress() const;
    
    std::string getPrivateKeyHex() const;
    std::string getPublicKeyHex() const;

    // Get P2PKH script for this wallet's pubkey
    std::vector<uint8_t> getP2PKHScript() const;

    // Create P2PKH script from address
    static std::vector<uint8_t> addressToP2PKHScript(const std::string& address);

    // Get total balance from UTXOs owned by this wallet
    uint64_t getBalance(Blockchain& chain) const;

    // Build a transaction sending `amount` to `to` using UTXOs from `chain`.
    // Throws on insufficient funds.
    Transaction createTransaction(const std::string& to, uint64_t amount, Blockchain& chain) const;

    // Sign all inputs of a transaction
    bool signTransaction(Transaction& tx) const;

    // HD Support
    void deriveAddress(const std::string& path);
    const ExtendedKey& getMasterKey() const { return masterKey; }
    std::string getMnemonic() const { return mnemonic; }
    void setMnemonic(const std::string& m) { mnemonic = m; }

private:
    PrivateKey privKey;
    PublicKey pubKey;
    std::string address;
    std::string mnemonic; // stored if generated/restored via BIP-8192
    
    ExtendedKey masterKey;
    ExtendedKey derivedCurrent;
};

} // namespace Bucks

#endif // WALLET_H
