#!/usr/bin/env python3
"""One-shot: turn wordlists/review/arabic-proposed-changes.csv into build inputs.

Writes tools/ar_respell.csv (word -> modern respelling, read by make_ar_quran_list.py) and the 'r' line of
tools/ar_exclusions.txt (words the review found must go). Policy rows are NOT applied: the entries of categories
b and c with severity Should (named narratives, single-scene hell/paradise/Last-Day words), and every Consider row.
Run once per review; the generated files are committed so the build stays reproducible.
"""
import csv, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
rows = list(csv.DictReader(open(os.path.join(HERE, "..", "wordlists", "review", "arabic-proposed-changes.csv"), encoding="utf-8")))

respell, drop = {}, []
for r in rows:
    act, sev, kind = r["proposed_action"], r["severity"], r["issue_type"]
    backfill = "backfill" in r["reason"]
    if act == "replace" and not backfill and r["replacement"]:
        respell[r["word"]] = r["replacement"]
        continue
    if sev == "Consider":
        continue
    if act in ("drop", "replace"):                       # replace + backfill means: this word goes
        policy = kind.startswith(("sens-b", "sens-c")) and sev == "Should"
        if policy:
            continue
        drop.append(r["word"])

with open(os.path.join(HERE, "ar_respell.csv"), "w", encoding="utf-8", newline="") as f:
    w = csv.writer(f)
    w.writerow(["word", "replacement"])
    for k, v in respell.items():
        w.writerow([k, v])

path = os.path.join(HERE, "ar_exclusions.txt")
text = open(path, encoding="utf-8").read()
text = re.sub(r"\n# Arabic review 2026-10-14.*\Z", "", text, flags=re.S)
text += ("\n# Arabic review 2026-10-14 (reports/lists-review/arabic-review.md): every Must-fix word and the rule-based\n"
         "# Should-fix words (divine names and attributes, ritual, scripture, punishment, formulae, second meanings,\n"
         "# near-duplicates). Policy rows (named scenes, single-context hell/paradise words) are deliberately not here.\n"
         "r " + " ".join(sorted(set(drop))) + "\n"
         "root بتل توب نسك قنت رتل لظي صلي سعر\n")
open(path, "w", encoding="utf-8").write(text)
print(f"{len(respell)} respellings, {len(set(drop))} exclusions")
