// Run: node web/test/phrase.test.mjs
// The backup check compares words the way the node does (ONB-16).
import assert from "node:assert/strict";
import fs from "node:fs";
import { normalizeWord, wordMatches, checkWordCount, pickCheckIndexes } from "../js/phrase.js";

// Arabic words are written with escapes so that invisible marks stay visible.
// The examples of ONB-16: diacritics and a missing hamza, which POST /api/wallets/restore accepts.
assert.equal(wordMatches("\u0639\u064E\u0627\u064E\u0635\u064E\u0641", "\u0639\u0627\u0635\u0641", "arabic"), true);   // "asif" with fathas ~ "asif"
assert.equal(wordMatches("\u0645\u064E\u0627\u064E\u0645\u064E\u0648\u064E\u0646", "\u0645\u0623\u0645\u0648\u0646", "arabic"), true); // "mamun" with fathas, no hamza ~ "ma'mun"
// Decomposed hamza, alef madda, tatweel, a zero-width joiner and a right-to-left mark.
assert.equal(wordMatches("\u0645\u0627\u0654\u0645\u0648\u0646", "\u0645\u0623\u0645\u0648\u0646", "arabic"), true);
assert.equal(wordMatches("\u0622\u0645\u0646", "\u0627\u0645\u0646", "arabic"), true);
assert.equal(wordMatches("\u0639\u0640\u0627\u0635\u0641\u200D\u200F", "\u0639\u0627\u0635\u0641", "arabic"), true);
// Letter folds: alef maqsura and farsi yeh to yeh, teh marbuta to heh, keheh to kaf.
assert.equal(normalizeWord("\u0647\u062F\u0649", "arabic"), "\u0647\u062F\u064A");
assert.equal(normalizeWord("\u0647\u062F\u06CC", "arabic"), "\u0647\u062F\u064A");
assert.equal(normalizeWord("\u0631\u062D\u0645\u0629", "arabic"), "\u0631\u062D\u0645\u0647");
assert.equal(normalizeWord("\u06A9\u062A\u0628", "arabic"), "\u0643\u062A\u0628");
// Presentation forms (NFKD): the isolated lam-alef ligature is lam + alef.
assert.equal(normalizeWord("\uFEFB", "arabic"), "\u0644\u0627");
// A different word still fails.
assert.equal(wordMatches("\u0639\u0627\u0635\u0641\u0629", "\u0639\u0627\u0635\u0641", "arabic"), false);
assert.equal(wordMatches("", "\u0639\u0627\u0635\u0641", "arabic"), false);

// latin: ASCII case-folded and trimmed; anything else is refused, never equal to anything.
assert.equal(wordMatches("  Apple\t", "apple", "latin"), true);
assert.equal(wordMatches("appl\u00E9", "apple", "latin"), false);
assert.equal(normalizeWord("caf\u00E9", "latin"), "");
assert.equal(wordMatches("caf\u00E9", "caf\u00E9", "latin"), false);
assert.equal(wordMatches("", "", "latin"), false);

// Every shipped Arabic and English list is unique under these normalizers, as the node requires.
const dir = new URL("../../wordlists/", import.meta.url);
const manifest = JSON.parse(fs.readFileSync(new URL("manifest.json", dir), "utf8"));
for (const l of manifest.lists) {
  const words = fs.readFileSync(new URL(l.file, dir), "utf8").split(/\r?\n/).filter((w) => w && !w.startsWith("#"));
  assert.equal(words.length, 2048, l.lang);
  const seen = new Set(words.map((w) => normalizeWord(w, l.normalizer)));
  assert.equal(seen.size, 2048, `${l.lang} is unique after normalizing`);
  assert.equal(seen.has(""), false, `${l.lang} has no word the normalizer refuses`);
}

// How many words are checked, and which.
assert.equal(checkWordCount(13), 3);
assert.equal(checkWordCount(25), 4);
const seq = (vals) => (a) => { a.set(vals.slice(0, a.length)); return a; };
assert.deepEqual(pickCheckIndexes(13, 3, seq([5, 18, 2, 7, 1])), [2, 5, 7]);              // 18 % 13 = 5 repeats
assert.deepEqual(pickCheckIndexes(13, 2, seq([12, 25])), [0, 12]);
assert.deepEqual(pickCheckIndexes(25, undefined, seq(Array(16).fill(4))), [0, 1, 2, 4]);  // collisions fall back
for (let i = 0; i < 50; i++) {
  const idx = pickCheckIndexes(13);
  assert.equal(idx.length, 3);
  assert.equal(new Set(idx).size, 3);
  assert.ok(idx.every((x) => x >= 0 && x < 13));
}

console.log("phrase.test.mjs: ok");
