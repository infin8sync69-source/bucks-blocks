# Mnemonic word-list design rules applied to an Arabic (Quranic) 8192-word list, verbs-only, with index-aligned meaning lists

Labels used below:
- **[Established]**: written into a published spec, a merged list's rules, or shipping code.
- **[Own computation]**: numbers I computed in this session from the linked public data with Python 3.14 / `unicodedata` (Unicode 16.0.0). The method is described in each entry so it can be reproduced.
- **Inferences**: my own reasoning, not established practice.

Bucks context (supplied by the owner, not researched): 8192-word lists at 13 bits per word. 128-bit entropy plus a 15-bit checksum gives 11 words. 256-bit entropy plus a 17-bit checksum gives 21 words. The seed comes from the entropy, not the text, so index i means the same value in every language list.

---

## Q1. What are the official BIP-39 word-list rules, and how did each non-English list handle language-specific issues?

### Takeaway
BIP-39 itself sets only three soft criteria: the first 4 letters identify the word, similar words are avoided, and the list is sorted. It also requires UTF-8 NFKD. Each list's authors wrote their own stricter rules: one canonical dictionary form per word, no diacritic-dependent distinctions, no 1-letter-difference pairs, no homophones, and no words shared with other lists. Since 2020 the BIP "strongly discourages" non-English lists, and its authors accept no new ones.

### Cited Findings
**Core BIP-39 text [Established]**
- BIP-39 lists the traits of an ideal wordlist:
  - (a) "smart selection of words": "it's enough to type the first four letters to unambiguously identify the word".
  - (b) "similar words avoided": pairs like "build"/"built", "woman"/"women", "quick"/"quickly" "not only make remembering the sentence difficult but are also more error prone and more difficult to guess".
  - (c) "sorted wordlists", so lookups can use binary search or a trie.
  - "The wordlist can contain native characters, but they must be encoded in UTF-8 using Normalization Form Compatibility Decomposition (NFKD)."
  - [bip-0039.mediawiki](https://github.com/bitcoin/bips/blob/master/bip-0039.mediawiki)
- The seed is PBKDF2-HMAC-SHA512. The password is the mnemonic sentence in UTF-8 NFKD, the salt is "mnemonic"+passphrase, and it runs 2048 iterations. The seed therefore depends on the text, not just the entropy. — [bip-0039.mediawiki](https://github.com/bitcoin/bips/blob/master/bip-0039.mediawiki)
- "Since the vast majority of BIP39 wallets supports only the English wordlist, it is **strongly discouraged** to use non-English wordlists for generating the mnemonic sentences." This text was added by [PR #1047 (2020), "bip39: discourage from using localized wordlists"](https://github.com/bitcoin/bips/pull/1047). — [bip-0039.mediawiki](https://github.com/bitcoin/bips/blob/master/bip-0039.mediawiki)
- A "Shortcomings" section was merged in 2026 ([PR #2184](https://github.com/bitcoin/bips/pull/2184)). It says:
  - the seed depends on the wordlist, so "translating the mnemonic to a different wordlist necessarily creates a completely different seed";
  - "The checksum is short… (1-in-256 errors will be missed)… not able to provide any assistance in correcting errors";
  - there is no versioning scheme.
  - [bip-0039.mediawiki §Shortcomings](https://github.com/bitcoin/bips/blob/master/bip-0039.mediawiki)
- One BIP-39 author (prusnak), 2024: "My intention is not to support any wordlists except for the English one to maximize interoperability." — [PR #1395 comment](https://github.com/bitcoin/bips/pull/1395)
- The same author in Aug 2026: "On several occasions I raised my will that we should drop non-English wordlists from BIP39, but I was always blocked by editors." — [PR #2237](https://github.com/bitcoin/bips/pull/2237)

**Official lists [Established]**
- The official lists are English, Japanese, Korean, Spanish, Chinese (Simplified), Chinese (Traditional), French, Italian, Czech and Portuguese. Korean has no special-considerations section. — [bip-0039-wordlists.md](https://github.com/bitcoin/bips/blob/master/bip-0039/bip-0039-wordlists.md)

**Per-language rules [Established]** (all from [bip-0039-wordlists.md](https://github.com/bitcoin/bips/blob/master/bip-0039/bip-0039-wordlists.md))
- **Japanese**
  - Phrases must be separated with the ideographic space U+3000 (UTF-8 0xE38080).
  - Seed normalization turns U+3000 into an ASCII space.
  - Word-wrapping must break only at ideographic spaces, "as a long word split in two could be mistaken easily for two smaller words".
- **Japanese follow-up, 2026 [Established]:** the reference implementation (python-mnemonic) splits input on U+0020 and so "cannot read back the Japanese sentences it writes: `to_entropy` raises on all 24 Japanese vectors". The BIP does not say how to handle leading, trailing or doubled whitespace, so a stray whitespace can quietly derive "a valid-looking wallet that is not the user's". — [PR #2237](https://github.com/bitcoin/bips/pull/2237)
- **Spanish**
  - First 4 characters identify the word.
  - "'ñ', 'ü', 'á', etc… are considered equal to 'n', 'u', 'a'", so no Spanish keyboard is needed.
  - "There are no words in common between the Spanish wordlist and any other language wordlist, therefore it is possible to detect the language with just one word."
- **Chinese:** each word is a single character. Words are separated by ASCII spaces (0x20) "for the sake of uniformity", even though Chinese normally uses no spaces.
- **French (17 rules)**
  - Simple, common words of 5–8 letters.
  - Recognizable from the first 4 letters, with é/è counted as e ("museau" and "musée" cannot both be in).
  - "Only infinitive verbs, adjectives and nouns".
  - No pronouns, adverbs, prepositions, conjunctions or interjections, and no numeral adjectives.
  - No plurals and no feminine adjectives.
  - No same-sound/different-spelling pairs like "verre-vert".
  - "No very similar words with only 1 letter of difference".
  - No essentially reflexive verbs.
  - No words containing ô â ç ê œ æ î ï û ù à ë ÿ, and no words ending in é/ée/è/et/ai/ait.
  - No demonyms, and nothing that conflicts with the 1990 spelling reform.
  - "No embarrassing words (in a very, very large scope) or belonging to a particular religion".
  - No words identical to the Spanish list.
- **Italian (12 rules)**
  - 4–8 characters, and the first 4 letters must be unique.
  - No accents, complex verb forms or plurals.
  - Nothing negative or sad.
  - Masculine form when both genders exist; no double vowels.
  - "No words already used in other language mnemonic sets".
  - Rules 11–12: if 3 of the first 4 letters repeat in another word, at least 3 other letters must differ. For example, "atono" being in the list rejects "atomo", but "atomico" is allowed.
- **Czech**
  - 4–8 letters, first 4 unique.
  - No diacritics ("the hardest task, because one third of all Czech letters has diacritical marks").
  - Only nouns, verbs and adverbs, all in basic form.
  - No personal or place names; no 1-letter-difference pairs.
  - No overlap with the English, Italian, French or Spanish lists, with diacritics folded before comparing.
- **Portuguese**
  - First 4 characters unique; no accents.
  - No complex verb forms; no plurals unless there is no singular.
  - "No words with double spelling"; no homophones.
  - No offensive or negative words.
  - No words used in other lists.
  - Words spelled differently in Brazil and Portugal are excluded.
  - No 1-letter-difference pairs.

**Own measurements on the official lists [Own computation]**
- 100 words appear in both the English and French BIP-39 lists (for example abandon, animal, badge, bicycle, brave, canal, capable). None of the 100 is at the same index. Method: set intersection of [english.txt](https://github.com/bitcoin/bips/blob/master/bip-0039/english.txt) and [french.txt](https://github.com/bitcoin/bips/blob/master/bip-0039/french.txt).
- 1,275 of the 2,048 Chinese Simplified and Traditional entries are the same character, and all 1,275 sit at the same index. Method: comparison of [chinese_simplified.txt](https://github.com/bitcoin/bips/blob/master/bip-0039/chinese_simplified.txt) and [chinese_traditional.txt](https://github.com/bitcoin/bips/blob/master/bip-0039/chinese_traditional.txt).
- Share of words with another word at Damerau–Levenshtein (DL) distance 1. DL distance counts single-letter substitutions, insertions, deletions and adjacent swaps.
  - English BIP-39: 36.8% (for example car/cat).
  - French BIP-39 (after NFKD): 0.2%.
  - SLIP-39: 0%.
  - All three have fully unique 4-letter prefixes, except French at 1,919/2,048, where accents count as distinct.
  - Sources: [english.txt](https://github.com/bitcoin/bips/blob/master/bip-0039/english.txt), [french.txt](https://github.com/bitcoin/bips/blob/master/bip-0039/french.txt), [SLIP-39 wordlist](https://github.com/trezor/python-shamir-mnemonic/blob/master/shamir_mnemonic/wordlist.txt)

### Inferences
- BIP-39 derives the seed from the text, which is the main reason it can't be translated. Bucks derives from entropy, so that flaw doesn't apply. The parts worth copying are the list-construction rules, not the derivation.
- The per-language rules agree on a de-facto standard:
  1. one canonical dictionary form per lexeme (French: infinitive; Czech: "basic form"; no plurals, feminines or complex verb forms);
  2. no distinction that depends on diacritics (Spanish, Czech, Italian and Portuguese ban or fold accents);
  3. no near-duplicates (1-letter-difference ban; Italian 3-of-4 rule);
  4. no homophones;
  5. no overlap with other lists;
  6. no offensive, negative or religion-specific words.
- For Arabic, point 2 maps to "no distinction that depends on tashkeel or hamza". Point 1 maps to choosing one citation form (see Q5).
- **Tension:** French rule 16 excludes words "belonging to a particular religion". A Quranic list goes the other way by design. The broader BIP-39 tradition prefers religiously neutral vocabulary, so Bucks should state its reasons for the choice explicitly.
- The English/French overlap shows that even official lists did not apply "no shared words" consistently. This matters a lot for Bucks's index-aligned design (see Q6).

### Gaps
- Korean has no special-considerations notes, so I did not check Hangul NFKD decomposition behaviour or how Korean handles prefixes.
- I did not read the original discussions behind each language's PR (e.g. the French issue #152) beyond the rules recorded in the wordlists file.

---

## Q2. What stricter criteria do SLIP-39 and Monero/Electrum-family lists use?

### Takeaway
SLIP-39 is the strictest published standard. Its 1024 words are 4–8 letters, have unique 4-letter prefixes, are at least DL distance 2 apart, and are chosen to minimize similar pronunciation. That standard was reached only at 1024 English words, and SLIP-39 refuses localization outright. Monero and Polyseed show the other approach: they encode the secret directly as list indices, use a per-language prefix length, and handle language detection and normalization in code.

### Cited Findings
**SLIP-39 [Established]**
- Wordlist criteria: "alphabetically sorted"; "No word is shorter than 4 letters"; "No word is longer than 8 letters"; "All words begin with a unique 4-letter prefix"; "only common English words (+ the word 'satoshi')"; "The minimum Damerau-Levenshtein distance between any two words is at least 2"; "The similarity between the pronunciation of any two words has been minimized". A test script checks the criteria. — [SLIP-0039](https://github.com/satoshilabs/slips/blob/master/slip-0039.md)
- Localization: "No localization is supported… Previous attempts with arbitrary wordlists caused lots of confusion among users and decreased interoperability across various implementations." — [SLIP-0039](https://github.com/satoshilabs/slips/blob/master/slip-0039.md)
- Length and checksum: 10 bits per word. A 128-bit secret gives 20 words; a 256-bit secret gives 33 words. The last 3 words are an RS1024 checksum that "guarantees detection of any error affecting at most 3 words and has less than a 1 in 10^9 chance of failing to detect more errors". — [SLIP-0039](https://github.com/satoshilabs/slips/blob/master/slip-0039.md)
- SLIP-39 rejects building on BIP-39 because of its "overly coupled design… and its use of a one-way derivation function". — [SLIP-0039](https://github.com/satoshilabs/slips/blob/master/slip-0039.md)

**Monero (legacy 25-word seed) [Established]**
- The list has 1626 words ("4 bytes -> 3 words. 8 digits base 16 -> 3 digits base 1626"). A 32-byte key gives 24 words plus 1 checksum word, which repeats one of the 24 (`create_checksum_index`). `bytes_to_words(src, …, language_name)` turns key bytes into words in any chosen language list. — [electrum-words.cpp](https://github.com/monero-project/monero/blob/master/src/mnemonics/electrum-words.cpp)
- Each language declares a unique-prefix length: English 3, Japanese 3, Chinese Simplified 1, and Spanish, German, Russian, Esperanto, Lojban, Portuguese, French, Italian, Dutch and English-old 4. — [monero src/mnemonics/*.h](https://github.com/monero-project/monero/tree/master/src/mnemonics)
- Language detection (`find_seed_language`) loops over every language. It tries exact word matches first, then unique-prefix matches, and when it used prefixes it validates the checksum "to avoid false positives due to prefix set being too common". — [electrum-words.cpp](https://github.com/monero-project/monero/blob/master/src/mnemonics/electrum-words.cpp)

**Polyseed (newer Monero seed) [Established]**
- 2048-word lists and 16 words. Word 1 is an 11-bit checksum; 150 bits are the secret seed. The checksum is a Reed–Solomon code over GF(2048): "All single-word errors can be detected and all single-word erasures can be corrected without false positives." — [polyseed README](https://github.com/tevador/polyseed)
- Latin-alphabet languages need only the first 4 characters. "French and Spanish seeds can be input with or without accents." The lists are BIP-39 lists "with a few minor changes". The library needs `u8_nfc` and `u8_nfkd` functions supplied by the caller. It supports the 10 BIP-39 languages only, with no Arabic. — [polyseed README](https://github.com/tevador/polyseed), [polyseed src/lang.c](https://github.com/tevador/polyseed/blob/master/src/lang.c)

**Electrum v2 seeds [Established]**
- `normalize_text` does NFKD, then lowercases, then removes all combining characters ("remove accents"), then collapses whitespace, then removes whitespace between CJK characters. — [electrum/mnemonic.py](https://github.com/spesmilo/electrum/blob/master/electrum/mnemonic.py)
- The seed is PBKDF2-HMAC-SHA512 over `normalize_text(mnemonic)` with salt `b'electrum' + passphrase`. The code comments say "Seed derivation does not follow BIP39" and "Mnemonic phrase uses a hash based checksum, instead of a wordlist-dependent checksum". Bundled lists: en, es, ja, pt, zh. — [electrum/mnemonic.py](https://github.com/spesmilo/electrum/blob/master/electrum/mnemonic.py)

**WLIPs [Established]**
- WLIPs ("Word List Improvement Proposals") was the community repo BIP-39 maintainers pointed people to from 2019–2021. It proposes building each language's list by translating a "preliminary word list" (a master English list) and grouping languages by character set. — [p2w34/wlips README](https://github.com/p2w34/wlips); [luke-jr on PR #863](https://github.com/bitcoin/bips/pull/863)

**Electrum's normalizer on Arabic [Own computation]**
- Because NFKD decomposes أ إ آ ؤ ئ into base letter + combining hamza or madda, Electrum's normalizer folds أَمَرَ→امر, إِعْلان→اعلان, آمَنَ→امن, سُؤال→سوال, سائِل→سايل and مُؤْمِن→مومن.
- It leaves ٱللَّه as ٱلله, because alef wasla U+0671 has no decomposition.
- It never folds ى/ي, ة/ه, or Persian ی/ک.
- Method: `normalize_text` applied to sample strings. Decomposition data from the [Unicode Character Database 16.0](https://www.unicode.org/Public/16.0.0/ucd/UnicodeData.txt).

### Inferences
- SLIP-39 is the strongest benchmark for "mutually distinct", and it reached that with 1024 English words. Bucks wants 8× as many words, in a language whose roots are mostly 3 letters, under a meaning constraint. The SLIP-39 standard (DL ≥ 2 on top of unique prefixes) is therefore unlikely to be reachable as is (see Q5 and Q7 numbers). A realistic Arabic version is DL ≥ 2 measured on the normalized key, plus a separate check on handwriting confusability.
- Monero shows that prefix length can be set per language (Chinese 1, English 3, others 4). An Arabic list should set its own prefix rule rather than copy "first 4 letters".
- Monero, Polyseed and Bucks all encode the entropy as indices, so the same secret can be shown in any language. Monero's code does this today. Monero's languages are not meaning-aligned, though. Bucks's meaning alignment is new; the closest prior art is the TZUR draft (Q6).
- Generic "strip combining marks" normalizers (Electrum style) only partly work for Arabic. They catch tashkeel and hamza on carriers by accident of NFKD, but miss ٱ, ى, ة, Persian ی/ک and tatweel. Arabic needs an explicit folding table (Q4).

### Gaps
- I did not find a published design document explaining how Monero's 1626-word lists were chosen.
- I did not confirm whether Monero's non-English lists are translations of the English list.

---

## Q3. Has anyone proposed or published an Arabic (or Persian/Urdu) BIP-39 word list, and what problems were raised?

### Takeaway
One Arabic BIP-39 PR existed (#1925, 2025). It was closed within two days, mainly because BIP-39 accepts no new lists, with reviewers also noting that the words were stored reversed and the Arabic variety was unclear. Three Persian PRs (2018–2019) were closed in 2021, and review pointed out shared 3-letter prefixes and 1-letter-difference pairs. In 2026 the TZUR project published an index-aligned Arabic display list and an open BIP draft. Its Arabic list contains exactly the normalization hazards Bucks must avoid.

### Cited Findings
**Arabic PR #1925 [Established]**
- "BIP39: Add Arabic Language Support", PR #1925 by qusaismael. Opened 2025-08-10, closed 2025-08-12. — [PR #1925](https://github.com/bitcoin/bips/pull/1925)
- Reviewer comment: "all the words in `arabic.txt` are reversed, but Arabic is a right-to-left (RTL) language". Also "very few (if any) widely used Arabic Bitcoin wallets", and "it's unclear which variant of Arabic is being used here (e.g., Modern Standard Arabic, a specific dialect, etc.)". — [PR #1925](https://github.com/bitcoin/bips/pull/1925)
- Closing comment: "the BIP39 authors no longer support further wordlists except for the English one, in order to maximize interoperability". — [jonatack on PR #1925](https://github.com/bitcoin/bips/pull/1925)

**Persian PRs [Established]**
- Persian PR #863 (2019) listed its rules:
  1. 4–8 letters;
  2. unique first 4 letters;
  3. no words with "گچپژ" "so these words can use in Arabic language as well";
  4. sorted by the Persian alphabet;
  5. proper nouns allowed;
  6. "No words with more than one part".
  - Closed 2021 and redirected to WLIPs. — [PR #863](https://github.com/bitcoin/bips/pull/863)
- Review of Persian PR #723 (2018–19) by msnhdr. — [PR #723](https://github.com/bitcoin/bips/pull/723)
  - "remove words that begin with identical three letters, as auto-compelete keyboards cannot differentiate them easily". Examples: نیک, نیکچهر, نیکدل…, and about 30 words starting مهر….
  - "remove words that differ only by one letter or sound too similar" (هومن/هوتن/هورتن).
  - Remove uncommon words (نساک, هلیله).
  - Also recommended "run a script to detect and remove words that start with 3 identical letters".
- On Persian PR #858, junderw said he personally knew of "at least 500BTC spread across 15 users or so that have been lost or nearly lost… due to the user not having strong English skills". This is an anecdote, not data. — [PR #858](https://github.com/bitcoin/bips/pull/858)
- My search of bitcoin/bips found no Urdu PR. Hebrew PRs ([#1138](https://github.com/bitcoin/bips/pull/1138), [#1565](https://github.com/bitcoin/bips/pull/1565)) were also closed. — [gh search of bitcoin/bips PRs](https://github.com/bitcoin/bips/pulls?q=is%3Apr+wordlist)

**TZUR registry and BIP draft (2026) [Established]**
- The TZUR registry and its BIP draft ([PR #2200](https://github.com/bitcoin/bips/pull/2200), open, 2026) ship 30 index-paired "display" lists, including arabic.txt, farsi.txt, urdu.txt and hebrew.txt. — [bip39-wordlists-tzur](https://github.com/osem23/bip39-wordlists-tzur)
- Its Arabic list (as the registry reports it):
  - 1,970 distinct 4-character prefixes out of 2,048; largest collision group is 3. — [prefix-statistics.md](https://github.com/osem23/bip39-wordlists-tzur/blob/main/docs/prefix-statistics.md)
  - "No tashkeel (diacritics) are used"; "Hamza variations (أ إ ؤ ئ ء) are distinct characters". — [encoding-notes.md](https://github.com/osem23/bip39-wordlists-tzur/blob/main/validation/encoding-notes.md)
  - 123 entries (6.0%) are not NFKD-stable. — [encoding-notes.md](https://github.com/osem23/bip39-wordlists-tzur/blob/main/validation/encoding-notes.md)
  - 11 groups collide under diacritic folding, e.g. `إعلان` / `اعلان`. — [encoding-notes.md](https://github.com/osem23/bip39-wordlists-tzur/blob/main/validation/encoding-notes.md)
- The Farsi list contains 556 ZWNJ characters across 534 words, and NFC/NFKD keep ZWNJ. — [encoding-notes.md](https://github.com/osem23/bip39-wordlists-tzur/blob/main/validation/encoding-notes.md)
- The draft's author says it ships in production in the TZUR Wallet suite (iPhone and Windows). — [PR #2200](https://github.com/bitcoin/bips/pull/2200)

**My audit of the TZUR Arabic list [Own computation]** (source: [tzur arabic.txt](https://github.com/osem23/bip39-wordlists-tzur/blob/main/wordlists/tzur-original/arabic.txt))
- 147 entries change under NFKD. This differs from the registry's figure of 123, possibly because the list version changed.
- After removing tashkeel: 2,048 distinct.
- After folding أإآٱ→ا: 2,037 distinct (11 collisions: اعلان/إعلان, اخلاق/أخلاق, اضافي/إضافي, اصبع/إصبع, اداة/أداة, الزام/إلزام…). These are the same word spelled with and without hamza, stored at two different indices.
- After also folding ة→ه: 2,034 distinct. New collisions are تحفة/تحفه and دراجة/دراجه, again the same word.
- After also folding ؤ→و, ئ→ي and dropping ء: 2,032 distinct.
- 67.3% of normalized entries have another entry at DL distance 1 (French BIP-39: 0.2%).
- 173 entries in 84 groups share a dotless skeleton (rasm), e.g. تحت/يجب/بحث, يوم/ثوم/نوم, قرص/فرض/قرض, عبر/غير, البوم/اليوم.
- It also includes تقدم/يقدم, the same verb in two different persons.

### Inferences
- **What the past attempts got wrong:**
  - storing words in display order instead of logical order (#1925);
  - unclear variety (MSA vs Classical vs dialect);
  - long clusters sharing a 3-letter prefix (Persian);
  - 1-letter-difference pairs;
  - and, in the most complete Arabic list published to date (TZUR), the same word appearing twice in different hamza or ta-marbuta spellings, plus inflected variants of one lexeme.
- A user typing on a normal keyboard without hamza would hit an ambiguous match for those 11–16 TZUR entries. The TZUR draft's own rule then forces the wallet to reject the word and ask the user to pick. A Bucks list should be built so this never happens (Q4 rules A1–A3).
- **Bucks should declare its variety explicitly**, e.g. "Classical/Quranic Arabic vocabulary in standard modern (imla'i) spelling, not Uthmani script", to answer the "which Arabic" objection.

### Gaps
- I found no Arabic list shipped by a major wallet (Trezor, Ledger, Electrum, Monero, Polyseed). TZUR's production claim comes only from its author.
- I found no Urdu PR in bitcoin/bips. I did not audit TZUR's urdu.txt or farsi.txt for Arabic-script cross-collisions.

---

## Q4. What Arabic-specific issues apply, and what normalization pipeline and distinctness rules make a list robust to typing without diacritics?

### Takeaway
Unicode normalization (NFC/NFKC/NFKD) does **not** solve the Arabic problem. It never merges ى/ي, ة/ه, ٱ/ا, or Persian ی ک ہ with their Arabic counterparts, and it never removes tatweel or tashkeel. NFKD only *decomposes* أ إ آ ؤ ئ, and Electrum-style stripping then removes the hamza by accident. Bucks needs an explicit, versioned Arabic folding function (the same kind of folding used in Arabic search engines and NLP). Every list entry must stay unique under the most aggressive fold, and should also be checked for edit distance and dot-only (rasm) confusability.

### Cited Findings
**Unicode facts [Established]** (data from the [Unicode Character Database 16.0](https://www.unicode.org/Public/16.0.0/ucd/UnicodeData.txt); normalization forms per [UAX #15](https://www.unicode.org/reports/tr15/); the hamza decompositions are also confirmed in [Unicode 16 core spec ch. 9](https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-9/))
- Canonical decompositions (the same under NFD and NFKD):
  - آ U+0622 → ا + U+0653 (maddah)
  - أ U+0623 → ا + U+0654 (hamza above)
  - إ U+0625 → ا + U+0655 (hamza below)
  - ؤ U+0624 → و + U+0654
  - ئ U+0626 → ي + U+0654
  - NFC/NFKC recompose them.
- No decomposition at all: ٱ alef wasla U+0671, ى alef maksura U+0649, ی Farsi yeh U+06CC, ک keheh U+06A9, ہ heh goal U+06C1, ة teh marbuta U+0629. **No Unicode normalization form will ever equate these with ا, ي, ك, ه.**
- Presentation forms fold only under compatibility forms (NFKC/NFKD): ﻻ U+FEFB → ل+ا; ﺍ U+FE8D → ا; ﮎ U+FB8E → ک (note: to *keheh*, not kaf).
- Tatweel U+0640 is category Lm (modifier letter). It survives every normalization form.
- Harakat (U+064B–U+065F), superscript alef U+0670 and Quranic annotation marks (e.g. U+06D6, U+06E1) are category Mn (non-spacing marks). They survive normalization but are removed by "strip combining marks".
- Arabic-Indic digits U+0660–U+0669 and Extended Arabic-Indic digits U+06F0–U+06F9 are separate digit sets (Nd).
- Unicode 16 ch. 9 states:
  - "U+0622 is given a canonical decomposition to the sequence of alef followed by the combining maddah".
  - "FARSI YEH is exactly like U+0649 ARABIC LETTER ALEF MAKSURA in final and isolated forms, but exactly like U+064A ARABIC LETTER YEH in initial and medial forms".
  - "The isolated forms of U+0647 ARABIC LETTER HEH and U+06C1 ARABIC LETTER HEH GOAL both look like U+06D5 ARABIC LETTER AE".
  - lam-alef ligatures "are considered obligatory".
  - More Quranic annotation signs are encoded in Arabic Extended-A (U+08A0–U+08FF) and Extended-B (U+0870–U+089F).
  - [Unicode 16.0 core spec ch. 9](https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-9/)

**Standard Arabic folding in search and NLP tools [Established]**
- Apache Lucene's ArabicNormalizer: "Normalization of hamza with alef seat to a bare alef"; "teh marbuta to heh"; "dotless yeh (alef maksura) to yeh"; "Removal of Arabic diacritics (the harakat)"; "Removal of tatweel". — [Lucene ArabicNormalizer](https://lucene.apache.org/core/4_2_1/analyzers-common/org/apache/lucene/analysis/ar/ArabicNormalizer.html)
- CAMeL Tools (NYU Abu Dhabi, Habash lab) provides `normalize_unicode` (NFC/NFKC), `normalize_alef_ar` ("various Alef variations to plain a Alef"), `normalize_alef_maksura_ar` (alef maksura → yeh) and `normalize_teh_marbuta_ar` (teh marbuta → heh). — [camel_tools.utils.normalize](https://camel-tools.readthedocs.io/en/latest/api/utils/normalize.html)

**Undiacritized ambiguity [Established]**
- "Arabic is orthographically highly ambiguous. This is due to its orthographic conventions where diacritical marks are often omitted… MSA can have 12 different morphological analyses per word on average (Pasha et al., 2014)." — [Obeid, Inoue & Habash 2022, Camelira](https://arxiv.org/abs/2211.16807)

**Persian/Urdu letters [lower confidence: search snippet only, not fetched]**
- "Iranian Standard ISIRI 6219:2002 requires using U+06A9 ARABIC LETTER KEHEH for the Persian Kaf." The ISIRI 2901:1994 keyboard has both KEHEH and KAF (kaf on a shifted key). — [Unicode mailing list, 2002](https://unicode.org/mail-arch/unicode-ml/y2002-m12/0203.html); [W3C ALREQ](https://www.w3.org/TR/alreq/)

**Right-to-left handling [Established]**
- RTL scripts write digits left-to-right, so a numbered Arabic phrase is bidirectional text. The isolate controls LRI/RLI/FSI/PDI (U+2066–U+2069) were added in Unicode 6.3, and "characters within an isolate cannot affect the ordering of characters outside it". — [UAX #9](https://www.unicode.org/reports/tr9/); [Wikipedia summary](https://en.wikipedia.org/wiki/Unicode_bidirectional_algorithm)
- PR #1925's list stored the words reversed (see Q3). — [PR #1925](https://github.com/bitcoin/bips/pull/1925)

**TZUR draft input rules [Established, in an open draft]**
- If a lossy fold "maps a token to more than one wordlist entry, the wallet MUST reject the token and ask the user to disambiguate. It MUST NOT silently pick one entry." — [PR #2200 draft](https://github.com/bitcoin/bips/pull/2200)
- ZWNJ handling at lookup must match how the stored list was written. — [PR #2200 draft](https://github.com/bitcoin/bips/pull/2200)
- Wallets "SHOULD treat any character outside the wordlist's expected script block as a build-time error" (the homograph supply-chain attack). — [PR #2200 draft](https://github.com/bitcoin/bips/pull/2200)

**How much folding collapses Quranic vocabulary [Own computation]**
- Data: [Quranic Arabic Corpus morphology v0.4, mustafa0x fork](https://github.com/mustafa0x/quran-morphology).
- Folding levels:
  - L1 = remove harakat, U+0670, Quranic marks U+06D6–U+06ED and tatweel; ٱ→ا
  - L2 = L1 + أإآ→ا
  - L3 = L2 + ى/ی→ي, ک→ك, ہ→ه
  - L4 = L3 + ة→ه
  - L5 = L4 + ؤ→و, ئ→ي, drop ء
- Distinct word forms: 18,993 diacritized → 14,972 (L1) → 14,712 (L2) → 14,695 (L3) → 14,657 (L4) → 14,543 (L5).
- Distinct lemmas (all parts of speech): 4,776 → 4,335 (L1) → 4,270 (L4) → 4,255 (L5).
- Removing diacritics alone does most of the collapsing. Alef/ya/ta folding adds a smaller but non-zero amount.

**Dot-only (rasm) confusability [Own computation]**
- Method: map each letter to its dotless skeleton by position. ب ت ث and non-final ن ي → ٮ; ج خ → ح; ذ → د; ز → ر; ش → س; ض → ص; ظ → ط; غ → ع; ف/ق → ڡ.
- 4,270 folded Quranic lemmas reduce to 3,750 skeletons; 938 lemmas share a skeleton with another. Examples: يسر/بشر/نشر/تسر; قيل/قبل/قتل/فيل; كنز/كبر/كثر.
- Data: [quran-morphology](https://github.com/mustafa0x/quran-morphology)

### Inferences (own reasoning; proposed, checkable rules)

**Proposed fold key F(·)**, applied identically when building the list and when matching user input:
1. **Unicode step:** NFKC. This folds presentation forms and lam-alef ligatures and keeps letters composed. Do not use NFKD for matching: it splits أ into ا + U+0654, and code that later strips marks will behave inconsistently.
2. **Remove** U+064B–U+065F, U+0670, U+06D6–U+06ED, U+08D3–U+08FF, tatweel U+0640, ZWNJ/ZWJ U+200C/U+200D, and bidi controls U+200E/U+200F/U+061C/U+202A–U+202E/U+2066–U+2069.
3. **Alef:** أ إ آ ٱ (and U+0672/U+0673) → ا.
4. **Ya:** ى U+0649 and ی U+06CC → ي. **Kaf:** ک U+06A9 → ك. **Ha:** ہ U+06C1, ە U+06D5 and ة U+0629 → ه.
5. **Hamza carriers:** ؤ → و, ئ → ي; then either drop standalone ء or keep it. Pick one, document it, and test both choices for collisions.
6. **Whitespace:** split on any Unicode White_Space (including NBSP U+00A0, U+3000 and Arabic comma ، U+060C if pasted). Collapse runs of whitespace and trim. This avoids the problem in PR #2237.
7. **Version the fold function together with the list**, and record a fold-version identifier with every backup.

**Proposed Arabic list rules** (each one can be checked by script):
- **A1 – unique under the full fold:** F(wᵢ) ≠ F(wⱼ) for every i ≠ j at the most aggressive level (L5). This rules out pairs like TZUR's إعلان/اعلان and تحفة/تحفه.
- **A2 – edit distance:** DL(F(wᵢ), F(wⱼ)) ≥ 2 for all pairs (SLIP-39 standard), or at minimum for all pairs where either word is 4 letters or shorter. Short Arabic words are where 1-letter collisions cluster.
- **A3 – dot-only confusability:** no two entries share a rasm skeleton. This guards against handwritten backups where dots are lost or misplaced. A weaker version: allow a shared skeleton only if DL ≥ 2 on F.
- **A4 – no clitics:** no entry starts with ال, or with a proclitic و ف ب ل ك س that turns another entry into a phrase. No entry equals another entry plus a pronoun suffix (ه ها هم هن ك كم نا ي). Recommended: list entries are bare stems; at input time, strip a leading ال only if the result is a unique match.
- **A5 – one lexeme per entry:** no two entries are inflections of the same lemma or root-and-pattern (TZUR's تقدم/يقدم). One citation form for all entries (see Q5).
- **A6 – allowed characters:** the 28 letters plus ء أ إ آ ؤ ئ ة ى only. No Persian/Urdu letters, presentation forms, digits, ZW characters or tashkeel in the stored canonical form. Store NFC (fold-equivalent).
- **A7 – logical order:** entries are stored in logical (typing) order. Add a round-trip test that renders each word and checks its first code point equals the first typed letter. This catches the PR #1925 reversal.
- **A8 – prefix rule set per language (Monero-style):** most Arabic verbs are 3–4 letters, so "first 4 letters" usually means "the whole word". Require prefix uniqueness at n = 3 for words of 4+ letters, and that no full entry is a prefix of another entry. Don't promise 4-letter autocomplete.
- **A9 – length:** 3–7 letters after F. Ban 1–2-letter forms (weak-verb imperatives like ق, ع, قِ, كُل; 55 of the folded Quranic verb lemmas are 2 letters). They are fragile in handwriting and easy to mistake for clitics.
- **A10 – display:** show each word as its own bidi-isolated unit (FSI…PDI or a separate element with dir="rtl"). Put the number in its own LTR cell with Latin or Arabic-Indic digits, never inside the word. Line-wrap only between words.
- **A11 – undiacritized meaning:** the "unique meaning" of an entry is the meaning of its *undiacritized* string. If that string has a common second reading (عَلِمَ "knew" / عَلَّمَ "taught" / عِلْم "knowledge"), the entry is ambiguous and should be dropped or explicitly assigned one reading. Never include a second entry whose diacritized form is the other reading.

### Gaps
- I did not obtain a primary source for exact key layouts. That covers which characters a standard Arabic (Windows 101 / macOS / Android Gboard) keyboard produces, e.g. whether the لا key emits U+0644 U+0627 or U+FEFB, and whether ى and ة sit on unshifted keys. The NFKC step covers the ligature either way.
- I could not quantify how often real users omit hamza or confuse ى/ي and ة/ه when typing. Search engines and NLP tools fold these routinely, which suggests it is common, but I have no figure.
- Arabic Extended-A/B mark ranges: I removed U+08D3–U+08FF in the proposal but did not check every code point in U+0870–U+089F.

---

## Q5. For a verbs-only list, which citation form is best, and how much do undiacritized homographs collapse distinct meanings?

### Takeaway
A Quranic verbs-only list **cannot reach 8192 entries**. The Quran has 1,473 distinct verb lemmas (940 verb roots), which fold to only 1,415 distinct undiacritized strings. About 724 of those can be kept if no two may be within one edit of each other (SLIP-39-style DL ≥ 2). Even all Quranic lemmas of every part of speech give only 4,270 folded strings. The traditional 3rd-person masculine singular perfect citation form (كتب) collapses the Form I/II pairs that matter most (علم, كذب, نزل…). It also collides with nouns in 270 cases.

### Cited Findings
**Counts from the Quranic corpus [Own computation]** (all from [Quranic Arabic Corpus v0.4 morphology, mustafa0x fork](https://github.com/mustafa0x/quran-morphology), which reproduces [corpus.quran.com](http://corpus.quran.com) data with fixes)
- Size: 77,429 words; 4,776 distinct lemmas; 1,651 distinct roots (all parts of speech).
- Verbs (POS = V): 1,473 distinct lemmas from 940 roots.
- Verb lemmas by form: I 670, II 171, III 63, IV 263, V 81, VI 36, VII 15, VIII 101, IX 2, X 71.
- Distinct verb strings after each folding level:

  | Level | Distinct strings | Collision groups | Lemmas in collisions |
  |---|---|---|---|
  | L1 (diacritics removed) | 1,424 | 49 | 98 |
  | L2 (+alef) | 1,418 | 54 | 109 |
  | L3/L4 (+ya, ta marbuta) | 1,415 | 57 | 115 |
  | L5 (+hamza carriers) | 1,414 | 58 | — |

- Example verb collisions after folding:
  - **علم** ← عَلِمَ "knew" (I) | عَلَّمَ "taught" (II)
  - **كذب** ← كَذَبَ "lied" | كَذَّبَ "denied"
  - **نزل** ← نَزَلَ "descended" | نَزَّلَ "sent down"
  - **امن** ← أَمِنَ "was safe" | آمَنَ "believed"
  - **اذن** ← أَذِنَ | أَذَّنَ | آذَنَ (3 lemmas)
  - **اتي** ← أَتَى "came" | آتَى "gave"
  - also كفر/كفّر, صدق/صدّق, ذكر/ذكّر, قتل/قتّل, عرف/عرّف, بلغ/بلّغ, سخر/سخّر, كبر/كبّر, كره/كرّه, فرق/فرّق, قدم/قدّم, اتبع (VIII)/أتبع (IV).
- Collisions with nouns: 270 of the 1,473 verb lemmas fold to the same string as a noun lemma, e.g. عَلِمَ~عِلْم, عَبَدَ~عَبْد, هَدَى~هُدًى, خَلَقَ~خَلْق, كَفَرَ~كُفْر, ذَهَبَ~ذَهَب, رَزَقَ~رِزْق.
- Lengths of folded verb strings: 2 letters 55, 3 letters 483, 4 letters 602, 5 letters 196, 6 letters 78, 7 letters 1.
- Neighbours: 79.4% of folded verb strings have another verb at DL distance 1. Only 1,096 distinct 3-letter prefixes exist among 1,415 strings.
- Largest subset with pairwise DL ≥ 2 (greedy, most frequent first; this is a lower bound on the true maximum):
  - Quranic verb lemmas: **724** of 1,415.
  - All Quranic lemmas: **1,818** of 4,270.
  - All Quranic inflected word forms including clitics: **6,814** of 14,657.
- Dot-only confusability: 1,415 folded verb strings give 1,284 dotless skeletons; 245 verbs share a skeleton (يسر/بشر/تسر/بسر; كنز/كبر/كثر; يميز/تميز/نمير; نفر/نقر/تقر).

**Undiacritized ambiguity [Established]**
- MSA averages about 12 morphological analyses per undiacritized word. — [Camelira, Obeid/Inoue/Habash 2022, citing Pasha et al. 2014](https://arxiv.org/abs/2211.16807)

**Precedent for a single citation form [Established]**
- French BIP-39 used "Only infinitive verbs" and banned reflexive forms. Czech used "basic form". Italian and Portuguese banned "complex verb forms". — [bip-0039-wordlists.md](https://github.com/bitcoin/bips/blob/master/bip-0039/bip-0039-wordlists.md)

### Inferences (own reasoning)
**Comparing citation forms:**
- **3ms perfect (فَعَلَ, e.g. كتب)**
  - For: it is the traditional dictionary citation form and the closest match to French's "infinitive".
  - Against: it is the shortest form (mostly 3 letters), so it has the most DL-1 neighbours and the most Form I/II/IV collisions without diacritics. It also collides with nouns (علم, عبد, خلق).
  - **Usable only if you keep at most one lemma per folded string** and drop strings whose noun reading is more common than the intended verb.
- **Masdar (verbal noun: تعليم, إيمان, تكذيب)**
  - For: it carries the derived-form pattern in its consonants and long vowels. Without diacritics, Form II تفعيل, Form IV إفعال (→ افعال after folding), Form VIII افتعال and Form X استفعال are distinct from each other and from Form I. So علم ("knowing", Form I masdar) vs تعليم ("teaching") separates cleanly. It is also longer (4–7 letters), which helps prefix and edit-distance rules.
  - Against: Form I masdars are irregular and often equal common nouns. Many end in ة (folding hazard) or begin with إ (folding hazard). Many masdars of Quranic verbs never appear in the Quran. Grammatically masdars are nouns, so "verbs-only" becomes "actions-only".
- **Imperfect 3ms (يفعل)**
  - Adds a ي prefix. Form I and Form II still collide (يعلم = yaʿlamu / yuʿallimu), so it gains nothing over the perfect.
  - All entries starting with ي/ت/ن would also crowd the prefix space.
- **Imperative**
  - Worst option: weak verbs shrink to 1–2 letters (قِ, عِ, كُلْ, قُلْ), forms are irregular, and imperatives are easily read as commands to the user.

**Verdict on verbs-only from the Quran:** "unique meaning + verbs-only + Quranic + 8192" is impossible as stated.
- Unique-meaning Quranic verbs give at most about 1,415 undiacritized strings, about 724 at DL ≥ 2, and fewer once rasm and noun-homograph pruning are applied.
- Even every Quranic lemma (about 4,270 folded; about 1,818 at DL ≥ 2) is below 8192.
- Reaching 8192 from Quranic text alone requires inflected forms (different person, tense or clitic). That breaks "unique meaning" and creates exactly the near-duplicates every list standard bans.

**Realistic options:**
- (a) Use Quranic vocabulary of all parts of speech, which fits a 2048 list (and maybe 4096 if DL ≥ 2 is relaxed). This would need a protocol change from 8192.
- (b) Keep 8192 and draw from Classical or modern dictionaries of Quranic roots (words built from Quranic roots but not necessarily in the text). Keep verbs (as masdars or perfects) as a theme rather than a hard rule.
- (c) Keep the Quranic list for display only, with a different base list. I don't recommend this: it adds a second list to maintain.

### Gaps
- I did not count verb or masdar inventories in large Classical or MSA lexicons (Lisan al-Arab, Hans Wehr, Almaany, or the CAMeL/CALIMA-star databases). That count is needed to confirm option (b) can reach 8192 at DL ≥ 2.
- The corpus lemma inventory has quirks: some lemmas are listed in the imperfect (e.g. يَشْعُرُ), and some splits or merges are editorial. Counts could move by a few percent with another lemmatization.
- The greedy DL ≥ 2 numbers are lower bounds. An exact maximum set (maximum independent set) was not computed.

---

## Q6. What rules does each index-aligned translation list need, and what prior art exists?

### Takeaway
The only prior art for meaning-aligned lists is the 2026 TZUR BIP draft (PR #2200, open). It specifies single-token entries with no spaces or hyphens, a bijective mapping, NFC storage, a (language, version, SHA-256) identifier, rejection of ambiguous lossy folds, and no silent auto-detection. Monero, Polyseed and Bucks share the entropy-based encoding that makes translation safe. BIP-39's lists are not aligned, and they overlap across languages at *different* indices: English/French share 100 words. That overlap is the one hazard Bucks must rule out by construction.

### Cited Findings
**TZUR draft (PR #2200) [Established, in an open draft]** — all from [PR #2200 / bip-multilingual-mnemonic.md](https://github.com/bitcoin/bips/pull/2200)
- Display wordlists are "index-parallel to the canonical English BIP-39 wordlist"; "the bidirectional mapping is bijective across all 2048 entries".
- Entries must "Have no embedded whitespace and no hyphen or dash character inside any entry". Forbidden characters include U+0020, U+3000, any other `White_Space`, U+002D, U+2013, U+2014, U+2011 and U+00AD. The reason: "an entry containing whitespace cannot survive the paper-backup round trip".
- Multi-word concepts are glued into one token, e.g. Hebrew `רופאשיניים`, Turkish `hindistancevizi`, Indonesian `kebunbinatang`.
- Lists are stored in NFC. Every token and the list must be normalized to the same form before comparing.
- Each backup carries the triple (BCP-47 language tag, version string, SHA-256 of the list file). The language tag is `ar` for Arabic. Lists are immutable per version.
- Lossy folds that map a token to more than one entry must be rejected (see Q4). ZWNJ handling must match how the list was written.
- Legacy and locale-mapped readings "must not" be auto-detected silently; the wallet should ask the user which kind of mnemonic it is.
- Homograph supply-chain risk, e.g. "an Arabic ا (U+0627) replacing a Persian ا from a different code-point". Mitigations: SHA-256 verification and an "outside the expected script block" check.
- 4-character prefix uniqueness is only a SHOULD. Across 30 languages, "4-character prefix uniqueness holds for only one language (Korean)". — [prefix-statistics.md](https://github.com/osem23/bip39-wordlists-tzur/blob/main/docs/prefix-statistics.md)

**Language detection and cross-list overlap**
- [Established] Spanish BIP-39 rule: no words shared with any other list, so the language can be detected from one word. Italian, Czech and Portuguese repeat "no words already used in other language mnemonic sets". — [bip-0039-wordlists.md](https://github.com/bitcoin/bips/blob/master/bip-0039/bip-0039-wordlists.md)
- [Own computation] English and French BIP-39 still share 100 words, all at different indices. Chinese Simplified and Traditional share 1,275 characters, all at the same index. — [english.txt](https://github.com/bitcoin/bips/blob/master/bip-0039/english.txt), [french.txt](https://github.com/bitcoin/bips/blob/master/bip-0039/french.txt), [chinese lists](https://github.com/bitcoin/bips/tree/master/bip-0039)
- [Established] Monero detects the language by trying each list: exact words, then unique prefixes, then the checksum. It encodes the secret with `bytes_to_words(…, language_name)`, so one key can be shown in any language. — [electrum-words.cpp](https://github.com/monero-project/monero/blob/master/src/mnemonics/electrum-words.cpp)
- [Established] BIP-39 translations are not possible because "translating the mnemonic to a different wordlist necessarily creates a completely different seed". — [bip-0039.mediawiki §Shortcomings](https://github.com/bitcoin/bips/blob/master/bip-0039.mediawiki)
- [Established] WLIPs proposed building every language list by translating a master list. — [p2w34/wlips](https://github.com/p2w34/wlips)

### Inferences (own reasoning; proposed rules for each translation list Tₗ)
- **T1 – bijection:** exactly 8192 entries. Entry i has the same meaning as Arabic entry i. Automated checks: count, no blanks, no duplicates.
- **T2 – unique within the language under that language's own fold:** apply NFKC, case-fold, remove accents (Spanish/Portuguese/French-style folding), and for Arabic-script languages use the Q4 fold. Folded entries must be unique, not just exact strings. TZUR's Swedish läger/lager and Vietnamese được/đuốc show why. — [TZUR encoding-notes](https://github.com/osem23/bip39-wordlists-tzur/blob/main/validation/encoding-notes.md)
- **T3 – single token:** no spaces, hyphens, apostrophes or dots. For verbs, drop "to" ("know", not "to know"). Where a language needs several words, pick a one-word synonym first and glue as a last resort.
- **T4 – distinctness inside each language:** at minimum, DL ≥ 1 after folding (that is, uniqueness) plus no 1-letter-difference pairs among words of 4 letters or fewer. Prefix uniqueness should be measured and published per language (Monero/TZUR style), not promised. At 8192 entries with fixed meanings, 4-letter prefix uniqueness is unlikely in most languages. TZUR reached it only for Korean, even at 2048.
- **T5 – cross-list rule (the key one for index alignment):** a folded string may appear in two lists only at the **same index**. A string that appears at index i in one list and index j ≠ i in another lets a phrase silently decode to a different secret under the wrong language. That is exactly the English/French BIP-39 situation. Check every pair of lists, and especially Arabic vs Persian vs Urdu (shared script, many Arabic loanwords) and Latin-script lists (English/French/Spanish/Indonesian/Malay/Turkish).
- **T6 – meaning must survive translation:** two different Arabic meanings often translate to the same word in a target language. For example, عَلِمَ and عَرَفَ both become "know" in English, and many Form I/IV verb pairs collapse into one English verb. The 8192 meanings must therefore be chosen so they stay distinct in *every* target language at once. This is a joint selection problem, not a per-language one. Expect it to be the single biggest constraint at 8192.
- **T7 – language detection:**
  1. Use the script block first (Arabic vs Latin vs CJK).
  2. Within a script, accept a language only if every word resolves in that list.
  3. If several languages resolve, use the 15/17-bit checksum to break the tie. A wrong-language reading passes only with probability about 2⁻¹⁵ or 2⁻¹⁷, if T5 is violated.
  4. If still ambiguous, ask the user. Never pick silently (TZUR draft).
- **T8 – versioning:** store (language, list version, fold version, SHA-256) with each backup.
- **T9 – Arabic-script sister lists (fa, ur):** either forbid Persian/Urdu code points in the Arabic list and fold ی ک ہ on input (Q4), or ship separate fa/ur lists. Either way, run T5 across all three after folding.
- **T10 – display:** every list is shown as bidi-isolated tokens with explicit numbering (Q4, rule A10). Mixed-script phrases are rejected.

### Gaps
- I did not find a Bucks-like scheme in production with meaning-aligned lists across many languages. The TZUR suite's production use comes only from its author, and TZUR aligns to English at 2048, not 8192.
- I could not measure how many of 8192 meanings survive translation as distinct single words in English or other target languages. This needs an empirical pass on the draft list.
- I did not check whether Monero's non-English lists are meaning-aligned translations.

---

## Q7. What are the trade-offs between 2048, 4096 and 8192 words?

### Takeaway
Going from 2048 to 8192 saves only 1 word at 128 bits (11 vs 12) and 3 at 256 bits (21 vs 24). The real gain is a much stronger checksum: 15/17 bits vs 4/8, so random-error detection improves by a factor of 2¹¹ to 2¹². The cost is needing 4× as many words that stay mutually distinct. For Arabic with distinct meanings that is the binding constraint: even a 2048-word Arabic list (TZUR) already has 67% of words one edit away from another word.

### Cited Findings
- BIP-39 (11 bits per word, 2048 words): the checksum is ENT/32 bits. 128 bits gives 12 words with a 4-bit checksum; 256 bits gives 24 words with an 8-bit checksum. The 256-bit case misses "1-in-256" random errors. — [bip-0039.mediawiki](https://github.com/bitcoin/bips/blob/master/bip-0039.mediawiki)
- SLIP-39 (10 bits per word, 1024 words): 20 words for 128 bits and 33 for 256 bits. The 3-word RS1024 checksum detects all errors in up to 3 words, with a miss chance below 10⁻⁹. — [SLIP-0039](https://github.com/satoshilabs/slips/blob/master/slip-0039.md)
- Polyseed (2048 words): 16 words; one 11-bit Reed–Solomon check word that detects all single-word errors and corrects one erased word. — [polyseed](https://github.com/tevador/polyseed)
- Monero legacy (1626 words): 25 words for a 256-bit key (24 + 1 checksum word). — [electrum-words.cpp](https://github.com/monero-project/monero/blob/master/src/mnemonics/electrum-words.cpp)
- [Own computation] Distinctness gets harder with size. Share of words with a DL-1 neighbour: SLIP-39 (1024) 0%, BIP-39 English (2048) 36.8%, TZUR Arabic (2048) 67.3%. Quranic verbs, all lemmas, and all word forms all fall short of 8192 at DL ≥ 2 (Q5). — [SLIP-39 list](https://github.com/trezor/python-shamir-mnemonic/blob/master/shamir_mnemonic/wordlist.txt), [english.txt](https://github.com/bitcoin/bips/blob/master/bip-0039/english.txt), [tzur arabic.txt](https://github.com/osem23/bip39-wordlists-tzur/blob/main/wordlists/tzur-original/arabic.txt), [quran-morphology](https://github.com/mustafa0x/quran-morphology)
- [Own computation] The 14,657 folded Quranic word forms have only 9,237 distinct 4-letter prefixes. A unique-4-letter-prefix list of 8192 from Quranic text would have to use almost every available prefix, including inflected and clitic forms. — [quran-morphology](https://github.com/mustafa0x/quran-morphology)

### Inferences (own arithmetic and reasoning)

| List size | Bits per word | 128-bit phrase | Checksum | Random-error miss | 256-bit phrase | Checksum | Random-error miss |
|---|---|---|---|---|---|---|---|
| 1024 (SLIP-39) | 10 | 20 words (with metadata) | RS 30-bit | <10⁻⁹ | 33 words | RS 30-bit | <10⁻⁹ |
| 2048 | 11 | 12 words (132 bits) | 4 bits | 1/16 | 24 words (264) | 8 bits | 1/256 |
| 4096 | 12 | 11 words (132) | 4 bits | 1/16 | 22 words (264) | 8 bits | 1/256 |
| 8192 (Bucks) | 13 | 11 words (143) | 15 bits | 1/32,768 | 21 words (273) | 17 bits | 1/131,072 |

- **4096 adds little:** same 4/8-bit checksum as 2048 and only 1–2 fewer words. For a "fill whole words" layout like Bucks's, 8192 is the size that actually buys checksum strength.
- **The checksum detects errors but cannot correct them by brute force.**
  - 128-bit phrase: there are 11 × 8191 ≈ 90,000 single-word substitutions against a 2¹⁵ = 32,768 checksum space, so a typo produces about 2.7 false "valid" candidates on average.
  - 256-bit phrase: 21 × 8191 ≈ 172,000 against 2¹⁷ = 131,072, about 1.3 false candidates.
  - So a single-word error cannot be fixed uniquely by search. If correction matters, an RS-style checksum (Polyseed/SLIP-39) would be needed. That is a protocol change.
- **Memorability and transcription:** each word carries more entropy (13 vs 11 bits), but users must recognise 4× more vocabulary, and the larger list pulls in rarer words. Because the checksum catches more errors, the list's *own* distinctness matters somewhat less for detection. It still matters fully for *recovery*, because users need to know which word they mis-wrote.
- **Size versus the owner's constraints:**
  - At 8192, "unique meaning + DL ≥ 2 + Quranic" is not reachable (Q5), and "verbs only" is far out of reach.
  - At 2048, a Quranic all-parts-of-speech list with unique meanings, A1–A11 and DL ≥ 2 looks feasible (about 1,818 greedy DL ≥ 2 lemmas, more if DL ≥ 2 is applied only to short words).
  - The choice is: change the list size (a protocol change), relax the vocabulary source to Quranic roots or Classical Arabic (keeps 8192), or relax distinctness. I recommend keeping 8192 and sourcing from a Quranic-root dictionary, with Quranic attestation as a preference rather than a requirement, so that A1–A3 and T5–T6 can still be met.

### Gaps
- I found no user studies comparing recall or transcription error rates for 2048 vs 8192-word lists. The memorability claims are reasoning, not data.
- I did not compute the exact maximum DL ≥ 2 subsets, or how many 8192-entry sets satisfy T6 (translation distinctness) for English.
