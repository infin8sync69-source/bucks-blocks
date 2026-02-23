#include "bip_8192.h"

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <iterator>

// Crypto primitives already in your codebase
#include "crypto.h"

namespace Bucks {

// ============================================================
// CONSTANTS (CONSENSUS-CRITICAL)
// ============================================================

static constexpr size_t WORD_BITS = 13;              // log2(8192)
static constexpr size_t PBKDF2_ROUNDS = 2048;
static constexpr char SALT_PREFIX[] = "bip-8192 mnemonic";

// ============================================================
// INTERNAL TYPES
// ============================================================

using BitVec = std::vector<uint8_t>; // each element is 0 or 1

// ============================================================
// HELPERS
// ============================================================

static bool loadWordlist(
    const std::string& path,
    std::vector<std::string>& words,
    std::unordered_map<std::string, size_t>& indexMap
) {
    std::ifstream in(path);
    if (!in) return false;

    std::unordered_set<std::string> seen;
    std::string line;

    while (std::getline(in, line)) {
        line = unicode_nfkd(line);
        if (line.empty()) continue;
        if (line.find(' ') != std::string::npos) return false;
        if (seen.count(line)) return false;

        seen.insert(line);
        indexMap[line] = words.size();
        words.push_back(line);
    }

    return words.size() == 8192;
}

static BitVec bytesToBits(const std::vector<uint8_t>& bytes) {
    BitVec bits;
    bits.reserve(bytes.size() * 8);

    for (uint8_t b : bytes) {
        for (int i = 7; i >= 0; --i)
            bits.push_back((b >> i) & 1);
    }
    return bits;
}

static std::vector<uint8_t> bitsToBytes(const BitVec& bits) {
    std::vector<uint8_t> out((bits.size() + 7) / 8, 0);
    for (size_t i = 0; i < bits.size(); ++i)
        out[i / 8] |= bits[i] << (7 - (i % 8));
    return out;
}

// ============================================================
// MNEMONIC GENERATION
// ============================================================

std::string bip8192MnemonicFromEntropy(
    const std::vector<uint8_t>& entropy,
    const std::string& wordlistPath,
    Bip8192Error* err
) {
    if (err) *err = Bip8192Error::OK;

    if (entropy.size() < 16 ||
        entropy.size() > 32 ||
        (entropy.size() % 4) != 0) {
        if (err) *err = Bip8192Error::INVALID_ENTROPY_SIZE;
        return {};
    }

    std::vector<std::string> wordlist;
    std::unordered_map<std::string, size_t> indexMap;
    if (!loadWordlist(wordlistPath, wordlist, indexMap)) {
        if (err) *err = Bip8192Error::WORDLIST_LOAD_FAILED;
        return {};
    }

    // Compute checksum
    Hash256 hash = sha256(entropy);
    std::vector<uint8_t> hashVec(hash.begin(), hash.end());
    size_t checksumBits = (entropy.size() * 8) / 32;

    BitVec bits = bytesToBits(entropy);
    BitVec hashBits = bytesToBits(hashVec);

    bits.insert(bits.end(), hashBits.begin(), hashBits.begin() + checksumBits);

    // Convert to words
    std::ostringstream out;
    size_t wordCount = bits.size() / WORD_BITS;

    for (size_t i = 0; i < wordCount; ++i) {
        size_t idx = 0;
        for (size_t b = 0; b < WORD_BITS; ++b)
            idx = (idx << 1) | bits[i * WORD_BITS + b];

        out << wordlist[idx];
        if (i + 1 < wordCount) out << ' ';
    }

    return out.str();
}

// ============================================================
// MNEMONIC VALIDATION
// ============================================================

Bip8192Error bip8192ValidateMnemonic(
    const std::string& mnemonic,
    const std::string& wordlistPath
) {
    std::vector<std::string> wordlist;
    std::unordered_map<std::string, size_t> indexMap;

    if (!loadWordlist(wordlistPath, wordlist, indexMap))
        return Bip8192Error::WORDLIST_LOAD_FAILED;

    std::istringstream iss(unicode_nfkd(mnemonic));
    std::vector<std::string> words{
        std::istream_iterator<std::string>{iss},
        std::istream_iterator<std::string>{}
    };

    if (words.empty())
        return Bip8192Error::INVALID_WORD_COUNT;

    BitVec bits;
    for (const auto& w : words) {
        auto it = indexMap.find(w);
        if (it == indexMap.end())
            return Bip8192Error::INVALID_WORD;

        size_t idx = it->second;
        for (int i = WORD_BITS - 1; i >= 0; --i)
            bits.push_back((idx >> i) & 1);
    }

    size_t totalBits = bits.size();
    size_t entropyBits = totalBits * 32 / 33;
    size_t checksumBits = entropyBits / 32;

    if (entropyBits % 8 != 0)
        return Bip8192Error::INVALID_WORD_COUNT;

    auto entropyBytes = bitsToBytes(
        BitVec(bits.begin(), bits.begin() + entropyBits)
    );

    auto hash = sha256(entropyBytes);
    std::vector<uint8_t> hashVec(hash.begin(), hash.end());
    auto hashBits = bytesToBits(hashVec);

    for (size_t i = 0; i < checksumBits; ++i) {
        if (bits[entropyBits + i] != hashBits[i])
            return Bip8192Error::CHECKSUM_MISMATCH;
    }

    return Bip8192Error::OK;
}

// ============================================================
// SEED DERIVATION
// ============================================================

std::vector<uint8_t> bip8192MnemonicToSeed(
    const std::string& mnemonic,
    const std::string& passphrase
) {
    std::string normMnemonic = unicode_nfkd(mnemonic);
    std::string normPass = unicode_nfkd(passphrase);

    std::string salt = std::string(SALT_PREFIX) + normPass;

    std::vector<uint8_t> seed(64);
    pbkdf2_hmac_sha512(
        normMnemonic.data(),
        normMnemonic.size(),
        salt.data(),
        salt.size(),
        PBKDF2_ROUNDS,
        seed.data(),
        seed.size()
    );

    return seed;
}

} // namespace Bucks
