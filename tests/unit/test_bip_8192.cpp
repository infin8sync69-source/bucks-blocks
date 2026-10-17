#include <gtest/gtest.h>
#include "bip_8192.h"
#include "bip_custom.h"
#include "crypto.h"
#include "mnemonic_test_util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using namespace Bucks;
using namespace bucks_test;

// ---------------------------------------------------------------------------
// Scheme constants are derived, and pinned to the spec.
// ---------------------------------------------------------------------------

TEST(MnemonicScheme, DerivedConstants) {
    EXPECT_EQ(MNEMONIC_WORD_BITS, 11u);
    EXPECT_EQ(mnemonicChecksumBits(128), 15u);
    EXPECT_EQ(mnemonicWordCount(128), 13u);
    EXPECT_EQ(mnemonicChecksumBits(256), 19u);
    EXPECT_EQ(mnemonicWordCount(256), 25u);
    EXPECT_TRUE(isSupportedMnemonicEntropySize(16));
    EXPECT_TRUE(isSupportedMnemonicEntropySize(32));
    EXPECT_FALSE(isSupportedMnemonicEntropySize(20));
    EXPECT_FALSE(isSupportedMnemonicEntropySize(24));
    EXPECT_FALSE(isSupportedMnemonicEntropySize(28));
}

// ---------------------------------------------------------------------------
// (a) Known-answer tests against values computed independently in Python.
// ---------------------------------------------------------------------------

class MnemonicKat : public MnemonicTest {};

static void checkKat(const Kat& kat) {
    std::vector<uint8_t> entropy = fromHex(kat.entropyHex);
    MnemonicError err = MnemonicError::INVALID_WORD;
    std::string phrase = mnemonicFromEntropy(entropy, "", &err);
    ASSERT_EQ(err, MnemonicError::OK) << mnemonicErrorString(err);
    EXPECT_EQ(phrase, kat.phrase);
    EXPECT_EQ(splitMnemonic(phrase).size(), kat.indices.size());

    // Word indices into the dev list match the Python encoder.
    auto list = findWordlist("ar-dev");
    ASSERT_TRUE(list);
    std::vector<std::string> words = splitMnemonic(phrase);
    for (size_t i = 0; i < words.size(); ++i)
        EXPECT_EQ(words[i], list->entries[kat.indices[i]]) << "word " << i;

    // Decoding gives back the entropy and identifies the language.
    std::vector<uint8_t> back;
    std::string lang;
    EXPECT_EQ(mnemonicToEntropy(kat.phrase, back, &lang), MnemonicError::OK);
    EXPECT_EQ(back, entropy);
    EXPECT_EQ(lang, "ar-dev");

    // Seeds are entropy-canonical PBKDF2 with the documented salt.
    EXPECT_EQ(hexStr(seedFromEntropy(entropy, "")), kat.seedNoPass);
    EXPECT_EQ(hexStr(seedFromEntropy(entropy, "TREZOR")), kat.seedTrezor);
    EXPECT_EQ(hexStr(mnemonicToSeed(kat.phrase, "TREZOR")), kat.seedTrezor);
}

TEST_F(MnemonicKat, Entropy128Gives13Words) { checkKat(kat128()); }
TEST_F(MnemonicKat, Entropy256Gives25Words) { checkKat(kat256()); }

TEST_F(MnemonicKat, LegacyEntryPointsForwardToV2) {
    std::vector<uint8_t> entropy = fromHex(kat128().entropyHex);
    Bip8192Error err = Bip8192Error::INVALID_WORD;
    // The path argument is ignored; lists come from the manifest directory.
    EXPECT_EQ(bip8192MnemonicFromEntropy(entropy, "wordlists/bip_8192.txt", &err), kat128().phrase);
    EXPECT_EQ(err, Bip8192Error::OK);
    EXPECT_EQ(bip8192ValidateMnemonic(kat128().phrase, "ignored"), Bip8192Error::OK);
    EXPECT_EQ(hexStr(bip8192MnemonicToSeed(kat128().phrase, "")), kat128().seedNoPass);
    EXPECT_EQ(generateCustomMnemonicFromEntropy(entropy, ""), kat128().phrase);
    EXPECT_TRUE(validateCustomMnemonic(kat128().phrase, ""));
    EXPECT_EQ(hexStr(mnemonicToSeedCustom(kat128().phrase, "")), kat128().seedNoPass);
}

TEST_F(MnemonicKat, SeedDependsOnPassphraseBytesVerbatim) {
    std::vector<uint8_t> entropy = fromHex(kat128().entropyHex);
    auto a = seedFromEntropy(entropy, "pass");
    auto b = seedFromEntropy(entropy, "Pass");
    auto c = seedFromEntropy(entropy, "pass ");
    ASSERT_EQ(a.size(), 64u);
    EXPECT_NE(a, b);
    EXPECT_NE(a, c);
    EXPECT_TRUE(seedFromEntropy(std::vector<uint8_t>(20, 1), "").empty());   // unsupported size
}

// ---------------------------------------------------------------------------
// (b) checksum flip, (c) wrong word counts, other rejections
// ---------------------------------------------------------------------------

class MnemonicReject : public MnemonicTest {};

TEST_F(MnemonicReject, UnsupportedEntropySizes) {
    MnemonicError err;
    for (size_t n : {0u, 8u, 15u, 17u, 20u, 24u, 28u, 31u, 33u, 64u}) {
        EXPECT_TRUE(mnemonicFromEntropy(std::vector<uint8_t>(n, 0x5a), "", &err).empty()) << n;
        EXPECT_EQ(err, MnemonicError::INVALID_ENTROPY_SIZE) << n;
    }
}

TEST_F(MnemonicReject, ChecksumFlipIsRejected) {
    auto list = findWordlist("ar-dev");
    ASSERT_TRUE(list);
    std::vector<std::string> words = splitMnemonic(kat128().phrase);
    ASSERT_EQ(words.size(), 13u);
    // The last word carries only checksum bits: changing it must fail.
    size_t lastIdx = kat128().indices.back();
    words.back() = list->entries[lastIdx ^ 1];
    std::string tampered;
    for (const auto& w : words) tampered += (tampered.empty() ? "" : " ") + w;
    std::vector<uint8_t> entropy;
    EXPECT_EQ(mnemonicToEntropy(tampered, entropy), MnemonicError::CHECKSUM_MISMATCH);
    EXPECT_TRUE(entropy.empty());

    // Swapping two words is also caught.
    words = splitMnemonic(kat256().phrase);
    std::swap(words[3], words[4]);
    tampered.clear();
    for (const auto& w : words) tampered += (tampered.empty() ? "" : " ") + w;
    EXPECT_EQ(validateMnemonic(tampered), MnemonicError::CHECKSUM_MISMATCH);
}

TEST_F(MnemonicReject, WrongWordCounts) {
    auto list = findWordlist("ar-dev");
    ASSERT_TRUE(list);
    for (size_t count : {0u, 1u, 11u, 12u, 14u, 21u, 24u, 26u}) {
        std::string phrase;
        for (size_t i = 0; i < count; ++i) phrase += (i ? " " : "") + list->entries[i];
        std::vector<uint8_t> entropy;
        EXPECT_EQ(mnemonicToEntropy(phrase, entropy), MnemonicError::INVALID_WORD_COUNT) << count;
    }
}

TEST_F(MnemonicReject, UnknownWordsAndLanguages) {
    std::vector<uint8_t> entropy;
    std::string thirteen;
    for (int i = 0; i < 13; ++i) thirteen += (i ? " hello" : "hello");
    EXPECT_EQ(mnemonicToEntropy(thirteen, entropy), MnemonicError::INVALID_WORD);
    EXPECT_EQ(mnemonicToEntropy(kat128().phrase, entropy, nullptr, "no-such-lang"), MnemonicError::UNKNOWN_LANGUAGE);
    MnemonicError err = MnemonicError::OK;
    EXPECT_TRUE(mnemonicFromEntropy(fromHex(kat128().entropyHex), "no-such-lang", &err).empty());
    EXPECT_EQ(err, MnemonicError::UNKNOWN_LANGUAGE);
    EXPECT_TRUE(convertMnemonic(kat128().phrase, "no-such-lang", &err).empty());
    EXPECT_EQ(err, MnemonicError::UNKNOWN_LANGUAGE);
}

// ---------------------------------------------------------------------------
// (e) Wordlist directory resolution and load failures
// ---------------------------------------------------------------------------

class MnemonicDir : public MnemonicTest {};

TEST_F(MnemonicDir, DefaultResolutionFindsRepoManifest) {
    std::string err;
    std::string dir = resolveWordlistDir(&err);
    ASSERT_FALSE(dir.empty()) << err;
    EXPECT_TRUE(fs::is_regular_file(fs::path(dir) / "manifest.json"));
    EXPECT_TRUE(loadWordlists(&err)) << err;
    EXPECT_EQ(defaultMnemonicLanguage(), "ar-dev");
    auto langs = mnemonicLanguages();
    ASSERT_GE(langs.size(), 1u);
    EXPECT_EQ(langs[0], "ar-dev");   // the placeholder stays first, so it stays the default
}

TEST_F(MnemonicDir, EmptyEnvDirFailsClosed) {
    TempDir empty;
    setWordlistEnv(empty.str());
    EXPECT_EQ(resolveWordlistDir(), empty.str());     // explicit setting: no fallback
    std::string err;
    EXPECT_FALSE(loadWordlists(&err));
    EXPECT_NE(err.find("manifest.json"), std::string::npos) << err;
    MnemonicError merr = MnemonicError::OK;
    EXPECT_TRUE(mnemonicFromEntropy(fromHex(kat128().entropyHex), "", &merr).empty());
    EXPECT_EQ(merr, MnemonicError::WORDLIST_LOAD_FAILED);
    std::vector<uint8_t> entropy;
    EXPECT_EQ(mnemonicToEntropy(kat128().phrase, entropy), MnemonicError::WORDLIST_LOAD_FAILED);
    EXPECT_TRUE(mnemonicToSeed(kat128().phrase, "").empty());
    EXPECT_TRUE(defaultMnemonicLanguage().empty());

    // Clearing the variable makes the default resolution work again.
    clearWordlistEnv();
    EXPECT_TRUE(loadWordlists(&err)) << err;
}

TEST_F(MnemonicDir, SetterBeatsEnvironment) {
    TempDir fromSetter;
    TempDir fromEnv;
    writeTwoLanguageDir(fromSetter);
    setWordlistEnv(fromEnv.str());
    setWordlistDir(fromSetter.str());
    EXPECT_EQ(resolveWordlistDir(), fromSetter.str());
    EXPECT_EQ(mnemonicLanguages().size(), 2u);
    setWordlistDir("");
    EXPECT_EQ(resolveWordlistDir(), fromEnv.str());
    EXPECT_TRUE(mnemonicLanguages().empty());
}

TEST_F(MnemonicDir, BrokenManifestsAreRejected) {
    TempDir dir;
    std::string err;
    auto expectFail = [&](const char* manifest, const char* what) {
        dir.write("manifest.json", manifest);
        setWordlistDir(dir.str());
        EXPECT_FALSE(loadWordlists(&err)) << what;
        EXPECT_FALSE(err.empty()) << what;
    };
    dir.write("xx-test-2048.txt", syntheticLatinList());
    expectFail("not json", "garbage");
    expectFail("{\"version\":1,\"lists\":[]}", "wrong version");
    expectFail("{\"version\":2,\"lists\":[]}", "no lists");
    expectFail("{\"version\":2,\"lists\":[{\"lang\":\"xx\",\"file\":\"missing.txt\",\"normalizer\":\"latin\",\"status\":\"dev\"}]}", "missing file");
    expectFail("{\"version\":2,\"lists\":[{\"lang\":\"xx\",\"file\":\"xx-test-2048.txt\",\"normalizer\":\"bogus\",\"status\":\"dev\"}]}", "unknown normalizer");
    expectFail("{\"version\":2,\"lists\":[{\"lang\":\"xx\",\"file\":\"../xx-test-2048.txt\",\"normalizer\":\"latin\",\"status\":\"dev\"}]}", "path escape");
    expectFail("{\"version\":2,\"lists\":[{\"lang\":\"xx\",\"file\":\"xx-test-2048.txt\",\"normalizer\":\"latin\",\"status\":\"draft\"}]}", "no default list");
    expectFail("{\"version\":2,\"lists\":[{\"lang\":\"xx\",\"file\":\"xx-test-2048.txt\",\"normalizer\":\"latin\"}]}", "missing status");
    expectFail("{\"version\":2,\"lists\":["
               "{\"lang\":\"xx\",\"file\":\"xx-test-2048.txt\",\"normalizer\":\"latin\",\"status\":\"dev\"},"
               "{\"lang\":\"xx\",\"file\":\"xx-test-2048.txt\",\"normalizer\":\"latin\",\"status\":\"dev\"}]}", "duplicate lang");
    // Under the "arabic" normalizer the ASCII list is fine too (bytes are
    // valid UTF-8), which shows the normalizer check is per list.
    dir.write("manifest.json", "{\"version\":2,\"lists\":[{\"lang\":\"xx\",\"file\":\"xx-test-2048.txt\",\"normalizer\":\"latin\",\"status\":\"dev\"}]}");
    EXPECT_TRUE(loadWordlists(&err)) << err;
}

namespace {

std::string fileSha256Hex(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const Hash256 d = sha256(std::vector<uint8_t>(bytes.begin(), bytes.end()));
    return hexStr(std::vector<uint8_t>(d.begin(), d.end()));
}

} // namespace

// WAL-14 / WORDLIST-1: every shipped list is pinned and the pin is the hash of its bytes. This fails when a
// list file changes and its manifest hash does not.
TEST_F(MnemonicDir, ShippedListsMatchTheirManifestPins) {
    const fs::path dir = fs::path(BUCKS_SOURCE_DIR) / "wordlists";
    std::ifstream in(dir / "manifest.json", std::ios::binary);
    ASSERT_TRUE(in);
    const nlohmann::json m = nlohmann::json::parse(in);
    ASSERT_TRUE(m.contains("lists") && m["lists"].is_array());
    for (const auto& e : m["lists"]) {
        const std::string file = e.value("file", "");
        ASSERT_TRUE(e.contains("sha256") && e["sha256"].is_string()) << file << " has no \"sha256\" pin in manifest.json";
        EXPECT_EQ(fileSha256Hex(dir / file), e["sha256"].get<std::string>())
            << "wordlists/" << file << " changed but manifest.json still pins the old hash. Phrases made with a "
               "published list need its exact bytes: publish a changed list under a new tag with its own pin.";
    }
    // The loader agrees with the independent hash above.
    std::string err;
    setWordlistDir(dir.string());
    ASSERT_TRUE(loadWordlists(&err)) << err;
    for (const auto& lang : mnemonicLanguages()) {
        auto wl = findWordlist(lang);
        ASSERT_TRUE(wl);
        EXPECT_TRUE(wl->pinned) << lang;
    }
}

TEST_F(MnemonicDir, PinnedListMustMatchItsBytes) {
    TempDir dir;
    dir.write("xx.txt", syntheticLatinList());
    const std::string pin = fileSha256Hex(dir.path() / "xx.txt");
    auto manifest = [](const std::string& status, const std::string& pinField) {
        return "{\"version\":2,\"lists\":[{\"lang\":\"xx\",\"file\":\"xx.txt\",\"normalizer\":\"latin\","
               "\"status\":\"" + status + "\"" + pinField + "}]}";
    };
    std::string err;
    setWordlistDir(dir.str());

    dir.write("manifest.json", manifest("release", ",\"sha256\":\"" + pin + "\""));
    ASSERT_TRUE(loadWordlists(&err)) << err;
    auto wl = findWordlist("xx");
    ASSERT_TRUE(wl);
    EXPECT_EQ(wl->sha256, pin);
    EXPECT_TRUE(wl->pinned);

    // One word changed in place: refused, and the error says why.
    std::string edited = syntheticLatinList();
    edited.replace(edited.find("w0007"), 5, "w9999");
    dir.write("xx.txt", edited);
    EXPECT_FALSE(loadWordlists(&err));
    EXPECT_NE(err.find("does not match the SHA-256"), std::string::npos) << err;
    EXPECT_NE(err.find("xx"), std::string::npos) << err;
    MnemonicError merr = MnemonicError::OK;
    EXPECT_TRUE(mnemonicFromEntropy(fromHex(kat128().entropyHex), "xx", &merr).empty());
    EXPECT_EQ(merr, MnemonicError::WORDLIST_LOAD_FAILED);

    // A release list must be pinned; a malformed pin is refused; placeholders may go unpinned.
    dir.write("xx.txt", syntheticLatinList());
    dir.write("manifest.json", manifest("release", ""));
    EXPECT_FALSE(loadWordlists(&err));
    EXPECT_NE(err.find("no \"sha256\" pin"), std::string::npos) << err;
    dir.write("manifest.json", manifest("release", ",\"sha256\":\"" + std::string(64, 'Z') + "\""));
    EXPECT_FALSE(loadWordlists(&err));
    dir.write("manifest.json", manifest("dev", ""));
    ASSERT_TRUE(loadWordlists(&err)) << err;
    EXPECT_FALSE(findWordlist("xx")->pinned);
}

// ---------------------------------------------------------------------------
// Wordlist file validation
// ---------------------------------------------------------------------------

class WordlistFile : public MnemonicTest {};

TEST_F(WordlistFile, AcceptsCommentsAndBlankLines) {
    TempDir dir;
    dir.write("list.txt", "# header\n\n" + syntheticLatinList() + "\n# trailer\n");
    Wordlist list;
    std::string err;
    ASSERT_TRUE(loadWordlistFile((dir.path() / "list.txt").string(), "xx", "latin", "dev", list, &err)) << err;
    EXPECT_EQ(list.entries.size(), 2048u);
    EXPECT_EQ(list.index.size(), 2048u);
    EXPECT_EQ(list.entries[0], "w0000");
    EXPECT_EQ(list.index.at("w2047"), 2047);
    EXPECT_EQ(list.lang, "xx");
    EXPECT_EQ(list.normalizer, "latin");
    EXPECT_EQ(list.status, "dev");
}

TEST_F(WordlistFile, RejectsBadLists) {
    TempDir dir;
    Wordlist list;
    std::string err;
    auto load = [&](const std::string& content, const char* normalizer) {
        dir.write("list.txt", content);
        err.clear();
        bool ok = loadWordlistFile((dir.path() / "list.txt").string(), "xx", normalizer, "dev", list, &err);
        if (!ok) EXPECT_FALSE(err.empty());
        return ok;
    };
    std::string good = syntheticLatinList();

    // 2047 entries
    std::string short_ = good.substr(0, good.rfind("w2047"));
    EXPECT_FALSE(load(short_, "latin"));
    // 2049 entries
    EXPECT_FALSE(load(good + "w2048\n", "latin"));
    // internal whitespace
    std::string spaced = good;
    spaced.replace(spaced.find("w0005"), 5, "w0 05");
    EXPECT_FALSE(load(spaced, "latin"));
    // duplicate under the normalizer (case-folded)
    std::string dup = good;
    dup.replace(dup.find("w0005"), 5, "W0004");
    EXPECT_FALSE(load(dup, "latin"));
    // non-ASCII under latin
    std::string nonAscii = good;
    nonAscii.replace(nonAscii.find("w0005"), 5, "w00\xC3\xA9");
    EXPECT_FALSE(load(nonAscii, "latin"));
    // invalid UTF-8 under arabic
    std::string badUtf8 = good;
    badUtf8.replace(badUtf8.find("w0005"), 5, "w00\xFF\xFE");
    EXPECT_FALSE(load(badUtf8, "arabic"));
    // missing file / unknown normalizer
    EXPECT_FALSE(loadWordlistFile((dir.path() / "nope.txt").string(), "xx", "latin", "dev", list, &err));
    EXPECT_FALSE(load(good, "bogus"));
    // and the good list still loads
    EXPECT_TRUE(load(good, "latin")) << err;
    EXPECT_TRUE(load(good, "exact")) << err;
    // "exact" keeps case distinct, so W0004 is NOT a duplicate there
    EXPECT_TRUE(load(dup, "exact")) << err;
}

// ---------------------------------------------------------------------------
// (j) The generated dev list: 2048 unique folded entries derived from the
//     legacy list.
// ---------------------------------------------------------------------------

TEST_F(WordlistFile, DevListHas2048UniqueFoldedEntries) {
    std::string dir = repoWordlistDir();
    Wordlist list;
    std::string err;
    ASSERT_TRUE(loadWordlistFile(dir + "/ar-dev-2048.txt", "ar-dev", "arabic", "dev", list, &err)) << err;
    ASSERT_EQ(list.entries.size(), 2048u);
    EXPECT_EQ(list.index.size(), 2048u);

    std::set<std::string> folded;
    for (const auto& e : list.entries) {
        std::string f;
        ASSERT_TRUE(arabicFold(e, f));
        EXPECT_EQ(f, e) << "dev entries are stored folded";   // idempotent, no marks
        EXPECT_GE(f.size() / 2, 3u);                            // 3..7 Arabic letters (2 bytes each)
        EXPECT_LE(f.size() / 2, 7u);
        EXPECT_EQ(f.find("\xD8\xA7\xD9\x84\xD9\x84\xD9\x87"), std::string::npos);   // الله
        EXPECT_EQ(f.find("\xD8\xA7\xD9\x84\xD9\x87"), std::string::npos);           // اله
        folded.insert(f);
    }
    EXPECT_EQ(folded.size(), 2048u);

    // Every dev entry is the fold of some legacy (diacritic-laden) form, and
    // the legacy spellings resolve to their dev index through the normalizer.
    std::ifstream legacy(dir + "/bip_8192.txt", std::ios::binary);
    ASSERT_TRUE(legacy.good());
    std::set<std::string> legacyFolded;
    std::string line;
    size_t resolved = 0;
    while (std::getline(legacy, line)) {
        if (line.empty()) continue;
        std::string f;
        ASSERT_TRUE(arabicFold(line, f));
        legacyFolded.insert(f);
        std::string norm;
        if (normalizeMnemonicWord("arabic", line, norm) && list.index.count(norm)) {
            EXPECT_EQ(list.entries[list.index.at(norm)], f);
            ++resolved;
        }
    }
    for (const auto& e : list.entries) EXPECT_TRUE(legacyFolded.count(e)) << e;
    EXPECT_EQ(resolved, 2048u) << "each dev entry has exactly one legacy spelling";
}

// ---------------------------------------------------------------------------
// (g) Arabic fold: typed variants resolve to the same index
// ---------------------------------------------------------------------------

class ArabicFold : public MnemonicTest {};

TEST_F(ArabicFold, VariantsResolveToSameIndex) {
    auto list = findWordlist("ar-dev");
    ASSERT_TRUE(list);
    // index 96 = ابويه (alef, beh, waw, yeh, heh)
    const uint16_t idx = kat128().indices[0];
    ASSERT_EQ(list->entries[idx], "ابويه");

    struct Variant { const char* what; const char* typed; };
    const Variant variants[] = {
        {"plain",                 "ابويه"},
        {"with harakat",          "اَبَوَيْهِ"},
        {"shadda + sukun",        "ابّويْه"},
        {"alef with hamza above", "أبويه"},
        {"alef with hamza below", "إبويه"},
        {"alef with madda",       "آبويه"},
        {"alef wasla",            "ٱبويه"},
        {"decomposed hamza",      "ا\xD9\x94" "بويه"},      // U+0627 U+0654
        {"Persian yeh U+06CC",    "ابو\xDB\x8C" "ه"},
        {"alef maksura",          "ابو\xD9\x89" "ه"},
        {"teh marbuta",           "ابوي\xD8\xA9"},
        {"Urdu heh goal U+06C1",  "ابوي\xDB\x81"},
        {"Kurdish ae U+06D5",     "ابوي\xDB\x95"},
        {"tatweel",               "ابـوـيه"},
        {"ZWNJ inside",           "ابو\xE2\x80\x8C" "يه"},
        {"ZWJ + ZWSP + RLM",      "\xE2\x80\x8D" "اب\xE2\x80\x8B" "ويه\xE2\x80\x8F"},
        {"ALM + BOM",             "\xEF\xBB\xBF" "ابويه\xD8\x9C"},
        {"superscript alef",      "ابو\xD9\xB0" "يه"},
        {"Quranic small marks",   "ابو\xDB\x96" "يه\xDB\xA1"},
        {"surrounding spaces",    "  ابويه\t"},
        {"presentation forms",    "\xEF\xBA\x8D\xEF\xBA\x91\xEF\xBB\xAE\xEF\xBB\xB4\xEF\xBB\xAA"},   // FE8D FE91 FEEE FEF4 FEEA
    };
    for (const auto& v : variants) {
        std::string norm;
        ASSERT_TRUE(normalizeMnemonicWord("arabic", v.typed, norm)) << v.what;
        EXPECT_EQ(norm, "ابويه") << v.what;
        auto it = list->index.find(norm);
        ASSERT_NE(it, list->index.end()) << v.what;
        EXPECT_EQ(it->second, idx) << v.what;
    }
}

TEST_F(ArabicFold, KafWawAndLigatureVariants) {
    auto list = findWordlist("ar-dev");
    ASSERT_TRUE(list);
    // ابلغكم (index 764) typed with Persian keheh U+06A9
    std::string norm;
    ASSERT_TRUE(normalizeMnemonicWord("arabic", "ابلغ\xDA\xA9" "م", norm));
    EXPECT_EQ(list->index.at(norm), kat128().indices[6]);
    // بالازلم (index 1365) typed with the lam-alef ligature U+FEFB
    ASSERT_TRUE(normalizeMnemonicWord("arabic", "با\xEF\xBB\xBB" "زلم", norm));
    EXPECT_EQ(list->index.at(norm), kat256().indices[9]);
    // waw with hamza / yeh with hamza fold to waw / yeh
    std::string f;
    ASSERT_TRUE(arabicFold("ؤئ", f));
    EXPECT_EQ(f, "وي");
    // precomposed Urdu/Kurdish hamza letters fold like their decomposed forms
    ASSERT_TRUE(arabicFold("\xDB\x82", f));            // U+06C2 heh goal with hamza
    EXPECT_EQ(f, "ه");
    ASSERT_TRUE(arabicFold("\xDB\x80", f));            // U+06C0 heh with yeh above
    EXPECT_EQ(f, "ه");
    // invalid UTF-8 is rejected, never silently matched
    EXPECT_FALSE(arabicFold("\xC0\xAF", f));
    EXPECT_FALSE(normalizeMnemonicWord("arabic", "\xED\xA0\x80", norm));   // surrogate
}

TEST_F(ArabicFold, WholePhraseTypedWithoutDiacriticsDecodes) {
    // The 128-bit KAT phrase re-typed with Persian yeh/keheh and a hamza alef.
    std::string typed = kat128().phrase;
    auto replaceAll = [&](const std::string& from, const std::string& to) {
        for (size_t p = typed.find(from); p != std::string::npos; p = typed.find(from, p + to.size()))
            typed.replace(p, from.size(), to);
    };
    replaceAll("\xD9\x8A", "\xDB\x8C");   // ي -> ی
    replaceAll("\xD9\x83", "\xDA\xA9");   // ك -> ک
    typed.replace(0, 2, "\xD8\xA3");      // leading ا -> أ
    ASSERT_NE(typed, kat128().phrase);
    std::vector<uint8_t> entropy;
    std::string lang;
    EXPECT_EQ(mnemonicToEntropy(typed, entropy, &lang), MnemonicError::OK);
    EXPECT_EQ(hexStr(entropy), kat128().entropyHex);
    EXPECT_EQ(lang, "ar-dev");
}

TEST(Normalizers, ExactAndLatin) {
    std::string out;
    EXPECT_TRUE(normalizeMnemonicWord("exact", "  Abc\xC3\xA9 ", out));
    EXPECT_EQ(out, "Abc\xC3\xA9");
    EXPECT_TRUE(normalizeMnemonicWord("latin", " AbC ", out));
    EXPECT_EQ(out, "abc");
    EXPECT_FALSE(normalizeMnemonicWord("latin", "caf\xC3\xA9", out));
    EXPECT_FALSE(normalizeMnemonicWord("latin", "", out));
    EXPECT_FALSE(normalizeMnemonicWord("exact", " \t", out));
    EXPECT_FALSE(normalizeMnemonicWord("nope", "abc", out));
    EXPECT_TRUE(isKnownMnemonicNormalizer("arabic"));
    EXPECT_FALSE(isKnownMnemonicNormalizer("Arabic"));
    auto words = splitMnemonic(" a\tb\r\nc  d ");
    ASSERT_EQ(words.size(), 4u);
    EXPECT_EQ(words[3], "d");
}

// ---------------------------------------------------------------------------
// (f) Index alignment, conversion, auto-detection with a second language
// ---------------------------------------------------------------------------

class MultiLanguage : public MnemonicTest {
protected:
    TempDir dir;
    void SetUp() override {
        MnemonicTest::SetUp();
        writeTwoLanguageDir(dir);
        setWordlistDir(dir.str());
        std::string err;
        ASSERT_TRUE(loadWordlists(&err)) << err;
    }
};

TEST_F(MultiLanguage, ManifestOrderAndDefault) {
    auto langs = mnemonicLanguages();
    ASSERT_EQ(langs.size(), 2u);
    EXPECT_EQ(langs[0], "ar-dev");
    EXPECT_EQ(langs[1], "xx-test");
    EXPECT_EQ(defaultMnemonicLanguage(), "ar-dev");   // "test" status is never the default
    auto xx = findWordlist("xx-test");
    ASSERT_TRUE(xx);
    EXPECT_EQ(xx->normalizer, "latin");
    EXPECT_EQ(xx->entries[2047], "w2047");
}

TEST_F(MultiLanguage, IndexAlignedConversionRoundTrip) {
    for (const Kat* kat : {&kat128(), &kat256()}) {
        std::string expectedLatin = latinPhrase(kat->indices);
        MnemonicError err = MnemonicError::INVALID_WORD;
        EXPECT_EQ(mnemonicFromEntropy(fromHex(kat->entropyHex), "xx-test", &err), expectedLatin);
        EXPECT_EQ(err, MnemonicError::OK);
        EXPECT_EQ(convertMnemonic(kat->phrase, "xx-test", &err), expectedLatin);
        EXPECT_EQ(err, MnemonicError::OK);
        EXPECT_EQ(convertMnemonic(expectedLatin, "ar-dev", &err), kat->phrase);
        EXPECT_EQ(err, MnemonicError::OK);
        // Case is folded by the latin normalizer
        std::string upper = expectedLatin;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        EXPECT_EQ(convertMnemonic(upper, "ar-dev", &err), kat->phrase);
    }
}

TEST_F(MultiLanguage, AutoDetectAndIdenticalSeeds) {
    std::vector<uint8_t> a, b;
    std::string langA, langB;
    EXPECT_EQ(mnemonicToEntropy(kat128().phrase, a, &langA), MnemonicError::OK);
    EXPECT_EQ(mnemonicToEntropy(latinPhrase(kat128().indices), b, &langB), MnemonicError::OK);
    EXPECT_EQ(langA, "ar-dev");
    EXPECT_EQ(langB, "xx-test");
    EXPECT_EQ(a, b);
    EXPECT_EQ(hexStr(mnemonicToSeed(kat128().phrase, "x")),
              hexStr(mnemonicToSeed(latinPhrase(kat128().indices), "x")));
    EXPECT_EQ(hexStr(mnemonicToSeed(latinPhrase(kat128().indices), "")), kat128().seedNoPass);

    // A phrase mixing the two languages is in neither list.
    std::string mixed = latinPhrase(kat128().indices);
    mixed.replace(0, 5, "ابويه");
    EXPECT_EQ(validateMnemonic(mixed), MnemonicError::INVALID_WORD);
    // Explicit language: the other list's phrase is just unknown words.
    EXPECT_EQ(validateMnemonic(kat128().phrase, nullptr, "xx-test"), MnemonicError::INVALID_WORD);
    EXPECT_EQ(validateMnemonic(kat128().phrase, nullptr, "ar-dev"), MnemonicError::OK);
}

TEST_F(MnemonicDir, ListsThatAgreeOnTheEntropyAreOneMatch) {
    // Two lists with the same words at the same indices: a phrase valid in both is the same wallet either
    // way, so it is not ambiguous (gate WORDLIST-2). The first list in manifest order is reported.
    TempDir two;
    two.write("a.txt", syntheticLatinList());
    two.write("b.txt", syntheticLatinList());
    two.write("manifest.json",
        "{\"version\":2,\"lists\":["
        "{\"lang\":\"aa\",\"file\":\"a.txt\",\"normalizer\":\"latin\",\"status\":\"dev\"},"
        "{\"lang\":\"bb\",\"file\":\"b.txt\",\"normalizer\":\"exact\",\"status\":\"dev\"}]}");
    setWordlistDir(two.str());
    std::string phrase = latinPhrase(kat128().indices);
    std::vector<uint8_t> entropy;
    std::string lang;
    EXPECT_EQ(mnemonicToEntropy(phrase, entropy, &lang), MnemonicError::OK);
    EXPECT_EQ(lang, "aa");
    EXPECT_EQ(hexStr(entropy), kat128().entropyHex);
    EXPECT_EQ(mnemonicToEntropy(phrase, entropy, &lang, "bb"), MnemonicError::OK);
    EXPECT_EQ(lang, "bb");
    EXPECT_EQ(hexStr(entropy), kat128().entropyHex);
    // Restricting the candidates reports the list that was allowed.
    EXPECT_EQ(mnemonicToEntropyAmong(phrase, {"bb"}, entropy, &lang), MnemonicError::OK);
    EXPECT_EQ(lang, "bb");
    EXPECT_EQ(mnemonicToEntropyAmong(phrase, {"zz"}, entropy, &lang), MnemonicError::INVALID_WORD);
    EXPECT_TRUE(entropy.empty());
    // Upper case is only valid under the latin list.
    std::string upper = phrase;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    EXPECT_EQ(mnemonicToEntropy(upper, entropy, &lang), MnemonicError::OK);
    EXPECT_EQ(lang, "aa");
}

TEST_F(MnemonicDir, ListsThatDisagreeOnTheEntropyAreAmbiguous) {
    // The same words in reverse order: a phrase whose checksum happens to hold under both lists means two
    // different wallets, so auto-detection must refuse it.
    std::string reversed;
    for (int i = 2047; i >= 0; --i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "w%04d\n", i);
        reversed += buf;
    }
    TempDir two;
    two.write("a.txt", syntheticLatinList());
    two.write("b.txt", reversed);
    two.write("manifest.json",
        "{\"version\":2,\"lists\":["
        "{\"lang\":\"aa\",\"file\":\"a.txt\",\"normalizer\":\"latin\",\"status\":\"dev\"},"
        "{\"lang\":\"bb\",\"file\":\"b.txt\",\"normalizer\":\"latin\",\"status\":\"dev\"}]}");
    setWordlistDir(two.str());

    // About one entropy in 2^15 gives such a phrase; search deterministically.
    std::string phrase;
    std::vector<uint8_t> entropy;
    for (uint32_t i = 0; i < 2000000 && phrase.empty(); ++i) {
        const std::vector<uint8_t> seed{uint8_t(i), uint8_t(i >> 8), uint8_t(i >> 16), uint8_t(i >> 24)};
        const Hash256 h = sha256(seed);
        const std::vector<uint8_t> candidate(h.begin(), h.begin() + 16);
        const std::string p = mnemonicFromEntropy(candidate, "aa");
        if (validateMnemonic(p, nullptr, "bb") == MnemonicError::OK) phrase = p;
    }
    ASSERT_FALSE(phrase.empty());
    EXPECT_EQ(mnemonicToEntropy(phrase, entropy), MnemonicError::AMBIGUOUS_LANGUAGE);
    EXPECT_TRUE(entropy.empty());
    std::vector<uint8_t> a, b;
    EXPECT_EQ(mnemonicToEntropy(phrase, a, nullptr, "aa"), MnemonicError::OK);
    EXPECT_EQ(mnemonicToEntropy(phrase, b, nullptr, "bb"), MnemonicError::OK);
    EXPECT_NE(a, b);
    std::string lang;
    EXPECT_EQ(mnemonicToEntropyAmong(phrase, {"bb"}, entropy, &lang), MnemonicError::OK);
    EXPECT_EQ(lang, "bb");
    EXPECT_EQ(entropy, b);
}

TEST(MnemonicErrors, StringsNeverEmpty) {
    for (int i = 0; i <= static_cast<int>(MnemonicError::SEED_DERIVATION_FAILED); ++i)
        EXPECT_GT(std::string(mnemonicErrorString(static_cast<MnemonicError>(i))).size(), 0u);
}

// ---------------------------------------------------------------------------
// (g) checkWordlistSet: rules that need more than one list
// ---------------------------------------------------------------------------

namespace {

// 2048 distinct a-z words (base-26, 4 letters), optionally with two entries swapped.
std::string letterList(size_t swapA = 0, size_t swapB = 0) {
    std::vector<std::string> w;
    for (size_t i = 0; i < 2048; ++i) {
        std::string s = "qqqq";
        size_t v = i;
        for (int k = 3; k >= 0; --k) { s[k] = char('a' + v % 26); v /= 26; }
        w.push_back(s);
    }
    if (swapA != swapB) std::swap(w[swapA], w[swapB]);
    std::string out;
    for (auto& s : w) out += s + "\n";
    return out;
}

} // namespace

TEST_F(MnemonicDir, SetCheckAcceptsIdenticalIndexingAndRejectsSwaps) {
    auto manifest = [](const char* statusB) {
        // "dd" is the placeholder default (a loaded set needs one); aa and bb are what is compared.
        return std::string("{\"version\":2,\"lists\":["
            "{\"lang\":\"dd\",\"file\":\"d.txt\",\"normalizer\":\"latin\",\"status\":\"dev\"},"
            "{\"lang\":\"aa\",\"file\":\"a.txt\",\"normalizer\":\"latin\",\"status\":\"draft\"},"
            "{\"lang\":\"bb\",\"file\":\"b.txt\",\"normalizer\":\"latin\",\"status\":\"") + statusB + "\"}]}";
    };
    // Same words at the same indices: harmless (a shared word encodes the same value).
    {
        TempDir d;
        d.write("d.txt", syntheticLatinList());
        d.write("a.txt", letterList());
        d.write("b.txt", letterList());
        d.write("manifest.json", manifest("draft"));
        setWordlistDir(d.str());
        auto r = checkWordlistSet();
        EXPECT_TRUE(r.ok()) << (r.errors.empty() ? "" : r.errors[0]);
    }
    // Two words swapped in one list: same spelling, different index. A phrase could decode to
    // the wrong wallet, so this must be an error.
    {
        TempDir d;
        d.write("d.txt", syntheticLatinList());
        d.write("a.txt", letterList());
        d.write("b.txt", letterList(3, 7));
        d.write("manifest.json", manifest("draft"));
        setWordlistDir(d.str());
        auto r = checkWordlistSet();
        ASSERT_FALSE(r.ok());
        EXPECT_NE(r.errors[0].find("different index"), std::string::npos) << r.errors[0];
    }
    // Lists with status "test" are exempt (the synthetic lists use digits).
    {
        TempDir d;
        d.write("d.txt", syntheticLatinList());
        d.write("a.txt", letterList());
        d.write("b.txt", syntheticLatinList());
        d.write("manifest.json", manifest("test"));
        setWordlistDir(d.str());
        EXPECT_TRUE(checkWordlistSet().ok());
    }
    // A shipped-style Latin list must be plain a-z.
    {
        TempDir d;
        d.write("d.txt", syntheticLatinList());
        d.write("a.txt", syntheticLatinList());
        d.write("manifest.json",
                "{\"version\":2,\"lists\":[{\"lang\":\"dd\",\"file\":\"d.txt\",\"normalizer\":\"latin\",\"status\":\"dev\"},"
                "{\"lang\":\"aa\",\"file\":\"a.txt\",\"normalizer\":\"latin\",\"status\":\"draft\"}]}");
        setWordlistDir(d.str());
        auto r = checkWordlistSet();
        ASSERT_FALSE(r.ok());
        EXPECT_NE(r.errors[0].find("only a-z"), std::string::npos) << r.errors[0];
    }
}
