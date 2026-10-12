#ifndef WALLET_H
#define WALLET_H

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

#include "crypto.h"
#include "transaction.h"
#include "sighash.h"
#include "chainparams.h"
#include "utxo.h"
#include "hd_8192.h"

namespace Bucks {

class Node;

class Wallet {
public:
    // Entropy for createNew(): 128 bits -> 13-word phrase (256 -> 25 words).
    static constexpr size_t DEFAULT_ENTROPY_BITS = 128;

    // An empty wallet: hasKeys() is false and every key accessor throws until
    // it is assigned from createNew(), fromMnemonic() or the key constructor.
    Wallet();

    // Load a saved wallet. Throws unless privHex is a valid secp256k1 key and
    // pubHex is exactly its compressed public key.
    explicit Wallet(const std::string& privHex, const std::string& pubHex);

    /**
     * New wallet from fresh OS entropy. The phrase is written to *mnemonicOut
     * (if given) in `lang` ("" = default list). Throws, never returning a
     * wallet, if the RNG fails, the word lists cannot be loaded, or the phrase
     * does not decode back to the same entropy.
     */
    static Wallet createNew(std::string* mnemonicOut,
                            const std::string& lang = "",
                            size_t entropyBits = DEFAULT_ENTROPY_BITS);

    /**
     * Restore from a phrase (language auto-detected) and optional passphrase.
     * Words and checksum are validated first; the error message never
     * contains the phrase. The stored mnemonic is the canonical spelling.
     */
    static Wallet fromMnemonic(const std::string& phrase, const std::string& passphrase = "");

    bool hasKeys() const { return initialised; }

    // Legacy spellings of the two factories (kept for existing callers):
    // *this = createNew(&mnemonic) and *this = fromMnemonic(...). Both throw
    // on failure and leave the wallet unchanged.
    void generateKeys();
    void restoreFromMnemonic(const std::string& mnemonic, const std::string& passphrase = "");

    const PrivateKey& getPrivateKey() const;
    const PublicKey& getPublicKey() const;
    std::string getAddress() const;

    // Base58Check version byte of this wallet's addresses: the network's
    // ChainParams::p2pkhAddressVersion. The default is regtest's; whoever
    // creates or loads a wallet for a node sets the node's value.
    void setAddressVersion(uint8_t version) { addressVersion_ = version; }
    uint8_t addressVersion() const { return addressVersion_; }

    std::string getPrivateKeyHex() const;
    std::string getPublicKeyHex() const;

    // Get P2PKH script for this wallet's pubkey
    std::vector<uint8_t> getP2PKHScript() const;

    // Create P2PKH script from an address of the network with this version byte.
    // Throws on a bad checksum or an address of another network.
    static std::vector<uint8_t> addressToP2PKHScript(const std::string& address, uint8_t version);

    // Get total balance from UTXOs owned by this wallet
    uint64_t getBalance(Node& node) const;

    // Build a transaction sending `amount` to `to` using mature, unspent coins
    // of this wallet from `node`. Throws on insufficient funds.
    Transaction createTransaction(const std::string& to, uint64_t amount, Node& node) const;

    // Sign all inputs of a transaction. `spent[i]` is the output spent by
    // input i; the signature commits to all of them and to the genesis hash
    // (sighash v1). Fails if this wallet does not own an input.
    bool signTransaction(Transaction& tx, const std::vector<SpentOutput>& spent,
                         const Hash256& genesisHash) const;

    // HD Support (only for wallets that came from a mnemonic)
    void deriveAddress(const std::string& path);
    const ExtendedKey& getMasterKey() const { return masterKey; }
    std::string getMnemonic() const { return mnemonic; }
    void setMnemonic(const std::string& m) { mnemonic = m; }

private:
    void requireKeys() const;
    // Seed -> master key -> default receive address. Cleanses `seed`.
    void initFromSeed(std::vector<uint8_t>& seed);

    bool initialised = false;
    PrivateKey privKey{};
    PublicKey pubKey;
    std::string address;
    uint8_t addressVersion_ = chainParams(NetworkId::REGTEST).p2pkhAddressVersion;
    std::string mnemonic; // canonical phrase if generated/restored from one

    ExtendedKey masterKey;
    ExtendedKey derivedCurrent;
};

} // namespace Bucks

#endif // WALLET_H
