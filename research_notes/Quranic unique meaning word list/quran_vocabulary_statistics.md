# Quranic Vocabulary Statistics and Feasibility of a Unique-Meaning Seed-Phrase Word List (2048 / 4096 / 8192)

Scope: Quranic Classical Arabic, Hafs, Uthmani text. All figures come from published pages, papers and documentation. No dataset files were downloaded. Where a figure needs computation over the data, the method is described under "Gaps". Abbreviations: QAC = Quranic Arabic Corpus (corpus.quran.com, Kais Dukes, University of Leeds); QuranMorph = Akra, Hammouda & Jarrar (Birzeit University, 2025).

---

## 1. How large is the Quran's vocabulary at each level (tokens, word forms with and without diacritics, lemmas, roots), and why do the sources disagree?

### Takeaway
The Quran has about 77,430 space-separated words (tokens). It has about 19,000 distinct written forms with diacritics, about 14,870 without diacritics, about 12,200 distinct stems, 3,382–4,616 lemmas depending on the lemma scheme (3,680 on the live QAC site), and only about 1,640–1,690 roots. Each step down this ladder cuts the count sharply. Even the most generous "unique meaning" level (lemmas) comes to only a few thousand entries.

### Cited Findings

**Tokens (running words)**
- QAC: "77,430 words of Quranic Arabic." The annotation for each of the 77,430 words was reviewed by two annotators — [Wikipedia: Quranic Arabic Corpus](https://en.wikipedia.org/wiki/Quranic_Arabic_Corpus); the same figure appears in Dukes & Habash, "Morphological Annotation of Quranic Arabic", LREC 2010 — [ACL Anthology L10-1190](https://aclanthology.org/L10-1190/)
- Dukes's PhD thesis uses 77,430 in its methodology chapter ("produced an analysis for 67,516 out of 77,430 words"). Elsewhere it gives 77,429, citing Salih's 12-volume i'rab reference, *al-i'rab al-mufassal* ("detailed linguistic analysis for each of the 77,429 words in the Quran") — [Dukes 2013 thesis, "Statistical Parsing by Machine Learning from a Classical Arabic Treebank", Univ. of Leeds](https://arxiv.org/pdf/1510.07193)
- The QAC release notes (v0.4, 1 May 2011) say "30,895 out of 77,429 words" — [QAC release notes](https://corpus.quran.com/releasenotes.jsp)
- QuranMorph: 77,429 tokens, 6,235 verses — [QuranMorph, arXiv 2506.18148](https://arxiv.org/html/2506.18148v1). ParallelQuran also gives 77,429 total words — [ParallelQuran language tools](https://parallelquran.com/language-tools/)
- A traditional or popular count: "Number of words in the entire Quran is 77,797", "Number of letters is 330,709", "Number of words without repetition is 14,870". The paper gives no method — [Harrag et al., "Quran Intelligent Ontology Construction Approach Using Association Rules Mining", arXiv 2008.03232](https://arxiv.org/pdf/2008.03232)
- Learner material from Understand Quran uses "a total of 77,800 words" (search-result snippet; page not fetched) — [Understand Quran slides (SlideShare)](https://www.slideshare.net/slideshow/understand-quran-161404197/161404197); Abdulraheem's booklet also says "approx. 77,800" — [alkitab.com listing](https://alkitab.com/44700.html)

**Morphological segments (sub-word units: prefixes, stems, suffixes and clitics)**
- "The morphological layer in the corpus consists of 128,223 segments," annotated with 42 possible features (about 783K feature-values, 6.1 per segment) — [Dukes thesis](https://arxiv.org/pdf/1510.07193)
- QuranMorph describes the QAC as having "128,219 forms (clitics)", a slightly different QAC version — [QuranMorph](https://arxiv.org/html/2506.18148v1)

**Distinct word forms (types)**
- Dukes (QAC author), quoted on a blog: "Total number of space-separated words = 77,430"; "Number of unique surface forms (i.e. space-separated word-forms, including clitics) = 18994"; "Number of unique words by stem = 12183"; "Number of unique words by lemma = 3382"; "Number of unique words by root = 1685". This is a secondary report of a Dukes statement from about 2013 — [Quickest Way to Quran blog, 2013](http://quickestwaytoquran.blogspot.com/2013/08/number-of-unique-words-in-quran.html)
- QuranMorph (word-level tokens with clitics kept attached; Qabas lemmas): 19,009 unique words, of which 9,789 are unique nouns, 8,265 unique verbs and 1,036 unique functional words — [QuranMorph Table 1](https://arxiv.org/html/2506.18148v1)
- Without diacritics: "14,870" words without repetition — [Harrag et al.](https://arxiv.org/pdf/2008.03232). The Kalimah Center repeats "approximately 14,870 unique word forms" without citing a source — [Kalimah Center](https://kalimah-center.com/quranic-arabic-words/)

**Lemmas**
- Live QAC lemma concordance: "3680" lemmas, sorted by frequency. QAC definition: a lemma "groups word-forms that differ only by inflectional (as opposed to derivational) morphology, and do not vary in meaning" — [QAC lemmas](https://corpus.quran.com/lemmas.jsp)
- The earlier Dukes figure is 3,382 lemmas — [blog quoting Dukes](http://quickestwaytoquran.blogspot.com/2013/08/number-of-unique-words-in-quran.html). The 2011 release notes mention "improved lemmatization consistency with traditional Arabic lexicons", which may explain why the count changed — [QAC release notes](https://corpus.quran.com/releasenotes.jsp)
- QuranMorph: 4,616 unique lemmas, made up of 3,057 noun lemmas, 1,479 verb lemmas and 173 functional-word lemmas. Its lemmas come from Qabas, "an Arabic lexicographic database linked with 110 lexicons and corpora of 2 million tokens", and it uses a 40-tag SAMA/Qabas tagset — [QuranMorph](https://arxiv.org/html/2506.18148v1)
- QUL (Tarteel's Quranic Universal Library) publishes lemma and root SQLite tables. Each row has `words_count` (total occurrences) and `uniq_words_count` (unique word forms derived from the lemma or root). The page gives no grand totals — [QUL morphology resource](https://qul.tarteel.ai/resources/morphology/75)

**Roots**
- 1,685 roots (Dukes, via blog) — [Quickest Way to Quran](http://quickestwaytoquran.blogspot.com/2013/08/number-of-unique-words-in-quran.html)
- 1,651 roots, data said to come from the QAC — [ParallelQuran](https://parallelquran.com/language-tools/)
- "1641 word roots occurring in the Qur'an" — [quran.so roots](https://quran.so/roots.md)
- "approximately 1,700–2,000 root families" (no source given; low reliability) — [Kalimah Center](https://kalimah-center.com/quranic-arabic-words/)
- QAC roots were first imported from the open-source Zekr Quran browser, which "contains an accurate verified root list for the Quran", and were then corrected by hand — [Dukes thesis](https://arxiv.org/pdf/1510.07193)
- For comparison with Classical Arabic as a whole: Lane's Lexicon lists 3,775 roots. Hegazi & El-Sharkawi estimate 6,350 triliteral plus 2,500 quadriliteral roots in the Classical lexicon. Ryding estimates 5,000–6,500 roots in use in Modern Arabic. The BAMA lexicon (MSA) has 40,219 lemmas — [Dukes thesis](https://arxiv.org/pdf/1510.07193)

**Script variants (Tanzil)**
- Tanzil publishes Uthmani (Medina Mushaf), Uthmani Minimal, Simple, Simple Plain, Simple Minimal and Simple Clean ("without any diacritics or symbols. Suitable for easy search") texts — [Tanzil Quran Text Types](https://tanzil.net/docs/Quran_Text_Types)
- Elie Wardini's *The Quran Word List* (Gorgias Press, 2020, 3 vols) lists every attested word form from the Tanzil Uthmani text as "word form < lemma < root". It notes that choosing roots and lemmas "is not always obvious, since classical dictionaries and commentaries sometimes either disagree" — [Gorgias Press](https://gorgiaspress.com/the-quran-word-list-tome-1)

### Inferences
- **Why token counts differ (77,429 / 77,430 / 77,797 / ~77,800):** word boundaries are set by whitespace, and Uthmani and Imla'i (simple) spelling place spaces differently. For example, the Uthmani text writes the vocative ya joined to the next word (يَٰٓأَيُّهَا), while simple script writes يا أيها as two words. The 1-word gap between 77,429 and 77,430 probably comes from one disputed boundary between QAC versions. The 77,797 / 77,800 figures come from traditional or popular counts that use other conventions and give no method.
- **Why form counts differ (18,994 / 19,009 / 14,870):** clitics (و، ف، ب، ل، ك، ال، and pronoun suffixes) stay attached to the word, so ربكم, ربهم and ربه count as separate forms. Diacritics also split forms: كَتَبَ and كُتِبَ are two forms with diacritics but one without. Removing diacritics brings ~19k down to ~14.9k. The owner's further normalization of alef, hamza, ya and ta-marbuta explains why their 8,192 Uthmani forms fell to ~5,731 distinct skeletons (70% kept).
- **Why lemma counts differ (3,382 / 3,680 / 4,616):** lemmatization schemes differ. The QAC lists a lemma separately for each part of speech (see Section 5). Qabas (QuranMorph) uses a different and apparently finer lemma inventory. Also, QuranMorph's three POS figures sum to 4,709, not 4,616, which suggests about 93 lemmas are counted in more than one POS class.
- **Why root counts differ (1,641–1,685):** sources disagree on weak and hamzated roots, on whether foreign proper nouns get a root, on quadriliteral analyses, and on version.
- Ratio for planning: the 3,680 lemmas are about 4.7% of tokens and about 19% of diacritized forms. Every extra level of "same meaning" collapse divides the count by roughly 3–5.

### Gaps
- No primary QAC page states an official root total. The 1,685 figure is a secondary quote of Dukes. **To compute:** from the QAC morphology file (one row per segment, with `ROOT:` and `LEM:` features), count distinct `ROOT:` values. Or count rows in QUL's `roots` table.
- No primary, documented count of distinct undiacritized forms in the Tanzil Simple-Clean or Uthmani text was found. 14,870 has no stated method. **To compute:** tokenize Tanzil Uthmani on spaces, strip U+064B–U+0652, U+0670, U+06D6–U+06ED and tatweel, normalize أ/إ/آ/ٱ→ا, ى→ي, ة→ه, ؤ/ئ→ء (or the owner's own rule set), then count unique strings.

---

## 2. Part-of-speech breakdown: verbs, nouns, proper nouns, particles; distinct verb lemmas, verb roots and conjugated verb forms

### Takeaway
About 19,350 verb occurrences fall into roughly 1,475–1,479 verb lemmas. These lemmas are (root, form) pairs, so the number of distinct verb roots is lower, probably about 1,000 (not published). They appear as about 8,265 distinct written verb word forms. Nouns, adjectives and proper nouns make up about 3,000 lemmas. Particles and pronouns add only a couple of hundred lemmas, though they account for a large share of tokens.

### Cited Findings
- **Verb lemmas, QAC:** "Verbs 1 to 50 of 1475". The verb list is "grouped by root and form, and sorted by frequency", so each entry is one root plus one derived form (I–X). Most frequent: قَالَ "to say" 1,618; كَانَ "to be" 1,358; ءَامَنَ (Form IV) "to believe" 537; ٱتَّقَىٰ (Form VIII) 166 — [QAC verb concordance](https://corpus.quran.com/verbs.jsp)
- **Verb lemmas, QuranMorph:** 1,479 unique verb lemmas, 8,265 unique verbs (distinct word-level verb forms with clitics attached) — [QuranMorph](https://arxiv.org/html/2506.18148v1)
- **Verb tokens:** QAC search `pos:v` returns "Results 1 to 50 of 19356" — [QAC search pos:v](https://corpus.quran.com/search.jsp?q=pos%3Av). Rootlabs also cites 19,356 verbs per QAC — [Rootlabs, "12,347 Verbs in the Quran"](https://rootlabs.substack.com/p/12347-verbs-in-the-quran)
- An independent Arabic database by Muhammad Yusuf Ahmad (Amman) counts 19,348 verbs: 9,170 perfect (ماضي), 8,316 imperfect (مضارع) and 1,862 imperative (أمر) — [Multaqa Ahl al-Tafsir thread](https://mtafsir.net/threads/%D8%AC%D8%A7%D9%85%D8%B9-%D8%A8%D9%8A%D8%A7%D9%86%D8%A7%D8%AA-%D8%A3%D9%81%D8%B9%D8%A7%D9%84-%D8%A7%D9%84%D9%82%D8%B1%D8%A2%D9%86-%D8%A7%D9%84%D8%B9%D8%B8%D9%8A%D9%85.73291/latest). This differs from the QAC by 8.
- **Form I share:** Form I has 12,347 tokens (63% of verbs): 5,918 past, 5,248 present and 1,181 command. These come from 149 distinct Form I imperative verbs. Forms II–X account for 37% — [Rootlabs](https://rootlabs.substack.com/p/12347-verbs-in-the-quran)
- **Noun tokens:** QAC `pos:n` gives 25,135 — [QAC search pos:n](https://corpus.quran.com/search.jsp?q=pos%3An)
- **Adjective tokens:** QAC `pos:adj` gives 1,961 — [QAC search pos:adj](https://corpus.quran.com/search.jsp?q=pos%3Aadj)
- **Proper-noun tokens:** QAC `pos:pn` gives 3,911 — [QAC search pos:pn](https://corpus.quran.com/search.jsp?q=pos%3Apn). Of these, the lemma ٱللَّه (Allah) alone has 2,699 — [QAC lemmas](https://corpus.quran.com/lemmas.jsp)
- **Top lemmas overall:** مِن 3,226; ٱللَّه 2,699; فِى 1,701; إِنّ 1,682; عَلَىٰ 1,445. Four of the top five are particles — [QAC lemmas](https://corpus.quran.com/lemmas.jsp)
- **Lemma counts by POS (QuranMorph):** nouns (including adjectives and proper nouns) 3,057; verbs 1,479; functional words 173 — [QuranMorph](https://arxiv.org/html/2506.18148v1)
- In the QAC, each morphological segment has its own POS. Verbal derivatives (active participle, passive participle, verbal noun) are tagged as nominals with a derivation feature: ACT PCPL, PASS PCPL, VN — [Dukes thesis](https://arxiv.org/pdf/1510.07193)

### Inferences
- **Non-verb QAC lemma entries:** 3,680 − 1,475 = **2,205**. This covers nouns, adjectives, proper nouns, pronouns, particles and adverbs.
- **Verb roots:** the QAC counts one verb entry per (root, form), and many roots occur in several forms (e.g. ع ل م in Forms I, II and V). So distinct verb roots must be well below 1,475. A rough estimate is ~900–1,100. This is inferred, not published.
- **Token shares:** verbs are about 25% of the 77,430 words (19,356 / 77,430). Nouns, adjectives and proper nouns are about 40% of words (≈31,000 segments). The rest are particles, pronouns and clitic segments. Proper nouns other than Allah are only about 1,212 tokens.
- **Conjugated forms:** 8,265 distinct verb words (QuranMorph) for about 1,479 verb lemmas. That is about 5.6 attested surface forms per verb lemma, all differing only in person, number, gender, tense, mood or attached pronoun. This is exactly the ربكم / ربهم / ربه problem, applied to verbs.

### Gaps
- No published count of distinct verb ROOTS was found. **To compute:** in the QAC morphology file, filter segments with `POS:V` and count distinct `ROOT:` values. Grouping by (ROOT, verb form) should reproduce about 1,475.
- No published count of distinct particle or pronoun lemmas in the QAC was found. QuranMorph gives 173 functional lemmas. **To compute:** count distinct `LEM:` values where POS is in {P, CONJ, NEG, REL, DEM, PRON, INTG, COND, etc.}.
- No published count of distinct conjugated verb forms with diacritics but without clitics (stem-level) was found. **To compute:** take `POS:V` segments and count distinct stem strings, with and without diacritics.

---

## 3. How verb lemmas are cited, and how derived forms (I–X) multiply distinct-meaning verbs per root

### Takeaway
Verbs are cited by convention in the 3rd-person masculine singular perfect (قَالَ, كَتَبَ, ءَامَنَ). Each derived form (I–X, plus quadriliteral forms) is a separate lexeme with its own dictionary meaning, as with عَلِمَ "to know", عَلَّمَ "to teach" and تَعَلَّمَ "to learn". Derived forms therefore add genuinely distinct meanings. The verb list is still capped at about 1,475 entries because that number already counts every root+form pair attested in the Quran.

### Cited Findings
- A lemma is "a conventional choice of one word that represents a lexeme". A lexeme is "a group of words with the same derivational morphology that differ only by inflection". Classical and Modern dictionaries are "organized by root then lemma" — [Dukes thesis §5.2.4](https://arxiv.org/pdf/1510.07193)
- QAC verb features: aspect PERF/IMPF/IMPV; mood IND/SUBJ/JUS; voice ACT/PASS; and verb form in Roman numerals. The feature table lists forms "I to XII" — [Dukes thesis §5.8.5](https://arxiv.org/pdf/1510.07193)
- QAC documentation describes ten triliteral forms (I–X) and four quadriliteral forms (I–IV): I kataba "he wrote"; II intensive or causative, e.g. ʿallama "taught"; III mutual action, e.g. qātala "fight"; IV causative, e.g. ahlaka "destroy"; V reflexive of II, e.g. tadhakkara; VI reciprocity, e.g. taẓāhara "support one another"; VII reflexive or agentless passive, e.g. inqalaba "turn back"; VIII; IX colours and defects, e.g. iswadda "become black"; X seeking, e.g. istaghfara "seek forgiveness" — [QAC verb forms documentation](https://corpus.quran.com/documentation/verbforms.jsp)
- **Root ع ل م** occurs 854 times in 14 derived forms. Verbs: ʿalima (I) "to know", 382 times; ʿallama (II) "to teach", 41; yataʿallamu (V) "to learn", 2. Nominals: ʿilm "knowledge" 105; ʿalīm "All-Knowing" 163; aʿlam "most knowing" 49; ʿālamīn "worlds" 73; ʿallām "All-Knower" 4; aʿlām "flags" 2; ʿalāmāt "landmarks" 1; maʿlūmāt "known" 2; ʿālim (active participle) "knower" 18; maʿlūm (passive participle) "known" 11; muʿallam (Form II passive participle) "taught" 1 — [QAC Quran Dictionary, root ʿlm](https://corpus.quran.com/qurandictionary.jsp?q=Elm)
- **Citation exceptions:** some QAC verb entries are cited in an attested non-perfect form when the perfect does not occur, for example أَوِّبِ (an imperative, "to repeat") and نُجَٰزِىٓ (an imperfect, "to recompense") — [QAC verbs page 20](https://corpus.quran.com/verbs.jsp?page=20). Form V of ʿlm also appears as yataʿallamu — [QAC root ʿlm](https://corpus.quran.com/qurandictionary.jsp?q=Elm)
- Form I is 63% of verb tokens; Forms II–X are 37% — [Rootlabs](https://rootlabs.substack.com/p/12347-verbs-in-the-quran)
- **Precedent:** the French BIP-39 list is restricted to "infinitive verbs, adjectives and nouns" of 5–8 letters, which is a lemma-level policy — [BIP-39 wordlists notes](https://github.com/bitcoin/bips/blob/master/bip-0039/bip-0039-wordlists.md)

### Inferences
- In the ع ل م example, one root yields 3 verb lexemes with different English glosses (know, teach, learn). It also yields about 11 nominal lemmas, some with clearly different meanings ("flags", "worlds", "knowledge") and some that overlap ("known" × 2, "knower" / "All-Knower" / "All-Knowing"). So a root is a semantic field, not one meaning. The (root, form) verb unit and the noun lemma are the right granularity for "one entry = one meaning".
- **Undiacritized collisions:** ʿalima "knew", ʿallama "taught", ʿilm "knowledge", ʿalam "flag (sing.)" and ʿulima "was known" all reduce to the skeleton علم. A list normalized to bare skeletons (as in the owner's 5,731 analysis) cannot keep these meanings apart. Either diacritics must stay in the Arabic entry, or only one meaning per skeleton can be kept.
- Passive voice (كَتَبَ / كُتِبَ) and the tenses (wrote / writes / write!) belong to the same lemma. They are not separate meanings and should not be separate entries.
- Using derived forms is the only way a verbs-only list could rise above the number of verb roots (about 1,000 by inference). Even so, the ceiling is about 1,475–1,479 (Section 4).

### Gaps
- No published distribution of QAC verb entries by form (how many Form II, IV, X lexemes, etc.) was found. **To compute:** take the (ROOT, form) pairs among `POS:V` segments and count them per form.
- No published measure of how many (root, form) pairs share an English gloss with another pair (e.g. Form I vs Form IV of the same root, or synonyms across roots) was found.

---

## 4. How many entries with distinct meanings are achievable at each level, and which levels reach 2048, 4096 or 8192?

### Takeaway
- **Root level:** about 1,640–1,690. This fails even at 2048.
- **Verb lemmas only:** about 1,475–1,479. This fails at 2048.
- **All-POS lemmas with content words only:** about 3,000–3,300 lemma entries before synonyms are merged, probably about 2,300–2,900 distinct English glosses after merging (estimate). So **2048 is feasible but tight, 4096 is not feasible with strictly unique meanings, and 8192 is impossible.**
- **Conjugated or cliticized forms:** 8,265 verb forms and ~19k forms in total. This can numerically pass 4096 (and 8192 using all POS), but only by treating person, number and tense differences as "meanings", which is the duplication the owner wants to remove.

### Cited Findings
- Roots: 1,641 / 1,651 / 1,685 — [quran.so](https://quran.so/roots.md); [ParallelQuran](https://parallelquran.com/language-tools/); [Dukes via blog](http://quickestwaytoquran.blogspot.com/2013/08/number-of-unique-words-in-quran.html)
- Lemmas: 3,680 (QAC live) — [QAC lemmas](https://corpus.quran.com/lemmas.jsp); 3,382 (older Dukes count) — [blog](http://quickestwaytoquran.blogspot.com/2013/08/number-of-unique-words-in-quran.html); 4,616 (QuranMorph/Qabas) — [QuranMorph](https://arxiv.org/html/2506.18148v1)
- Verb lemmas: 1,475 (QAC) — [QAC verbs](https://corpus.quran.com/verbs.jsp); 1,479 (QuranMorph) — [QuranMorph](https://arxiv.org/html/2506.18148v1)
- Distinct conjugated verb word forms: 8,265 — [QuranMorph](https://arxiv.org/html/2506.18148v1)
- Distinct stems: 12,183. Distinct surface forms: 18,994 — [Dukes via blog](http://quickestwaytoquran.blogspot.com/2013/08/number-of-unique-words-in-quran.html); 19,009 — [QuranMorph](https://arxiv.org/html/2506.18148v1)
- Dukes's own view of a "wide-coverage lexicon": there are "at most 7,000 unique 'words' in the Quran" in that sense — [Dukes via blog](http://quickestwaytoquran.blogspot.com/2013/08/number-of-unique-words-in-quran.html)
- BIP-39 uses a 2048-word list, 11 bits per word, and 12–24 words for 128–256 bits of entropy. Its design aims are words identifiable by their first 4 letters, avoiding "similar words", and a sorted list — [BIP-39](https://github.com/bitcoin/bips/blob/master/bip-0039.mediawiki); [BIP-39 wordlists notes](https://github.com/bitcoin/bips/blob/master/bip-0039/bip-0039-wordlists.md)
- BIP-39 derives the seed from "the mnemonic sentence (in UTF-8 NFKD)" via PBKDF2, so the seed depends on the actual word strings, not the indices — [BIP-39](https://github.com/bitcoin/bips/blob/master/bip-0039.mediawiki)

### Inferences

**Feasibility table** (estimates are marked ≈; derivations below):

| Level | Raw count (source) | Plausible distinct-meaning entries after cleaning | 2048 (2^11) | 4096 (2^12) | 8192 (2^13) |
|---|---|---|---|---|---|
| (a) Roots | 1,641–1,685 | ≈1,500–1,600 (proper-noun "roots" removed; root = semantic field, not one gloss) | No | No | No |
| (b) Lemmas, all POS | 3,680 QAC / 4,616 Qabas | ≈3,000–3,300 content lemmas → ≈2,300–2,900 distinct English glosses | Yes (tight; needs hundreds of hapax and rare words) | No (strict uniqueness); only with near-synonym glosses or Qabas-level splitting, and still doubtful | No |
| (c) Verb lemmas only | 1,475 QAC / 1,479 Qabas | ≈1,300–1,450 distinct glosses (synonymous verbs merge, e.g. two verbs both glossed "to come") | No | No | No |
| (d) Conjugated or word-form level | 8,265 verb forms; ~19,000 forms in total; ~14,870 undiacritized; owner's 5,731 normalized | Numerically enough, but meanings repeat ("he said / they said / we said") | Numerically yes | Numerically yes | Only by mixing all POS and keeping clitic and diacritic variants, which defeats "unique meaning" |

- **(a) Roots:** even the highest count (1,685) is 363 short of 2048. A root also does not carry a single meaning (ع ل م covers "know", "teach", "flag", "world").
- **(b) Lemmas:** starting from 3,680 QAC entries, remove functional and pronoun lemmas (≈170–250; QuranMorph has 173), proper nouns (≈100–150, see Section 5), divine names (≈80–100 if excluded), entries duplicated across POS, and two-letter content words. That leaves roughly **3,000–3,300** entries. Merging lemmas whose English glosses coincide or nearly coincide (many words for "fire" / "Hell", "punishment", "come", "see", "give", etc.) probably brings this to **≈2,300–2,900** distinct glosses. That reaches 2048 with a margin of a few hundred and rules out 4096. Starting from QuranMorph's 4,616 Qabas lemmas gives a higher raw ceiling (≈4,200 after exclusions). However, Qabas lemmas appear to be finer-grained, and synonym merging would still very likely bring the result below 4096.
- **(c) Verbs only:** the hard ceiling is 1,475–1,479. This is 569–573 short of 2048 before any synonym merging, so **a verbs-only list cannot reach even 2048**. Adding verbal nouns (masdar) and participles (e.g. ʿālim "knower", maʿlūm "known") as separate entries would raise the count. Those are nouns, though, and some of their glosses overlap the verb's gloss.
- **(d) Word forms:** this is the level where ربكم / ربهم / ربه live. The owner's 8,192 → 5,731 collapse shows that even orthographic normalization leaves many forms that share a lemma. Lemma-level uniqueness would cut the 5,731 much further, plausibly to about 2,000–2,500 lemmas. That figure is speculative; see the gap below.
- **Entropy cost of a smaller list:** for 128 bits plus checksum, 2048 words need 12 words (132 bits), 4096 need 11 (132 bits) and 8192 need 10 (130 bits). Choosing 2048 instead of 8192 costs only 2 extra words per phrase, and 2048 is the only size that a unique-meaning Quranic list can realistically reach.
- **Multi-language display:** under standard BIP-39, the seed is computed from the word string. An index-aligned multi-language scheme (Arabic / English / Urdu) only gives the same wallet if Bucks derives the seed from the entropy indices or from one canonical language. Otherwise, each language's phrase produces a different seed.
- **Prefix and similarity rules:** BIP-39 favours lists where the first 4 letters identify a word and near-identical words are avoided. Arabic words without diacritics share many 3-letter skeletons (علم, كتب). Applying a "unique normalized skeleton" or "unique first-N letters" rule on top of unique meaning would cut a lemma-level list further. This adds pressure on 2048.

### Gaps
- No published count of "distinct English glosses" for QAC lemmas was found. **To compute:** take the QAC lemma list (3,680 entries) with POS and the QAC word-by-word English gloss of each lemma's most frequent form. Normalize glosses (lowercase; strip "to", articles, "(the)"; collapse plurals), group lemmas that share a normalized gloss, and count groups. Repeat after excluding PN, particles, pronouns and divine names. Repeat with the QUL `lemmas` table, or with QuranMorph/Qabas glosses for comparison.
- How the owner's 5,731 normalized forms map to lemmas is unknown. **To compute:** map each of the 8,192 Uthmani forms to its QAC location(s), take the stem segment's `LEM:` (and `ROOT:`), and count distinct lemmas. Also count how many normalized skeletons correspond to more than one lemma (meaning collisions such as علم).

---

## 5. Hapax legomena and rare lemmas; proper nouns and divine names; what remains after exclusions

### Takeaway
The Quran's lexicon is extremely long-tailed. In the QAC lemma list, about 1,654 of 3,680 lemma entries (45%) occur only once, and only 2,026 entries occur at least twice. About 529 of the 1,475 verb entries (36%) occur only once. Any list of 2048 or more unique-meaning entries must therefore include hundreds of words that occur only once or twice. Some of these have disputed meanings.

### Cited Findings
- **QAC lemma frequency tail:**
  - "Lemmas 2001 to 2050 of 3680". Entries 2001–2026 have frequency 2. **The first frequency-1 lemma is entry 2027 (آخِر, time adverb)**, followed by آزَر (proper noun) — [QAC lemmas page 41](https://corpus.quran.com/lemmas.jsp?page=41)
  - Pages 36–40 (entries 1751–2000) are all frequency 2 — [page 36](https://corpus.quran.com/lemmas.jsp?page=36), [page 40](https://corpus.quran.com/lemmas.jsp?page=40)
  - The last page (entries 3651–3680) is all frequency 1 and includes the proper nouns يَثْرِب (Yathrib), يَعُوق and يَغُوث — [page 74](https://corpus.quran.com/lemmas.jsp?page=74)
- **QAC verb frequency tail:**
  - "Verbs 701 to 750 of 1475". Frequency goes from 3 to 2 at entry 724 (ٱسْتَـْٔجَرَ) — [QAC verbs page 15](https://corpus.quran.com/verbs.jsp?page=15)
  - Entries 801–900 are all frequency 2 — [page 17](https://corpus.quran.com/verbs.jsp?page=17), [page 18](https://corpus.quran.com/verbs.jsp?page=18)
  - The first frequency-1 verb is at about entry 947 (أَبَقَ "to run away"). The tool's frequency readout for this page was slightly garbled, so take this as ±2 — [page 19](https://corpus.quran.com/verbs.jsp?page=19)
- **QAC lists the same lemma string under different POS:** وَهَّاب appears as a noun (freq 2) and as an adjective (freq 1); ءَانَآء appears as a noun (2) and as a time adverb (1) — [QAC lemmas page 41](https://corpus.quran.com/lemmas.jsp?page=41), [page 74](https://corpus.quran.com/lemmas.jsp?page=74)
- **Proper nouns:** 3,911 PN tokens — [QAC search pos:pn](https://corpus.quran.com/search.jsp?q=pos%3Apn). Allah alone is 2,699 — [QAC lemmas](https://corpus.quran.com/lemmas.jsp). Most proper nouns "are definite without having to carry the al- determiner". Many are "of a foreign or ancient origin" and "fall outside the root and pattern system" — [Dukes thesis §5.5.2](https://arxiv.org/pdf/1510.07193). The QAC ontology has "300 linked concepts with 350 relations", including named entities (prophets, places, and so on) — [QAC ontology](https://corpus.quran.com/ontology.jsp) (search-result summary)
- **Divine attributes inside ordinary roots:** ʿalīm "All-Knowing" (163) and ʿallām "All-Knower" (4) are lemmas of root ع ل م — [QAC root ʿlm](https://corpus.quran.com/qurandictionary.jsp?q=Elm)
- **Quranic hapax legomena** are studied as a category (examples: ṣamad, iram, tasnīm, zanjabīl, yaqṭīn). An article in *Quranic Sciences and Tradition* (Univ. of Tehran) attempts a complete list, but the abstract gives no total — [JQST article 54280](https://jqst.ut.ac.ir/article_54280_en.html)

### Inferences
- **Hapax lemma entries:** 3,680 − 2,026 = **1,654 (≈45%)**. Lemma entries with frequency ≥ 2: **2,026**, which is just under 2048, and this figure still includes particles, pronouns and proper nouns.
- **Verbs:** about 723 entries with frequency ≥ 3, about 223 with frequency 2 (724–946), and about **529 with frequency 1 (≈36%)**. About 946 verbs occur at least twice.
- **Consequence:** a 2048-entry list with unique meanings and no proper nouns or particles must include roughly 300–600+ hapax lemmas. These tend to be rarer, harder for users to recognize, and sometimes semantically disputed. Examples are the classical-exegesis debates over ṣamad and over hapaxes such as abb or kalāla. Disputed meanings are a problem when the list needs a single fixed English, Urdu or other translation.
- **Rough estimate of what remains after exclusions** (starting from the 3,680 QAC entries; all subtractions are estimates):
  - minus particles, pronouns, demonstratives, relatives, etc.: ≈170–250 (QuranMorph has 173 functional lemmas)
  - minus proper nouns: ≈100–150 lemma entries (only about 1,212 PN tokens besides Allah; the lemma count is unknown)
  - minus divine names, if excluded: ≈80–100 (the "99 names" tradition; many are adjective lemmas such as ʿalīm, raḥīm, ghafūr)
  - minus entries duplicated across POS: unknown, probably dozens
  - minus content words shorter than 3 letters in normalized form: ≈10–30 (e.g. يد hand, دم blood, أب father, أخ brother, أم mother, فم mouth)
  - **This leaves ≈3,000–3,300 content lemma entries:** about 1,475 verbs plus about 1,600–1,800 nouns and adjectives. Synonym merging then gives ≈2,300–2,900 distinct glosses (Section 4).
- Excluding all hapax lemmas as well would leave about 2,026 minus the excluded classes, i.e. **≈1,600–1,800**. Under that policy, **2048 would not be reachable**.

### Gaps
- No published count of QAC proper-noun lemmas was found, and none for lemmas that are Names of Allah. **To compute:** count distinct `LEM:` where `POS:PN`. For divine names, cross-reference adjective lemmas against a standard list of 99 Names, or use QAC ontology concepts.
- No published total of Quranic hapax legomena was found. The JQST article's count is behind the full text. The QAC-derived figures above (1,654 lemma entries; ≈529 verbs) are the best available and depend on the QAC lemma scheme.
- The size of the overlap between lemmas listed under several POS in the QAC is unknown. **To compute:** count lemma strings that appear in more than one POS row of the lemma list.

---

## 6. Published frequency lists and core-vocabulary lists of the Quran

### Takeaway
Full lemma and verb frequency lists are published online by the QAC (with mirrors on ParallelQuran and QUL). Learner lists focus on the few hundred most frequent words, which cover 50–85% of running text. These lists cover the head of the distribution, not the 2,000+ entries a wordlist needs.

### Cited Findings
- **QAC lemma frequency list:** all 3,680 lemmas, sortable by frequency or alphabetically, 74 pages of 50 — [QAC lemmas](https://corpus.quran.com/lemmas.jsp)
- **QAC verb frequency list:** 1,475 verbs grouped by root and form, sorted by frequency — [QAC verbs](https://corpus.quran.com/verbs.jsp)
- **QAC Quran Dictionary:** organized by root, then lemma, with frequencies and English glosses — [QAC root ʿlm example](https://corpus.quran.com/qurandictionary.jsp?q=Elm)
- **ParallelQuran:** 1,651 roots "with meanings, frequency, related lemmas"; a top-200 verb lemma list sourced from the QAC, with 14,927 occurrences across those top 200 lemmas — [ParallelQuran tools](https://parallelquran.com/language-tools/); [ParallelQuran verbs](https://parallelquran.com/morphology/?pos=Verb)
- **QUL:** lemma and root SQLite exports with `words_count` and `uniq_words_count` per entry — [QUL](https://qul.tarteel.ai/resources/morphology/75)
- **Understand Quran Academy:** "125 important words that occur in the Quran 40,000 times" (about 50% of about 77,800 words) (search snippet) — [Understand Quran slides](https://www.slideshare.net/slideshow/understand-quran-161404197/161404197). The site also offers a download titled "85% of Quranic Words" — [understandquran.com](https://www.understandquran.com/)
- **Dr. Abdulazeez Abdulraheem, *80% of Qur'anic Words: Classified word lists for easy memorization* (IBT, Kuala Lumpur, 2009, 48 pp.):** claims coverage of "82.6% (64,282) of the total number of words (approx. 77,800)", and says the most frequent words make up "41.5% (32,263)". It lists verbs with past, imperfect, imperative, active participle and verbal noun — [alkitab.com](https://alkitab.com/44700.html)
- **Kalimah Center (uncited, low reliability):** top 10 words ≈25% of text, top 50 ≈50%, top 100 ≈55–60%, 300–500 roots ≈75–80% — [Kalimah Center](https://kalimah-center.com/quranic-arabic-words/)
- **Dukes:** the distribution is Zipfian, and "the top 100 words will cover a very large percentage" (80/20 rule) — [Dukes via blog](http://quickestwaytoquran.blogspot.com/2013/08/number-of-unique-words-in-quran.html)
- **Rootlabs:** in Form I, 9 key patterns (3 each for past, present and command) and "50 patterns of all 10 Verb Forms" — [Rootlabs](https://rootlabs.substack.com/p/12347-verbs-in-the-quran)
- **Elie Wardini, *The Quran Word List* (3 vols, Gorgias 2020):** every attested form, mapped form → lemma → root. Volume 3 is organized by lemma and root — [Gorgias Press](https://gorgiaspress.com/the-quran-word-list-tome-1)
- **Badawi & Abdel Haleem, *Arabic-English Dictionary of Qur'anic Usage* (Brill, Handbook of Oriental Studies 85):** arranged by root in 28 letter sections, about 1,070 pages. The number of root entries was not found — [JIS book review](https://jis.cis-ca.org/book-review-elsaid-m-badawi-and-muhammad-abdel-haleem-arabic-english-dictionary-of-quranic-usage.html)

### Inferences
- The best ready-made ranked source for candidate entries is the QAC lemma list (3,680, ranked by frequency, with POS) combined with the QAC verb list (1,475 root+form entries with glosses). The coverage claims of learner lists ("125 words = 50%", "≈80% with a few hundred words") confirm that the frequent core is small, a few hundred lemmas. A 2048-entry list will therefore be made mostly of mid- and low-frequency vocabulary.
- Learner lists treat inflected variants as one "word" (e.g. verbs taught with all their forms). This matches the owner's goal of one entry per meaning, i.e. the lemma level.

### Gaps
- The exact word counts of the Understand Quran "85%" list and of Abdulraheem's booklet were not found in the fetched pages.
- No academic, peer-reviewed lemma-frequency dictionary of Quranic Arabic was found. The online QAC concordance is the de facto reference.
