#ifndef BUCKS_MNEMONIC_TEST_UTIL_H
#define BUCKS_MNEMONIC_TEST_UTIL_H

// Shared helpers for the mnemonic / wallet unit tests: temp wordlist
// directories (never inside the repo or CWD), environment handling and a
// synthetic index-aligned second language.

#include <gtest/gtest.h>
#include "bip_8192.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define BUCKS_TEST_PID _getpid()
#else
#include <unistd.h>
#define BUCKS_TEST_PID getpid()
#endif

namespace bucks_test {

namespace fs = std::filesystem;

inline void setWordlistEnv(const std::string& value) {
#ifdef _WIN32
    _putenv_s("BUCKS_WORDLIST_DIR", value.c_str());
#else
    setenv("BUCKS_WORDLIST_DIR", value.c_str(), 1);
#endif
}

inline void clearWordlistEnv() {
#ifdef _WIN32
    _putenv_s("BUCKS_WORDLIST_DIR", "");
#else
    unsetenv("BUCKS_WORDLIST_DIR");
#endif
}

// The repo's wordlists directory, as the library resolves it with no override.
inline std::string repoWordlistDir() {
    Bucks::setWordlistDir("");
    clearWordlistEnv();
    std::string err;
    std::string dir = Bucks::resolveWordlistDir(&err);
    EXPECT_FALSE(dir.empty()) << err;
    return dir;
}

// Creates a fresh, empty temp directory and removes it on destruction.
class TempDir {
public:
    TempDir() {
        static int counter = 0;
        path_ = fs::temp_directory_path() /
                ("bucks-mnemonic-test-" + std::to_string(BUCKS_TEST_PID) + "-" + std::to_string(counter++));
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const fs::path& path() const { return path_; }
    std::string str() const { return path_.string(); }

    void write(const std::string& name, const std::string& content) const {
        std::ofstream out(path_ / name, std::ios::binary);
        out << content;
    }

private:
    fs::path path_;
};

// Resets every wordlist override before and after each test.
class MnemonicTest : public ::testing::Test {
protected:
    void SetUp() override { Bucks::setWordlistDir(""); clearWordlistEnv(); }
    void TearDown() override { Bucks::setWordlistDir(""); clearWordlistEnv(); }
};

// "w0000".."w2047": a synthetic index-aligned list under the "latin" normalizer.
inline std::string syntheticLatinList() {
    std::string s = "# synthetic test list\n";
    for (int i = 0; i < 2048; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "w%04d\n", i);
        s += buf;
    }
    return s;
}

// Manifest with the dev Arabic list (copied from the repo) first and the
// synthetic latin list second, written into `dir`.
inline void writeTwoLanguageDir(const TempDir& dir) {
    std::string repo = repoWordlistDir();
    fs::copy_file(fs::path(repo) / "ar-dev-2048.txt", dir.path() / "ar-dev-2048.txt",
                  fs::copy_options::overwrite_existing);
    dir.write("xx-test-2048.txt", syntheticLatinList());
    dir.write("manifest.json",
        "{\"version\":2,\"lists\":["
        "{\"lang\":\"ar-dev\",\"file\":\"ar-dev-2048.txt\",\"normalizer\":\"arabic\",\"status\":\"dev\"},"
        "{\"lang\":\"xx-test\",\"file\":\"xx-test-2048.txt\",\"normalizer\":\"latin\",\"status\":\"test\"}"
        "]}");
}

inline std::vector<uint8_t> fromHex(const std::string& hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return out;
}

// Known-answer test vectors computed independently in Python
// (hashlib / hmac / unicodedata; see the task notes). Entropy values are the
// BIP-39 test vectors' entropy, re-encoded under the Bucks v2 scheme.
struct Kat {
    const char* entropyHex;
    std::vector<uint16_t> indices;
    const char* phrase;            // in the ar-dev list
    const char* seedNoPass;
    const char* seedTrezor;        // passphrase "TREZOR"
};

inline const Kat& kat128() {
    static const Kat k{
        "0c1e24e5917779d297e14d45f14e1a1a",
        {96, 1929, 459, 279, 956, 1866, 764, 333, 559, 1107, 1076, 423, 882},
        "ابويه تمسوها اعجاز اذيهم بعلها تقر ابلغكم ازكي اكذبتم بجنوده ببدر اضلوا انيه",
        "06c829c78f780373bb9aad653573d4710da3d104411ecb0912650acd3b4c3f06"
        "b83b9260c0ab5143f52e0b2706e14f47336ecb961623fb2dad99664a3a91cdc8",
        "60e9d9a3f87ef3e1121927e40fd76665bc3c6571eeccda4f13a5da49e313ddc2"
        "4fb5265a79c26b25b82b66aae386b8435b65a46cd260158c687c40b9213bd632"
    };
    return k;
}

inline const Kat& kat256() {
    static const Kat k{
        "f585c11aec520db57dd353c69554b21a89b20fb0650966fa0a9d6f74fd989d8f",
        {1964, 368, 565, 1733, 262, 1749, 1978, 851, 1588, 1365, 356, 424, 1241,
         62, 1548, 1289, 823, 1666, 1338, 1783, 638, 1634, 945, 1897, 1458},
        "تنطقون اسمعهم اكبرنه تسميه ادلكم تشربون تنكح ارم تحنث بالازلم اسفارنا اضلونا "
        "بفكهه ءاسجد تجري بمعجز اوتيته ترتدوا بالروح تطيرنا انسينيه تدعوننا بعثنكم تكونا تامنا",
        "2c4d2ed614e2117f2c11d1e8ceafacada0fc0195407234b26ba4e91678cae4e5"
        "a57647e3fd8a14ba85ecf46f0ce6e1901db461fd5975cc715fc497625b5417d5",
        "a9ce184bffc937164e95f092b10d009601d81fe62d2c1c9b43c2715d67a704bd"
        "113a2ddab0c68841da2c4d8169215dda413b1efa0d8c8f70357a01a5df95c0a2"
    };
    return k;
}

// The same entropy phrased in the synthetic latin list.
inline std::string latinPhrase(const std::vector<uint16_t>& indices) {
    std::string s;
    for (uint16_t i : indices) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%sw%04u", s.empty() ? "" : " ", unsigned(i));
        s += buf;
    }
    return s;
}

} // namespace bucks_test

#endif // BUCKS_MNEMONIC_TEST_UTIL_H
