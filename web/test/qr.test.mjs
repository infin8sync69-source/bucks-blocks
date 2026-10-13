// Run: node web/test/qr.test.mjs
// Checks the QR encoder against known capacity figures and decodes its own
// output with an independent reader (format info, unmasking, de-interleaving,
// Reed-Solomon syndrome check, payload parse).
import assert from "node:assert/strict";
import { encode, numDataCodewords } from "../js/qr.js";

// Data capacity in bytes for ECC level M, versions 1..40 (ISO 18004 table 7).
const CAP_M_BYTES = [14,26,42,62,84,106,122,152,180,213,251,287,331,362,412,450,504,560,624,666,
  711,779,857,911,997,1059,1125,1190,1264,1370,1452,1538,1628,1722,1809,1911,1989,2099,2213,2331];
for (let v = 1; v <= 40; v++) {
  const ccBits = v <= 9 ? 8 : 16;
  const cap = Math.floor((numDataCodewords(v) * 8 - 4 - ccBits) / 8);
  assert.equal(cap, CAP_M_BYTES[v - 1], `capacity of version ${v}`);
}

// ---- independent reader ----
const exp = new Array(512), log = new Array(256);
for (let i = 0, x = 1; i < 255; i++) { exp[i] = x; log[x] = i; x <<= 1; if (x & 256) x ^= 0x11d; }
for (let i = 255; i < 512; i++) exp[i] = exp[i - 255];
const mul = (a, b) => (a && b ? exp[log[a] + log[b]] : 0);

const ECC = [-1,10,16,26,18,24,16,18,22,22,26,30,22,22,24,24,28,28,26,26,26,26,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28];
const BLK = [-1,1,1,1,2,2,4,4,4,5,5,5,8,9,9,10,10,11,13,14,16,17,17,18,20,21,23,25,26,28,29,31,33,35,37,38,40,43,45,47,49];

function align(ver) {
  if (ver === 1) return [];
  const n = Math.floor(ver / 7) + 2, size = ver * 4 + 17;
  const step = ver === 32 ? 26 : Math.ceil((ver * 4 + 4) / (n * 2 - 2)) * 2;
  const r = [6]; for (let p = size - 7; r.length < n; p -= step) r.splice(1, 0, p); return r;
}

function decode(q) {
  const { size, modules } = q;
  const ver = (size - 17) / 4;
  const at = (x, y) => modules[y * size + x];
  // format information (copy 1) must be a valid BCH codeword with ECC M
  let fmt = 0;
  for (let i = 0; i <= 5; i++) fmt |= at(8, i) << i;
  fmt |= at(8, 7) << 6; fmt |= at(8, 8) << 7; fmt |= at(7, 8) << 8;
  for (let i = 9; i < 15; i++) fmt |= at(14 - i, 8) << i;
  let fmt2 = 0;
  for (let i = 0; i < 8; i++) fmt2 |= at(size - 1 - i, 8) << i;
  for (let i = 8; i < 15; i++) fmt2 |= at(8, size - 15 + i) << i;
  assert.equal(fmt, fmt2, "both format copies agree");
  const f = fmt ^ 0x5412;
  const data10 = f >>> 10; // 5 bits: ecl(2) mask(3)
  assert.equal(data10 >>> 3, 0, "ECC level M");
  let rem = data10; for (let i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >>> 9) * 0x537);
  assert.equal(rem, f & 0x3ff, "format BCH");
  const mask = data10 & 7;

  // function-pattern map
  const fn = new Uint8Array(size * size);
  const mark = (x0, y0, w, h) => { for (let y = y0; y < y0 + h; y++) for (let x = x0; x < x0 + w; x++) if (x >= 0 && y >= 0 && x < size && y < size) fn[y * size + x] = 1; };
  mark(0, 0, 9, 9); mark(size - 8, 0, 8, 9); mark(0, size - 8, 9, 8);
  for (let i = 0; i < size; i++) { fn[6 * size + i] = 1; fn[i * size + 6] = 1; }
  const ap = align(ver), n = ap.length;
  for (let i = 0; i < n; i++) for (let j = 0; j < n; j++)
    if (!((i === 0 && j === 0) || (i === 0 && j === n - 1) || (i === n - 1 && j === 0))) mark(ap[i] - 2, ap[j] - 2, 5, 5);
  if (ver >= 7) { mark(size - 11, 0, 3, 6); mark(0, size - 11, 6, 3); }

  const maskFn = [(x,y)=>(x+y)%2===0,(x,y)=>y%2===0,(x,y)=>x%3===0,(x,y)=>(x+y)%3===0,
    (x,y)=>(Math.floor(x/3)+Math.floor(y/2))%2===0,(x,y)=>(x*y)%2+(x*y)%3===0,
    (x,y)=>((x*y)%2+(x*y)%3)%2===0,(x,y)=>((x+y)%2+(x*y)%3)%2===0][mask];
  const bits = [];
  for (let right = size - 1; right >= 1; right -= 2) {
    if (right === 6) right = 5;
    for (let v = 0; v < size; v++) for (let j = 0; j < 2; j++) {
      const x = right - j, up = ((right + 1) & 2) === 0, y = up ? size - 1 - v : v;
      if (!fn[y * size + x]) bits.push(at(x, y) ^ (maskFn(x, y) ? 1 : 0));
    }
  }
  const raw = Math.floor(bits.length / 8);
  const cw = []; for (let i = 0; i < raw; i++) { let b = 0; for (let k = 0; k < 8; k++) b = (b << 1) | bits[i * 8 + k]; cw.push(b); }
  // de-interleave
  const nb = BLK[ver], eccLen = ECC[ver], shortN = nb - (raw % nb), shortLen = Math.floor(raw / nb);
  const blocks = Array.from({ length: nb }, () => []);
  let k = 0;
  for (let i = 0; i < shortLen + 1; i++) for (let j = 0; j < nb; j++) {
    const dataLen = shortLen - eccLen + (j < shortN ? 0 : 1);
    if (i < dataLen) blocks[j].push(cw[k++]);
  }
  for (let i = 0; i < eccLen; i++) for (let j = 0; j < nb; j++) blocks[j].push(cw[k++]);
  assert.equal(k, raw);
  // RS syndromes must all be zero
  for (const blk of blocks) for (let s = 0; s < eccLen; s++) {
    let acc = 0; for (const c of blk) acc = mul(acc, exp[s]) ^ c;
    assert.equal(acc, 0, "RS syndrome");
  }
  const data = blocks.flatMap((b) => b.slice(0, b.length - eccLen));
  let pos = 0; const rd = (n) => { let v = 0; for (let i = 0; i < n; i++) { v = (v << 1) | ((data[pos >>> 3] >>> (7 - (pos & 7))) & 1); pos++; } return v; };
  assert.equal(rd(4), 4, "byte mode");
  const len = rd(ver <= 9 ? 8 : 16);
  const out = []; for (let i = 0; i < len; i++) out.push(rd(8));
  return { text: new TextDecoder().decode(Uint8Array.from(out)), version: ver };
}

const samples = [
  "", "a", "BUCKS", "1BoatSLRHtKNngkdXEeobR76b53LETtpyT",
  "bucks:mDk3PqSo7Z9Gm2yP3eY6vJ4nXw8kT1uQh5?amount=1.5&label=Tést ✓",
  "x".repeat(14), "x".repeat(15), "y".repeat(150), "z".repeat(500), "ö".repeat(300), "Q".repeat(1200), "7".repeat(2331),
];
const seen = new Set();
for (const s of samples) {
  const q = encode(s);
  const d = decode(q);
  assert.equal(d.text, s);
  seen.add(d.version);
}
assert.throws(() => encode("x".repeat(2332)), RangeError);
// every version 1..40 round-trips at its exact capacity
for (let v = 1; v <= 40; v++) {
  const s = "k".repeat(CAP_M_BYTES[v - 1]);
  const q = encode(s);
  assert.equal(q.version, v);
  assert.equal(decode(q).text, s);
}
// a 34-character address fits in version 3
assert.equal(encode("1BoatSLRHtKNngkdXEeobR76b53LETtpyT").version, 3);
console.log("qr.test.mjs: ok (versions seen:", [...seen].sort((a, b) => a - b).join(","), ")");
