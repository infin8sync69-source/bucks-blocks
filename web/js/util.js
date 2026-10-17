// Formatting, amount parsing and Base58Check address validation. No dependencies.

export const COIN = 100000000n; // base units per BUCKS
export const TICKER = "BUCKS";
export const MIN_FEE_UNITS = 100000n; // node policy: fee = max(0.001 BUCKS, size * 100)
export const DUST_LIMIT_UNITS = 546n; // node policy: smallest output it relays (also in /api/blockchain/info)
export const COINBASE_MATURITY = 100; // consensus: blocks before a reward can be spent

// Base58Check address version byte per network (src/chainparams.cpp).
// The API does not expose it yet, so it is mirrored here by network name.
export const ADDRESS_VERSION = { main: 0x1a, test: 0x41, regtest: 0x7f };

export const NETWORKS = {
  main: { label: "MAINNET", cls: "net-main" },
  test: { label: "TESTNET", cls: "net-test" },
  regtest: { label: "REGTEST", cls: "net-regtest" },
};

// ---- numbers ----
const grouper = (s) => s.replace(/\B(?=(\d{3})+(?!\d))/g, " ");

/** Base units (Number or BigInt) -> "1 234.50000000". trim keeps >= 2 decimals. */
export function fmtBucks(units, { trim = false } = {}) {
  let v;
  try { v = BigInt(units ?? 0); } catch { return "—"; }
  const neg = v < 0n;
  if (neg) v = -v;
  const whole = grouper((v / COIN).toString());
  let frac = (v % COIN).toString().padStart(8, "0");
  if (trim) frac = frac.replace(/0+$/, "").padEnd(2, "0");
  return `${neg ? "-" : ""}${whole}.${frac}`;
}

// Arabic-Indic (U+0660-0669) and Extended Arabic-Indic (U+06F0-06F9) digits, and the Arabic decimal separator.
const toAsciiDigits = (s) => s
  .replace(/[\u0660-\u0669]/g, (d) => String(d.charCodeAt(0) - 0x0660))
  .replace(/[\u06F0-\u06F9]/g, (d) => String(d.charCodeAt(0) - 0x06F0))
  .replace(/\u066B/g, ".");

/**
 * "12.5" -> 1250000000n. Returns { ok, units, error }. Strict: digits and at most 8 decimals.
 * A single comma is a decimal mark ("12,5"), except where it reads as a thousands separator: "1,000"
 * could be 1 or 1000, so it is refused rather than guessed (NEW-8, ONB-21).
 */
export function parseBucks(text) {
  let t = toAsciiDigits(String(text ?? "").trim());
  if (!t) return { ok: false, error: "Enter an amount." };
  const commas = (t.match(/,/g) || []).length;
  // U+066C is the Arabic thousands separator; "1,000" is a valid thousands grouping as well as a decimal.
  if (t.includes("\u066C") || commas > 1 || (commas && t.includes(".")) || /^[1-9]\d{0,2},\d{3}$/.test(t)) {
    return { ok: false, error: "Do not use thousands separators. Write the amount with a point for decimals, for example 1000 or 1.5." };
  }
  t = t.replace(",", ".");
  if (!/^\d*\.?\d*$/.test(t) || t === ".") return { ok: false, error: "Use digits and a decimal point only." };
  const [w, f = ""] = t.split(".");
  if (f.length > 8) return { ok: false, error: "At most 8 decimal places." };
  const units = BigInt(w || "0") * COIN + BigInt(f.padEnd(8, "0") || "0");
  if (units <= 0n) return { ok: false, error: "Amount must be greater than zero." };
  if (units > BigInt(Number.MAX_SAFE_INTEGER)) return { ok: false, error: "Amount is too large." };
  return { ok: true, units };
}

/**
 * Why a payment of `units` cannot be made from a wallet, or "" when it can. `spendable` is what the node can
 * spend now (balance minus immature rewards and coins locked by pending payments, ONB-07); `fee` is the
 * node's estimate when `feeKnown`, else the minimum fee; `dust` is the node's smallest relayed output (NEW-8).
 */
export function amountProblem(units, { balance, spendable, fee = MIN_FEE_UNITS, feeKnown = false, dust = DUST_LIMIT_UNITS }) {
  const u = BigInt(units), bal = BigInt(balance), sp = BigInt(spendable ?? balance), f = BigInt(fee), d = BigInt(dust);
  const t = (v) => fmtBucks(v, { trim: true });
  if (u < d) return `The network does not relay payments below ${t(d)} BUCKS.`;
  if (u > bal) return `That is more than this wallet holds (${t(bal)} BUCKS).`;
  if (u > sp) return `Only ${t(sp)} BUCKS can be spent now. The rest is mining rewards that are not mature yet, or coins locked in a payment that is still pending.`;
  if (u + f > sp) return `Not enough left to also pay the network fee (${feeKnown ? "" : "at least "}${t(f)} BUCKS). At most ${t(sp > f ? sp - f : 0n)} BUCKS can be sent.`;
  return "";
}

/**
 * How one entry of GET /api/wallets/<address>/history is shown: label, sign of the amount, status text and
 * pill tone. A mining reward is spendable once it has `maturity` confirmations.
 */
export function describeActivity(t, maturity = COINBASE_MATURITY) {
  const label = { sent: "Sent", received: "Received", mined: "Mining reward", self: "To yourself" }[t.kind] || "Transaction";
  const sign = t.kind === "sent" ? "-" : t.kind === "self" ? "" : "+";
  if (t.status === "pending") return { label, sign, status: "Pending", tone: "warn" };
  const conf = Number(t.confirmations) || 0;
  const left = maturity - conf;
  if (t.kind === "mined" && left > 0) return { label, sign, status: `Matures in ${left} block${left === 1 ? "" : "s"}`, tone: "warn" };
  return { label, sign, status: `${fmtInt(conf)} confirmation${conf === 1 ? "" : "s"}`, tone: "ok" };
}

export function fmtInt(n) {
  return Number.isFinite(Number(n)) ? grouper(String(Math.trunc(Number(n)))) : "—";
}

export function fmtHashRate(h) {
  const n = Number(h);
  if (!Number.isFinite(n) || n < 0) return "—";
  const u = ["H/s", "kH/s", "MH/s", "GH/s", "TH/s", "PH/s"];
  let i = 0, v = n;
  while (v >= 1000 && i < u.length - 1) { v /= 1000; i++; }
  return `${v >= 100 || i === 0 ? v.toFixed(0) : v.toFixed(2)} ${u[i]}`;
}

export function fmtDifficulty(d) {
  const n = Number(d);
  if (!Number.isFinite(n)) return "—";
  if (n >= 1e6) return n.toExponential(3).replace("e+", " × 10^");
  return n >= 100 ? n.toFixed(0) : n.toPrecision(4).replace(/\.?0+$/, "");
}

export function shortHash(h, head = 10, tail = 8) {
  if (!h || h.length <= head + tail + 1) return h || "";
  return `${h.slice(0, head)}…${h.slice(-tail)}`;
}

export function shortAddr(a) { return shortHash(a, 8, 6); }

export function fmtBits(bits) {
  return "0x" + (Number(bits) >>> 0).toString(16).padStart(8, "0");
}

export function timeAgo(unixSeconds, now = Date.now()) {
  const s = Math.round(now / 1000 - Number(unixSeconds));
  if (!Number.isFinite(s)) return "—";
  if (s < -300) return "in the future";
  if (s < 5) return "just now";
  if (s < 60) return `${s}s ago`;
  if (s < 3600) return `${Math.floor(s / 60)} min ago`;
  if (s < 86400) return `${Math.floor(s / 3600)} h ago`;
  return `${Math.floor(s / 86400)} d ago`;
}

export function fmtDate(unixSeconds) {
  const d = new Date(Number(unixSeconds) * 1000);
  if (isNaN(d)) return "—";
  return d.toLocaleString(undefined, { dateStyle: "medium", timeStyle: "medium" });
}

export function isoUtc(unixSeconds) {
  const d = new Date(Number(unixSeconds) * 1000);
  return isNaN(d) ? "" : d.toISOString().replace(".000Z", "Z");
}

// ---- hashing & Base58Check ----
const K = new Uint32Array([
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
]);

/** SHA-256 of a Uint8Array (synchronous; avoids needing a secure context for crypto.subtle). */
export function sha256(bytes) {
  const h = new Uint32Array([0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]);
  const len = bytes.length;
  const padded = new Uint8Array(((len + 9 + 63) >> 6) << 6);
  padded.set(bytes);
  padded[len] = 0x80;
  const dv = new DataView(padded.buffer);
  dv.setUint32(padded.length - 8, Math.floor((len * 8) / 0x100000000));
  dv.setUint32(padded.length - 4, (len * 8) >>> 0);
  const w = new Uint32Array(64);
  const rotr = (x, n) => (x >>> n) | (x << (32 - n));
  for (let off = 0; off < padded.length; off += 64) {
    for (let i = 0; i < 16; i++) w[i] = dv.getUint32(off + i * 4);
    for (let i = 16; i < 64; i++) {
      const s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >>> 3);
      const s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >>> 10);
      w[i] = (w[i - 16] + s0 + w[i - 7] + s1) | 0;
    }
    let [a, b, c, d, e, f, g, hh] = h;
    for (let i = 0; i < 64; i++) {
      const S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const ch = (e & f) ^ (~e & g);
      const t1 = (hh + S1 + ch + K[i] + w[i]) | 0;
      const S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const maj = (a & b) ^ (a & c) ^ (b & c);
      const t2 = (S0 + maj) | 0;
      hh = g; g = f; f = e; e = (d + t1) | 0; d = c; c = b; b = a; a = (t1 + t2) | 0;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  const out = new Uint8Array(32);
  const odv = new DataView(out.buffer);
  h.forEach((x, i) => odv.setUint32(i * 4, x));
  return out;
}

const B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

export function base58Decode(s) {
  let n = 0n;
  for (const ch of s) {
    const i = B58.indexOf(ch);
    if (i < 0) return null;
    n = n * 58n + BigInt(i);
  }
  const bytes = [];
  while (n > 0n) { bytes.push(Number(n & 0xffn)); n >>= 8n; }
  for (const ch of s) { if (ch === "1") bytes.push(0); else break; }
  return Uint8Array.from(bytes.reverse());
}

/**
 * Validate a P2PKH address for a network.
 * Returns { ok: true } or { ok: false, reason } with a message fit for the user.
 */
export function validateAddress(addr, network) {
  const a = String(addr ?? "").trim();
  if (!a) return { ok: false, reason: "Enter a recipient address.", empty: true };
  if (/\s/.test(a)) return { ok: false, reason: "The address must not contain spaces." };
  if (!/^[1-9A-HJ-NP-Za-km-z]+$/.test(a)) return { ok: false, reason: "The address contains characters that are not valid in Base58 (0, O, I, l are never used)." };
  const raw = base58Decode(a);
  if (!raw || raw.length !== 25) return { ok: false, reason: "That is not the right length for a Bucks address. Check that you copied all of it." };
  const sum = sha256(sha256(raw.subarray(0, 21))).subarray(0, 4);
  for (let i = 0; i < 4; i++) {
    if (sum[i] !== raw[21 + i]) return { ok: false, reason: "The address checksum does not match. It has a typo or is incomplete." };
  }
  const want = ADDRESS_VERSION[network];
  if (want === undefined) return { ok: false, reason: "Unknown network; cannot validate the address." };
  if (raw[0] !== want) {
    const other = Object.entries(ADDRESS_VERSION).find(([, v]) => v === raw[0]);
    return {
      ok: false,
      reason: other
        ? `This is a ${NETWORKS[other[0]].label.toLowerCase()} address, but this node is on ${NETWORKS[network].label.toLowerCase()}. Sending to it would lose the funds.`
        : "This address was made for a different network.",
    };
  }
  return { ok: true };
}

export function isHex64(s) { return /^[0-9a-fA-F]{64}$/.test(s); }
