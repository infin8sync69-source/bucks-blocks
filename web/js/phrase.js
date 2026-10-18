// Recovery-phrase checks done in the browser while the user confirms a backup. No DOM, no dependencies.

// The node's "arabic" normalizer (src/bip_8192.cpp, arabicFold): decompose, drop harakat, Quranic marks,
// tatweel and invisible direction marks, then fold the letter variants. Kept in sync by web/test/phrase.test.mjs.
const ARABIC_STRIP = /[\u064B-\u065F\u0670\u06D6-\u06ED\u0640\u200B-\u200F\u061C\uFEFF]/g;
const ARABIC_FOLD = {
  "\u0623": "\u0627", "\u0625": "\u0627", "\u0622": "\u0627", "\u0671": "\u0627", // alef variants
  "\u0649": "\u064A", "\u06CC": "\u064A", "\u0626": "\u064A", // yeh variants
  "\u06A9": "\u0643", // keheh -> kaf
  "\u0629": "\u0647", "\u06C1": "\u0647", "\u06D5": "\u0647", // heh variants
  "\u0624": "\u0648", // waw with hamza
};
const ARABIC_FOLD_RE = new RegExp(`[${Object.keys(ARABIC_FOLD).join("")}]`, "g");

/** One word as the node compares it ("" when the normalizer refuses it). */
export function normalizeWord(word, normalizer) {
  const w = String(word ?? "").replace(/^[ \t\n\r\f\v]+|[ \t\n\r\f\v]+$/g, "");
  if (!w) return "";
  if (normalizer === "latin") return /^[\x00-\x7F]+$/.test(w) ? w.toLowerCase() : "";
  if (normalizer === "arabic") return w.normalize("NFKD").replace(ARABIC_STRIP, "").replace(ARABIC_FOLD_RE, (c) => ARABIC_FOLD[c]);
  return w;
}

/** True when `typed` is the expected word under the normalizer the node would use. */
export function wordMatches(typed, expected, normalizer) {
  const t = normalizeWord(typed, normalizer);
  return t !== "" && t === normalizeWord(expected, normalizer);
}

/** How many words the backup check asks for: 3 of a 13-word phrase, 4 of a longer one (ONB-16). */
export const checkWordCount = (n) => (n > 13 ? 4 : 3);

/** `count` distinct word positions below n, ascending, chosen with `random` (fills a Uint32Array). */
export function pickCheckIndexes(n, count = checkWordCount(n), random = (a) => crypto.getRandomValues(a)) {
  const want = Math.min(count, n);
  const picked = new Set();
  const buf = new Uint32Array(want * 4);
  random(buf);
  for (const v of buf) { if (picked.size < want) picked.add(v % n); }
  for (let i = 0; picked.size < want; i++) picked.add(i); // only if the random values kept colliding
  return [...picked].sort((x, y) => x - y);
}
