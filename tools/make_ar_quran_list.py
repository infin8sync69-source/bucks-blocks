#!/usr/bin/env python3
"""Build wordlists/ar-quran-draft-2048.txt: the DRAFT Arabic list for Bucks Mnemonic v2.

Source: Quranic Arabic Corpus morphology (v0.4, Kais Dukes, University of Leeds, corpus.quran.com; copy
with Arabic-script lemmas from github.com/mustafa0x/quran-morphology). Every line is one word segment
with its part of speech, root and LEMMA (dictionary form). The corpus is used as DATA only.
Credit: "Quranic Arabic Corpus, http://corpus.quran.com" (required by its terms of use).

Rules (reports/Quranic unique meaning word list.md, table A1-A11)
  content words only: nouns, adjectives, verbs. Dropped: particles, pronouns, demonstratives, relatives,
      conditionals, interrogatives, negations, conjunctions, locatives, proper nouns (QAC tags);
  A1 unique after folding (homographs keep their more frequent lemma)    A9 3 to 7 letters
  A2 no two entries one edit apart among words of 4 letters or fewer      A4 no ال prefix
  A3 no dot-shape twins among words of 4 letters or fewer
  exclusions: tools/ar_exclusions.txt (divine names, prophets and persons, last-day terms, worship,
      scripture, punishment/disbelief, divine-act verbs, formulae) plus every lemma whose QAC root is
      listed there under "root" lines.
Selection: all words that pass; if more than 2048 remain, the most frequent in the Quran first.
The spelling is the corpus lemma without diacritics (dagger alef dropped, as in modern spelling).

This script produced the draft that was frozen as wordlists/ar-quran-v1-2048.txt (2026-10-18). Never commit its output
over a frozen list: a change to the words ships as a new list (ar-quran-v2).

Modes
  default      dictionary forms (lemmas), e.g. verbs in the past tense for "he"
  EXACT=2      every entry is a word exactly as the Quran writes it (modern spelling): per lemma, its most
               frequent form standing alone; lemmas that never stand alone use their most frequent form
               with the attached particle or pronoun (tier 2, listed last when trimming)

Usage: [EXACT=2] python3 -I tools/make_ar_quran_list.py <quran-morphology.txt> [out.txt] [review.csv]
"""
import collections
import csv
import hashlib
import os
import re
import sys
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from make_dev_wordlist import arabic_fold  # noqa: E402  (same fold as Bucks::arabicFold)

LIST_SIZE = 2048
FOLDED_LETTERS = set("ابتثجحخدذرزسشصضطظعغفقكلمنهويء")
# Tags that mark a segment as not a content word.
NOT_CONTENT = {"PN", "PRON", "DEM", "REL", "COND", "INTG", "NEG", "CONJ", "LOC", "P", "SUB", "RES", "VOC",
               "CERT", "CIRC", "RSLT", "PRP", "ATT", "AMD", "ANS", "AVR", "CAUS", "COM", "EQ", "EXH", "EXL",
               "EXP", "FUT", "IMPN", "INC", "INL", "PREV", "PRO", "REM", "SUR", "ADDR", "DIST", "T"}
DOTLESS = {"ب": "ٮ", "ت": "ٮ", "ث": "ٮ", "ن": "ٮ", "ي": "ٮ", "ج": "ح", "خ": "ح", "ذ": "د", "ز": "ر",
           "ش": "س", "ض": "ص", "ظ": "ط", "غ": "ع", "ف": "ڡ", "ق": "ڡ"}


def one_edit(a, b):
    if a == b or abs(len(a) - len(b)) > 1:
        return False
    if len(a) == len(b):
        diff = [i for i in range(len(a)) if a[i] != b[i]]
        if len(diff) == 1:
            return True
        return len(diff) == 2 and diff[1] == diff[0] + 1 and a[diff[0]] == b[diff[1]] and a[diff[1]] == b[diff[0]]
    if len(a) > len(b):
        a, b = b, a
    return any(a == b[:i] + b[i + 1:] for i in range(len(b)))


def dotless(w):
    return "".join(DOTLESS.get(c, c) for c in w)


MARKS = set(range(0x064B, 0x0653)) | {0x0653, 0x0670, 0x06DF, 0x06E2, 0x06E6, 0x06E5, 0x06ED}


def lemma_fold(lemma):
    return arabic_fold(lemma)


def display_form(lemma):
    """The corpus lemma as people write it: short vowels and Quranic marks removed, ٱ -> ا, ة ى أ إ ؤ ئ kept."""
    out = "".join("ا" if ch == "\u0671" else ch for ch in lemma if ord(ch) not in MARKS)
    return out


def modernize(t):
    """Spellings that make a modern typist's input fold to something else (audit of the Arabic list, problem 1):
    initial or post-prefix ءا is آ, and a final ؤا is أ (the Uthmani silent alef)."""
    t = re.sub(r"(^|(?<=[وفبلك]))ءا", "آ", t)
    if t.endswith("ؤا"):
        t = t[:-2] + "أ"
    return t


def exact_display(token):
    """A Quranic word in modern spelling: marks and tatweel removed, ٱ -> ا, and the dagger alef written out
    the way modern spelling does: after a final ى it is just the ى (هدى), after a medial ى it makes the ا
    (تقاة), elsewhere it is an ا (كتاب). None when a mark we cannot place is left (hamza seats)."""
    chars = [("ا" if ch == "\u0671" else ch) for ch in token
             if ord(ch) not in (MARKS - {0x0670}) and ord(ch) != 0x0640]
    out = []
    for i, ch in enumerate(chars):
        if ch == "\u0670":
            rest = [c for c in chars[i + 1:] if c != "\u0670"]
            if out and out[-1] == "ى":
                if rest:
                    out[-1] = "ا"          # medial: تقىٰة -> تقاة
                # final: keep the ى, drop the dagger
            else:
                out.append("ا")
        else:
            out.append(ch)
    t = modernize(unicodedata.normalize("NFC", "".join(out)))
    if any(ch not in set("ءآأؤإئابةتثجحخدذرزسشصضطظعغفقكلمنهوىي") for ch in t):
        return None
    return t


def read_exclusions():
    words, roots, cats = {}, set(), {}
    for line in open(os.path.join(HERE, "ar_exclusions.txt"), encoding="utf-8"):
        body = line.split("#", 1)[0].split()
        if len(body) < 2:
            continue
        cat = body[0]
        for w in body[1:]:
            if cat == "root":
                roots.add(arabic_fold(w))
            else:
                words[arabic_fold(w)] = cat
    return words, roots


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    src = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "wordlists", "ar-quran-draft-2048.txt")
    csv_path = sys.argv[3] if len(sys.argv) > 3 else os.path.join(HERE, "..", "wordlists", "review", "ar-quran-draft-review.csv")

    exact = bool(os.environ.get("EXACT"))
    lemmas = {}   # (lemma, pos) -> stats
    seg_by_word = collections.defaultdict(list)
    for line in open(src, encoding="utf-8"):
        p = line.rstrip("\n").split("\t")
        if len(p) < 4:
            continue
        seg_by_word[tuple(p[0].split(":")[:3])].append((p[1], p[2], p[3].split("|")))
        feats = p[3].split("|")
        lem = next((f[4:] for f in feats if f.startswith("LEM:")), None)
        if lem is None:
            continue
        root = next((f[5:] for f in feats if f.startswith("ROOT:")), "")
        e = lemmas.setdefault((lem, p[2]), {"n": 0, "bad": 0, "root": root})
        e["n"] += 1
        if p[2] == "P" or NOT_CONTENT & set(feats):
            e["bad"] += 1

    # Exact mode: a word exactly as the Quran writes it, with nothing attached (one corpus segment = the whole
    # word), in modern spelling. Per lemma we later keep its most frequent such form.
    bare_forms = collections.defaultdict(collections.Counter)   # lemma -> Counter((folded, shown))
    full_forms = collections.defaultdict(collections.Counter)   # fallback: the whole word with what is attached
    if exact:
        for segs in seg_by_word.values():
            stems = [sg for sg in segs if sg[1] != "P" and not ({"SUFF", "PREF"} & set(sg[2]))]
            stems = [sg for sg in stems if any(f.startswith("LEM:") for f in sg[2])]
            if len(stems) != 1:
                continue
            txt, pos, feats = stems[0]
            lem = next((f[4:] for f in feats if f.startswith("LEM:")), None)
            if lem is None or NOT_CONTENT & set(feats):
                continue
            if len(segs) == 1:
                shown = exact_display(txt)
                if shown is not None:
                    bare_forms[lem][(arabic_fold(shown), shown)] += 1
            else:
                whole = exact_display("".join(sg[0] for sg in segs))
                if whole is not None:
                    full_forms[lem][(arabic_fold(whole), whole)] += 1

    # Reviewed respellings (tools/ar_respell.csv): Uthmani spellings written the modern way, bare forms.
    respell = {}
    rp = os.path.join(HERE, "ar_respell.csv")
    if os.path.exists(rp):
        for row in csv.DictReader(open(rp, encoding="utf-8")):
            respell[row["word"]] = row["replacement"]
    respelled_from = {v: k for k, v in respell.items()}

    excl_words, excl_roots = read_exclusions()
    log = collections.Counter()
    by_fold = {}
    for (lem, pos), e in lemmas.items():
        if e["bad"]:
            log["not a content word (particle, pronoun, name, ...)"] += 1
            continue
        w = arabic_fold(lem)
        w = "".join(c for c in w if c in FOLDED_LETTERS)
        shown = None
        if exact:
            def usable(table):
                return [((f, sh), c) for (f, sh), c in table.get(lem, {}).items()
                        if 3 <= len(f) <= 7 and not (f.startswith("ال") and len(f) > 4)]
            options = usable(bare_forms)
            tier2 = False
            if not options and os.environ.get("EXACT") == "2":
                tier2 = True
                options = usable(full_forms)       # tier 2: the whole word with its attached particle or pronoun
                if options:
                    log["(tier 2: word with attached particle/pronoun)"] += 1
            if not options:
                log["no stem-only form of 3-7 letters in the Quran"] += 1
                continue
            (w, shown), best = max(options, key=lambda o: (o[1], -len(o[0][0])))
            e = dict(e, n=sum(c for _, c in options))
        if not 3 <= len(w) <= 7:
            log["length not 3-7 (A9)"] += 1
            continue
        if w.startswith("ال") and len(w) > 4:
            log["starts with ال (A4)"] += 1
            continue
        if w in excl_words or arabic_fold(lem) in excl_words:
            log[f"excluded category {excl_words.get(w) or excl_words[arabic_fold(lem)]}"] += 1
            continue
        if arabic_fold(e["root"]) in excl_roots:
            log["excluded root"] += 1
            continue
        cur = by_fold.get(w)
        if cur is not None:
            log["homograph, less frequent lemma dropped (A1/A11)"] += 1
            if cur["n"] >= e["n"]:
                continue
        shown = shown if exact else display_form(lem)
        if shown in respell:
            shown = respell[shown]
            w = "".join(c for c in arabic_fold(shown) if c in FOLDED_LETTERS)
            if not 3 <= len(w) <= 7:
                log["respelled form length not 3-7"] += 1
                continue
            if w in excl_words:
                log["excluded after respelling"] += 1
                continue
        if shown.startswith(("ئ", "ؤ", "ى", "ة")):
            log["malformed start (a seat or ending letter first)"] += 1
            continue
        if shown.endswith(("ؤ", "ئ")):
            log["odd final hamza (pausal spelling in the corpus lemma)"] += 1
            continue
        if arabic_fold(shown) != (w if exact else lemma_fold(lem)) and shown not in respelled_from:
            log["display form does not fold to the lemma (skipped)"] += 1
            continue
        by_fold[w] = {"tier2": exact and tier2, "word": w, "shown": shown, "lemma": lem, "pos": pos, "root": e["root"], "n": e["n"]}

    pool = sorted(by_fold.values(), key=lambda r: (-r["n"], len(r["word"]), r["word"]))
    print(f"content lemmas after rules A1/A4/A9 and exclusions: {len(pool)}")

    # Long words (5+ letters) have no typo rule. Short words (<= 4 letters) must be two edits apart and
    # not dot-shape twins: choose a large conflict-free set with a min-degree greedy pass (repeatedly
    # take the word with the fewest remaining conflicts, most frequent first on ties), then keep the
    # most frequent of everything if more than 2048 survive.
    longs = [r for r in pool if len(r["word"]) > 4]
    shorts = [r for r in pool if len(r["word"]) <= 4]
    conflicts = {r["word"]: set() for r in shorts}
    by_len = collections.defaultdict(list)
    for r in shorts:
        by_len[len(r["word"])].append(r["word"])
    for r in shorts:
        w = r["word"]
        for L in (len(w) - 1, len(w), len(w) + 1):
            for o in by_len.get(L, []):
                if o != w and one_edit(w, o):
                    conflicts[w].add(o)
    twins = collections.defaultdict(list)
    for r in shorts:
        twins[dotless(r["word"])].append(r["word"])
    for group in twins.values():
        for x in group:
            for y in group:
                if x != y:
                    conflicts[x].add(y)
    freq = {r["word"]: r for r in shorts}
    alive = set(conflicts)
    picked = []
    import heapq
    while alive:
        w = min(alive, key=lambda x: (len(conflicts[x] & alive), -freq[x]["n"], x))
        picked.append(freq[w])
        alive -= conflicts[w] | {w}
    log["short word in conflict (A2/A3), not picked"] = len(shorts) - len(picked)
    # bare words first, then words with an attached particle or pronoun (exact mode tier 2); then by frequency
    chosen = sorted(longs + picked, key=lambda r: (r["tier2"], -r["n"], len(r["word"]), r["word"]))
    print(f"after the typo rules: {len(chosen)}")
    for k, v in sorted(log.items(), key=lambda kv: -kv[1]):
        print(f"  dropped {v:5d}  {k}")
    if len(chosen) < LIST_SIZE:
        print(f"NOT ENOUGH: {len(chosen)} < {LIST_SIZE}")
        if not os.environ.get("ALLOW_SHORT"):
            sys.exit(1)
    chosen = chosen[:LIST_SIZE]
    once = sum(1 for r in chosen if r["n"] == 1)
    print(f"chosen {len(chosen)}; {once} occur only once in the Quran")

    chosen.sort(key=lambda r: r["word"])
    words = [r["word"] for r in chosen]          # folded forms, for the statistics below
    shown_words = [r["shown"] for r in chosen]   # what is written to the list
    by_len = collections.defaultdict(list)
    for w in words:
        by_len[len(w)].append(w)
    near = sum(1 for w in words if any(one_edit(w, o) for L in (len(w) - 1, len(w), len(w) + 1) for o in by_len.get(L, [])))
    prefix3 = collections.Counter(w[:3] for w in words if len(w) >= 4)
    print(f"informational: {near} words have a one-edit neighbour; "
          f"{sum(v for v in prefix3.values() if v > 1)} share their first 3 letters with another word")

    os.makedirs(os.path.dirname(os.path.abspath(csv_path)), exist_ok=True)
    # Keep what reviewers and translators already wrote, keyed by the Arabic word, so rebuilding the list
    # never throws their work away (a new word simply has empty cells).
    kept = {}
    if os.path.exists(csv_path):
        for old in csv.DictReader(open(csv_path, encoding="utf-8")):
            kept[old["word"]] = (old.get("reviewer_notes", ""), old.get("english_meaning", ""))
            kept.setdefault(modernize(old["word"]), kept[old["word"]])   # the same word, now spelled the modern way
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        what = ("2048 words exactly as written in the Quran (one per dictionary word, its most frequent form)"
                if exact else "2048 content lemmas (dictionary forms) attested in the Quran")
        f.write(f"# Bucks DRAFT Arabic word list (ar-quran-draft): {what},\n")
        f.write("# modern spelling, no diacritics. Built by tools/make_ar_quran_list.py from the\n")
        f.write("# Quranic Arabic Corpus (http://corpus.quran.com). NOT reviewed: scholar and linguist sign-off\n")
        f.write("# (research_notes/.../religious_considerations.md) is still required.\n")
        for w in shown_words:
            f.write(w + "\n")
    with open(csv_path, "w", encoding="utf-8", newline="") as f:
        wr = csv.writer(f)
        wr.writerow(["index", "word", "folded", "corpus_lemma", "pos", "root", "quran_occurrences", "reviewer_notes", "english_meaning"])
        for i, r in enumerate(chosen):
            notes, meaning = kept.get(r["shown"]) or kept.get(respelled_from.get(r["shown"], ""), ("", ""))
            wr.writerow([i, r["shown"], r["word"], r["lemma"], r["pos"], r["root"], r["n"], notes, meaning])
    print(f"wrote {out_path}\n  sha256 {hashlib.sha256(open(out_path, 'rb').read()).hexdigest()}\nwrote {csv_path}")


if __name__ == "__main__":
    main()
