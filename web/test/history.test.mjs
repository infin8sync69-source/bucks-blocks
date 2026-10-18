// Run: node web/test/history.test.mjs
// How entries of GET /api/wallets/<address>/history are labelled on the History page (ONB-05, NEW-7).
import assert from "node:assert/strict";
import { describeActivity } from "../js/util.js";

assert.deepEqual(describeActivity({ kind: "sent", status: "pending" }), { label: "Sent", sign: "-", status: "Pending", tone: "warn" });
assert.deepEqual(describeActivity({ kind: "received", status: "confirmed", confirmations: 1 }),
  { label: "Received", sign: "+", status: "1 confirmation", tone: "ok" });
assert.equal(describeActivity({ kind: "sent", status: "confirmed", confirmations: 12 }).status, "12 confirmations");
assert.equal(describeActivity({ kind: "self", status: "confirmed", confirmations: 3 }).label, "To yourself");
assert.equal(describeActivity({ kind: "self", status: "confirmed", confirmations: 3 }).sign, "");

// A reward is spendable once it has 100 confirmations (the next block may spend it).
assert.deepEqual(describeActivity({ kind: "mined", status: "confirmed", confirmations: 1 }),
  { label: "Mining reward", sign: "+", status: "Matures in 99 blocks", tone: "warn" });
assert.equal(describeActivity({ kind: "mined", status: "confirmed", confirmations: 99 }).status, "Matures in 1 block");
assert.equal(describeActivity({ kind: "mined", status: "confirmed", confirmations: 100 }).tone, "ok");
assert.equal(describeActivity({ kind: "mined", status: "confirmed", confirmations: 5 }, { maturity: 3 }).tone, "ok");
assert.equal(describeActivity({ kind: "other", status: "confirmed", confirmations: 2 }).label, "Transaction");

console.log("history.test.mjs: ok");

// A mining reward matures after COINBASE_MATURITY confirmations. Array.map passes the row index as the second
// argument; it once became the maturity and every reward showed "1 confirmation" (browser pass of 2026-10-18).
const rewards = [1, 1, 50, 100].map((confirmations) => ({ kind: "mined", status: "confirmed", confirmations }));
assert.deepEqual(rewards.map(describeActivity).map((r) => r.status),
  ["Matures in 99 blocks", "Matures in 99 blocks", "Matures in 50 blocks", "100 confirmations"]);
assert.equal(describeActivity({ kind: "mined", status: "confirmed", confirmations: 3 }, { maturity: 5 }).status, "Matures in 2 blocks");
