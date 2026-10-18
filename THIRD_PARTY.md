# Third-party code

| Component | Version | Source | Integrity |
|---|---|---|---|
| SQLite amalgamation (`src/sqlite3.c`, `include/sqlite3.h`) | 3.54.0 | https://www.sqlite.org/2026/sqlite-amalgamation-3540000.zip | SHA3-256 `7b670a62fdfbd672b75fef004cb703c8a3e87d3a5cc7d675b4a08337004a2d93`, matching the value published on https://www.sqlite.org/download.html |

Build definitions (CMakeLists.txt and build.sh, kept identical by `build_hygiene`):
`SQLITE_THREADSAFE=1`, `SQLITE_DQS=0`, `SQLITE_OMIT_LOAD_EXTENSION`.
The storage spec requires 3.51.3 or newer; `src/chaindb.cpp` fails to compile on older headers.

## Word list sources

| Component | Source | Terms |
|---|---|---|
| `wordlists/ar-quran-v1-2048.txt` (Arabic list, frozen version 1) | Selected by `tools/make_ar_quran_list.py` from the Quranic Arabic Corpus morphology, v0.4 (Kais Dukes, University of Leeds), Arabic-script copy at https://github.com/mustafa0x/quran-morphology (file `quran-morphology.txt`, 6,322,866 bytes, fetched 2026-10-13 and used as data only, never executed) | Terms of use: credit "Quranic Arabic Corpus, http://corpus.quran.com" and link to it; the corpus file itself is not redistributed here, only a list of 2048 words chosen from it |

The list was frozen on 2026-10-18 without a scholar review (it can only be improved by a new list). Reviewers use `wordlists/review/ar-quran-draft-review.csv` (word, corpus lemma, part of speech, root, occurrences in the Quran).
Rebuild: `EXACT=2 python3 -I tools/make_ar_quran_list.py <quran-morphology.txt>` (exact Quranic word forms; without `EXACT` it builds the dictionary-form variant).

## Test data

| Component | Source | Terms |
|---|---|---|
| `tests/unit/data/aserti3-2d/run01` … `run12` (official aserti3-2d test vectors, used only by tests) | https://gitlab.com/bitcoin-cash-node/bchn-sw/qa-assets, `test_vectors/aserti3-2d`, commit `e1a6a9d4b0cc701b9fdd57204a4d611af419b3d0`, fetched 2026-10-17; per-file SHA-256 in the directory's `README.md` | MIT; the licence text is kept beside the files (`LICENSE`) |
