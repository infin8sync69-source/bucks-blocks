// Run: node web/test/util.test.mjs
import assert from "node:assert/strict";
import crypto from "node:crypto";
import { sha256, fmtBucks, parseBucks, validateAddress, ADDRESS_VERSION, unusedRestoreAddress } from "../js/util.js";

for (const n of [0, 1, 55, 56, 63, 64, 65, 119, 120, 1000]) {
  const buf = crypto.randomBytes(n);
  assert.equal(Buffer.from(sha256(buf)).toString("hex"), crypto.createHash("sha256").update(buf).digest("hex"), `sha256 len ${n}`);
}

assert.equal(fmtBucks(0), "0.00000000");
assert.equal(fmtBucks(123456789), "1.23456789");
assert.equal(fmtBucks(1234500000000, { trim: true }), "12 345.00");
assert.equal(parseBucks("1").units, 100000000n);
assert.equal(parseBucks("0.00000001").units, 1n);
assert.equal(parseBucks("12,5").units, 1250000000n);
for (const bad of ["", "0", "abc", "1.123456789", "-1", "1e5", ".", "1.2.3"]) assert.equal(parseBucks(bad).ok, false, bad);

function mk(version, seed) {
  const payload = Buffer.concat([Buffer.from([version]), crypto.createHash("sha256").update(seed).digest().subarray(0, 20)]);
  const sum = crypto.createHash("sha256").update(crypto.createHash("sha256").update(payload).digest()).digest().subarray(0, 4);
  let n = BigInt("0x" + Buffer.concat([payload, sum]).toString("hex"));
  const A = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
  let s = ""; while (n > 0n) { s = A[Number(n % 58n)] + s; n /= 58n; }
  for (const b of Buffer.concat([payload, sum])) { if (b === 0) s = "1" + s; else break; }
  return s;
}
for (const [net, ver] of Object.entries(ADDRESS_VERSION)) {
  const a = mk(ver, "x" + net);
  assert.equal(validateAddress(a, net).ok, true, net);
  for (const other of Object.keys(ADDRESS_VERSION)) if (other !== net) assert.equal(validateAddress(a, other).ok, false);
  const typo = a.slice(0, -1) + (a.endsWith("2") ? "3" : "2");
  assert.equal(validateAddress(typo, net).ok, false);
}
assert.equal(validateAddress("", "test").empty, true);
assert.equal(validateAddress("0OIl", "test").ok, false);

// Restore with a passphrase that gives an empty wallet: the node answers 409 and the UI asks first (WAL-13).
const ask = { status: 409, body: { success: false, confirmRequired: true, reason: "unusedWithPassphrase", wallet: { address: "tAbc" } } };
assert.equal(unusedRestoreAddress(ask), "tAbc");
assert.equal(unusedRestoreAddress({ status: 400, body: ask.body }), "");
assert.equal(unusedRestoreAddress({ status: 409, body: { error: "x" } }), "");
assert.equal(unusedRestoreAddress({ status: 409, body: null }), "");
assert.equal(unusedRestoreAddress(null), "");
console.log("util.test.mjs: ok");
