// Run: node web/test/mining.test.mjs
// The mining page's pure helpers: expected time to a block (ONB-11), the payout choice (ONB-12)
// and why mining is blocked (ONB-14).
import assert from "node:assert/strict";
import { fmtDuration, expectedBlockTime, payoutBody, blockedText } from "../js/pages/mining.js";

assert.equal(fmtDuration(null), "—");
assert.equal(fmtDuration(undefined), "—");
assert.equal(fmtDuration(-1), "—");
assert.equal(fmtDuration(0.2), "under a second");
assert.equal(fmtDuration(8.4), "8 s");
assert.equal(fmtDuration(600), "10 min");
assert.equal(fmtDuration(2 * 3600), "2 h");
assert.equal(fmtDuration(8868), "2 h 28 min");          // 2^32 hashes at the old 0.48 MH/s
assert.equal(fmtDuration(3 * 86400), "3 days");
assert.equal(fmtDuration(2 * 365.25 * 86400), "2.0 years");

// Mainnet's minimum difficulty, about 2^32 hashes per block.
const perBlock = 4295032833;
const mining = { busy: true, hashRate: 8_000_000, expectedHashesPerBlock: perBlock };
assert.deepEqual(expectedBlockTime(mining), { seconds: perBlock / 8e6, basis: "live" });
assert.equal(fmtDuration(expectedBlockTime(mining).seconds), "9 min");
// Idle: the last measured rate of this session, else nothing to say.
const idle = { busy: false, hashRate: 0, expectedHashesPerBlock: perBlock };
assert.deepEqual(expectedBlockTime(idle, [{ v: 2e6 }, { v: 4e6 }, { v: 0 }]), { seconds: perBlock / 4e6, basis: "last" });
assert.equal(expectedBlockTime(idle, [{ v: 0 }]), null);
assert.equal(expectedBlockTime({ busy: true, hashRate: 0, expectedHashesPerBlock: perBlock }), null);   // still measuring
assert.equal(expectedBlockTime(null), null);
assert.equal(expectedBlockTime({ busy: true, hashRate: 5 }), null);   // an older node without the field

// The chosen wallet is sent, unless the operator fixed the payout with --mine-address.
assert.deepEqual(payoutBody({ fixedPayoutAddress: "" }, "tAddr"), { minerAddress: "tAddr" });
assert.deepEqual(payoutBody({ fixedPayoutAddress: "tCold" }, "tAddr"), {});
assert.deepEqual(payoutBody({ fixedPayoutAddress: "" }, ""), {});
assert.deepEqual(payoutBody(null, "tAddr"), {});

assert.equal(blockedText(""), "");
assert.match(blockedText("node is syncing"), /cannot mine yet/);
assert.equal(blockedText("no chain tip"), "This node cannot mine right now: no chain tip.");
console.log("mining.test.mjs: ok");
