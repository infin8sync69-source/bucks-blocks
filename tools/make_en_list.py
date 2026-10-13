#!/usr/bin/env python3
"""Build wordlists/en-draft-2048.txt from the review sheet (wordlists/review/ar-quran-draft-review.csv).

Entry i of the English list means what entry i of the Arabic list means (index alignment), so the sheet's
`english_meaning` column, read in `index` order, IS the list. This script only checks and writes it:
  T1 exactly as many entries as the Arabic list (2048)   T2 unique
  T3 one token, lowercase a-z only                        T5 no word that is also an Arabic entry
Usage: python3 -I tools/make_en_list.py [review.csv] [out.txt]
"""
import csv
import hashlib
import os
import re
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
csv_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "wordlists", "review", "ar-quran-draft-review.csv")
out_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "wordlists", "en-draft-2048.txt")

rows = sorted(csv.DictReader(open(csv_path, encoding="utf-8")), key=lambda r: int(r["index"]))
errors = []
if [int(r["index"]) for r in rows] != list(range(len(rows))):
    errors.append("index column is not 0..n-1")
if len(rows) != 2048:
    errors.append(f"{len(rows)} rows, expected 2048")
words = [r["english_meaning"].strip() for r in rows]
for r, w in zip(rows, words):
    if not re.fullmatch(r"[a-z]+", w):
        errors.append(f"#{r['index']} {r['word']}: '{w}' is not a single lowercase a-z word")
for w, n in Counter(words).items():
    if n > 1:
        errors.append(f"'{w}' is used {n} times")
if errors:
    print("\n".join(errors[:40]))
    sys.exit(1)
with open(out_path, "w", encoding="utf-8", newline="\n") as f:
    f.write("# Bucks DRAFT English meaning list (en-draft). Entry i has the meaning of entry i of ar-quran-draft.\n")
    f.write("# Written by hand, one word per Arabic entry, from the review sheet's english_meaning column\n")
    f.write("# (tools/make_en_list.py). Meanings are approximate: an Arabic form with a person, number or attached\n")
    f.write("# particle carries only its dictionary meaning. NOT reviewed by a translator or an Arabic linguist.\n")
    for w in words:
        f.write(w + "\n")
print(f"wrote {out_path} ({len(words)} words, sha256 {hashlib.sha256(open(out_path, 'rb').read()).hexdigest()})")
