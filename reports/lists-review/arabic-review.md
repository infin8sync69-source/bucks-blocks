# Arabic word list review: ar-quran-draft-2048

Reviewer: Arabic linguist pass, 2026-10-14. Read-only review: nothing in `wordlists/`, `tools/` or `src/` was changed. Companion file: `wordlists/review/arabic-proposed-changes.csv` (one row per flagged entry; columns index, word, issue_type, severity, reason, proposed_action, replacement).

## 1. Summary

All 2048 entries were read and checked. **719 entries (35%) are flagged**: **90 Must-fix**, **356 Should-fix**, **273 Consider**.

| Severity | Entries | Meaning |
|---|---|---|
| Must-fix | 90 | Certain. Breaks an explicit rule: divine name or attribute, excluded root family, liturgical formula, obscene or forbidden word, a spelling that does not match modern orthography or is not attested, a fold mismatch. |
| Should-fix | 356 | Strong judgement or a lesser rule break. A reviewer would normally object; the Shariah adviser or the build owner can overrule. |
| Consider | 273 | Judgement call. Acceptable to keep; noted so the reviewer sees it. |

Proposed actions: **drop 298**, **replace 167**, **keep-note 254**. Of the 167 replacements, 102 are *suggested backfill words* for dropped entries (all taken from the build tool's own leftover pool, see section 2 step 7) and 65 are respellings or bare-form fixes of the same word.

Entries by primary issue type (an entry with several issues is counted once, under its most severe issue):

| Issue type | Must | Should | Consider | Total |
|---|---|---|---|---|
| a. Divine names and attributes | 8 | 14 | 15 | 37 |
| b. Prophets, angels, persons, peoples, places, narratives | 3 | 26 | 39 | 68 |
| c. Last-day, hell, paradise, unseen vocabulary | 12 | 113 | 63 | 188 |
| d. Worship and ritual | 12 | 24 | 23 | 59 |
| e. Scripture, revelation, recitation | 4 | 21 | 16 | 41 |
| f. Punishment, curse, disbelief, sin, obscene-adjacent, insult | 8 | 40 | 25 | 73 |
| g. Verbs of exclusively divine acts | 1 | 11 | 4 | 16 |
| h. Quranic formulae | 8 | 16 | 11 | 35 |
| Spelling and orthography (Uthmani leaks, malformed words) | 30 | 46 | 1 | 77 |
| Second meaning without vowels (rule A11) | 3 | 20 | 18 | 41 |
| Near-duplicates and same-lexeme pairs (A2/A3/A5) | 0 | 20 | 30 | 50 |
| Article ال or attached preposition (A4) | 0 | 3 | 1 | 4 |
| Attached particle or pronoun where a cleaner form exists | 0 | 1 | 19 | 20 |
| Function words and other oddities (A1) | 1 | 1 | 8 | 10 |
| **All** | **90** | **356** | **273** | **719** |

How to read the Should column: 139 of the Should rows are in categories (b) and (c) (named narratives, last-day/hell/paradise vocabulary that is *not* a core term like janna or nar). Whether these go is a policy decision, not a defect: the research note excludes only the core terms of (c), and a stricter policy removes about 140 more words. The remaining Should rows are rule-based (spelling, near-duplicates, second meanings, formulae, root families of excluded words).

A minimal package that fixes everything certain plus the rule-based Should items touches 180 entries and needs 128 new words; the rest are respellings.

## 2. Method

1. **Full read.** All 2048 entries were read in order alongside the review CSV (corpus lemma, part of speech, root, count, gloss) in four passes, and each entry was tested against the six checks in the brief: religious sensitivity (categories a-h of the research note, including root families), malformed or odd spelling, second meaning without vowels (A11), easy-to-confuse neighbours, a cleaner form of the same word, and anything else a scholar would object to.
2. **Attestation.** Every entry was looked up as a token in `bucks browser/data/quran_source.json` using the builder's own normalisation. 2045 of 2048 are attested. **Three are not**: `استئس` (112), `كهئة` (1253), `هنئا` (1773). The builder merged an Uthmani yaa-plus-hamza pair into one hamza seat and lost a letter (modern `استيأس`, `كهيئة`, `هنيئا`).
3. **Orthography scan.** Every entry was tested for Uthmani-only features: initial `ءا`, final `ى` that modern writes `ي`, silent alef after a final waw, `ؤا`, hamza on the line, `ت` for `ة` in construct forms, pausal ha, waw for alef. Each result was checked against the fold in `tools/make_dev_wordlist.py` (for example, `آباء` folds to `اباء` but the list holds `ءاباء`, which folds to itself, so a user typing the modern spelling will not match).
4. **Neighbour analysis.** All pairs one edit apart or differing only by dots were listed for every length (309 one-edit pairs; 251 have both words of 5+ letters, where rules A2 and A3 do not apply; 22 dot-twin groups). Entries sharing a root or lemma family were grouped.
5. **Neighbour-word test.** For every entry the share of its Quran occurrences next to a word on the exclusion list was computed. Entries above 60% with at least two uses and not already flagged were added (37 hits; six kept as Consider/Should).
6. **Citations audited.** Every verse reference quoted in a reason was machine-checked against the Quran text. The surface counts in the CSV (`[Quran surface count: n]`) are recomputed from the text; the `quran_occurrences` column of the review CSV is a lemma count and differs (for example `تتلى`: lemma 39, surface 16).
7. **Replacements.** The build was reproduced byte-for-byte from `quran-morphology.txt` (EXACT=2, scratch copy, repo untouched). The 102 backfill suggestions come from that run's *leftover pool*: words that passed every builder rule (A1-A9, exclusions, typo rules) but were cut at 2048. They are therefore attested in the Quran, not in the list, and free of list conflicts. Respellings were checked for fold collisions with the list (none) and mapped to the attested Uthmani token. Bare-form fixes are attested tokens not in the list.

Sensitivity judgements follow the research note: a word is Must-fix when it is on an excluded list in a form the matcher missed, or is a liturgical/forbidden word; Should-fix when the word is tied to a single excluded context (a prophet narrative, a hell or paradise element, a formula); Consider when it is ordinary vocabulary with a strong association.

## 3. Systematic problems the build tool should handle

1. **Uthmani spellings leak into the modern list (about 80 entries).** The exact-form mode strips marks but keeps Uthmani letter choices. Cases: initial `ءا` for `آ` (16 entries, indices 0-15; `ء` survives the fold, so the modern spelling does not match); final `ى` where modern writes `ي` (33: pronoun yaa, `ثانى`, `تبتغى`, `يستوى`, `يمشى`, `ينبغى`, and so on); silent alef after the final waw of a singular verb (`يدعوا`, `يرجوا`, `يربوا`, `يمحوا`); alef after `ؤ` (`أتوكؤا`, `تظمؤا`, `تفتؤا`, `يبدؤا`, `يتفيؤا`, `ينشؤا`, `يعبؤا`, `امرؤا`); hamza on the line (`رءوف`, `مبرءون`, `مذءوما`, `ويدرءون`, `رءياى`); waw for alef (`حيواة`); `ت` for `ة` in construct forms (`امرأت`, `جمالت`, `غيابت`, `مرضات`); the joined vocative `يامعشر`; pausal ha (`يتسنه`, `اقتده`) and dropped final yaa (`ننج`, `لتردين`); a construct alef (`أقصا` for `أقصى`). Fix: derive the shown form from the lemma's modern orthography, not the token, or add a per-entry override table; then assert `arabic_fold(shown)` equals the fold of what a modern typist would produce.
2. **Lost letters in hamza-on-yaa words (3 entries, not real words).** `استئس`, `كهئة`, `هنئا` and `يلون` (small waw dropped from `يلوون`). Fix: do not remove U+06E5/U+06E6/U+06E7 or compose yeh+hamza without keeping the yeh; add a test that every entry is a token of the Quran text after modern respelling.
3. **The exclusion matcher is exact-word and spelling-dependent, so whole families escape.** `رءوف` (divine name) escaped only because the exclusion has `رووف`. Inflections and derivatives of excluded words are in the list: `تتلى` (تلا), `ترتيلا`/`ورتل` (قرأ/رتل), `قانتات`/`قانتون` (قنت), `تبتيلا` (تبتل), `متابا` (توبة), `منسكا` (نسك), `اعتمر` (عمرة), `يطوف` (طواف), `وإقام`/`وإيتاء` (the prayer and zakat formula), `عبادته`, `نصليه`/`يصلى`/`صالوا` (صلي), `تلظى` (لظى), `وسعر` (سعير), `أرحم` (رحيم), `علام`/`قادر`/`مقتدر`/`فاطر`/`مقيتا` (divine names), `نزل` (انزل). Fix: add roots `بتل توب نسك قنت رتل لظي صلي سعر` to the `root` line, add the folded lemmas above to the word lists, and match after stripping attached prefixes and pronoun suffixes (`ومأواهم`, `عبادته`, `وإقام`).
4. **Eschatological, scripture and narrative hapaxes make up the largest group (about 190 + 70 entries).** Words used once or twice, always inside one hell, paradise, Last-Day, prophet or ritual context, are not on any list because the exclusion is by word, not by context. Many are harmless in isolation (`سرر`, `سندس`, `رفرف`), others are the vocabulary of one named scene (`ضريع`, `يحموم`, `مقامع`, `ثيبات` for the Prophet's wives, `خالاتك`/`عماتك` for 33:50). Fix: give the tool a context test. Use the neighbour-word test of section 2 (share of uses next to an excluded word) plus a single-verse test (all uses in one verse or one sura named in categories b-e), report the hits, and let the adviser decide. This review found 37 further entries by the neighbour test alone.
5. **Formula words (about 35 entries).** Words whose Quranic uses are nearly all one formula: `بإذن` (بإذن الله), `حسبنا`, `أدراك` (وما أدراك ما), `بحمد` (سبح بحمد ربك), `أعوذ`/`فاستعذ`, `تعالى`, `معاذ`, `نستعين`, `رضى` (رضي الله عنهم), `يشاء`, `يحب`, `يعلم`, `مستقيم`, `لقاء`, `وأعتدنا`, `فذوقوا`. Category h lists only a handful of words (`قل بسم سبحان تبارك ياايها` and a few particles). Fix: add every flagged form to category h and automate with the neighbour test.
6. **A11 homographs are checked only against corpus lemmas.** The second meaning is often outside the Quran lemma list, in modern Arabic or in a function-word spelling: `تكن` (21 uses of the كان jussive), `دعا` (he called; دعاء is excluded), `يصلى`/`يصلي` (prayer), `بنى`/`بني`, `سنة` (year, sleep, sunna), `خفت` (light / I feared), `ذروا`, `كدنا`, `وكل`, `يمل` (he gets bored), `هاجر` (Hagar), `نيلا` (the Nile), `طائف`, `خضر` (al-Khidr), `غول` (ghoul), `جرم`, `ذنوب`, `فراغ`, `سلما`. Fix: fold every candidate and look it up in (a) all corpus segments including particles and verbs (b) a modern Arabic frequency list and a names list; drop on any hit.
7. **Near-duplicates in words of 5+ letters and same-lexeme pairs.** Rules A2 and A3 apply only to words of 4 letters or fewer, so 251 pairs of longer words differ by one letter. Clusters: `سرابا`/`سراجا`/`سراحا`/`سراعا`/`ترابا`/`غرابا`; `محجورا`/`محذورا`/`محسورا`/`محظورا`/`مهجورا`; `مستورا`/`مسحورا`/`مسرورا`/`مسطورا`; `مجنون`/`مسنون`/`مغنون`/`مكنون`/`ممنون`; dot twins `اختلاف`/`اختلاق`, `تفصيلا`/`تفضيلا`, `نفيرا`/`نقيرا`. Rule A5 is not enforced for number and gender: `اثنين`/`اثنتين`, `كثيرا`/`كثيرة`, `صغيرا`/`صغيرة`, `عاليا`/`عالين`/`عالية`, `صديق`/`صديقا`/`صديقة`, `مفتر`/`مفترون`/`مفترى`/`مفتريات`, `رابيا`/`رابية`, `كاشف`/`كاشفة`. Fix: extend A2/A3 to words of up to 6 letters or to clusters of 3 or more; for A5 dedupe on root plus pattern, not on the corpus lemma string (which differs per gender form, so the existing same-lemma check found nothing).
8. **Function words and auxiliaries pass the content-word filter.** The QAC tags do not mark interjections and auxiliaries: `هلم`, `هاتوا`, `فلانا`, `يزال`, `يكاد`, `كلتا`, `كافة`, `هيهات`, `وكأين`, `ويكأن`, `غير`, `مرحبا`. Fix: add a stoplist of these lemmas.
9. **Attached forms.** About 230 entries carry a prefix or pronoun suffix (`ف`, `و`, `ب`, `ل`, `ك`, pronouns `ه ها هم كم نا ك`). Most are intended (tier 2). Some have a bare form attested and unused (`وثلاث`→`ثلاث`, `فأجمعوا`→`أجمعوا`, `وأبقى`→`أبقى`; 20 listed). Some pull in an excluded context through the pronoun (`ومأواهم`, `عبادته`). The count rises if the tool backfills (see 11). Consider capping tier 2 at 15% and preferring the bare form when it exists.
10. **Accusative alef.** About 500 entries (24%) end in the tanween alef (`ترابا`, `باطلا`); 67 of them have the unmarked form attested in the Quran and unused (`تراب`, `إثم`, `حكم`, `قول`). The accusative alef is easy to omit when handwriting. Consider preferring the bare noun when it exists.
11. **Backfill capacity is the practical limit.** The builder had 2722 candidates after the typo rules; 674 were left over, all tier-2 (attached) forms. After removing those tied to prophets, ritual, hell, paradise or the Last Day, about 100 are clean (the 102 suggestions in the CSV). Applying every Should-fix drop would need about 300 new words, and the pool cannot supply them under the current rules. Either relax rules (allow forms longer than 7 letters, allow stripping of ال, allow A5 to admit a second form of a high-frequency lemma) or keep the Should-fix categories (b) and (c) as notes.
12. **Glosses in the review CSV are unreliable.** The gloss comes from the lemma, not from the form in the list: `يعص` is glossed 'staff' but means 'he disobeys' (4:14, 33:36, 72:23); `ثوب` 'recompensed'; `بطن` 'inner'; `تسمع` 'proclaim'; `فراغ` 'slink'; `ذنوب` 'share'. A translator or reviewer reading these will be misled. Fix: gloss from the form's own morphology or mark the gloss as lemma-level.

## 4. Decisions for the owner

* Apply the 90 Must-fix rows. Of these, about 30 are spelling fixes and the rest remove or replace excluded or forbidden words; none is a matter of taste.
* Apply the rule-based Should rows (spelling 47, near-duplicates 20, second meanings 20, formulae and root families). Spelling Should rows are respellings and need no new word.
* Decide the policy for categories (b) and (c): remove the 139 named-scene and hell/paradise/Last-Day words, or keep them and add a sentence to the adviser packet. The CSV marks each row so the decision can be made group by group.
* Do not apply every drop at once: the backfill pool holds about 100 clean words (section 3, point 11).

## 5. Flagged entries by issue type

Columns: index in the list (0-based), word as listed, severity, `n` = surface count in the Quran text, reason (shortened; the CSV has the full text), proposed action. `replace: X` means swap in X (a respelling or bare form of the same word, or, where the reason names a sensitivity, an unrelated backfill word taken from the builder's leftover pool).

### a. Divine names and attributes (37: 8 Must, 14 Should, 15 Consider)

| # | Word | Sev | n | Reason | Action |
|---|---|---|---|---|---|
| 86 | أرحم | Must | 4 | أرحم is the divine superlative in أرحم الراحمين (every use); category a covers attributes in every form | replace: يحاوره |
| 713 | رءوف | Must | 6 | رءوف is al-Ra'uf (divine name); the exclusion رووف only matched because of the folded spelling ؤ=و; the Uthmani spelling رءوف escaped | replace: يرضعن |
| 1038 | علام | Must | 4 | علام occurs only in علام الغيوب (5:109,116; 9:78; 34:48), a divine attribute (عليم excluded) | replace: تريحون |
| 1098 | فاطر | Must | 6 | فاطر occurs only as the divine epithet فاطر السماوات والأرض, an exclusive attribute of God | replace: تسيمون |
| 1153 | قادر | Must | 2 | قادر is al-Qadir, a name of God (6:37 and 17:99 إن الله قادر على); قدير is excluded | replace: تغفلون |
| 1244 | كفوا | Must | 2 | كفوا: 112:4 (ولم يكن له كفوا أحد), the last verse of the Ikhlas sura whose other words احد/صمد are excluded; the same spelling is also the imperative 'restrain' in 4:77 | replace: تفكهون |
| 1546 | مقتدر | Must | 2 | مقتدر (54:42, 54:55) is a divine name (al-Muqtadir) | replace: فسيحوا |
| 1563 | مقيتا | Must | 1 | مقيتا is a divine attribute (4:85 وكان الله على كل شيء مقيتا, al-Muqit); modern مقيت means odious (A11) | replace: فيحفكم |
| 735 | رحمة | Should | 34 | رحمة is God's mercy (mostly رحمة الله / رحمة ربك), the noun of the excluded names رحمن/رحيم; also a woman's name | drop |
| 862 | شاكرا | Should | 3 | شاكرا: divine attribute الشاكر (4:147 وكان الله شاكرا عليما); شكور excluded | drop |
| 1215 | كاشف | Should | 2 | كاشف: 'none can remove it but God' (6:17, 10:107); divine act | drop |
| 1368 | محيط | Should | 7 | محيط: mostly God's encompassing (والله بما يعملون محيط) | drop |
| 1523 | مغلولة | Should | 2 | مغلولة (5:64) the quote يد الله مغلولة (Jews about God); sensitive | drop |
| 1603 | منتقمون | Should | 3 | منتقمون: God's vengeance (32:22; 43:41; 44:16); al-Muntaqim attribute | drop |
| 1674 | ناضرة | Should | 1 | ناضرة occurs only in 75:22 (وجوه يومئذ ناضرة إلى ربها ناظرة), the vision of God | drop |
| 1675 | ناظرة | Should | 1 | ناظرة occurs only in 75:23, the vision of God (with ناضرة) | drop |
| 1779 | واحدة | Should | 29 | واحدة: feminine of واحد (excluded); 'نفس واحدة', 'صيحة واحدة' | drop |
| 1816 | وحده | Should | 6 | وحده: God alone, tawhid formula (وحده لا شريك له) | drop |
| 1861 | وهب | Should | 3 | وهب (14:39 Ibrahim's thanks for Ishmael and Isaac; 19:53; root وهب of excluded وهاب) | drop |
| 1907 | يحب | Should | 39 | يحب: الله يحب المحسنين / المتقين / التوابين; divine attribute (wording of the verse endings) | drop |
| 1954 | يشاء | Should | 116 | يشاء: mostly the formula من يشاء / ما يشاء, God's will, in verse endings such as يغفر لمن يشاء | drop |
| 1984 | يعلم | Should | 71 | يعلم: mostly الله يعلم / والله يعلم; عليم is excluded | drop |
| 269 | باقية | Consider | 2 | باقية is the feminine of الباقي, a divine name; root بقي | keep-note |
| 273 | بالحسنى | Consider | 3 | بالحسنى resembles الأسماء الحسنى; attached ب and article | keep-note |
| 465 | تقدير | Consider | 3 | تقدير occurs only in ذلك تقدير العزيز العليم (6:96، 36:38، 41:12) with two divine names | keep-note |
| 893 | صاحبة | Consider | 2 | صاحبة occurs in the negation 'God has no consort' (6:101, 72:3) | keep-note |
| 902 | صبغة | Consider | 2 | صبغة occurs mostly as صبغة الله (2:138) | keep-note |
| 1023 | عزة | Consider | 1 | عزة: العزة لله; root عزز of excluded عزيز | keep-note |
| 1053 | غالب | Consider | 3 | غالب (12:21 والله غالب على أمره) is a divine attribute | keep-note |
| 1164 | قاهرون | Consider | 1 | قاهرون root قهر of excluded قهار (Pharaoh's boast, 7:127) | keep-note |
| 1381 | مدادا | Consider | 1 | مدادا (18:109): ink for the words of my Lord | keep-note |
| 1509 | معقب | Consider | 1 | معقب (13:41 لا معقب لحكمه), divine | keep-note |
| 1526 | مفاتح | Consider | 1 | مفاتح: 6:59 وعنده مفاتح الغيب; 28:76 Qarun | keep-note |
| 1540 | مقاليد | Consider | 2 | مقاليد (39:63; 42:12) له مقاليد السماوات والأرض, God's | keep-note |
| 1912 | يحكم | Consider | 17 | يحكم: the judgement of God (الله يحكم بينكم يوم القيامة) ; حكيم excluded | keep-note |
| 1940 | يستجيب | Consider | 2 | يستجيب: God answering (42:26); مجيب is excluded | keep-note |
| 2037 | يؤاخذ | Consider | 2 | يؤاخذ: God holding people to account (يؤاخذكم الله, 2:225; 16:61); found by the neighbour-word check | keep-note |

### b. Prophets, angels, persons, peoples, places, narratives (68: 3 Must, 26 Should, 39 Consider)

| # | Word | Sev | n | Reason | Action |
|---|---|---|---|---|---|
| 199 | الرس | Must | 2 | الرس is a named people/place (أصحاب الرس, 25:38، 50:12) and carries ال (A4) | replace: عقدتم |
| 974 | طور | Must | 1 | طور (23:20 طور سيناء) is Mount Sinai (Tur), a named place | replace: بدينار |
| 1758 | هادوا | Must | 10 | هادوا: 'those who became Jews' (الذين هادوا); same root as excluded يهود | replace: مصيبها |
| 248 | أواه | Should | 1 | أواه is the epithet of Ibrahim (9:114، 11:75); also an interjection in Arabic (A11) | replace: لأعنتكم |
| 655 | خضر | Should | 4 | خضر: homograph of الخضر (Khidr, the figure of Surah al-Kahf in tradition), and mostly paradise garments (18:31, 76:21) | drop |
| 909 | صديقا | Should | 2 | صديقا: epithet of prophets Ibrahim, Idris (19:41, 19:56); siddiq | drop |
| 910 | صديقة | Should | 1 | صديقة: epithet of Mary (5:75) | drop |
| 925 | صلبوه | Should | 1 | صلبوه (4:157) is the crucifixion of Jesus; doctrinally sensitive | drop |
| 1334 | مثابة | Should | 1 | مثابة occurs only in 2:125 (the Ka'ba as a resort); holy place | drop |
| 1367 | محمودا | Should | 1 | محمودا: 17:79 مقاما محمودا (the Prophet's station); from the root of excluded محمد/احمد | drop |
| 1403 | مردفين | Should | 1 | مردفين (8:9) is the thousand angels at Badr; Badr excluded | drop |
| 1459 | مسومين | Should | 1 | مسومين (3:125) are the five thousand angels at Badr; attached | drop |
| 1517 | مغاضبا | Should | 1 | مغاضبا occurs only in 21:87 (Yunus, ذا النون) | drop |
| 1541 | مقام | Should | 7 | مقام: مقام إبراهيم (2:125; 3:97) and مقام ربه | drop |
| 1574 | مكظوم | Should | 1 | مكظوم occurs only in 68:48 (Yunus and the whale) | drop |
| 1607 | منذر | Should | 5 | منذر is the Prophet's role (إنما أنت منذر) | drop |
| 1628 | مهاجر | Should | 1 | مهاجر: the Hijra (29:26 Ibrahim; 4:100); a historic religious term | drop |
| 1629 | مهاجرات | Should | 1 | مهاجرات (60:10) the women emigrants of the Hijra | drop |
| 1678 | ناقة | Should | 4 | ناقة: the she-camel of Salih (ناقة الله, 7:73; 11:64; 26:155) | drop |
| 1694 | نذير | Should | 29 | نذير is the Prophet's role (إنما أنا نذير مبين); epithet of messengers | drop |
| 1796 | والسلوى | Should | 3 | والسلوى (2:57; 7:160; 20:80) the quails of the Israelites; attached و+ال | drop |
| 1815 | وجيها | Should | 2 | وجيها: epithet of Jesus (3:45) and Moses (33:69) | drop |
| 1833 | وسيدا | Should | 1 | وسيدا (3:39) John the Baptist as sayyid; attached و | drop |
| 1839 | وطفقا | Should | 2 | وطفقا occurs only in 7:22 and 20:121 (Adam and Hawwa covering themselves) | drop |
| 1850 | وقفينا | Should | 3 | وقفينا (2:87; 5:46; 57:27) follows the prophets with Jesus | drop |
| 1853 | وكهلا | Should | 2 | وكهلا occurs only in 3:46 and 5:110 (Jesus speaks 'in the cradle and as a grown man') | drop |
| 1898 | يثخن | Should | 1 | يثخن occurs only in 8:67 (the prisoners of Badr) | drop |
| 1946 | يستنكف | Should | 2 | يستنكف occurs only in 4:172 (Jesus and the angels do not disdain being servants of God) | drop |
| 1999 | يقطين | Should | 1 | يقطين occurs only in 37:146 (Yunus under the plant) | drop |
| 177 | إفك | Consider | 4 | إفك also names the slander episode of Surah al-Nur (حديث الإفك, Aisha); judgement | keep-note |
| 214 | أميون | Consider | 1 | أميون (unlettered) refers to the Arabs/unlettered scripture-less people, root of 'the unlettered prophet'; judgement | keep-note |
| 257 | أئمة | Consider | 5 | أئمة (leaders, imams) carries the religious office imam (أئمة الكفر / أئمة يهدون بأمرنا) | keep-note |
| 439 | تظاهرا | Consider | 2 | تظاهرا addresses two of the Prophet's wives (66:4) | keep-note |
| 462 | تقاة | Consider | 1 | تقاة is the verse often cited for taqiyya (3:28), sectarian association | keep-note |
| 480 | تلقف | Consider | 3 | تلقف belongs to the story of Moses' staff (7:117، 20:69، 26:45) | keep-note |
| 592 | حزب | Consider | 7 | حزب occurs mostly in حزب الله / حزب الشيطان; modern political 'party' and Hezbollah | keep-note |
| 626 | خالاتك | Consider | 1 | خالاتك occurs only in 33:50, the verse of the Prophet's permitted marriages | keep-note |
| 630 | خالك | Consider | 1 | خالك occurs only in 33:50, the verse of the Prophet's permitted marriages | keep-note |
| 665 | خليلا | Consider | 3 | خليلا: the epithet of Ibrahim (4:125 خليل الله) | keep-note |
| 710 | ذمة | Consider | 2 | ذمة is the covenant of protection (dhimma, 9:8,10) | keep-note |
| 802 | سائحات | Consider | 1 | سائحات occurs only in 66:5, the verse about the Prophet's wives | keep-note |
| 817 | سراجا | Consider | 3 | سراجا (33:46 وسراجا منيرا) is an epithet of the Prophet | keep-note |
| 857 | سيهزم | Consider | 1 | سيهزم (54:45) is the verse about the Battle of Badr (excluded) | keep-note |
| 911 | صرحا | Consider | 2 | صرحا: Pharaoh/Haman (28:38, 40:36) and the Queen of Sheba | keep-note |
| 928 | صلصال | Consider | 4 | صلصال: the material of the creation of man (15:26, 55:14) | keep-note |
| 1041 | عماتك | Consider | 1 | عماتك occurs only in 33:50, the verse of the Prophet's permitted marriages | keep-note |
| 1042 | عمك | Consider | 1 | عمك occurs only in 33:50, the Prophet's marriage verse | keep-note |
| 1059 | غدوت | Consider | 1 | غدوت (3:121) is the Battle of Uhud verse | keep-note |
| 1083 | غيابت | Consider | 2 | غيابت is the pit of Joseph (12:10, 12:15); Uthmani plural spelling (modern غيابة) | drop |
| 1141 | فعقروها | Consider | 3 | فعقروها (11:65; 26:157; 91:14): Salih's she-camel | keep-note |
| 1196 | قلى | Consider | 1 | قلى occurs only in 93:3 (ما ودعك ربك وما قلى), addressed to the Prophet | keep-note |
| 1200 | قميصه | Consider | 5 | قميصه: Joseph's shirt (12:18, 25, 26...) | keep-note |
| 1207 | قوسين | Consider | 1 | قوسين (53:9) the vision of Gabriel | keep-note |
| 1252 | كهفهم | Consider | 2 | كهفهم: the cave of the sleepers (Surah al-Kahf) | keep-note |
| 1254 | كوكبا | Consider | 2 | كوكبا: Ibrahim (6:76) and Yusuf (12:4) | keep-note |
| 1275 | لفتاه | Consider | 2 | لفتاه: Moses and his young servant (18:60) | keep-note |
| 1295 | مأكول | Consider | 1 | مأكول (105:5) the Elephant sura | keep-note |
| 1298 | مائدة | Consider | 2 | مائدة is Surat al-Ma'ida; the table of Jesus (5:112-114) | keep-note |
| 1335 | مثبورا | Consider | 1 | مثبورا: Pharaoh (17:102) | keep-note |
| 1351 | محررا | Consider | 1 | محررا: dedicated to God's service by Mary's mother (3:35) | keep-note |
| 1455 | مسنون | Consider | 3 | مسنون: creation of man from molded clay | keep-note |
| 1506 | معزل | Consider | 1 | معزل (11:42): Noah's son | keep-note |
| 1519 | مغتسل | Consider | 1 | مغتسل (38:42) Ayyub's spring | keep-note |
| 1606 | منجوك | Consider | 1 | منجوك is Lot's angelic message (29:33) | keep-note |
| 1640 | مواطن | Consider | 1 | مواطن occurs only in 9:25 (Hunayn, excluded) | keep-note |
| 1652 | موعدة | Consider | 1 | موعدة occurs only in 9:114 (Ibrahim and his father) | keep-note |
| 1828 | وزيرا | Consider | 2 | وزيرا is Moses' request for Haroun (20:29) and 25:35 | keep-note |
| 1837 | وصى | Consider | 1 | وصى occurs only in 42:13 (وصى به نوحا) | keep-note |

### c. Last-day, hell, paradise, unseen vocabulary (188: 12 Must, 113 Should, 63 Consider)

| # | Word | Sev | n | Reason | Action |
|---|---|---|---|---|---|
| 478 | تلظى | Must | 1 | تلظى is the hell-fire verb (92:14), the root لظي is excluded (category c) | replace: وحفدة |
| 479 | تلفح | Must | 1 | تلفح is hellfire (23:104 تلفح وجوههم النار) | replace: وريشا |
| 732 | رجيم | Must | 4 | رجيم occurs only in الشيطان الرجيم / شيطان رجيم (the epithet of Satan); shaytan is excluded | replace: أبرموا |
| 899 | صالوا | Must | 1 | صالوا (38:59 إنهم صالوا النار): enterers of hell fire; root صلي (excluded as صلي) | replace: أغفلنا |
| 944 | ضريع | Must | 1 | ضريع occurs only in 88:6 (the food of hell); consistent with excluded غسلين/غساق | replace: أوجفتم |
| 1033 | عفريت | Must | 1 | عفريت (27:39) is a demon of the jinn; jinn excluded | replace: تحاضون |
| 1283 | لواحة | Must | 1 | لواحة occurs only in 74:29 (hell, Saqar) ; سقر is excluded | replace: ركوبهم |
| 1389 | مذءوما | Must | 1 | مذءوما occurs only in 7:18 (اخرج منها مذءوما مدحورا) to Iblis; Uthmani spelling (modern مذؤوما) | replace: عاديتم |
| 1713 | نصليه | Must | 1 | نصليه (4:30) 'we shall roast him in fire' (other forms in 4:115, 74:26); صلي is excluded | replace: مبلغهم |
| 1858 | ومأواهم | Must | 6 | ومأواهم: ومأواهم النار / ومأواهم جهنم (hell formula); attached و | replace: وتفاخر |
| 1915 | يحموم | Must | 1 | يحموم occurs only in 56:43 (the black smoke of hell) | replace: يجمحون |
| 1966 | يصلى | Must | 3 | يصلى (87:12; 84:12 يصلى سعيرا) burning in hell; bare spelling is also يصلي 'he prays' (A11) and صلي is excluded | replace: يخربون |
| 91 | أزفت | Should | 1 | أزفت is the verb of الآزفة (the Hour drew near, 53:57); last-day term | replace: تداينتم |
| 98 | إستبرق | Should | 1 | إستبرق is paradise-garment vocabulary (18:31, 44:53, 55:54, 76:21: every Quranic use) | replace: تعاسرتم |
| 169 | أغويتنى | Should | 2 | أغويتنى is Iblis' accusation to God (7:16; 15:39 رب بما أغويتني); final ى for ي and attached pronoun; excluded Iblis context | replace: تنابزوا |
| 180 | أقتت | Should | 1 | أقتت is the time-fixing of the messengers in the Last Day sura al-Mursalat (77:11); eschatological | replace: توكيدها |
| 228 | أنشره | Should | 1 | أنشره is the resurrection verb (80:22 ثم إذا شاء أنشره); نشور and بعث excluded | replace: فاستغلظ |
| 242 | انكدرت | Should | 1 | انكدرت occurs only in 81:2 (إذا النجوم انكدرت), a Takwir last-day sign | replace: كالصريم |
| 288 | بررة | Should | 1 | بررة occurs only in 80:16 (كرام بررة), the angelic scribes; with سفرة (825) it forms one angel phrase | replace: مانعتهم |
| 289 | برزخ | Should | 2 | برزخ (barzakh) is the intermediate state after death (23:100) as well as the barrier of 55:20; eschatological term | replace: والبغال |
| 309 | بعثر | Should | 1 | بعثر upheaved: بعثرت القبور (82:4، 100:9), resurrection day; بعث family excluded | replace: وزيتونا |
| 577 | حامية | Should | 2 | حامية is hellfire (88:4 نارا حامية; 101:11 نار حامية) | drop |
| 625 | خافضة | Should | 1 | خافضة is from 56:3 (خافضة رافعة), the Event (الواقعة, excluded) sura | drop |
| 627 | خالدين | Should | 44 | خالدين is nearly always خالدين فيها: abiding for ever in hell/paradise; eschatological formula | drop |
| 646 | خزنتها | Should | 3 | خزنتها (39:71, 39:73, 67:8) are the keepers of hell/paradise, the angelic wardens | drop |
| 741 | رضوان | Should | 3 | رضوان (3:162; 3:174; 57:27) divine pleasure; Ridwan is also the name of paradise's keeper in tradition and a common name | drop |
| 746 | رفرف | Should | 1 | رفرف (55:76) is paradise upholstery | drop |
| 768 | زجرة | Should | 2 | زجرة (37:19, 79:13) is the Last Day blast | drop |
| 774 | زفير | Should | 2 | زفير (11:106, 25:12) is the sound of the damned in hell | drop |
| 776 | زلزلت | Should | 1 | زلزلت occurs in 99:1 (إذا زلزلت الأرض زلزالها), the Last Day sura | drop |
| 777 | زلزلة | Should | 1 | زلزلة occurs in 22:1 (إن زلزلة الساعة شيء عظيم); الساعة is excluded | drop |
| 780 | زمرا | Should | 2 | زمرا occurs only in 39:71,73 (hell and paradise groups) | drop |
| 781 | زمهريرا | Should | 1 | زمهريرا (76:13) is bitter cold, hell in tradition | drop |
| 786 | زوجت | Should | 1 | زوجت occurs in 81:7 (وإذا النفوس زوجت), the Takwir last-day verse; also modern 'I married' | drop |
| 821 | سرر | Should | 5 | سرر: couches of paradise (every use) | drop |
| 825 | سفرة | Should | 1 | سفرة occurs only in 80:15, the angelic scribes | drop |
| 847 | سندس | Should | 3 | سندس is the paradise silk (18:31, 44:53, 76:21 only) | drop |
| 856 | سيرت | Should | 2 | سيرت (78:20, 81:3) the mountains set moving on the Last Day | drop |
| 861 | شافعين | Should | 1 | شافعين (26:100) intercession (Last Day) | drop |
| 879 | شفاعة | Should | 6 | شفاعة is intercession on the Day of Judgement | drop |
| 880 | شفيع | Should | 5 | شفيع is the intercessor of the Last Day | drop |
| 887 | شهيقا | Should | 1 | شهيقا (67:7) the sound of the damned in hell (with زفير in 11:106) | drop |
| 888 | شواظ | Should | 1 | شواظ (55:35) is hellfire flame | drop |
| 1001 | عاملة | Should | 1 | عاملة occurs only in 88:3 (عاملة ناصبة), where ناصبة is excluded | drop |
| 1061 | غراما | Should | 1 | غراما is the punishment of hell (25:65); modern 'passion / gram' (A11) | drop |
| 1080 | غواش | Should | 1 | غواش (7:41) is the covering of hellfire above the damned | drop |
| 1100 | فاقرة | Should | 1 | فاقرة (75:25) is a calamity of the Last Day sura (القيامة) | drop |
| 1103 | فاكهين | Should | 2 | فاكهين (44:27; 52:18): delighted, said of the people of paradise | drop |
| 1138 | فصعق | Should | 1 | فصعق is the trumpet blast of the Last Day (39:68) | drop |
| 1142 | ففزع | Should | 2 | ففزع is the trumpet terror of the Last Day (27:87) | drop |
| 1156 | قاصرات | Should | 3 | قاصرات is the houris (قاصرات الطرف, 37:48; 38:52; 55:56) | drop |
| 1182 | قرين | Should | 2 | قرين (43:36 a devil assigned as companion; 37:51) the jinn/devil companion | drop |
| 1191 | قطران | Should | 1 | قطران (14:50) is the tar garments of hell | drop |
| 1216 | كاشفة | Should | 1 | كاشفة (53:58) the Hour has none to remove it besides God | drop |
| 1219 | كالحون | Should | 1 | كالحون (23:104) the disfigured faces of the damned | drop |
| 1220 | كالمهل | Should | 3 | كالمهل: molten brass of hell (18:29; 44:45; 70:8); attached ك and ال | drop |
| 1240 | كشطت | Should | 1 | كشطت (81:11) is a Takwir last-day verse | drop |
| 1255 | لابثين | Should | 1 | لابثين occurs only in 78:23 (لابثين فيها أحقابا), hell | drop |
| 1258 | لاغية | Should | 1 | لاغية (88:11) is the paradise description | drop |
| 1278 | لقاء | Should | 8 | لقاء: لقاء ربهم / لقاء الله / لقاء الآخرة, meeting God on the Last Day | drop |
| 1293 | مارج | Should | 1 | مارج (55:15) is the jinn's fire; jinn excluded | drop |
| 1294 | ماكثين | Should | 1 | ماكثين (18:3): abiding for ever in paradise; 43:77 has the hell counterpart ماكثون | drop |
| 1311 | مبلسون | Should | 3 | مبلسون (6:44; 23:77; 43:75) the despairing, the root of إبليس (excluded) | drop |
| 1341 | مثوى | Should | 9 | مثوى: mostly the abode in hell (النار مثواكم) | drop |
| 1361 | محضرون | Should | 5 | محضرون: mostly those brought to the punishment/Reckoning | drop |
| 1375 | مخضود | Should | 1 | مخضود (56:28) paradise | drop |
| 1376 | مخلدون | Should | 2 | مخلدون (56:17; 76:19) the immortal youths of paradise | drop |
| 1383 | مدحورا | Should | 3 | مدحورا (7:18; 17:18, 39): Iblis' expulsion, hell | drop |
| 1399 | مرجعكم | Should | 11 | مرجعكم: إلى الله مرجعكم / إلى ربكم مرجعكم, return to God | drop |
| 1405 | مرساها | Should | 2 | مرساها (7:187; 79:42) are the questions about the Hour; الساعة excluded | drop |
| 1407 | مرصادا | Should | 1 | مرصادا occurs only in 78:21 (إن جهنم كانت مرصادا) | drop |
| 1417 | مرقوم | Should | 2 | مرقوم (83:9,20) the record of deeds (كتاب مرقوم) | drop |
| 1423 | مزيد | Should | 2 | مزيد: 50:30, 50:35 the dialogue of hell/paradise (هل من مزيد) | drop |
| 1490 | مطهرة | Should | 5 | مطهرة: purified spouses of paradise (2:25; 3:15; 4:57); 80:14 and 98:2 the pure scrolls | drop |
| 1491 | مطويات | Should | 1 | مطويات (39:67) the heavens rolled up in His right hand on the Last Day | drop |
| 1510 | معقبات | Should | 1 | معقبات (13:11) are angels who guard | drop |
| 1527 | مفازا | Should | 1 | مفازا (78:31) paradise for the God-fearing | drop |
| 1528 | مفتحة | Should | 1 | مفتحة (38:50) the opened gates of the gardens of Eden | drop |
| 1542 | مقامع | Should | 1 | مقامع (22:21) the iron rods of hell | drop |
| 1545 | مقتحم | Should | 1 | مقتحم (38:59) the damned troop in hell | drop |
| 1552 | مقداره | Should | 2 | مقداره (32:5; 70:4) the length of the Day | drop |
| 1557 | مقصورات | Should | 1 | مقصورات (55:72) the houris in the tents | drop |
| 1572 | مكرمون | Should | 3 | مكرمون (21:26 angels; 37:42 and 70:35 paradise) | drop |
| 1579 | ملاق | Should | 1 | ملاق occurs in 69:20 (meeting my reckoning); meeting God/Reckoning | drop |
| 1580 | ملاقوا | Should | 3 | ملاقوا (2:46 ملاقوا ربهم; 2:249) meeting the Lord | drop |
| 1615 | منضود | Should | 2 | منضود (11:82 stones of sijjil, excluded; 56:29 paradise) | drop |
| 1617 | منفطر | Should | 1 | منفطر occurs only in 73:18 (the heaven rent on the Last Day) | drop |
| 1630 | مهاد | Should | 1 | مهاد (2:206; 3:197; 13:18; 38:56) the bed of hell | drop |
| 1631 | مهانا | Should | 1 | مهانا occurs only in 25:69 (hell, يخلد فيه مهانا) | drop |
| 1635 | مهطعين | Should | 3 | مهطعين (14:43; 54:8; 70:36) the Last Day | drop |
| 1641 | موبقا | Should | 1 | موبقا occurs only in 18:52 (the abyss between idols and their worshippers on the Last Day) | drop |
| 1647 | مؤصدة | Should | 2 | مؤصدة (90:20; 104:8) the sealed fire of hell | drop |
| 1649 | موضونة | Should | 1 | موضونة occurs only in 56:15 (paradise couches) | drop |
| 1654 | موفورا | Should | 1 | موفورا occurs only in 17:63 (to Iblis, jahannam as ample recompense) | drop |
| 1657 | موقوفون | Should | 1 | موقوفون (34:31) the wrongdoers standing before their Lord | drop |
| 1696 | نزاعة | Should | 1 | نزاعة occurs only in 70:16 (hell) | drop |
| 1704 | نسفت | Should | 1 | نسفت occurs only in 77:10 (Last Day sura) | drop |
| 1714 | نضاختان | Should | 1 | نضاختان occurs only in 55:66 (paradise springs) | drop |
| 1715 | نضرة | Should | 2 | نضرة (76:11; 83:24) the glow of paradise; نعيم excluded | drop |
| 1722 | نعقل | Should | 1 | نعقل occurs only in 67:10 (the dialogue of the dwellers of the blaze) | drop |
| 1735 | نقتبس | Should | 1 | نقتبس occurs only in 57:13 (Last Day, 'let us take light'; نور excluded) | drop |
| 1741 | نقيض | Should | 1 | نقيض (43:36 نقيض له شيطانا) assigns a devil; modern 'opposite' | drop |
| 1746 | نكص | Should | 1 | نكص occurs only in 8:48 (Iblis at Badr) | drop |
| 1753 | نورث | Should | 1 | نورث occurs only in 19:63 (the inheritance of paradise) | drop |
| 1772 | همزات | Should | 1 | همزات occurs only in 23:97 (همزات الشياطين); shayatin excluded | drop |
| 1776 | هيهات | Should | 2 | هيهات (23:36) is the interjection of the disbelievers disbelieving the Resurrection; not a content word | drop |
| 1778 | واجفة | Should | 1 | واجفة occurs only in 79:8 (Last Day hearts) | drop |
| 1783 | واردون | Should | 1 | واردون occurs only in 21:98 (you shall enter it: hell) | drop |
| 1793 | وأكواب | Should | 3 | وأكواب (43:71; 76:15; 88:14) the goblets of paradise; attached و | drop |
| 1803 | وبرزت | Should | 2 | وبرزت (26:91; 79:36) 'hell was brought into view' | drop |
| 1830 | وسعر | Should | 2 | وسعر (54:24, 54:47) 'blaze', same root as excluded سعير; modern 'price' | drop |
| 1844 | وفاقا | Should | 1 | وفاقا occurs only in 78:26 (hell: جزاء وفاقا) | drop |
| 1845 | وفدا | Should | 1 | وفدا occurs only in 19:85 (the God-fearing gathered to al-Rahman on the Last Day) | drop |
| 1849 | وقفوا | Should | 2 | وقفوا (6:27 وقفوا على النار; 6:30 وقفوا على ربهم) the Last Day standing | drop |
| 1856 | ولدان | Should | 2 | ولدان (56:17; 76:19) the immortal youths of paradise | drop |
| 1877 | يبلس | Should | 1 | يبلس occurs only in 30:12 (Last Day); root of إبليس | drop |
| 1893 | يتلقى | Should | 1 | يتلقى occurs only in 50:17 (the two recording angels) | drop |
| 1905 | يحاسب | Should | 1 | يحاسب occurs only in 84:8 (Last Day reckoning) | drop |
| 1914 | يحلون | Should | 4 | يحلون (18:31; 22:23; 35:33; 43:? ) adornment of the people of paradise | drop |
| 1916 | يحمى | Should | 1 | يحمى occurs only in 9:35 (heated in the fire of jahannam) | drop |
| 1959 | يشفع | Should | 3 | يشفع: intercession (2:255; 4:85) | drop |
| 1967 | يصهر | Should | 1 | يصهر occurs only in 22:20 (melted by hell water) | drop |
| 2004 | يلاقوا | Should | 3 | يلاقوا (43:83; 52:45; 70:42) meeting their Day | drop |
| 2009 | يلفظ | Should | 1 | يلفظ occurs only in 50:18 (the recording angel) | drop |
| 2030 | ينفخ | Should | 4 | ينفخ: ينفخ في الصور, the trumpet of the Last Day | drop |
| 16 | أبابيل | Consider | 1 | أبابيل belongs to one event (birds of the Elephant, Surah al-Fil); distinctive Quranic story term | keep-note |
| 27 | أترابا | Consider | 2 | أترابا is the age-mates of the houris in the paradise description (56:37, 78:33; 38:52) | keep-note |
| 92 | أزلفت | Consider | 1 | أزلفت used for paradise brought near (26:90، 50:31، 81:13) and hell brought near; eschatological context | keep-note |
| 225 | إنسيا | Consider | 1 | إنسيا: the human (as against جن/انس excluded); 19:26 Mary; low | keep-note |
| 237 | انفطرت | Consider | 1 | انفطرت (82:1 إذا السماء انفطرت) is a last-day sura opener | keep-note |
| 240 | أنكالا | Consider | 1 | أنكالا is a hell punishment (73:12 إن لدينا أنكالا وجحيما) | keep-note |
| 265 | باسرة | Consider | 1 | باسرة is a Last Day face description (75:24) | keep-note |
| 347 | تبسل | Consider | 1 | تبسل (6:70 doom of a soul) in hell/eschatology context | keep-note |
| 393 | تذهل | Consider | 1 | تذهل is the Day-of-Resurrection quake verse (22:2) | keep-note |
| 487 | تمور | Consider | 2 | تمور: the mountain/heaven sway on Judgement Day (52:9) | keep-note |
| 494 | تنفس | Consider | 1 | تنفس (81:18) is a Takwir last-day verse | keep-note |
| 502 | توسوس | Consider | 1 | توسوس root وسوس (whispering of Satan, 50:16) | keep-note |
| 512 | ثقلت | Consider | 4 | ثقلت is the weighing of the scales verb (7:8، 23:102، 101:6) | keep-note |
| 576 | حافين | Consider | 1 | حافين is the angels around the Throne (39:75) | keep-note |
| 603 | حطاما | Consider | 3 | حطاما shares root with excluded حطمة (hell); here the ordinary 'debris' sense | keep-note |
| 660 | خطوات | Consider | 5 | خطوات occurs only in خطوات الشيطان | keep-note |
| 672 | خيرات | Consider | 1 | خيرات 55:70 (فيهن خيرات حسان) refers to the houris of paradise | keep-note |
| 682 | دانية | Consider | 2 | دانية is paradise fruit hanging low (69:23, 76:14) | keep-note |
| 697 | دكة | Consider | 1 | دكة (69:14) pulverising of the earth on the Last Day | keep-note |
| 699 | دهاقا | Consider | 1 | دهاقا (78:34) paradise cups | keep-note |
| 702 | ذائقة | Consider | 3 | ذائقة is the formula كل نفس ذائقة الموت (3:185, 21:35, 29:57) | keep-note |
| 731 | رجوما | Consider | 1 | رجوما (67:5) 'missiles' for the devils | keep-note |
| 736 | رحيق | Consider | 1 | رحيق (83:25 paradise drink) | keep-note |
| 767 | زجاجة | Consider | 1 | زجاجة is part of the light verse (24:35) | keep-note |
| 782 | زنجبيلا | Consider | 1 | زنجبيلا is a paradise drink (76:17) | keep-note |
| 787 | زيادة | Consider | 1 | زيادة 10:26 is understood as the vision of God | keep-note |
| 794 | سافلين | Consider | 1 | سافلين occurs only in أسفل سافلين (95:5) | keep-note |
| 837 | سلسلة | Consider | 1 | سلسلة (69:32) is the chain of hell | keep-note |
| 845 | سموم | Consider | 1 | سموم is the hellfire wind (52:27, 56:42) | keep-note |
| 859 | شاخصة | Consider | 1 | شاخصة (21:97) Last Day stare | keep-note |
| 915 | صعودا | Consider | 1 | صعودا (74:17) the climb in hell | keep-note |
| 1008 | عبوسا | Consider | 1 | عبوسا (76:10) the Day of Judgement | keep-note |
| 1026 | عسعس | Consider | 1 | عسعس occurs only in 81:17 (Takwir) | keep-note |
| 1034 | عقبى | Consider | 4 | عقبى (13:22,24) the final abode (paradise/hell) | keep-note |
| 1050 | عيشة | Consider | 2 | عيشة occurs only in عيشة راضية (paradise) | keep-note |
| 1063 | غرف | Consider | 2 | غرف: chambers of paradise (25:75, 29:58, 39:20) | keep-note |
| 1064 | غرورا | Consider | 5 | غرورا: the deceit of Satan (4:120, 17:64, 35:40) | keep-note |
| 1102 | فاكهة | Consider | 6 | فاكهة is mainly the fruit of paradise (55:11, 55:52, 56:20,32, 38:51) | keep-note |
| 1114 | فتمنوا | Consider | 2 | فتمنوا فتمنوا الموت (2:94, 62:6) | keep-note |
| 1127 | فرادى | Consider | 1 | فرادى: Resurrection (6:94 جئتمونا فرادى) | keep-note |
| 1132 | فرش | Consider | 1 | فرش is paradise furnishing (55:54, 56:34) | keep-note |
| 1169 | قبيلا | Consider | 1 | قبيلا (17:92) lists God and the angels; قبيلا belongs to the -يلا cluster (خليلا, دليلا, قليلا, قبيلا, وبيلا, فتيلا) [near-duplicate] | keep-note |
| 1171 | قترة | Consider | 1 | قترة (10:26, 80:41) the darkening of faces on the Last Day | keep-note |
| 1204 | قوارير | Consider | 1 | قوارير (76:15,16) paradise vessels | keep-note |
| 1211 | كادح | Consider | 1 | كادح (84:6) meeting the Lord on the Last Day | keep-note |
| 1227 | كثيبا | Consider | 1 | كثيبا (73:14) mountains on the Last Day | keep-note |
| 1269 | لذة | Consider | 2 | لذة: paradise drink (37:46, 47:15) | keep-note |
| 1274 | لغوب | Consider | 2 | لغوب (35:35) paradise | keep-note |
| 1292 | مأتيا | Consider | 1 | مأتيا (19:61) the promise of paradise | keep-note |
| 1302 | مبثوثة | Consider | 1 | مبثوثة (88:16) paradise | keep-note |
| 1312 | مبنية | Consider | 1 | مبنية (39:20) the paradise rooms; مبنية and مبينة differ by a transposition/dot (ن/ي) [near-duplicate] | drop |
| 1371 | مختوم | Consider | 1 | مختوم (83:25) sealed paradise drink | keep-note |
| 1397 | مرتفقا | Consider | 2 | مرتفقا (18:29 hell; 18:31 paradise) | keep-note |
| 1416 | مرفوعة | Consider | 3 | مرفوعة: raised couches of paradise (56:34, 88:13) | keep-note |
| 1427 | مساكن | Consider | 1 | مساكن (9:72 paradise dwellings) | keep-note |
| 1428 | مستبشرة | Consider | 1 | مستبشرة: faces on the Last Day (80:39) | keep-note |
| 1450 | مسكوب | Consider | 1 | مسكوب (56:31) paradise water | keep-note |
| 1466 | مشهود | Consider | 1 | مشهود (11:103 ذلك يوم مشهود; 85:3) | keep-note |
| 1479 | مصيرا | Consider | 3 | مصيرا: mostly hell (4:97; 25:15) | keep-note |
| 1549 | مقترنين | Consider | 1 | مقترنين (43:53) the angels | keep-note |
| 1555 | مقرنين | Consider | 4 | مقرنين (14:49 chained in hell; 43:13) | keep-note |
| 1560 | مقطوعة | Consider | 1 | مقطوعة occurs only in 56:33 (paradise fruit, لا مقطوعة ولا ممنوعة) | keep-note |
| 1677 | ناعمة | Consider | 1 | ناعمة (88:8) faces on the Last Day | keep-note |

### d. Worship and ritual (59: 12 Must, 24 Should, 23 Consider)

| # | Word | Sev | n | Reason | Action |
|---|---|---|---|---|---|
| 154 | اعتمر | Must | 1 | اعتمر is the verb of performing umrah (2:158); umrah is an excluded ritual term (category d) | replace: خلطوا |
| 340 | تبتيلا | Must | 1 | تبتيلا is the verbal noun of تبتل, which is excluded (category d: devotion/retreat of worship, 73:8) | replace: فنظرة |
| 1005 | عبادته | Must | 3 | عبادته is worship itself (category d: acts of worship); عبادة is the central ritual noun; attached pronoun | replace: تجسسوا |
| 1162 | قانتات | Must | 2 | قانتات: قنت is excluded (devotion in prayer); this is its feminine plural (33:35, 66:5) | replace: تغمضوا |
| 1163 | قانتون | Must | 2 | قانتون: قنت is excluded (2:116, 30:26) | replace: تفسحوا |
| 1315 | متابا | Must | 1 | متابا is the noun of repentance (25:71, 13:30); توبة, تائب are excluded | replace: شاكلته |
| 1365 | محلقين | Must | 1 | محلقين occurs only in 48:27 (محلقين رءوسكم ومقصرين), the ritual of the pilgrimage | replace: شغلتنا |
| 1513 | معلومات | Must | 2 | معلومات (2:197 أشهر معلومات; 22:28 أيام معلومات) are the Hajj months and days; hajj is excluded | replace: فثبطهم |
| 1610 | منسكا | Must | 2 | منسكا (22:34, 22:67) is the rite of worship (manasik); نسك is excluded | replace: فيدمغه |
| 1792 | وإقام | Must | 2 | وإقام occurs only in وإقام الصلاة (21:73; 24:37), establishing prayer | replace: وازينت |
| 1802 | وإيتاء | Must | 2 | وإيتاء occurs in وإيتاء الزكاة (zakat is excluded) | replace: وبدارا |
| 1975 | يطوف | Must | 2 | يطوف (2:158 أن يطوف بهما Safa and Marwa; 56:17) is the tawaf verb; طواف is excluded | replace: يصحبون |
| 152 | أعبد | Should | 13 | أعبد (I worship) is the verb of worship, used in Surat al-Kafirun and creed statements (category d) | replace: تفادوهم |
| 473 | تكبيرا | Should | 1 | تكبيرا is the takbir (17:111 وكبره تكبيرا) | drop |
| 590 | حرمات | Should | 1 | حرمات (inviolable things, حرمات الله) belongs to the excluded حرم/احرام holy-sanctity family | drop |
| 693 | دعواهم | Should | 4 | دعواهم: their supplication/cry; includes 10:10 دعواهم فيها سبحانك اللهم (formula); du'a family | drop |
| 694 | دعوة | Should | 4 | دعوة is supplication (2:186 دعوة الداع إذا دعان; 13:14 دعوة الحق); du'a excluded | drop |
| 905 | صدقة | Should | 4 | صدقة: sadaqa, charity as an act of worship (zakat excluded) | drop |
| 940 | ضامر | Should | 1 | ضامر occurs only in 22:27 (the call to pilgrimage, hajj) | drop |
| 987 | عابدات | Should | 1 | عابدات (66:5, the Prophet's wives) is the worship vocabulary | drop |
| 988 | عابدون | Should | 4 | عابدون (2:138; 23:47; 109:3, 109:5): worshippers, worship root عبد | drop |
| 996 | عاكفون | Should | 2 | عاكفون (2:187 in the mosques; 21:52 devoted to idols): i'tikaf/devotion | drop |
| 1105 | فامسحوا | Should | 2 | فامسحوا is wudu' / tayammum (4:43, 5:6) | drop |
| 1135 | فريضة | Should | 5 | فريضة is a religious obligation (fard); 9:60 'فريضة من الله' | drop |
| 1279 | للمتقين | Should | 18 | للمتقين: the God-fearing (هدى للمتقين); attached ل and ال; المتقين is a religious term | drop |
| 1346 | محاريب | Should | 1 | محاريب (34:13) is the mihrab, prayer niche | drop |
| 1377 | مخلصا | Should | 4 | مخلصا (19:51; 39:2,11,14): sincerity in worship | drop |
| 1378 | مخلصين | Should | 7 | مخلصين: مخلصين له الدين, sincere worship; also 'the chosen servants' (37:40) | drop |
| 1511 | معكوفا | Should | 1 | معكوفا (48:25 والهدي معكوفا) is the offering animals at Hudaybiyyah; هدي is excluded | drop |
| 1627 | منيب | Should | 4 | منيب: turning in repentance to God (11:75 Ibrahim; 50:8, 50:33); توبة family excluded | drop |
| 1656 | موقوتا | Should | 1 | موقوتا occurs only in 4:103 (الصلاة كانت ... كتابا موقوتا), the ordained prayer times | drop |
| 1670 | ناشئة | Should | 1 | ناشئة occurs only in 73:6 (ناشئة الليل), the night prayer (تهجد is excluded) | drop |
| 1693 | نذرت | Should | 2 | نذرت (3:35 Mary's mother; 19:26 Mary's vow of fasting) | drop |
| 1799 | والميسر | Should | 3 | والميسر (2:219; 5:90, 91) gambling in the prohibition of wine and games of chance; attached و+ال | drop |
| 1906 | يحافظون | Should | 3 | يحافظون: the guarding of prayers (6:92; 23:9; 70:34) | drop |
| 1929 | يربوا | Should | 1 | يربوا occurs only in 30:39 (riba); the usury verse; يربوا carries the silent alef of the Uthmani singular (30:39); modern يربو [spelling] | drop |
| 357 | تتجافى | Consider | 1 | تتجافى is the verse of night prayer (32:16 تتجافى جنوبهم عن المضاجع) | keep-note |
| 389 | تدعون | Consider | 20 | تدعون root دعو (prayer, supplication); du'a excluded; the verb is also an ordinary 'you call' | keep-note |
| 433 | تضرعا | Consider | 3 | تضرعا (6:63; 7:55; 7:205): humble supplication, du'a vocabulary | keep-note |
| 438 | تطهيرا | Consider | 1 | تطهيرا is the ahl al-bayt verse (33:33 ويطهركم تطهيرا) and ritual purity | keep-note |
| 581 | حجاب | Consider | 4 | حجاب modern 'hijab'; religious dress term; Quranic 'veil/screen' | keep-note |
| 800 | ساهون | Consider | 2 | ساهون: 107:5 (عن صلاتهم ساهون) and 51:11 | keep-note |
| 808 | سبيل | Consider | 87 | سبيل: mostly سبيل الله; very frequent and religiously loaded, but also an ordinary 'way' | keep-note |
| 829 | سقاية | Consider | 1 | سقاية is the water-giving of the pilgrims (9:19) | keep-note |
| 916 | صعيدا | Consider | 4 | صعيدا is the soil of tayammum (4:43, 5:6) | keep-note |
| 933 | صوامع | Consider | 1 | صوامع (22:40) places of worship | keep-note |
| 935 | صيد | Consider | 2 | صيد: hunting in the ihram verses | keep-note |
| 973 | طهورا | Consider | 2 | طهورا: purifying water (25:48), ritual purity | keep-note |
| 1004 | عباد | Consider | 11 | عباد mostly عباد الله / عبادي | keep-note |
| 1043 | عميق | Consider | 1 | عميق occurs in the Hajj verse (22:27 فج عميق) and 'deep' | keep-note |
| 1049 | عيدا | Consider | 1 | عيدا (5:114) is the festival (Eid) of the table sent down for Jesus | keep-note |
| 1079 | غنمتم | Consider | 2 | غنمتم is the spoils/khums verse (8:41) | keep-note |
| 1177 | قربات | Consider | 1 | قربات (9:99) acts of devotion | keep-note |
| 1237 | كسالى | Consider | 2 | كسالى: lazy in prayer (4:142, 9:54) | keep-note |
| 1318 | متبرجات | Consider | 1 | متبرجات (24:60; 33:33) dress code | keep-note |
| 1408 | مرصد | Consider | 1 | مرصد (9:5) the sword verse | keep-note |
| 1512 | معلوم | Consider | 9 | معلوم includes 2:197 (Hajj months) and 'a known station' (37:164): mixed | keep-note |
| 1667 | ميقات | Consider | 2 | ميقات: Moses' appointment (7:142), the Day (56:50); modern miqat of the pilgrimage | keep-note |
| 1885 | يتطهرون | Consider | 2 | يتطهرون (7:82; 27:56): Lot's people mocking 'they pose as pure'; purification root | keep-note |

### e. Scripture, revelation, recitation (41: 4 Must, 21 Should, 16 Consider)

| # | Word | Sev | n | Reason | Action |
|---|---|---|---|---|---|
| 361 | تتلى | Must | 16 | تتلى is the passive of تلا, which is excluded (category e: recitation of the verses, تتلى عليهم آياتنا) | replace: مأمنه |
| 400 | ترتيلا | Must | 2 | ترتيلا is Quran recitation (tartil: 73:4 ورتل القرآن ترتيلا; 25:32); قرا and تلا are excluded | replace: مدتهم |
| 1697 | نزل | Must | 22 | نزل is 'sent down' for the revelation (نزل به الروح الأمين, ما نزل); انزل, تنزيل, تنزلت are excluded | replace: للأنام |
| 1823 | ورتل | Must | 1 | ورتل (73:4 ورتل القرآن ترتيلا; 25:32): Quran recitation; قرا and تلا are excluded | replace: وتشاور |
| 707 | ذكرى | Should | 11 | ذكرى (reminder) is the verbal noun in the family of excluded ذكر/تذكرة; modern also 'memory' | drop |
| 874 | شريعة | Should | 1 | شريعة is Shari'a, the divine law (45:18) | drop |
| 1020 | عربيا | Should | 8 | عربيا: almost always قرآنا عربيا, naming the Quran as Arabic | drop |
| 1286 | لوح | Should | 1 | لوح occurs only in 85:22 (لوح محفوظ), the Preserved Tablet | drop |
| 1363 | محفوظ | Should | 1 | محفوظ occurs in لوح محفوظ (85:22) | drop |
| 1364 | محكمة | Should | 1 | محكمة (47:20 سورة محكمة): scripture terminology | drop |
| 1471 | مصدقا | Should | 11 | مصدقا: مصدقا لما بين يديه, the Quran/Torah confirming scripture | drop |
| 1531 | مفترى | Should | 2 | مفترى (28:36; 34:43) the Quran / signs called forged; A5 duplicate of the فرى family | drop |
| 1532 | مفتريات | Should | 1 | مفتريات (11:13) the challenge to produce ten fabricated suras | drop |
| 1536 | مفصلا | Should | 1 | مفصلا (6:114 أنزل إليكم الكتاب مفصلا) the Quran | drop |
| 1567 | مكتوبا | Should | 1 | مكتوبا occurs only in 7:157 (the Prophet written in the Torah and Gospel); scripture | drop |
| 1571 | مكرمة | Should | 1 | مكرمة occurs only in 80:13 (صحف مكرمة), the honoured scrolls | drop |
| 1576 | مكنون | Should | 3 | مكنون (56:78 كتاب مكنون the guarded Book; 37:49 houris; 52:24 paradise) | drop |
| 1612 | منشرة | Should | 1 | منشرة occurs only in 74:52 (صحفا منشرة), scrolls | drop |
| 1613 | منشور | Should | 1 | منشور (17:13 the open record; 52:3 رق منشور the unfurled scroll) | drop |
| 1633 | مهجورا | Should | 1 | مهجورا occurs only in 25:30 (they abandoned this Quran) | drop |
| 1639 | مواضعه | Should | 3 | مواضعه (4:46; 5:13; 5:41): yuharrifuna al-kalim 'an mawadi'ih, the corruption of scripture | drop |
| 1653 | موعظة | Should | 3 | موعظة (10:57; 2:275; 24:34) is mostly the Quran as admonition | drop |
| 1752 | ننسخ | Should | 1 | ننسخ occurs only in 2:106 (ما ننسخ من آية), abrogation of verses | drop |
| 1880 | يتدبرون | Should | 2 | يتدبرون (4:82; 47:24) the instruction to ponder the Quran | drop |
| 1911 | يحرفون | Should | 3 | يحرفون: the corruption of the scriptures (2:75; 4:46; 5:13) | drop |
| 286 | براءة | Consider | 2 | براءة is the name of Surah al-Tawba and its opening declaration | keep-note |
| 352 | تبيانا | Consider | 1 | تبيانا is the Quran described as a clarification of everything (16:89) | keep-note |
| 457 | تفسيرا | Consider | 1 | تفسيرا: tafsir (25:33) | keep-note |
| 505 | تيسر | Consider | 2 | تيسر is mostly the instruction on recitation (73:20 فاقرؤوا ما تيسر منه) | keep-note |
| 585 | حديث | Consider | 10 | حديث in modern Arabic is the hadith (traditions of the Prophet); also 'talk, modern' | keep-note |
| 835 | سكينة | Consider | 1 | سكينة: tranquillity sent down by God (2:248, 48:4); also a name | keep-note |
| 860 | شاعر | Consider | 3 | شاعر: the accusation against the Prophet (قالوا شاعر) | keep-note |
| 871 | شرعة | Consider | 1 | شرعة (5:48) the revealed law | keep-note |
| 889 | شورى | Consider | 1 | شورى is the title of Surah 42 and a modern political term | keep-note |
| 898 | صافات | Consider | 2 | صافات: title of Surah 37 (والصافات صفا) and angels in ranks | keep-note |
| 1007 | عبس | Consider | 2 | عبس is the opening of Surah 80, about the Prophet and the blind man | keep-note |
| 1051 | غاسق | Consider | 1 | غاسق is the opening of the protective sura al-Falaq (113:3) | keep-note |
| 1328 | متشابها | Consider | 3 | متشابها: partly the Quran 'متشابها مثاني' (39:23) | keep-note |
| 1388 | مدهنون | Consider | 1 | مدهنون (56:81) is about the Quran | keep-note |
| 1568 | مكث | Consider | 1 | مكث (17:106) is the pace of reciting the Quran; also a very common verb 'stay' (امكثوا, ماكثين) | keep-note |
| 1765 | هزوا | Consider | 11 | هزوا: اتخذوا آيات الله هزوا (2:231, 18:56, 45:9) | keep-note |

### f. Punishment, curse, disbelief, sin, obscene-adjacent, insult (73: 8 Must, 40 Should, 25 Consider)

| # | Word | Sev | n | Reason | Action |
|---|---|---|---|---|---|
| 609 | حمالة | Must | 1 | حمالة occurs only in 111:4 (وامرأته حمالة الحطب), the curse on Abu Lahab's wife (Surah al-Masad) | replace: وقضبا |
| 719 | راعنا | Must | 1 | راعنا is the word the Quran forbids believers to use (2:104 لا تقولوا راعنا); an insult in origin | replace: يلتكم |
| 850 | سوءة | Must | 2 | سوءة (5:31, Cain; other forms in 7:20-27 and 20:121) is the private parts/nakedness; obscene-adjacent, also root سوأ of excluded سوء | replace: اعتراك |
| 1425 | مسافحات | Must | 1 | مسافحات: fornicating women (4:25) ; zina is excluded and this is its obscene-adjacent counterpart | replace: فأقبره |
| 1426 | مسافحين | Must | 2 | مسافحين: fornicators (4:24; 5:5) ; zina is excluded | replace: فإمساك |
| 1443 | مسد | Must | 1 | مسد occurs only in 111:5 (Surah al-Masad, the curse on Abu Lahab's wife) | replace: فانهار |
| 1682 | نبتهل | Must | 1 | نبتهل is the mubahala verse (3:61 نبتهل فنجعل لعنة الله على الكاذبين); لعنة is excluded | replace: كسادها |
| 1982 | يعص | Must | 3 | يعص: the corpus lemma is عصا (staff) but the uses (4:14; 33:36; 72:23) are ومن يعص الله ورسوله, the disobedience-to-God threat of hell; the glossed meaning is wrong | replace: يقتروا |
| 34 | أثاما | Should | 1 | أثاما is the punishment/penalty in hell (25:68 يلق أثاما); sin family with excluded punishment words | replace: يمهدون |
| 40 | أثيم | Should | 5 | أثيم means wicked sinner, mostly in hell (طعام الأثيم); semantically excluded فاسق/ظالم class | replace: بالعقود |
| 45 | أجرموا | Should | 3 | أجرموا is a verb of the excluded root family مجرم/جريمة (crime/guilt); root جرم not in root exclusions | replace: بحاملين |
| 67 | أخزى | Should | 1 | أخزى comparative of خزي (disgrace), خزي is excluded (category f); root خزي | replace: بمزحزحه |
| 355 | تتبيب | Should | 1 | تتبيب (perdition, 11:101) is the same root as the curse تبت يدا أبي لهب (111:1) | replace: ومستودع |
| 521 | ثيبات | Should | 1 | ثيبات (non-virgin women, 66:5 in the verse about the Prophet's wives) is sexual-status vocabulary tied to a specific household | drop |
| 669 | خنزير | Should | 1 | خنزير (swine/pig) is the unclean animal and a common insult in Arabic | drop |
| 783 | زنيم | Should | 1 | زنيم (68:13) is glossed 'bastard / of illegitimate descent'; obscene-adjacent | drop |
| 858 | سيئة | Should | 12 | سيئة (misdeed): same sin vocabulary as excluded سوء | drop |
| 882 | شقيا | Should | 3 | شقيا is the damned/wretched (root of excluded شقاء); 19:4,32,48 | drop |
| 939 | ضالين | Should | 2 | ضالين: the last word of the Fatiha (ولا الضالين), the pair of excluded مغضوب | drop |
| 956 | طاغين | Should | 2 | طاغين (37:30; 38:55; 78:22) transgressors who go to hell; root طغي of excluded طاغوت | drop |
| 993 | عاقب | Should | 1 | عاقب: the verb of punishment (counterpart of excluded عقاب/عقوبة) | drop |
| 1048 | عورات | Should | 2 | عورات (24:31; 24:58) are the private parts / times of nakedness; obscene-adjacent like سوءة | drop |
| 1069 | غضب | Should | 8 | غضب: mostly غضب الله; root غضب of excluded مغضوب | drop |
| 1087 | فاجرا | Should | 1 | فاجرا shares root with excluded فجور; 71:27 Noah's curse | drop |
| 1126 | فذوقوا | Should | 11 | فذوقوا is the address to the damned (ذوقوا العذاب); العذاب is excluded | drop |
| 1178 | قردة | Should | 2 | قردة (apes) is the transformation punishment of the Sabbath-breakers and a common slur on Jews | drop |
| 1187 | قصاص | Should | 2 | قصاص is retaliation (qisas), the punishment law (2:178-179) | drop |
| 1224 | كبائر | Should | 3 | كبائر are the grave sins (4:31, 42:37, 53:32) | drop |
| 1247 | كلبهم | Should | 3 | كلبهم: 'dog' is a common insult in Arabic; the cave dog of 18:18, 22; attached pronoun | drop |
| 1259 | لامستم | Should | 2 | لامستم (4:43, 5:6) is the touching of women (sexual euphemism) in the tayammum verse | drop |
| 1282 | لمزة | Should | 1 | لمزة (104:1) is paired with excluded ويل; the slanderer | drop |
| 1345 | مجنون | Should | 6 | مجنون is an insult (used against the Prophet and prophets); modern 'crazy' | drop |
| 1358 | محصنات | Should | 1 | محصنات (4:25) chaste women in the verse on marriage with slave girls and zina | drop |
| 1360 | محصنين | Should | 2 | محصنين occurs in 4:24 غير مسافحين (zina law) | drop |
| 1390 | مذبذبين | Should | 1 | مذبذبين (4:143) describes the hypocrites; نفاق excluded | drop |
| 1456 | مسودا | Should | 2 | مسودا: faces blackened (16:58; 43:17); racially sensitive | drop |
| 1457 | مسودة | Should | 1 | مسودة (39:60) faces of the damned blackened; racially sensitive | drop |
| 1584 | ملكت | Should | 15 | ملكت is nearly always ما ملكت أيمانكم (slavery/concubinage formula); root ملك of excluded ملك/مالك | drop |
| 1618 | منفكين | Should | 1 | منفكين occurs only in 98:1 (Surah al-Bayyina about disbelievers and People of the Book) | drop |
| 1775 | هيت | Should | 1 | هيت occurs only in 12:23 (Zulaikha's هيت لك), a seduction word | drop |
| 1789 | وأعتدنا | Should | 7 | وأعتدنا: وأعتدنا لهم عذابا أليما, the prepared-punishment formula; attached و | drop |
| 1791 | واغلظ | Should | 2 | واغلظ (9:73; 66:9) is the command to be harsh to the unbelievers and hypocrites | drop |
| 1797 | والسن | Should | 1 | والسن (5:45 والسن بالسن), the lex talionis; attached و+ال | drop |
| 1955 | يشاقق | Should | 2 | يشاقق (4:115; 8:13; 59:4) opposing God and the Messenger, threat of punishment | drop |
| 2007 | يلحدون | Should | 3 | يلحدون (7:180, 16:103, 41:40) deviating in His names; modern إلحاد 'atheism' | drop |
| 2010 | يلق | Should | 1 | يلق occurs only in 25:68 (يلق أثاما, punishment in hell); bare form is also jussive of لقي | drop |
| 2013 | يلهث | Should | 2 | يلهث (7:176 twice): the simile of the dog for the one who denied the signs | drop |
| 2033 | ينكح | Should | 2 | ينكح (24:3 الزاني لا ينكح إلا زانية; 4:22); zina excluded; 'نكح' is also vulgar in colloquial Arabic | drop |
| 37 | إثما | Consider | 7 | إثما (sin) belongs to the sin family (ءاثم، أثيم، تأثيم، أثاما); moral censure similar to excluded فسق/ظلم; low severity for the neutral noun | keep-note |
| 173 | افترى | Consider | 14 | افترى is mostly افترى على الله كذبا (86% of uses next to an excluded word); the disbelief formula | keep-note |
| 223 | أندادا | Consider | 6 | أندادا is the shirk word (rivals set up to God) | keep-note |
| 337 | تبارا | Consider | 1 | تبارا is Noah's curse (71:28 ولا تزد الظالمين إلا تبارا) | keep-note |
| 398 | تراود | Consider | 1 | تراود is attempted seduction (12:30), obscene-adjacent in modern Arabic | keep-note |
| 402 | ترجمون | Consider | 1 | ترجمون: stoning (rajm) as punishment; 44:20 is a threat of stoning | keep-note |
| 463 | تقتيلا | Consider | 1 | تقتيلا is slaughter (33:61 وقتلوا تقتيلا), a violence word with the cursed | keep-note |
| 467 | تقربوا | Consider | 5 | تقربوا is mostly the prohibition formula ولا تقربوا (الزنا، الفواحش) | keep-note |
| 506 | ثالث | Consider | 1 | ثالث is the Trinity statement (5:73 ثالث ثلاثة); also an ordinary ordinal | keep-note |
| 545 | جلدة | Consider | 2 | جلدة is the lash of the zina punishment (24:2) | keep-note |
| 579 | حبطت | Consider | 6 | حبطت: deeds voided by disbelief (almost always حبطت أعمالهم) | keep-note |
| 729 | رجز | Consider | 3 | رجز: punishment/impurity (74:5, 7:134) | keep-note |
| 792 | ساحر | Consider | 7 | ساحر: sorcery accusation against prophets (sihr) | keep-note |
| 811 | سحروا | Consider | 1 | سحروا: sorcery verb | keep-note |
| 848 | سنقتل | Consider | 1 | سنقتل attached future س; Pharaoh's threat (7:127) | keep-note |
| 868 | شديد | Consider | 35 | شديد is mostly followed by the excluded العقاب / العذاب (شديد العقاب), 77% of its uses sit next to an excluded word | keep-note |
| 886 | شهوة | Consider | 2 | شهوة: sexual desire (7:81) | keep-note |
| 948 | ضلال | Consider | 27 | ضلال: 'clear error' (ضلال مبين), paired with disbelief | keep-note |
| 966 | طغى | Consider | 5 | طغى: Pharaoh story (20:24, 79:17) | keep-note |
| 1065 | غشاوة | Consider | 2 | غشاوة is the covering on the hearts of disbelievers (2:7, 45:23) | keep-note |
| 1181 | قروء | Consider | 1 | قروء: menstrual periods (2:228) | keep-note |
| 1251 | كنزتم | Consider | 1 | كنزتم: the punishment verse (9:35 فذوقوا ما كنتم تكنزون) | keep-note |
| 1544 | مقتا | Consider | 3 | مقتا (4:22 on marrying one's father's wife; 35:39) | keep-note |
| 1673 | ناصية | Consider | 1 | ناصية: 96:15-16 (Abu Jahl); a stray word | keep-note |
| 1903 | يجرمنكم | Consider | 3 | يجرمنكم: the root جرم of excluded مجرم; 5:2, 5:8, 11:89 | keep-note |

### g. Verbs of exclusively divine acts (16: 1 Must, 11 Should, 4 Consider)

| # | Word | Sev | n | Reason | Action |
|---|---|---|---|---|---|
| 1281 | لمحى | Must | 2 | لمحى is لمحيي (30:50, 41:39) 'the one who gives life to the dead'; احيا excluded; spelled with ى for ي and attached ل | replace: حصونهم |
| 234 | أنعم | Should | 5 | أنعم: all uses have God as subject (أنعم الله عليهم / أنعمت عليهم), a divine act of bestowing grace; found by the neighbour-word check | replace: فتنادوا |
| 476 | تكليما | Should | 1 | تكليما is God speaking to Moses (4:164 وكلم الله موسى تكليما), exclusively divine | drop |
| 961 | طبع | Should | 3 | طبع occurs in طبع الله على قلوبهم: God sealing hearts (divine act) | drop |
| 1032 | عفا | Should | 6 | عفا is mostly God pardoning (عفا الله عنك); عفو is excluded | drop |
| 1104 | فالق | Should | 2 | فالق (6:95,96) فالق الحب والنوى / الإصباح: divine epithet | drop |
| 1836 | وصوركم | Should | 2 | وصوركم (40:64; 64:3) God shaping humans; مصور is excluded | drop |
| 1875 | يبسط | Should | 9 | يبسط is God expanding provision (الله يبسط الرزق); الباسط and رزق family excluded | drop |
| 1896 | يتوفى | Should | 4 | يتوفى (8:50; 22:5; 39:42; 40:67): God taking souls; death is a divine act in the Quran | drop |
| 1970 | يضلل | Should | 12 | يضلل: من يضلل الله فما له من هاد, God causing to err | drop |
| 1997 | يقدر | Should | 4 | يقدر: God measuring out provision (يبسط الرزق لمن يشاء ويقدر) | drop |
| 2046 | يولج | Should | 4 | يولج (22:61; 31:29; 35:13; 57:6): God inserting night into day | drop |
| 226 | أنشأ | Consider | 2 | أنشأ: creation verb whose subject is almost always God (6:98، 53:32) | keep-note |
| 328 | بورك | Consider | 1 | بورك: blessing verb with divine subject (27:8) | keep-note |
| 1089 | فأحبط | Consider | 3 | فأحبط: God voiding deeds (33:19) | keep-note |
| 1609 | منزلون | Consider | 1 | منزلون: 29:34 sending down punishment on Lot's people; root نزل of excluded تنزيل/انزل | keep-note |

### h. Quranic formulae and oaths (35: 8 Must, 16 Should, 11 Consider)

| # | Word | Sev | n | Reason | Action |
|---|---|---|---|---|---|
| 12 | ءامين | Must | 1 | آمين: after respelling it is the liturgical Amen said after Fatiha and supplication (formula); in the Quran it is a different word (heading to) but the bare spelling is the... | replace: تنازعتم |
| 165 | أعوذ | Must | 6 | أعوذ is the isti'adha formula (أعوذ بالله...); formula and supplication | replace: عرضتم |
| 280 | بحمد | Must | 10 | بحمد is the tasbih formula سبح بحمد ربك / يسبحون بحمد ربهم (category h) | replace: فسالت |
| 442 | تعالى | Must | 3 | تعالى is the divine exaltation formula تعالى الله عما يشركون, parallel to excluded متعال and سبحان | replace: مزقتم |
| 742 | رضى | Must | 5 | رضى (read رضي) is the formula رضي الله عنهم (5:119, 9:100, 58:22, 98:8); also modern رضى 'satisfaction'; final ى | replace: أذاعوا |
| 1096 | فاستعذ | Must | 4 | فاستعذ is the instruction to say the isti'adha (7:200, 16:98, 40:56, 41:36); formula | replace: تسرحون |
| 1493 | معاذ | Must | 2 | معاذ occurs only in معاذ الله (12:23, 12:79); formula; also a Companion's name | replace: فتطاول |
| 1702 | نستعين | Must | 1 | نستعين occurs only in 1:5 (إياك نعبد وإياك نستعين), the Fatiha | replace: ليبطئن |
| 74 | أدراك | Should | 13 | أدراك appears 13 times in the formula وما أدراك ما (القارعة، سجين، الحاقة...) leading into hell and last-day terms | replace: تبايعتم |
| 260 | بإذن | Should | 23 | بإذن is mostly the formula بإذن الله; attached preposition ب | replace: لموفوهم |
| 504 | توفيقى | Should | 1 | توفيقى is the formula وما توفيقي إلا بالله (11:88); final ى for ي; tawfiq is also a name | drop |
| 568 | حاش | Should | 2 | حاش occurs only in حاش لله (12:31، 12:51), an exclamation of God's purity (parallel to سبحان) | drop |
| 594 | حسبنا | Should | 3 | حسبنا occurs in the formula حسبنا الله ونعم الوكيل (3:173; 9:59) with excluded divine names; attached pronoun | drop |
| 604 | حطة | Should | 2 | حطة is the formula the Israelites were told to say (2:58، 7:161) | drop |
| 717 | راجعون | Should | 4 | راجعون occurs in إنا لله وإنا إليه راجعون (2:156) and the return-to-God verses | drop |
| 1093 | فارهبون | Should | 2 | فارهبون is God's first-person imperative (2:40, 16:51 وإياي فارهبون); attached ف and pronoun | drop |
| 1387 | مدكر | Should | 6 | مدكر is the refrain فهل من مدكر (54:15, 17, 22, 32, 40) and root ذكر | drop |
| 1411 | مرضات | Should | 4 | مرضات (2:207; 2:265; 4:114; 66:1) is the formula ابتغاء مرضات الله; Uthmani spelling (modern مرضاة) | drop |
| 1434 | مستقيم | Should | 26 | مستقيم: almost always صراط مستقيم (Fatiha); صراط is excluded | drop |
| 1831 | وسعها | Should | 5 | وسعها: the formula لا يكلف الله نفسا إلا وسعها | drop |
| 1886 | يتعد | Should | 2 | يتعد (4:14; 65:1) is the formula ومن يتعد حدود الله | drop |
| 1897 | يتوكل | Should | 3 | يتوكل (65:3 ومن يتوكل على الله فهو حسبه); وكيل is excluded | drop |
| 1965 | يصدون | Should | 6 | يصدون: يصدون عن سبيل الله | drop |
| 1998 | يقرض | Should | 2 | يقرض (2:245; 57:11) من ذا الذي يقرض الله قرضا حسنا, the loan to God | drop |
| 186 | أقسم | Consider | 8 | أقسم (56:75; 69:38; 70:40; 75:1-2; 81:15; 84:16; 90:1) is the oath-opening لا أقسم | keep-note |
| 193 | إكراه | Consider | 1 | إكراه is almost only the verse لا إكراه في الدين; formula-like | keep-note |
| 364 | تثريب | Consider | 1 | تثريب is the formula لا تثريب عليكم اليوم يغفر الله لكم (12:92) only | keep-note |
| 584 | حدود | Consider | 12 | حدود is mostly حدود الله | keep-note |
| 807 | سبقت | Consider | 7 | سبقت occurs mostly in the formula سبقت كلمة ربك | keep-note |
| 885 | شهادة | Consider | 5 | شهادة: testimony, also the Muslim creed (shahada) | keep-note |
| 1040 | علوا | Consider | 4 | علوا: mostly the formula تعالى عما يقولون علوا كبيرا (17:43) | keep-note |
| 1179 | قرضا | Consider | 6 | قرضا: mostly قرضا حسنا (a loan to God, 2:245 etc.) | keep-note |
| 1565 | مكانتكم | Consider | 4 | مكانتكم: mostly the formula اعملوا على مكانتكم إني عامل (to the unbelievers); attached pronoun كم | keep-note |
| 1784 | وازرة | Consider | 5 | وازرة is the formula ولا تزر وازرة وزر أخرى | keep-note |
| 1890 | يتفكرون | Consider | 10 | يتفكرون: mostly لآيات لقوم يتفكرون, the formula after signs | keep-note |

### Spelling and orthography (Uthmani leaks, malformed words) (77: 30 Must, 46 Should, 1 Consider)

| # | Word | Sev | n | Reason | Action |
|---|---|---|---|---|---|
| 0 | ءاباء | Must | 1 | Initial ءا is the Uthmani spelling of آ; typed modern آباء folds to اباء and will not match ءاباء | replace: آباء |
| 1 | ءاتية | Must | 2 | Initial ءا is Uthmani for آ (modern آتية); folded form differs from what a user types | replace: آتية |
| 2 | ءاثرك | Must | 1 | Initial ءا is Uthmani for آ (modern آثرك); also carries attached pronoun ك | replace: آثرك |
| 3 | ءاثم | Must | 1 | Initial ءا is Uthmani for آ (modern آثم); the word is also in the sin family (Consider) | replace: آثم |
| 4 | ءاخذ | Must | 1 | Initial ءا is Uthmani for آ (modern آخذ) | replace: آخذ |
| 5 | ءاذان | Must | 4 | Initial ءا is Uthmani for آ (modern آذان) | replace: آذان |
| 6 | ءاذناك | Must | 1 | Initial ءا is Uthmani for آ (modern آذناك) | replace: آذناك |
| 7 | ءاسن | Must | 1 | Initial ءا is Uthmani for آ (modern آسن) | replace: آسن |
| 8 | ءال | Must | 22 | Initial ءا is Uthmani for آ (modern آل); آل is used in the Quran almost only in named families (آل فرعون، آل عمران، آل إبراهيم، آل لوط) and evokes the Prophet's family; jud... [sens-b] | replace: كسوتهم |
| 9 | ءالاء | Must | 34 | Initial ءا is Uthmani for آ (modern آلاء); آلاء is the refrain of Surah al-Rahman (فبأي آلاء ربكما تكذبان); formulaic [sens-h] | replace: أصابعهم |
| 10 | ءامنة | Must | 1 | Initial ءا is Uthmani for آ (modern آمنة); آمنة is a very common woman's name (mother of the Prophet, Amina); name-like without vowels [a11-meaning] | replace: أضغانهم |
| 11 | ءامنين | Must | 7 | Initial ءا is Uthmani for آ (modern آمنين) | replace: آمنين |
| 13 | ءانفا | Must | 1 | Initial ءا is Uthmani for آ (modern آنفا) | replace: آنفا |
| 14 | ءانية | Must | 1 | Initial ءا is Uthmani for آ (modern آنية) | replace: آنية |
| 15 | ءاوى | Must | 3 | Initial ءا is Uthmani for آ (modern آوى) | replace: آوى |
| 31 | أتوكؤا | Must | 1 | أتوكؤا is the Uthmani spelling (waw-hamza plus alef); modern is أتوكأ | replace: أتوكأ |
| 112 | استئس | Must | 0 | استئس is not a word and not attested in the Quran: the corpus form is استيأس (a small yaa seat was dropped by the exact_display step) | replace: استيأس |
| 187 | أقصا | Must | 2 | أقصا uses the alef of the Uthmani construct; modern spelling is أقصى (typed أقصى folds to اقصي, not اقصا); also evokes al-Masjid al-Aqsa (holy place) | replace: جرحتم |
| 205 | امرأت | Must | 6 | امرأت is the construct (pausal-style) spelling with ت; modern is امرأة; the Quran uses it only before named persons (امرأت عمران، فرعون، نوح، لوط، العزيز) | replace: فرهان |
| 206 | امرؤا | Must | 1 | امرؤا is the Uthmani spelling (with extra alef); modern is امرؤ | replace: امرؤ |
| 440 | تظمؤا | Must | 1 | تظمؤا is the Uthmani spelling; modern is تظمأ | replace: تظمأ |
| 454 | تفتؤا | Must | 1 | تفتؤا is the Uthmani spelling; modern is تفتأ (12:85 تالله تفتؤا) | replace: تفتأ |
| 616 | حيواة | Must | 4 | حيواة is the Uthmani spelling (waw for alef); modern is حياة; typed حياة folds to حياه and does not match حيواه | replace: حياة |
| 714 | رءياى | Must | 2 | رءياى: Uthmani رءيا + final ى; modern is رؤياي; typed form does not fold to رءياى | replace: رؤياي |
| 1253 | كهئة | Must | 0 | كهئة is not attested: the Quran writes كهيئة (3:49, 5:110, the bird of Jesus); the small yaa was dropped | replace: تمرحون |
| 1303 | مبرءون | Must | 1 | مبرءون is the Uthmani spelling (hamza on the line); modern is مبرؤون; context is the acquittal of Aisha (24:26) | replace: سلقوكم |
| 1773 | هنئا | Must | 0 | هنئا is not attested; the Quran writes هنيئا (4:4, 52:19, 69:24, 77:43); the small yaa seat was dropped | replace: هنيئا |
| 1871 | يامعشر | Must | 3 | يامعشر is the Uthmani joined vocative يا معشر (6:128, 6:130, 55:33 to jinn and mankind); like the excluded ياايها | replace: وشفتين |
| 1883 | يتسنه | Must | 1 | يتسنه is the pausal ha (هاء السكت) form (2:259 لم يتسنه): not a normal word | replace: وقبائل |
| 2014 | يلون | Must | 1 | يلون: the Quran writes يلوون with a small waw (3:78 يلوون ألسنتهم بالكتاب) which the build dropped; modern is يلوون; also scripture distortion; ambiguous with 'they follow' | replace: يلوون |
| 156 | أعجمى | Should | 1 | Final ى for ي (Uthmani): modern is أعجمي; also describes the language of revelation (41:44، 16:103) | replace: أعجمي |
| 202 | أمانى | Should | 2 | Final ى for ي (Uthmani): modern is أماني | replace: أماني |
| 251 | أوتى | Should | 14 | Final ى for ي (Uthmani): modern أوتي | replace: أوتي |
| 254 | أوزعنى | Should | 2 | Final ى for ي (Uthmani): modern أوزعني; it is the formula of supplication رب أوزعني أن أشكر نعمتك | replace: لتضيقوا |
| 293 | برىء | Should | 9 | Final ى for ي (Uthmani): modern is بريء | replace: بريء |
| 319 | بمصرخى | Should | 1 | Final ى for ي (Uthmani): modern بمصرخي; Iblis' address in 14:22; attached ب and pronoun | replace: وفصيلته |
| 338 | تبتغى | Should | 2 | Final ى for ي (Uthmani): modern تبتغي | replace: تبتغي |
| 377 | تخاطبنى | Should | 2 | Final ى for ي (Uthmani): modern تخاطبني; Noah's speech (11:37، 23:27) | replace: يألونكم |
| 404 | ترهقنى | Should | 1 | Final ى for ي (Uthmani): modern ترهقني; Moses' speech 18:73 | replace: يتراجعا |
| 408 | تزدرى | Should | 1 | Final ى for ي (Uthmani): modern تزدري | replace: تزدري |
| 461 | تفىء | Should | 1 | تفىء final ى for ي and hamza spelling; modern is تفيء | replace: تفيء |
| 507 | ثانى | Should | 2 | Final ى for ي (Uthmani): modern ثاني; uses are 9:40 (ثاني اثنين, the Prophet and Abu Bakr in the cave) and 22:9 | drop |
| 515 | ثمانى | Should | 1 | Final ى for ي (Uthmani): modern ثماني; one letter from ثمانين | replace: ثماني |
| 548 | جمالت | Should | 1 | جمالت is the Uthmani plural (no alef before ت); modern جمالات; also appears only in hell simile 77:33 | drop |
| 573 | حاضرى | Should | 1 | Final ى for ي (Uthmani): modern حاضري; used only in حاضري المسجد الحرام (2:196), a holy-place phrase | drop |
| 679 | داعى | Should | 2 | Final ى for ي (Uthmani): modern داعي; occurs in 46:31-32 (أجيبوا داعي الله) | replace: داعي |
| 759 | رواسى | Should | 9 | Final ى for ي (Uthmani): modern رواسي | replace: رواسي |
| 989 | عابرى | Should | 1 | Final ى for ي (Uthmani): modern عابري; only in عابري سبيل (4:43, prayer verse) | drop |
| 1264 | لتردين | Should | 1 | لتردين is the Uthmani writing of لتردين with the pronoun yaa dropped (37:56); hell context; looks like a feminine plural verb | drop |
| 1267 | لجى | Should | 1 | Final ى for ي (Uthmani): modern لجي (24:40 بحر لجي) | replace: لجي |
| 1366 | محلى | Should | 1 | Final ى for ي (Uthmani): modern محلي; also 5:1 غير محلي الصيد وأنتم حرم (ihram) | drop |
| 1497 | معجزى | Should | 2 | Final ى for ي (Uthmani): modern معجزي (9:2-3 غير معجزي الله) | drop |
| 1562 | مقنعى | Should | 1 | Final ى for ي (Uthmani): modern مقنعي (14:43 Last Day: مقنعي رءوسهم) | drop |
| 1636 | مهلكى | Should | 1 | Final ى for ي (Uthmani): modern مهلكي (28:59 وما كنا مهلكي القرى) | drop |
| 1687 | نجازى | Should | 1 | Final ى for ي (Uthmani): modern نجازي (34:17 وهل نجازي إلا الكفور) | drop |
| 1688 | نجزى | Should | 19 | Final ى for ي (Uthmani): modern نجزي; mostly كذلك نجزي المحسنين / القوم المجرمين | replace: نجزي |
| 1708 | نشترى | Should | 1 | Final ى for ي (Uthmani): modern نشتري | replace: نشتري |
| 1719 | نطوى | Should | 1 | Final ى for ي (Uthmani): modern نطوي (21:104 Last Day: يوم نطوي السماء) | replace: نطوي |
| 1750 | نملى | Should | 2 | Final ى for ي (Uthmani): modern نملي | replace: نملي |
| 1751 | ننج | Should | 1 | ننج is the pausal/Uthmani writing (10:103 ننج المؤمنين, final ي dropped); not a modern word | replace: ننجي |
| 1760 | هاؤم | Should | 1 | هاؤم is a rare Uthmani word (69:19 هاؤم اقرؤوا كتابيه) in the Last Day sura; no ordinary meaning | drop |
| 1864 | ويدرءون | Should | 2 | ويدرءون is the Uthmani spelling (hamza on the line); modern ويدرؤون; attached و | replace: ويدرؤون |
| 1874 | يبدؤا | Should | 6 | يبدؤا is the Uthmani spelling (modern يبدؤوا / يبدأوا) | replace: يبدؤوا |
| 1891 | يتفيؤا | Should | 1 | يتفيؤا is the Uthmani spelling; modern يتفيأ | replace: يتفيأ |
| 1901 | يجتبى | Should | 2 | Final ى for ي (Uthmani): modern يجتبي; 12:6, 42:13 God choosing prophets | replace: يجتبي |
| 1928 | يدعوا | Should | 8 | يدعوا carries the silent alef of the Uthmani singular (يدعوا إلى الجنة, 2:221; 22:12); modern يدعو; يدعوا: du'a root دعو; 'invoke' [sens-d] | replace: يدعو |
| 1932 | يرجوا | Should | 4 | يرجوا carries the silent alef of the Uthmani singular (18:110 من كان يرجوا لقاء ربه); modern يرجو | replace: يرجو |
| 1938 | يزجى | Should | 2 | Final ى for ي (Uthmani): modern يزجي | replace: يزجي |
| 1941 | يستحى | Should | 2 | Final ى for ي (Uthmani): modern يستحيي / يستحي; 2:26 and 33:53 God does not shy | replace: يستحيي |
| 1947 | يستوى | Should | 12 | Final ى for ي (Uthmani): modern يستوي | replace: يستوي |
| 1962 | يشوى | Should | 1 | Final ى for ي (Uthmani): modern يشوي; occurs only in 18:29 (hell) | replace: يشوي |
| 1978 | يعبؤا | Should | 1 | يعبؤا is the Uthmani spelling (25:77 قل ما يعبأ بكم ربي); modern يعبأ | replace: يعبأ |
| 2016 | يمحوا | Should | 1 | يمحوا is the Uthmani spelling (13:39 يمحو الله ما يشاء); modern يمحو | replace: يمحو |
| 2021 | ينبغى | Should | 6 | Final ى for ي (Uthmani): modern ينبغي | replace: ينبغي |
| 2025 | ينشؤا | Should | 1 | ينشؤا is the Uthmani spelling (43:18); modern ينشأ | replace: ينشأ |
| 2038 | يوارى | Should | 2 | Final ى for ي (Uthmani): modern يواري; 5:31 and 7:26 concern سوءة | replace: يواري |
| 183 | اقتده | Consider | 1 | اقتده ends in the Quranic pausal ha (هاء السكت, 6:90 فبهداهم اقتده); it reads as imitate-him in modern Arabic, so only a mild oddity | keep-note |

### Second meaning without vowels (rule A11) (41: 3 Must, 20 Should, 18 Consider)

| # | Word | Sev | n | Reason | Action |
|---|---|---|---|---|---|
| 477 | تكن | Must | 21 | تكن is a form of كان (excluded) in most Quranic uses (ولا تكن من...); the corpus lemma is a rare other verb meaning conceal | replace: وجدكم |
| 540 | جرم | Must | 5 | جرم in the Quran is only the idiom لا جرم (surely) but the bare word means crime/guilt, the family of excluded مجرم/جريمة; root جرم | replace: وصهرا |
| 692 | دعا | Must | 4 | دعا in modern Arabic is 'he called / supplicated' (دعاء is excluded); Quranic 'thrust' (52:13 يدعون ... دعا) is obscure | replace: يثنون |
| 213 | أمين | Should | 11 | أمين is a very common personal name and the epithet of the Prophet and Gabriel (أمين); also close to the formula آمين | replace: فارقوهن |
| 320 | بنى | Should | 31 | بنى (modern بنى built) and بني (sons of, my son) are the same bare spelling after folding; the Quranic use is almost only بني إسرائيل (named people) | replace: ومرعاها |
| 471 | تقول | Should | 11 | تقول bare spelling is the extremely common 'you say / she says'; the Quranic gloss forge (69:44 تقول) is rare | replace: يتسللون |
| 661 | خفت | Should | 5 | خفت is 'light' (7:9) but also 'I feared' (19:5 خفت الموالي) in the Quran; two common readings | drop |
| 705 | ذروا | Should | 1 | ذروا is both the winnowing of 51:1 and the imperative 'leave!' (ذروا ظاهر الإثم, ذروا البيع) | drop |
| 711 | ذنوب | Should | 1 | ذنوب in the Quran is 'a bucket/share' (51:59 only) but in modern Arabic it is 'sins' (plural of ذنب): the sin vocabulary of category f | drop |
| 766 | زبر | Should | 2 | زبر is 'pieces of iron' (18:96) and also the scriptures الزبر (zabur is excluded) | drop |
| 840 | سلما | Should | 2 | سلما: ladder (6:35) / 'wholly one man's' (39:29); same spelling as سلم 'peace' from the root of excluded سلام | drop |
| 849 | سنة | Should | 14 | سنة is year, drowsiness (2:255 آية الكرسي), and the Prophetic sunna (a scripture-adjacent term) | drop |
| 958 | طائف | Should | 2 | طائف is Ta'if (the city) as a homograph; 68:19 'a visitation' and 7:201 | drop |
| 1082 | غول | Should | 1 | غول (37:47) 'bane in wine' but 'ghoul/demon' in Arabic; jinn-adjacent | drop |
| 1129 | فراغ | Should | 3 | فراغ is 'emptiness / free time' in modern Arabic; Quranic V راغ 'he slipped' (37:91) | drop |
| 1231 | كدنا | Should | 1 | كدنا: 'we almost' (كاد) and 'we plotted' (كيد, 12:76); two readings | drop |
| 1243 | كفل | Should | 1 | كفل: 'a share' (4:85) and the name ذو الكفل (Dhul-Kifl, a prophet) | drop |
| 1352 | محرم | Should | 1 | محرم: pilgrim in ihram, the month Muharram, a mahram relative; ihram excluded; Quranic 'forbidden' (2:85) | drop |
| 1755 | نيلا | Should | 1 | نيلا (9:120): modern نيل is the Nile (named river) and indigo | drop |
| 1757 | هاجر | Should | 1 | هاجر is Hagar (Hajar, wife of Ibrahim) in modern Arabic; Quranic 'emigrate'; the Hijra verb | drop |
| 1852 | وكل | Should | 14 | وكل: bare spelling is both 'and every' (و+كل) and the verb 'he delegated'; vakil (excluded) family | drop |
| 1927 | يدع | Should | 3 | يدع: 107:2 'pushes away' but يدع is also 'he calls/leaves' (jussive of دعا/ودع); du'a family | drop |
| 2018 | يمل | Should | 1 | يمل in modern Arabic is 'he gets bored'; Quranic V 'he dictates' (2:282 يملل) | drop |
| 24 | أبواه | Consider | 2 | أبواه (his parents) is the dual of أب; family of أبو root already represented by ءاباء; none | keep-note |
| 281 | بحيرة | Consider | 1 | بحيرة in modern Arabic is a lake; the Quranic gloss 'dromedary' (a cut-eared camel dedicated to idols) is obscure | keep-note |
| 524 | جارية | Consider | 1 | جارية is modern 'young girl / slave girl'; Quranic gloss 'flowing' (88:12) differs | keep-note |
| 605 | حفظ | Consider | 1 | حفظ in modern Arabic mainly means memorising (Quran); Quranic 'guarded' | keep-note |
| 700 | ديارا | Consider | 1 | ديارا appears only in Noah's curse (71:26); modern 'homes' | keep-note |
| 701 | دين | Consider | 11 | دين is religion/creed and also debt in modern Arabic | keep-note |
| 723 | راق | Consider | 1 | راق: death-bed healer (75:27); modern 'refined' / ruqyah practitioner | keep-note |
| 831 | سكارى | Consider | 2 | سكارى (intoxicated): alcohol connotation; 4:43 and 22:2 | keep-note |
| 917 | صغار | Consider | 1 | صغار: Quranic 'abasement' (6:124), modern 'little ones' | keep-note |
| 924 | صفوان | Consider | 1 | صفوان: modern personal name Safwan | keep-note |
| 926 | صلح | Consider | 2 | صلح modern 'peace treaty / reconciliation'; Quranic V 'was fitting' | keep-note |
| 977 | طيبة | Consider | 9 | طيبة is a name of Medina (Taybah) | keep-note |
| 1012 | عجل | Consider | 2 | عجل 'hasten' vs عجل 'a calf' (the golden calf) | keep-note |
| 1013 | عجوز | Consider | 2 | عجوز is 'old woman' but often derogatory ('crone') in Arabic usage | keep-note |
| 1214 | كأسا | Consider | 2 | كأسا: paradise wine cup (76:5, 78:34) and 'drinking glass' | keep-note |
| 1300 | مبارك | Consider | 4 | مبارك is a common male name and a Quranic epithet for the book/Jesus | keep-note |
| 1348 | محتضر | Consider | 1 | محتضر: dying person in modern Arabic; Quran 54:28 'attended' | keep-note |
| 2028 | ينظرون | Consider | 25 | ينظرون: 'they look' and 'they are given respite' | keep-note |

### Near-duplicates and same-lexeme pairs (rules A2, A3, A5) (50: 0 Must, 20 Should, 30 Consider)

| # | Word | Sev | n | Reason | Action |
|---|---|---|---|---|---|
| 38 | اثنتين | Should | 4 | اثنتين and اثنين are the same numeral (fem/masc) and one letter apart; easy to confuse | replace: أصوافها |
| 39 | اثنين | Should | 10 | اثنين and اثنتين are one letter apart; same lexeme (A5) | replace: إقامتكم |
| 61 | اختلاق | Should | 1 | اختلاق and اختلاف differ only by the dot of ف/ق | replace: بفاتنين |
| 460 | تفضيلا | Should | 2 | تفضيلا and تفصيلا differ only by the dot of ص/ض | replace: يترددون |
| 716 | رابية | Should | 1 | رابية and رابيا are the same adjective, one letter apart after folding (ة/ا) | drop |
| 818 | سراحا | Should | 2 | سراحا is one dot or letter from سراجا, سرابا, سراعا and ترابا/غرابا (a cluster of 7 words one letter apart) | drop |
| 819 | سراعا | Should | 2 | سراعا is in the same one-letter cluster as سرابا, سراجا, سراحا, ترابا, غرابا, سحابا; سراعا (50:44, 70:43) Last Day rising from graves [sens-c] | drop |
| 998 | عالين | Should | 1 | عالين, عاليا, عالية are one adjective in three forms (A5) | drop |
| 999 | عالية | Should | 2 | عالية and عاليا are one adjective in two forms (A5); 88:10 refers to paradise | drop |
| 1350 | محذورا | Should | 1 | محذورا belongs to the cluster محجورا, محذورا, محسورا, محظورا, مهجورا (five words one letter apart) | drop |
| 1362 | محظورا | Should | 1 | محظورا belongs to the cluster محجورا, محذورا, محسورا, محظورا, مهجورا | drop |
| 1441 | مسحورا | Should | 3 | مسحورا belongs to the cluster مستورا, مسحورا, مسرورا, مسطورا, محسورا; مسحورا: the accusation against the Prophet (17:47; 25:8) [sens-b] | drop |
| 1446 | مسطورا | Should | 2 | مسطورا belongs to the cluster مستورا, مسحورا, مسرورا, مسطورا, محسورا (one letter apart); مسطورا: written in the Book (17:58; 33:6) [sens-e] | drop |
| 1524 | مغنون | Should | 2 | مغنون belongs to the cluster مجنون, مسنون, مغنون, مكنون, ممنون; مغنون (40:47) dialogue in hell [sens-c] | drop |
| 1529 | مفتر | Should | 1 | مفتر, مفترون, مفترى, مفتريات are four forms of the root فرى (A5); افتراء, افترى already in the list | drop |
| 1530 | مفترون | Should | 1 | مفترون: A5 duplicate of the فرى family | drop |
| 1593 | ممنون | Should | 4 | ممنون belongs to the cluster مجنون, مسنون, مغنون, مكنون, ممنون (five words one letter apart); ممنون: غير ممنون (41:8; 84:25; 95:6) is the paradise reward formula [sens-c] | drop |
| 1740 | نقيرا | Should | 2 | نقيرا is one letter from نقيبا and نفيرا (ر/ب/ف/ق dot twins) | drop |
| 1763 | هدا | Should | 1 | هدا is one dot/letter from هدى (guidance, an excluded word) ; also 19:90 (the Christian claim about God's son) | drop |
| 1957 | يشعرن | Should | 1 | يشعرن is one letter from يشعرون; two forms of one verb (A5) | drop |
| 516 | ثمانين | Consider | 1 | ثمانين and ثمانى (ثماني) are both 'eight/eighty' and one edit apart | keep-note |
| 517 | ثمر | Consider | 1 | ثمر and ثمرات are singular/plural of one lexeme (A5) | keep-note |
| 518 | ثمرات | Consider | 4 | ثمرات is the plural of ثمر (A5) | keep-note |
| 758 | رهينة | Consider | 1 | رهينة and رهين are one lexeme (A5); 52:21, 74:38 | keep-note |
| 775 | زلزالا | Consider | 1 | زلزالا, زلزلت, زلزلة are three forms of one lexeme (A5) | keep-note |
| 872 | شرقيا | Consider | 1 | شرقيا and شرقية are one lexeme (A5) | keep-note |
| 892 | صابرة | Consider | 1 | صابرة and صابرا: one lexeme (A5) | keep-note |
| 919 | صغيرا | Consider | 2 | صغيرا and صغيرة: one lexeme (A5) | keep-note |
| 936 | ضاحكا | Consider | 1 | ضاحكا and ضاحكة: one lexeme (A5) | keep-note |
| 1117 | فتيان | Consider | 1 | فتيان and فتية are plurals of one noun (A5) | keep-note |
| 1119 | فتية | Consider | 1 | فتية and فتيان are plurals of one noun (A5); Companions of the Cave (18:10,13) | keep-note |
| 1145 | فكهين | Consider | 1 | فكهين is the same word as فاكهين (1103); one letter apart | keep-note |
| 1167 | قائمة | Consider | 5 | قائمة and قائما are one adjective | keep-note |
| 1198 | قليلة | Consider | 1 | قليلة and قليلا are one adjective | keep-note |
| 1222 | كاملين | Consider | 1 | كاملين and كاملة are one adjective | keep-note |
| 1229 | كثيرة | Consider | 11 | كثيرة and كثيرا are one adjective | keep-note |
| 1232 | كذاب | Consider | 4 | كذاب and كذابا are one noun (A5) | keep-note |
| 1301 | مباركة | Consider | 3 | مباركة and مبارك: one adjective (A5) | keep-note |
| 1308 | مبصرة | Consider | 3 | مبصرة and مبصرا: one adjective (A5) | keep-note |
| 1333 | متفرقون | Consider | 1 | متفرقون and متفرقة: one adjective (A5) | keep-note |
| 1359 | محصنة | Consider | 1 | محصنة and محصنات: one lexeme (A5) | keep-note |
| 1413 | مرضيا | Consider | 1 | مرضيا and مرضية: one adjective (A5); 19:55 Ismail | keep-note |
| 1414 | مرضية | Consider | 1 | مرضية and مرضيا: one adjective (A5); راضية مرضية (89:28) is paradise | keep-note |
| 1494 | معاشا | Consider | 1 | معاشا, معايش, معيشة are one noun in three forms (A5) | keep-note |
| 1495 | معايش | Consider | 2 | معايش is the plural of معيشة (A5) | keep-note |
| 1499 | معدودة | Consider | 3 | معدودة and معدود: one adjective (A5) | keep-note |
| 1505 | معروفة | Consider | 1 | معروفة and معروفا: one noun | keep-note |
| 1551 | مقتصدة | Consider | 1 | مقتصدة and مقتصد: one adjective | keep-note |
| 1873 | يبدله | Consider | 1 | يبدله is يبدل with an attached pronoun (A5) | drop |
| 1974 | يطعمه | Consider | 2 | يطعمه is يطعم with an attached pronoun (A5) | drop |

### Article ال (rule A4) (4: 0 Must, 3 Should, 1 Consider)

| # | Word | Sev | n | Reason | Action |
|---|---|---|---|---|---|
| 197 | الجب | Should | 2 | الجب carries the article ال (rule A4) and is the well of Joseph (named narrative) | replace: شمائلهم |
| 198 | الذل | Should | 3 | الذل carries the article ال (rule A4) | replace: فاثبتوا |
| 200 | الضر | Should | 7 | الضر carries the article ال (rule A4); also Ayyub's cry مسني الضر | replace: فاحترقت |
| 1280 | للناس | Consider | 51 | للناس attached ل and ال (A4); a bare ناس is attested in the Quran | keep-note |

### Attached particle or pronoun where a bare form exists (20: 0 Must, 1 Should, 19 Consider)

| # | Word | Sev | n | Reason | Action |
|---|---|---|---|---|---|
| 1230 | كجهر | Should | 1 | كجهر: ك prefixed to جهر; جهارا and جهرة are already in the list (A5) | drop |
| 212 | أمهاتكم | Consider | 6 | أمهاتكم also in the list of forbidden marriages and 'mothers of the believers'; attached pronoun كم; low severity | keep-note |
| 270 | بالأفق | Consider | 2 | بالأفق carries both prepositional ب and the article ال; refers to Gabriel's vision (53:7، 81:23) | drop |
| 272 | بالبخل | Consider | 2 | بالبخل carries ب and the article ال; the bare بخل is attested | replace: بخل |
| 277 | بتابع | Consider | 2 | بتابع carries a ب; context is the qibla (2:145) | keep-note |
| 315 | بقطع | Consider | 2 | بقطع carries the preposition ب; the bare قطع is attested | replace: قطع |
| 586 | حذركم | Consider | 2 | حذركم carries the pronoun كم; the bare حذر is attested (4:71, 4:102) | replace: حذر |
| 725 | ربطنا | Consider | 1 | ربطنا is the verb with attached pronoun (we bound) used for God binding hearts (18:14) | keep-note |
| 1085 | فاتكم | Consider | 3 | فاتكم carries the pronoun كم; the bare فات is attested | replace: فات |
| 1088 | فأجمعوا | Consider | 2 | فأجمعوا carries the conjunction ف; the bare أجمعوا is attested | replace: أجمعوا |
| 1123 | فخلف | Consider | 2 | فخلف carries the conjunction ف; the bare خلف is attested (also 'behind', two meanings) | keep-note |
| 1272 | لصاحبه | Consider | 2 | لصاحبه carries a ل; 9:40 is the Prophet and Abu Bakr in the cave | drop |
| 1777 | وأبقى | Consider | 7 | وأبقى carries an attached و; the bare أبقى is attested (53:51); وأبقى carries the conjunction و; the bare أبقى is attested | replace: أبقى |
| 1804 | وبرق | Consider | 1 | وبرق carries the conjunction و; the bare برق is attested | replace: برق |
| 1807 | وتمت | Consider | 3 | وتمت carries the conjunction و; the bare تمت is attested | replace: تمت |
| 1808 | وتنحتون | Consider | 2 | وتنحتون carries the conjunction و; the bare تنحتون is attested | replace: تنحتون |
| 1810 | وثلاث | Consider | 2 | وثلاث carries the conjunction و; the bare ثلاث is attested | replace: ثلاث |
| 1819 | وخر | Consider | 2 | وخر carries the conjunction و; the bare خر is attested | replace: خر |
| 1859 | ونقر | Consider | 1 | ونقر carries the conjunction و; the bare نقر is attested | replace: نقر |
| 1862 | ويثبت | Consider | 3 | ويثبت carries the conjunction و; the bare يثبت is attested | replace: يثبت |

### Function words and other oddities (rule A1) (10: 1 Must, 1 Should, 8 Consider)

| # | Word | Sev | n | Reason | Action |
|---|---|---|---|---|---|
| 1867 | ويكأن | Must | 1 | ويكأن occurs only in 28:82 (the Qarun story); a compound interjection not a content word | replace: وتكاثر |
| 1851 | وكأين | Should | 6 | وكأين is a compound particle 'how many' not a content word (rule A1) with attached و | drop |
| 1084 | غير | Consider | 69 | غير is a function word ('other than / not'); rule A1 excludes particles | keep-note |
| 1146 | فلانا | Consider | 1 | فلانا is the indefinite pronoun 'so-and-so' (25:28), pronoun-like (A1) | keep-note |
| 1218 | كافة | Consider | 5 | كافة is the adverbial 'all/entirely' (2:208; 9:36), function-like (A1) | keep-note |
| 1248 | كلتا | Consider | 1 | كلتا is the dual pronoun-like 'both' (18:33), a function word (A1) | keep-note |
| 1756 | هاتوا | Consider | 4 | هاتوا is the imperative 'bring!' (هاتوا برهانكم), a challenge formula; verb of a defective type that is not a normal lemma | keep-note |
| 1769 | هلم | Consider | 2 | هلم is an interjection 'come here' (6:150; 33:18), a function word rather than a content word (A1) | keep-note |
| 1937 | يزال | Consider | 3 | يزال occurs only in the auxiliary ولا يزالون / لا يزال (to keep doing), a function verb (A1) | keep-note |
| 2002 | يكاد | Consider | 6 | يكاد is the auxiliary 'almost' (يكاد البرق يخطف أبصارهم), a function verb (A1) | keep-note |

## 6. Limits of this review

* Severity is a calibrated judgement. Must-fix rows were each verified against the Quran text and the exclusion note; Should and Consider rows depend on policy and a Shariah adviser can reasonably differ.
* No scholar's ruling was available, and no source evaluates lemma-form vocabulary; categories (c)-(h) are, as the research note says, a precautionary design inference.
* Rarity alone was not flagged. A rare ordinary word with no association is not in this report.
* The respellings were checked mechanically for fold collisions and by Quranic token; a native proofreader should confirm the 65 respellings before they are applied.
