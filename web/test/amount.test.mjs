// Run: node web/test/amount.test.mjs
// Amount entry on the Send page: parsing (NEW-8, ONB-21) and the checks against the wallet (ONB-07).
import assert from "node:assert/strict";
import { parseBucks, amountProblem, MIN_FEE_UNITS, DUST_LIMIT_UNITS } from "../js/util.js";

const units = (s) => { const r = parseBucks(s); assert.equal(r.ok, true, `${s}: ${r.error}`); return r.units; };
const refused = (s) => { const r = parseBucks(s); assert.equal(r.ok, false, s); return r.error; };

// A single comma is a decimal mark ...
assert.equal(units("12,5"), 1250000000n);
assert.equal(units("0,00000001"), 1n);
assert.equal(units("0,125"), 12500000n);       // a leading 0 is never a thousands group
assert.equal(units("1234,567"), 123456700000n); // not a valid grouping: decimal
assert.equal(units("1,00"), 100000000n);
// ... unless it reads as a thousands separator: "1,000" used to become 1 BUCKS.
for (const s of ["1,000", "12,345", "999,999", "1,000.5", "1.000,5", "1,000,000", "1٬000"]) {
  assert.match(refused(s), /thousands separators/, s);
}
// Arabic-Indic and extended Arabic-Indic digits, and the Arabic decimal separator.
assert.equal(units("١٢٫٥"), 1250000000n);   // ١٢٫٥
assert.equal(units("۱۰"), 1000000000n);                // ۱۰
assert.equal(units("٠٫٠٠٠٠٠٠٠١"), 1n);
// Unchanged rules.
assert.equal(units(" 1 "), 100000000n);
assert.equal(units("21000000"), 2100000000000000n);
for (const s of ["", "0", "abc", "1.123456789", "-1", "1e5", ".", "1.2.3", "1 000", "0x10"]) refused(s);

// amountProblem: spendable, not the balance, limits a payment; the fee and the dust limit count too.
const w = { balance: 60_00000000n, spendable: 10_00000000n };
assert.equal(amountProblem(5_00000000n, w), "");
assert.match(amountProblem(70_00000000n, w), /more than this wallet holds/);
assert.match(amountProblem(20_00000000n, w), /Only 10\.00 BUCKS can be spent now/);
assert.match(amountProblem(10_00000000n, w), /at least 0\.001 BUCKS\)\. At most 9\.999 BUCKS/);
assert.match(amountProblem(9_99000000n, { ...w, fee: 2000000n, feeKnown: true }), /fee \(0\.02 BUCKS\)/);
assert.equal(amountProblem(9_99900000n, w), "");
assert.match(amountProblem(DUST_LIMIT_UNITS - 1n, w), /below 0\.00000546 BUCKS/);
assert.equal(amountProblem(DUST_LIMIT_UNITS, w), "");
assert.match(amountProblem(100n, { ...w, dust: 1000n }), /below 0\.00001 BUCKS/);
// Without a spendable figure the balance is used.
assert.equal(amountProblem(50_00000000n, { balance: 60_00000000n }), "");
assert.equal(MIN_FEE_UNITS, 100000n);

console.log("amount.test.mjs: ok");
