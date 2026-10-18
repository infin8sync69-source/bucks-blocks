#include <gtest/gtest.h>
#include "wallet.h"
#include "bip_8192.h"
#include "hd_8192.h"
#include "crypto.h"
#include "mnemonic_test_util.h"

#include <stdexcept>
#include <string>

using namespace Bucks;
using namespace bucks_test;

class WalletTest : public MnemonicTest {};

// (d) Fresh wallets differ (CRY-1 / OPS-1 / API-V1: every wallet used to get
//     the same key because the empty mnemonic was silently accepted).
TEST_F(WalletTest, CreateNewWalletsDiffer) {
    std::string m1, m2;
    Wallet a = Wallet::createNew(&m1);
    Wallet b = Wallet::createNew(&m2);
    ASSERT_TRUE(a.hasKeys());
    ASSERT_TRUE(b.hasKeys());
    EXPECT_NE(a.getAddress(), b.getAddress());
    EXPECT_NE(a.getPrivateKey(), b.getPrivateKey());
    EXPECT_NE(m1, m2);
    EXPECT_EQ(splitMnemonic(m1).size(), 13u);
    EXPECT_EQ(a.getMnemonic(), m1);
    EXPECT_EQ(validateMnemonic(m1), MnemonicError::OK);
    EXPECT_TRUE(isValidPrivateKey(a.getPrivateKey()));
    EXPECT_TRUE(isValidPublicKey(a.getPublicKey()));
}

// (e) No usable word list -> createNew throws instead of handing out a wallet.
TEST_F(WalletTest, CreateNewThrowsWhenWordlistsMissing) {
    TempDir empty;
    setWordlistEnv(empty.str());
    std::string m = "untouched";
    EXPECT_THROW(Wallet::createNew(&m), std::runtime_error);
    EXPECT_EQ(m, "untouched");
    Wallet legacy;
    EXPECT_THROW(legacy.generateKeys(), std::runtime_error);
    EXPECT_FALSE(legacy.hasKeys());
    EXPECT_THROW(Wallet::fromMnemonic(kat128().phrase), std::runtime_error);

    clearWordlistEnv();
    EXPECT_NO_THROW(Wallet::createNew(&m));
    EXPECT_NE(m, "untouched");
}

TEST_F(WalletTest, CreateNew256BitsGives25Words) {
    std::string m;
    Wallet w = Wallet::createNew(&m, "", 256);
    EXPECT_EQ(splitMnemonic(m).size(), 25u);
    Wallet back = Wallet::fromMnemonic(m);
    EXPECT_EQ(back.getAddress(), w.getAddress());
    EXPECT_THROW(Wallet::createNew(&m, "", 192), std::invalid_argument);
    EXPECT_THROW(Wallet::createNew(&m, "", 100), std::invalid_argument);
    EXPECT_THROW(Wallet::createNew(&m, "no-such-lang"), std::runtime_error);
}

TEST_F(WalletTest, RoundTripThroughMnemonicAndPassphrase) {
    std::string m;
    Wallet w = Wallet::createNew(&m);
    Wallet same = Wallet::fromMnemonic(m);
    EXPECT_EQ(same.getAddress(), w.getAddress());
    EXPECT_EQ(same.getPrivateKeyHex(), w.getPrivateKeyHex());
    EXPECT_EQ(same.getMnemonic(), m);
    Wallet other = Wallet::fromMnemonic(m, "passphrase");
    EXPECT_NE(other.getAddress(), w.getAddress());
    // Legacy spelling
    Wallet legacy;
    legacy.restoreFromMnemonic(m);
    EXPECT_EQ(legacy.getAddress(), w.getAddress());
}

// Phrase -> address known-answer test (seed, BIP-32 path and Base58Check all
// computed independently in Python).
TEST_F(WalletTest, KnownAnswerAddress) {
    EXPECT_EQ(defaultReceivePath(), "m/44'/1'/0'/0/0");
    Wallet w = Wallet::fromMnemonic(kat128().phrase);
    EXPECT_EQ(w.getPrivateKeyHex(), "676af76915a054759e7036b909709aaa99e44b7c3fe6f510f982d17172c26b49");
    EXPECT_EQ(w.getPublicKeyHex(), "030ed08e028debe59e06f6de0436e1c3e74c7e26e6a535308b62d8a23561b24d76");
    // The original known answers use Base58Check version 0; the network versions differ only in that byte.
    w.setAddressVersion(0x00);
    EXPECT_EQ(w.getAddress(), "1BACHKk8wwxSUpd3ydEQS1tqchCQC62DuN");
    Wallet p = Wallet::fromMnemonic(kat128().phrase, "TREZOR");
    EXPECT_EQ(p.getPrivateKeyHex(), "d0c2407b2616107d727b05c81614364d6cdd3130fb5a686a63e77f1eacb5ff5c");
    p.setAddressVersion(0x00);
    EXPECT_EQ(p.getAddress(), "187oNyHNSkXHT7yPX7b8Ld4uHeZsPFLZyA");

    // The same key on regtest (0x7F), test (0x41) and main (0x1A): independently computed, and every
    // network refuses the others' addresses.
    w.setAddressVersion(0x7F);
    EXPECT_EQ(w.getAddress(), "tH5oL7bj7roiLsL45uXt1wTkYkvCPq65n5");
    w.setAddressVersion(0x41);
    EXPECT_EQ(w.getAddress(), "TL8QHP7s6g5PdzggZtt7xAayXUwiKZ5pzi");
    w.setAddressVersion(0x1A);
    EXPECT_EQ(w.getAddress(), "Bdwst9VdQe1Dk6FJcXth3GyHyotvh8j5gh");
    const std::string mainAddress = w.getAddress();
    EXPECT_NO_THROW(Wallet::addressToP2PKHScript(mainAddress, 0x1A));
    EXPECT_THROW(Wallet::addressToP2PKHScript(mainAddress, 0x41), std::runtime_error);
    EXPECT_THROW(Wallet::addressToP2PKHScript(mainAddress, 0x00), std::runtime_error);
}

// CRY-11 / API-11: restore validates words and checksum, and never echoes
// the phrase.
TEST_F(WalletTest, FromMnemonicRejectsInvalidPhrases) {
    const char* garbage = "correct horse battery staple correct horse battery staple correct horse battery staple x";
    try {
        Wallet::fromMnemonic(garbage);
        FAIL() << "garbage accepted";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_EQ(msg.find("horse"), std::string::npos) << msg;
        EXPECT_NE(msg.find("Invalid mnemonic"), std::string::npos) << msg;
    }
    EXPECT_THROW(Wallet::fromMnemonic(""), std::runtime_error);
    EXPECT_THROW(Wallet::fromMnemonic("my memorable sentence"), std::runtime_error);

    // 12 words (BIP-39 length) and a flipped checksum word are refused.
    auto words = splitMnemonic(kat128().phrase);
    std::string twelve;
    for (size_t i = 0; i < 12; ++i) twelve += (i ? " " : "") + words[i];
    EXPECT_THROW(Wallet::fromMnemonic(twelve), std::runtime_error);
    auto list = findWordlist("ar-dev");
    ASSERT_TRUE(list);
    words.back() = list->entries[kat128().indices.back() ^ 1];
    std::string flipped;
    for (const auto& w : words) flipped += (flipped.empty() ? "" : " ") + w;
    try {
        Wallet::fromMnemonic(flipped);
        FAIL() << "bad checksum accepted";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("checksum"), std::string::npos) << e.what();
        EXPECT_EQ(std::string(e.what()).find(words[0]), std::string::npos) << e.what();
    }
}

TEST_F(WalletTest, FromMnemonicFoldsTypedVariantsAndStoresCanonical) {
    std::string typed = kat128().phrase;
    typed.replace(0, 2, "\xD8\xA3");                       // أ for ا
    for (size_t p = typed.find("\xD9\x8A"); p != std::string::npos; p = typed.find("\xD9\x8A", p + 2))
        typed.replace(p, 2, "\xDB\x8C");                   // Persian yeh
    Wallet w = Wallet::fromMnemonic("  " + typed + "\n");
    w.setAddressVersion(0x00);
    EXPECT_EQ(w.getAddress(), "1BACHKk8wwxSUpd3ydEQS1tqchCQC62DuN");
    EXPECT_EQ(w.getMnemonic(), kat128().phrase);           // canonical spelling is stored
}

// (f) The same wallet from either language's phrase.
TEST_F(WalletTest, IdenticalKeysFromBothLanguages) {
    TempDir dir;
    writeTwoLanguageDir(dir);
    setWordlistDir(dir.str());
    Wallet ar = Wallet::fromMnemonic(kat128().phrase);
    Wallet xx = Wallet::fromMnemonic(latinPhrase(kat128().indices));
    ar.setAddressVersion(0x00);
    xx.setAddressVersion(0x00);
    EXPECT_EQ(ar.getAddress(), "1BACHKk8wwxSUpd3ydEQS1tqchCQC62DuN");
    EXPECT_EQ(xx.getAddress(), ar.getAddress());
    EXPECT_EQ(xx.getPrivateKeyHex(), ar.getPrivateKeyHex());
    EXPECT_EQ(xx.getMnemonic(), latinPhrase(kat128().indices));
    EXPECT_EQ(ar.getMnemonic(), kat128().phrase);
    EXPECT_EQ(convertMnemonic(xx.getMnemonic(), "ar-dev"), ar.getMnemonic());

    std::string m;
    Wallet latinNew = Wallet::createNew(&m, "xx-test");
    EXPECT_EQ(m.substr(0, 1), "w");
    EXPECT_EQ(Wallet::fromMnemonic(convertMnemonic(m, "ar-dev")).getAddress(), latinNew.getAddress());

    // A named list decodes with that list only (restore's optional "language", gate WORDLIST-2).
    EXPECT_EQ(Wallet::fromMnemonic(kat128().phrase, "", "ar-dev").getPrivateKeyHex(), ar.getPrivateKeyHex());
    EXPECT_THROW(Wallet::fromMnemonic(kat128().phrase, "", "xx-test"), std::runtime_error);
    EXPECT_THROW(Wallet::fromMnemonic(kat128().phrase, "", "no-such-list"), std::runtime_error);
}

TEST_F(WalletTest, EmptyWalletFailsClosed) {
    Wallet w;
    EXPECT_FALSE(w.hasKeys());
    EXPECT_THROW(w.getAddress(), std::logic_error);
    EXPECT_THROW(w.getPrivateKey(), std::logic_error);
    EXPECT_THROW(w.getPublicKey(), std::logic_error);
    EXPECT_THROW(w.getPrivateKeyHex(), std::logic_error);
    EXPECT_THROW(w.getPublicKeyHex(), std::logic_error);
    EXPECT_THROW(w.getP2PKHScript(), std::logic_error);
    EXPECT_THROW(w.deriveAddress("m/0"), std::runtime_error);
    Transaction tx;
    EXPECT_FALSE(w.signTransaction(tx, {}, Hash256{}));
}

TEST_F(WalletTest, KeyConstructorValidatesPair) {
    std::string m;
    Wallet w = Wallet::createNew(&m);
    Wallet loaded(w.getPrivateKeyHex(), w.getPublicKeyHex());
    EXPECT_TRUE(loaded.hasKeys());
    EXPECT_EQ(loaded.getAddress(), w.getAddress());
    EXPECT_TRUE(loaded.getMnemonic().empty());
    // A loaded wallet has no master key, so HD derivation is refused.
    EXPECT_THROW(loaded.deriveAddress("m/0"), std::runtime_error);

    Wallet other = Wallet::createNew(&m);
    EXPECT_THROW(Wallet(w.getPrivateKeyHex(), other.getPublicKeyHex()), std::runtime_error);
    EXPECT_THROW(Wallet(std::string(64, '0'), w.getPublicKeyHex()), std::runtime_error);
    EXPECT_THROW(Wallet("abcd", w.getPublicKeyHex()), std::runtime_error);
    EXPECT_THROW(Wallet(w.getPrivateKeyHex(), ""), std::runtime_error);
}

TEST_F(WalletTest, DeriveAddressUsesStrictPaths) {
    std::string m;
    Wallet w = Wallet::createNew(&m);
    std::string addr = w.getAddress();
    EXPECT_THROW(w.deriveAddress("m/"), std::runtime_error);
    EXPECT_THROW(w.deriveAddress("m/0x"), std::runtime_error);
    EXPECT_THROW(w.deriveAddress("m/2147483648"), std::runtime_error);
    EXPECT_NO_THROW(w.deriveAddress("m/44'/1'/0'/0/1"));
    EXPECT_NE(w.getAddress(), addr);
    EXPECT_NO_THROW(w.deriveAddress(defaultReceivePath()));
    EXPECT_EQ(w.getAddress(), addr);
}
