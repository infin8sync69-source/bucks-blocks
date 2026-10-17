#ifndef BIP_8192_H
#define BIP_8192_H

// Bucks Mnemonic v2 — see the specification block at the top of
// src/bip_8192.cpp. (The file keeps its historical name; the 8192-word
// scheme it was named after is gone.)

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include <cstddef>

namespace Bucks {

// ============================================================
// Errors
// ============================================================

enum class MnemonicError {
    OK = 0,
    INVALID_ENTROPY_SIZE,      // only 16 or 32 bytes are supported
    WORDLIST_LOAD_FAILED,      // manifest/list missing or invalid (fail closed)
    INVALID_WORD_COUNT,        // not 13 or 25 words
    INVALID_WORD,              // a word is in no loaded list (typo or unknown language)
    CHECKSUM_MISMATCH,         // words resolve but the checksum does not
    UNKNOWN_LANGUAGE,          // requested language tag is not in the manifest
    AMBIGUOUS_LANGUAGE,        // phrase is valid under more than one list
    SEED_DERIVATION_FAILED     // PBKDF2 failed
};

// Legacy name kept for existing callers.
using Bip8192Error = MnemonicError;

// Short, stable description (safe to show to users; never contains the phrase).
const char* mnemonicErrorString(MnemonicError err);

// ============================================================
// Scheme constants (derived, not scattered)
// ============================================================

constexpr size_t MNEMONIC_WORDLIST_SIZE = 2048;

constexpr size_t mnemonicLog2(size_t n) {
    return n <= 1 ? 0 : 1 + mnemonicLog2(n / 2);
}
constexpr size_t MNEMONIC_WORD_BITS = mnemonicLog2(MNEMONIC_WORDLIST_SIZE);   // 11
static_assert((size_t(1) << MNEMONIC_WORD_BITS) == MNEMONIC_WORDLIST_SIZE,
              "wordlist size must be a power of two");

// Checksum: at least ENT/16 bits (twice BIP-39's ENT/32), grown until the
// bit string fills whole words. 128 -> 15 bits, 256 -> 19 bits.
constexpr size_t mnemonicChecksumBits(size_t entropyBits) {
    size_t cs = entropyBits / 16;
    while ((entropyBits + cs) % MNEMONIC_WORD_BITS != 0) ++cs;
    return cs;
}
constexpr size_t mnemonicWordCount(size_t entropyBits) {
    return (entropyBits + mnemonicChecksumBits(entropyBits)) / MNEMONIC_WORD_BITS;
}

constexpr size_t MNEMONIC_ENTROPY_BYTES_SMALL = 16;   // 128 bits
constexpr size_t MNEMONIC_ENTROPY_BYTES_LARGE = 32;   // 256 bits
static_assert(mnemonicChecksumBits(128) == 15 && mnemonicWordCount(128) == 13, "spec: 128 bits -> 13 words");
static_assert(mnemonicChecksumBits(256) == 19 && mnemonicWordCount(256) == 25, "spec: 256 bits -> 25 words");

constexpr bool isSupportedMnemonicEntropySize(size_t bytes) {
    return bytes == MNEMONIC_ENTROPY_BYTES_SMALL || bytes == MNEMONIC_ENTROPY_BYTES_LARGE;
}

constexpr uint32_t MNEMONIC_PBKDF2_ROUNDS = 2048;
constexpr size_t MNEMONIC_SEED_BYTES = 64;
constexpr char MNEMONIC_SALT_PREFIX[] = "bucks-mnemonic-v2";

// ============================================================
// Word lists
// ============================================================

/**
 * One language's list. Lists are INDEX-ALIGNED across languages: entry i of
 * every list encodes the same 11-bit value, so phrases convert between
 * languages without touching the entropy.
 */
struct Wordlist {
    std::string lang;         // manifest tag, e.g. "ar-dev"
    std::string normalizer;   // "exact" | "arabic" | "latin"
    std::string status;       // "dev" | "release" | anything else = not a default
    std::string sha256;       // lower-case hex SHA-256 of the file bytes
    bool pinned = false;      // the manifest pins sha256 (checked when the list loads)
    std::vector<std::string> entries;                 // display forms, index order
    std::unordered_map<std::string, uint16_t> index;  // normalized form -> index
};

/**
 * Normalize a typed word under a named normalizer. Returns false if the
 * normalizer is unknown or rejects the input (e.g. non-ASCII under "latin",
 * invalid UTF-8 under "arabic").
 */
bool normalizeMnemonicWord(const std::string& normalizer,
                           const std::string& word,
                           std::string& out);

/**
 * The "arabic" fold on its own (exposed for tests and tools): NFKD via explicit
 * Arabic-block tables, strip harakat/Quranic marks/tatweel/zero-width
 * characters, map letter variants (alef, yeh, kaf, heh, waw) to one form.
 * Returns false on invalid UTF-8.
 */
bool arabicFold(const std::string& in, std::string& out);

bool isKnownMnemonicNormalizer(const std::string& name);

/**
 * Load and validate one list file: exactly MNEMONIC_WORDLIST_SIZE non-empty
 * entries ('#' lines are comments), no internal whitespace, unique under the
 * normalizer. Any problem -> false with a reason in *err.
 */
bool loadWordlistFile(const std::string& path,
                      const std::string& lang,
                      const std::string& normalizer,
                      const std::string& status,
                      Wordlist& out,
                      std::string* err = nullptr);

/**
 * Where lists are looked up (first hit wins):
 *   1. setWordlistDir()            (explicit; no fallback if it is wrong)
 *   2. $BUCKS_WORDLIST_DIR         (explicit; no fallback if it is wrong)
 *   3. <executable dir>/../wordlists
 *   4. <executable dir>/wordlists
 *   5. BUCKS_SOURCE_DIR/wordlists  (compile-time, if defined)
 * Candidates 3-5 qualify only if they contain manifest.json. The working
 * directory is never searched.
 * An empty string clears the override. Changing the directory drops the cache.
 */
void setWordlistDir(const std::string& dir);
std::string resolveWordlistDir(std::string* err = nullptr);   // "" if nothing qualifies

/**
 * Force a (re)load of manifest.json and every list it names. The other
 * functions load lazily, so calling this is optional; it exists so a node can
 * fail at startup instead of at the first wallet operation.
 */
bool loadWordlists(std::string* err = nullptr);

std::vector<std::string> mnemonicLanguages(std::string* err = nullptr);   // manifest order
std::string defaultMnemonicLanguage(std::string* err = nullptr);          // first dev/release list
std::shared_ptr<const Wordlist> findWordlist(const std::string& lang, std::string* err = nullptr);

/**
 * Whole-set checks that a single list cannot do alone (run by tests, CI and
 * tools/bucks-check-wordlists):
 *   errors   - a word that two lists spell identically but give different indices (a phrase
 *              could then decode to the wrong wallet); a release/draft Latin-script list whose
 *              entries are not plain a-z; a non-test list that is not 2048 entries (loader).
 *   warnings - words sharing their first four letters inside one list (harder to type).
 * Lists with status "test" or "dev" (placeholders) are exempt from the cross-list spelling rule.
 */
struct WordlistCheck {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    bool ok() const { return errors.empty(); }
};
WordlistCheck checkWordlistSet();

// ============================================================
// Encode / decode
// ============================================================

/**
 * entropy (16 or 32 bytes) -> phrase in `lang` ("" = default language).
 * Returns "" and sets *err on any failure.
 */
std::string mnemonicFromEntropy(const std::vector<uint8_t>& entropy,
                                const std::string& lang = "",
                                MnemonicError* err = nullptr);

/**
 * phrase -> entropy. Words are split on ASCII whitespace and matched through
 * each list's normalizer. With lang == "" the language is auto-detected:
 * exactly one loaded list must accept every word AND pass the checksum.
 * On success entropyOut holds the raw entropy and *langOut the list used.
 */
MnemonicError mnemonicToEntropy(const std::string& phrase,
                                std::vector<uint8_t>& entropyOut,
                                std::string* langOut = nullptr,
                                const std::string& lang = "");

MnemonicError validateMnemonic(const std::string& phrase,
                               std::string* langOut = nullptr,
                               const std::string& lang = "");

/**
 * Re-encode a phrase in another language (same entropy, index-aligned lists).
 * Returns "" and sets *err on failure.
 */
std::string convertMnemonic(const std::string& phrase,
                            const std::string& targetLang,
                            MnemonicError* err = nullptr);

/** Tokens of a phrase, split on ASCII whitespace (no normalization). */
std::vector<std::string> splitMnemonic(const std::string& phrase);

// ============================================================
// Seed (entropy-canonical, language independent)
// ============================================================

/**
 * seed = PBKDF2-HMAC-SHA512(password = raw entropy bytes,
 *                           salt = "bucks-mnemonic-v2" || passphrase bytes,
 *                           2048 iterations, 64 bytes)
 * The passphrase is used verbatim (no Unicode normalisation): type it
 * exactly. Returns an empty vector on failure.
 */
std::vector<uint8_t> seedFromEntropy(const std::vector<uint8_t>& entropy,
                                     const std::string& passphrase);

/** Decode (validate) the phrase, then seedFromEntropy. Empty on failure. */
std::vector<uint8_t> mnemonicToSeed(const std::string& phrase,
                                    const std::string& passphrase,
                                    MnemonicError* err = nullptr);

// ============================================================
// Legacy entry points (kept for existing callers)
// ============================================================
// The wordlist-path argument is ignored: lists always come from the
// manifest directory described above. New code should use the v2 API.

std::string bip8192MnemonicFromEntropy(const std::vector<uint8_t>& entropy,
                                       const std::string& legacyWordlistPath,
                                       Bip8192Error* err = nullptr);

Bip8192Error bip8192ValidateMnemonic(const std::string& mnemonic,
                                     const std::string& legacyWordlistPath);

std::vector<uint8_t> bip8192MnemonicToSeed(const std::string& mnemonic,
                                           const std::string& passphrase);

} // namespace Bucks

#endif // BIP_8192_H
