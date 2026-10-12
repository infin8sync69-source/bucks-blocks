# Quranic Arabic Datasets, Word-by-Word Meanings and Their Licenses (for a multilingual seed-phrase word list)

Research date: 2026-10-07. Method: official download, license, terms and FAQ pages, GitHub repository pages and search results. No dataset files were downloaded, so the descriptions of file contents come from documentation, READMEs and secondary pages, not from opening the files. "Confirmed" means a fetched page states it. "Inferred" means my own reasoning. None of this is legal advice.

Project context used in the inferences: the Bucks code is currently "All rights reserved" (closed source). The word list would ship inside wallet software. Entry i in Arabic and entry i in every translation list must have the same meaning.

---

## 1. Quranic Arabic Corpus (corpus.quran.com): files, contents, English glosses, license

### Takeaway
The downloadable QAC morphology (v0.4, 2011, Kais Dukes) gives segments, POS tags, lemmas and roots. It is labelled "GNU General Public License", but extra terms forbid changing the file and require attribution with a link. The FAQ adds that shared data is for research and not for commercial purposes. The English word-by-word glosses are contextual (per occurrence), based on existing translations, and are shown on the website. No page I read says they are in the download. Using QAC-derived lemma or root lists in closed-source commercial wallet software is legally risky without written permission.

### Cited Findings
- The download page offers "Version 0.4", copyright 2011 Kais Dukes. It describes the data only as "syntactic and morphological annotation of the Quran". It does not list file names or columns. — [QAC download](https://corpus.quran.com/download/)
- The license on the download page is the "GNU General Public License", with these terms of use (verbatim):
  - "Permission is granted to copy and distribute verbatim copies of this file, but CHANGING IT IS NOT ALLOWED."
  - "This annotation can be used in any website or application, provided its source (the Quranic Arabic Corpus) is clearly indicated, and a link is made to http://corpus.quran.com"
  - "This copyright notice shall be included in all verbatim copies of the text, and shall be reproduced appropriately in all works derived from or containing substantial portion of this file."
  — [QAC download](https://corpus.quran.com/download/)
- The FAQ states the corpus "is available under the GNU public license with terms of use." — [QAC FAQ](https://corpus.quran.com/faq.jsp)
- Under the FAQ question "Can I obtain part-of-speech tagged data for my research project?" the project says it will share data for research as long as: "1. You do not use the data for commercial purposes. The data should be used purely for research and is made available under the GNU public license. 2. You give a citation in your research publication." — [QAC FAQ](https://corpus.quran.com/faq.jsp)
- English word-by-word glosses: "Corresponding English words are based on standard and accepted sources of English translation of the Quran, including: Sahih International, Mohammed Pickthall, Yusuf Ali." The FAQ says this is not a new translation but contextual glossing. — [QAC FAQ](https://corpus.quran.com/faq.jsp)
- The FAQ has no question about whether the word-by-word translation can be downloaded. It says only that all data is "freely available for online viewing" and "also available on the download page." — [QAC FAQ](https://corpus.quran.com/faq.jsp)
- The QAC Quran Dictionary pages do give one short gloss per derived form (lemma-level). Example for root k-t-b: "Verb (form I) - to write, to prescribe, to ordain". Contextual per-occurrence translations follow, e.g. "(2:79:3) yaktubūna [write]". — [QAC Quran Dictionary, root ktb](https://corpus.quran.com/qurandictionary.jsp?q=ktb)
- Contents of the morphology file, as described by a derived fork: "quran-morphology.txt" with "POS tags, roots, lemmas, and grammatical information". The fork "replaced Buckwalter transliteration with Arabic script", which implies the original v0.4 uses Buckwalter. The fork also added roots for four-letter words and proper nouns, fixed spelling and retagged occurrences. No license appears in the fork's README. — [mustafa0x/quran-morphology](https://github.com/mustafa0x/quran-morphology)
- QAC has 77,430 words and is GPL-licensed. It was created at the University of Leeds, led by Kais Dukes. — [Wikipedia: Quranic Arabic Corpus](https://en.wikipedia.org/wiki/Quranic_Arabic_Corpus) (via search summary)
- QAC version 2: GitHub repo `kaisdukes/quranic-corpus` is labelled GPL-3.0. Its README describes POS tagging "manually verified by community experts", grammar diagrams about 50% complete, and a new mobile site, qurancorpus.app, in development. It does not separate code and data licensing. — [GitHub kaisdukes/quranic-corpus](https://github.com/kaisdukes/quranic-corpus)
- A third-party redistributor, Quranpedia, re-licenses QAC morphology as "GNU GPL" with the condition "Any use must clearly indicate the Quranic Arabic Corpus as the source with a link". — [Quranpedia dumps LICENSE](https://quranpedia.net/dumps/LICENSE.md)

### Inferences
- The QAC terms contradict each other. The GPL allows modification, but the added terms say "CHANGING IT IS NOT ALLOWED". The download page names no GPL version, and v2 is GPL-3.0. A lemma or root word list is a derived work that does not reproduce the file verbatim. The notice clause mentions "works derived from" the file, which suggests derived works are expected, but the no-change clause and the FAQ's non-commercial research language make the permission scope unclear.
- If the GPL governs, shipping GPL-derived data inside closed-source wallet software could be argued to create GPL obligations. That depends on whether the word list counts as part of a combined work or as separate aggregated data. With an "All rights reserved" codebase this is the main risk.
- Individual facts, such as "word X has root Y", may not be protected by copyright in many jurisdictions. A systematic selection taken from the dataset could still raise database-right issues; QAC originated in the UK at the University of Leeds, and the UK has a sui generis database right. Unverified; needs legal counsel.
- Safest path: get written permission from Kais Dukes or the QAC project for commercial, closed-source use of a derived lemma/root/POS list, with attribution and a link to corpus.quran.com.
- The per-form English glosses in the QAC Quran Dictionary are the closest thing to per-lemma English meanings in QAC. They appear on the website, and no page I read says they are in the download. Scraping them would also need permission.

### Gaps
- I did not open quranic-corpus-morphology-0.4.txt, as instructed. The exact column layout (LOCATION / FORM / TAG / FEATURES with POS, LEM and ROOT features in Buckwalter) is my understanding and is not confirmed by a fetched page. Only "POS tags, roots, lemmas" and Buckwalter-based transliteration are confirmed, via the fork's README.
- I could not confirm whether a GPL version and the terms of use cover the qurancorpus.app / v2 data separately from the code.
- The number of distinct lemmas and roots in QAC was not verified from a primary source.

---

## 2. Tanzil.net: text editions, terms of use, word extraction, translations

### Takeaway
Tanzil provides six verified Quran text editions under "Creative Commons Attribution 3.0" with extra terms: verbatim copying only, "CHANGING IT IS NOT ALLOWED", credit plus a link to tanzil.net, and the copyright notice carried in derived works. Tanzil's translations are verse-level, not word-level, and "for non-commercial purposes only" unless the translator or publisher grants permission. Tanzil is a good source for the Arabic word forms, with attribution, but not for multilingual meanings.

### Cited Findings
- Six text types:
  - Uthmani ("similar to Medina Mushaf")
  - Uthmani Minimal ("minimal number of diacritics and symbols")
  - Simple
  - Simple Plain (no special demonstration of Ikhfas and Idghams)
  - Simple Minimal ("Suitable for embedding in other texts")
  - Simple Clean ("Simple text without any diacritics or symbols. Suitable for easy search.")
  — [Tanzil text types](https://tanzil.net/docs/quran_text_types)
- Text license: Creative Commons Attribution 3.0, copyright Tanzil Project 2007–2021. Verbatim term: "Permission is granted to copy and distribute verbatim copies of this text, but CHANGING IT IS NOT ALLOWED." Paraphrased terms (from the fetch summary, not verbatim):
  - The text may be used in websites or applications if the Tanzil Project is clearly credited and a link to tanzil.net is given so users can track updates.
  - The copyright notice must be reproduced in derived works or files containing substantial portions of the text.
  — [Tanzil text license](https://tanzil.net/docs/text_license)
- Translations terms (verbatim):
  - "The translations provided at this page are for non-commercial purposes only. If used otherwise, you need to obtain necessary permission from the translator or the publisher."
  - "If you are using more than three of the following translations in a website or application, we require you to put a link back to this page"
  - "Redistributing the following list in another website is not allowed, unless direct permission is granted by the Tanzil Project."
  — [Tanzil translations](https://tanzil.net/trans/)
- Tanzil offers translations in more than 50 languages, including Bengali, English, French, Hindi, Indonesian, Persian, Turkish and Urdu. — [Tanzil translations](https://tanzil.net/trans/)
- QAC "builds on" the Tanzil text. Quranpedia also notes the QAC/Tanzil lineage. — [QAC download](https://corpus.quran.com/download/); [Quranpedia LICENSE](https://quranpedia.net/dumps/LICENSE.md)

### Inferences
- Taking individual words (for example from Simple Clean or Simple Minimal) does not "change" the Quran text in the sense of corrupting a verse. However, the license text does not explicitly address extracting words. A 2,048-word list is a small fraction of about 77,000 tokens, so it is probably not a "substantial portion". Including the Tanzil credit and link anyway is cheap and prudent.
- The underlying Quran text is centuries old and not copyrightable as such. Tanzil's claim covers its verified digital edition (encoding and diacritic choices). Copying exact Tanzil word forms, with their Unicode and diacritic conventions, is the part that falls under Tanzil's terms.
- CC BY 3.0 with an added no-modification clause is an unusual mix. CC BY normally permits adaptations. Treat the stricter added terms as controlling.
- Tanzil translations are verse-level and non-commercial, so they are unsuitable as a source of per-lemma meanings for a commercial wallet.

### Gaps
- I did not get the full verbatim text of the Tanzil "can be used in any website or application" clause. The fetch tool paraphrased it.
- I found no explicit Tanzil statement on word-level extraction or on use inside cryptocurrency or commercial software. An email to Tanzil would settle it.

---

## 3. quran.com / Quran Foundation API (v4) and QUL (qul.tarteel.ai): word-by-word resources, languages, formats, terms

### Takeaway
QUL hosts the largest set of downloadable word-by-word (WBW) translations, in JSON and SQLite: English (plus a colour-coded English), Urdu, Hindi, Persian, Bengali, French, Indonesian, Tamil, Turkish and Ingush. Russian and German are marked copyrighted and cannot be downloaded. These glosses are per occurrence (one per word position), not per lemma. Licensing is per resource and mostly not stated on the listing pages. Quran Foundation's developer terms require a signed commercial license to redistribute API data as a dataset or content package, and forbid caching longer than 1 week without Content Sync. Neither source gives a clear licence to bundle WBW-derived meaning lists in closed-source wallet software.

### Cited Findings
- The QUL homepage says there are 22 word-by-word translations among 205 translations, and offers to "Download ayah by ayah and word by word translation in different languages." — [QUL word-translation resources](https://qul.tarteel.ai/resources/word-translation)
- WBW resources listed on QUL's translation resources page:
  - English Word by Word Translation (JSON, SQLite)
  - Colored English wbw (Tajweed)
  - Hindi, Persian, Urdu, Bengali, French, Indonesian, Tamil, Turkish and Ingush wbw (JSON, SQLite)
  - Russian wbw: "copyrighted", no formats available
  - German wbw: "This resource is © copyrighted", no downloads
  — [QUL translation resources](https://qul.tarteel.ai/resources/translation)
- QUL also hosts morphology resources as SQLite downloads: Word root, Word stem, Word lemma (word-by-word mappings) and Ayah root/stem/lemma. The listing shows no source attribution or license text. — [QUL morphology](https://qul.tarteel.ai/resources/morphology)
- The QUL word translation tool manages translations of "each word of the Quran", with columns for word number, Uthmani text, English and Urdu. This shows the glosses are per occurrence. — [QUL word translation tool example](https://qul.tarteel.ai/word_translations/1401)
- QUL FAQ (verbatim):
  - "The resources available on QUL vary in their copyright status. Some are in the public domain, while others may be subject to specific licenses."
  - "Yes, you can use QUL data in commercial projects. However, please review the licensing terms for each resource. Some data may have restrictions or require attribution"
  - "We recommend reviewing the licensing information provided by each resource's author before use."
  - "All tafsir and translation content on QUL is sourced from trusted Sunni platforms."
  — [QUL FAQ](https://qul.tarteel.ai/faq)
- The QUL code repository (TarteelAI/quranic-universal-library) is MIT-licensed. The README does not state data licences. — [GitHub TarteelAI/quranic-universal-library](https://github.com/TarteelAI/quranic-universal-library)
- Quran Foundation Developer Terms (fetched; "Last Updated: 2026-10-04"; a search snippet showed 2026-08-10, so the terms were recently revised). Verbatim:
  - License grant: "non-exclusive, revocable, non-transferable, non-sublicensable license to access and use the APIs solely to develop and operate Applications that provide beneficial Quranic experiences to end users."
  - Display is allowed provided "The text of the Quran is not modified in any way."
  - Redistribution: "A Developer must obtain a signed commercial license before selling, sublicensing, or redistributing QF Content or raw API data—for example, as a dataset, data feed, API, content package, or other separately distributed product."
  - Caching: developers may not "Cache or store QF Content longer than 1 week unless it is obtained and maintained through the Content Sync APIs". Content Sync users "must perform a next sync at least every 7 days".
  - Scraping: developers may not "extract, scrape, or index QF Content or User Data outside the API responses."
  - Commercial apps may charge, run ads, etc., provided "QF Content and raw API data are not sold, sublicensed, or redistributed."
  — [Quran Foundation Developer Terms](https://api-docs.quran.foundation/legal/developer-terms/)
- The Quran.com API SDK exposes a `get_word_by_word_translation_snapshot()` method. — [api-docs.quran.com Python SDK resources](https://api-docs.quran.com/docs/sdk/python/resources/) (via search summary)

### Inferences
- A seed-phrase word list derived from quran.com/QF WBW data and compiled into wallet software is very likely "redistributing ... raw API data ... as a ... content package" and needs a signed commercial license. The 1-week cache rule is also incompatible with a fixed, permanent word list.
- QUL WBW files are easy to download but come without per-resource licences on the listing. Under the FAQ, each resource's author must be identified and their terms checked. Some, such as Russian and German, are explicitly copyrighted.
- Coverage of the target languages in QUL: English, Urdu, Indonesian, Bengali, Turkish, French, Hindi and Persian are present. Malay was not seen in the WBW list.
- Because the glosses are per occurrence, the same lemma can have several different glosses. Building a per-lemma list would require choosing or normalising one gloss per lemma per language. That editorial work produces a new derived work whose licensing depends on the source.

### Gaps
- QUL's homepage says there are 22 WBW resources, but only 13 distinct WBW entries showed on the listing page (pagination may not have loaded). Malay, Malayalam, Bosnian, Albanian, Spanish and Chinese WBW availability was not confirmed.
- I could not see per-resource author or licence metadata for the QUL English, Urdu, Indonesian and other WBW files. The detail pages were not reached.
- The provenance of quran.com's English WBW (whether it derives from QAC glosses or was produced separately) is unconfirmed.
- Exactly which languages api.quran.com v4 supports for `word_translation_language` was not confirmed from the API docs.
- The Tarteel/QUL site-level "Terms of use" (linked in the footer at tarteel.ai) was not read.

---

## 4. Other sources: KFGQPC, classical dictionaries, open datasets, WordNets, WBW apps

### Takeaway
- **KFGQPC fonts:** proprietary; no reproduction or modification without written approval.
- **Penrice (1873) and Lane's Lexicon (1863–1893):** public-domain 19th-century works. Penrice is Quran-specific and organised by root with English meanings per word. Lane's Perseus TEI digitisation is CC BY-SA.
- **QuranMorph (Birzeit SinaLab, 2025):** a newer, manually lemmatised corpus linked to the Qabas lexicon. The paper is CC BY 4.0, but the data licence is unconfirmed.
- **Arabic WordNet:** open-licensed (CC BY-SA 3.0 in OMW; v4.0 CC BY 4.0) but Modern Standard Arabic, not Quran-specific.
- **QuranWBW.com:** states its data belongs to its owners and copying is not allowed.

### Cited Findings
- KFGQPC Uthmanic Script HAFS font licence (verbatim): "This Font is the property of King Fahd Glorious Quran Printing Complex, and may not be reproduced, modified without the express written approval of King Fahd Glorious Quran Printing Complex." — [ScanCode LicenseDB: kfgqpc-uthmanic-script-hafs](https://scancode-licensedb.aboutcode.org/kfgqpc-uthmanic-script-hafs.LICENSE)
- Quran Foundation has a separate legal page on Mushaf fonts and images. — [QF Mushaf Fonts and Images](https://api-docs.quran.foundation/legal/mushaf-fonts-and-images/) (not read in detail)
- Penrice:
  - "A Dictionary and Glossary of the Koran" was first published 1873 by John Penrice and drew on Flügel's *Concordantiae Corani arabicae*.
  - It has been reprinted (Adam Publishers 1988; Asian Educational Services 2006).
  - Badawi called it "the first of its kind and 'for a long time the only one'".
  - An 1874 review noted "a suspicion of plagiarism".
  — [Wikipedia: Dictionary and Glossary of the Koran](https://en.wikipedia.org/wiki/Dictionary_and_Glossary_of_the_Koran)
- Archive.org scans of Penrice (London, Henry S. King & Co., 1873) exist as items `bub_gb_2oCjQOQvhg0C` (PDF) and `adictionaryandg00penrgoog` (EPUB). — [archive.org PDF](https://www.archive.org/download/bub_gb_2oCjQOQvhg0C/bub_gb_2oCjQOQvhg0C.pdf); [archive.org EPUB](https://dn790002.ca.archive.org/0/items/adictionaryandg00penrgoog/adictionaryandg00penrgoog.epub) (links not opened)
- Lane's Lexicon: Perseus published the first digital edition of Lane's Arabic-English Lexicon in TEI XML under a Creative Commons licence. Perseus repository materials are licensed CC BY-SA 3.0 US. — [Perseus announcements](https://www.perseus.tufts.edu/hopper/help/oldannounce.jsp); [PerseusDL canonical](https://github.com/PerseusDL/canonical) (via search summary)
- QuranMorph (Akra, Hammouda, Jarrar):
  - 77,429 tokens, "manually lemmatized and tagged with its part-of-speech by three expert linguists".
  - Uses the SAMA/Qabas tagset (40 tags), with lemmas linked to the Qabas lexicon.
  - Described as "open-source and publicly available" at sina.birzeit.edu/quran.
  - The arXiv article carries a CC BY 4.0 icon.
  — [arXiv 2506.18148](https://arxiv.org/abs/2506.18148)
- The SinaLab page describes it as "Morphology tagging of the Holy Quran. Each word is linked with a lemma in Qabas lexicon, and is tagged with a part of speach tag." Download is via a form, and the page shows no licence text. — [SinaLab QuranMorph](https://sina.birzeit.edu/quran/)
- Arabic WordNet:
  - Standard Arabic WordNet (AWN v2) in the Open Multilingual Wordnet is listed as CC BY-SA 3.0.
  - Arabic WordNet 4.0 (MSA) on Zenodo is listed as CC BY 4.0.
  - The Arabic Ontology is CC BY-NC-SA 4.0.
  — [OMW wordnets list](https://compling.upol.cz/omw/omw_wns); [Zenodo Arabic WordNet 4.0](https://zenodo.org/records/18872077); [arXiv: Arabic Ontology](https://arxiv.org/pdf/2205.09664) (search-snippet level; the OMW page returned HTTP 500 when fetched)
- QuranWBW.com: "all the data used on the website belongs to their respective owners/authors and copying them is not allowed". The README says it uses "its very own data" and references QUL. — [old.quranwbw.com](https://old.quranwbw.com/98) (via search summary); [GitHub marwan/quranwbw](https://github.com/marwan/quranwbw)
- Quranpedia dumps:
  - Quran text and classical tafsirs: "Free to use inside apps, websites, bots, and research tools — no attribution required". Republishing requires credit.
  - QAC morphology stays GNU GPL.
  - Translations "remain the intellectual property of their respective authors and publishers".
  - Distributors must keep the data updated via a changes API.
  — [Quranpedia LICENSE](https://quranpedia.net/dumps/LICENSE.md)
- The quRan R package is MIT-licensed and includes the Yusuf Ali and Saheeh International translations (verse-level). — [CRAN quRan](https://cran.r-project.org/web/packages/quRan/quRan.pdf) (via search summary)

### Inferences
- Penrice (1873) is in the US public domain (published before 1931). Its author lived in the 19th century, so it is almost certainly public domain in life+70 jurisdictions as well. Penrice is the most legally clean source of per-lemma English Quranic meanings. Drawbacks: Victorian English, an OCR-quality digitisation (no clean machine-readable dataset found), and English only.
- Lane's Lexicon text is public domain, but the Perseus TEI digitisation is CC BY-SA. A word list adapted from Perseus data would have to be shared under CC BY-SA (the list file itself, not necessarily the wallet code). Lane is far broader than Quranic usage.
- KFGQPC fonts or text should not be bundled without written approval. They are not needed for a word list.
- An MIT package licence (quRan) does not relicense the copyrighted translations it contains. Saheeh International is a commercially published translation; its copyright status was not verified here.
- Arabic WordNet could in principle supply cross-lingual synset links to other wordnets, a possible route to multilingual per-sense meanings. It is MSA-oriented, coverage of Quranic lemmas is unknown, and CC BY-SA share-alike applies to v2.

### Gaps
- Penrice's dates of birth and death were not confirmed. No clean, machine-readable transcription of Penrice was found.
- The data licence for QuranMorph/Qabas (as opposed to the paper) was not confirmed. The download form terms were not visible.
- No Quranic WordNet with a confirmed licence was found. Other modern Quranic dictionaries (Badawi & Abdel Haleem 2008; Abdul Mannan Omar; Hans Wehr) are presumed copyrighted. Not researched in detail.
- No Kaggle WBW dataset with clear provenance was reviewed. Most GitHub/Kaggle WBW dumps appear to be scraped from quran.com, QAC or QUL and would inherit those restrictions (inferred).

---

## 5. Per-lemma (dictionary) glosses versus per-occurrence (contextual) glosses

### Takeaway
Almost all machine-readable multilingual WBW data (QUL, quran.com API, QAC word-by-word view, QuranWBW) is per occurrence. One gloss per word position (about 77k tokens), so meanings vary with context. Per-lemma or per-form glosses exist in the QAC Quran Dictionary (English only, website, permission needed) and in public-domain classical dictionaries (Penrice, Lane; English only). I found no open, per-lemma, multilingual, index-aligned gloss dataset.

### Cited Findings
- QAC English glosses are contextual and based on Sahih International, Pickthall and Yusuf Ali. — [QAC FAQ](https://corpus.quran.com/faq.jsp)
- The QAC Quran Dictionary gives one gloss per derived form (e.g. "to write, to prescribe, to ordain" for form I kataba) plus per-occurrence renderings. — [QAC Quran Dictionary ktb](https://corpus.quran.com/qurandictionary.jsp?q=ktb)
- The QUL word translation tool stores one translation per Quran word position (word number plus Uthmani text plus English/Urdu). — [QUL word translation tool](https://qul.tarteel.ai/word_translations/1401)
- QUL lemma, root and stem mapping files map each word occurrence to its lemma, root or stem. They contain no glosses. — [QUL morphology](https://qul.tarteel.ai/resources/morphology)
- Penrice is a dictionary and glossary giving word meanings, compiled from Flügel's concordance. — [Wikipedia: Penrice](https://en.wikipedia.org/wiki/Dictionary_and_Glossary_of_the_Koran)

### Inferences
- A per-lemma multilingual list could in theory be built by joining a lemma mapping (QAC or QUL) to per-occurrence WBW glosses and taking the most frequent gloss per lemma per language. That output is derived from per-occurrence datasets, so their licence terms apply, including QF's redistribution clause and QUL per-author terms.
- For a seed-phrase list it is also better technically to choose about 2,048 lemmas with one clear, concrete, non-ambiguous meaning (nouns and concrete verbs) and write the per-language meanings fresh, rather than inheriting context-dependent glosses.

### Gaps
- I found no study measuring how consistent WBW glosses are per lemma across languages.

---

## 6. Practical recommendation: safest, highest-quality combination and the permissions needed

### Takeaway
For the Arabic lemma list with roots and POS, use QAC v0.4 (or its improved fork) or QuranMorph as the reference. Because the wallet is closed source and commercial, get written permission from QAC/Kais Dukes, or confirm SinaLab's data licence, before shipping any derived list. Take word spellings from Tanzil's Simple-Clean or Simple-Minimal text, with Tanzil credit and a link. For the multilingual meanings, the legally safest path is a project-owned translation: use public-domain Penrice and Lane as English references, then commission or produce translations into each target language for the selected lemmas. QUL WBW and the quran.com/QF API should be used only for consistency checking, or under explicit licences.

### Cited Findings
- QAC needs attribution with a link, verbatim-only copying of the file, and a copyright notice in derived works. The FAQ says shared data is for research, not commercial use. — [QAC download](https://corpus.quran.com/download/); [QAC FAQ](https://corpus.quran.com/faq.jsp)
- Tanzil text: CC BY 3.0 with no changes allowed and credit plus a link required. Tanzil translations are non-commercial unless the translator or publisher grants permission. — [Tanzil text license](https://tanzil.net/docs/text_license); [Tanzil translations](https://tanzil.net/trans/)
- QF API data redistribution "as a dataset ... content package" needs a signed commercial license. — [QF Developer Terms](https://api-docs.quran.foundation/legal/developer-terms/)
- QUL: commercial use is possible subject to each resource's licence, and some resources (Russian, German WBW) are copyrighted. — [QUL FAQ](https://qul.tarteel.ai/faq); [QUL translation resources](https://qul.tarteel.ai/resources/translation)
- Penrice (1873) and Lane's Lexicon are 19th-century works. Lane's Perseus TEI is CC BY-SA 3.0 US. — [Wikipedia: Penrice](https://en.wikipedia.org/wiki/Dictionary_and_Glossary_of_the_Koran); [Perseus announcements](https://www.perseus.tufts.edu/hopper/help/oldannounce.jsp)

### Inferences
Recommended pipeline (inferred, not legally verified):
1. **Arabic list.**
   - Pick candidate lemmas using QAC v0.4 lemma, root and POS data. QAC is the most established source; the mustafa0x fork has corrections. Cross-check with QuranMorph/Qabas if its licence allows.
   - Store the Arabic form as the lemma in Tanzil-consistent Unicode, e.g. Simple-Clean for unambiguous typing, with a vocalised display form from Simple or Uthmani.
   - Credit Tanzil with a link to tanzil.net and QAC with a link to corpus.quran.com.
   - Request written permission from QAC (Kais Dukes) covering: commercial use, a closed-source licence, and derivation or alteration of the lemma, root and POS fields.
2. **English meanings.** Write one short, unambiguous English meaning per lemma. Use Penrice (public domain) and Lane (public domain; avoid copying Perseus TEI text unless CC BY-SA is acceptable for the list file) as references. Optionally ask QAC for permission to use its Quran Dictionary per-form glosses.
3. **Other languages** (Urdu, Indonesian, Bengali, Turkish, Malay, French, Hindi, Persian, etc.).
   - Commission native-speaker translators, ideally with Islamic-studies background, to translate the fixed English or Arabic meaning list. The result is an original work owned by Bucks, and per-lemma alignment (entry i means the same in every list) is designed in rather than inherited.
   - QUL WBW (Urdu, Indonesian, Bengali, Turkish, French, Hindi, Persian, Tamil) can serve as a reference to check terminology. Do not copy it unless the specific resource's author licence permits.
   - Malay has no QUL WBW source and needs fresh translation in any case.
4. **Avoid:**
   - bundling quran.com/QF API data without a signed QF commercial license;
   - Tanzil translations (non-commercial);
   - QuranWBW data ("copying them is not allowed");
   - KFGQPC fonts or text without written approval.
5. **Permission or attribution actions:**
   - email QAC/Kais Dukes (permission);
   - optionally email Tanzil to confirm word extraction is fine;
   - if QUL or QF data is used, contact Quran Foundation for a commercial license and each QUL resource author;
   - include attribution notices (Tanzil, QAC, Perseus if used) in the wallet's about or licence screen and in the word-list file header.
6. **Fallback if QAC permission is refused:**
   - derive lemma and root assignments from public-domain Penrice and Lane, plus project linguists working directly on the Tanzil text;
   - or use QuranMorph if SinaLab confirms a commercial-compatible licence such as CC BY 4.0.

### Gaps
- No primary source confirms how GPL data inside closed-source software is treated in this specific case. Legal review is recommended.
- Whether QAC (Kais Dukes) grants commercial permissions, and on what terms, is unknown. Contact details and response policy were not researched.
- The data licence of QuranMorph/Qabas is unconfirmed.
- Per-resource authors and licences of QUL WBW translations are unconfirmed.
- I found no evidence of any existing open, per-lemma, index-aligned, multilingual Quranic gloss list. If one exists, these searches did not surface it.
