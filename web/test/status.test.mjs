// Run: node web/test/status.test.mjs
// The console never says "Synced" for a node without peers (ONB-04).
import assert from "node:assert/strict";
import { syncStatus } from "../js/util.js";

const info = (o = {}) => ({ network: "main", height: 5, syncing: false, ...o });
const node = (peerCount, uptimeSeconds = 3600) => ({ peerCount, uptimeSeconds });

assert.equal(syncStatus(null, node(3)), null);
assert.deepEqual(syncStatus(info(), node(3)), { label: "Synced", tone: "ok", sub: "Following the chain tip with 3 peers", noPeers: false, alone: false });
assert.equal(syncStatus(info(), node(1)).sub, "Following the chain tip with 1 peer");

// No peers: never "Synced", and the banner is asked for on main and test.
const alone = syncStatus(info(), node(0));
assert.equal(alone.label, "No peers");
assert.equal(alone.tone, "warn");
assert.equal(alone.alone, true);
assert.equal(syncStatus(info({ network: "test" }), node(0)).alone, true);
// The first minute is spent dialling seeds: "Connecting", no banner yet.
assert.equal(syncStatus(info(), node(0, 20)).label, "Connecting");
assert.equal(syncStatus(info(), node(0, 20)).alone, false);
// Regtest is a private chain: no banner, but still not "Synced".
assert.equal(syncStatus(info({ network: "regtest" }), node(0)).label, "No peers");
assert.equal(syncStatus(info({ network: "regtest" }), node(0)).alone, false);
// Syncing stays syncing, and says why when there is nobody to download from.
assert.equal(syncStatus(info({ syncing: true }), node(2)).label, "Syncing");
assert.equal(syncStatus(info({ syncing: true }), node(0)).sub, "Waiting for peers to download from");
assert.equal(syncStatus(info({ syncing: true }), node(0)).alone, true);
// /api/status not loaded yet: neither "Synced" nor an alarm.
assert.deepEqual(syncStatus(info(), undefined), { label: "Tip", tone: "", sub: "Peer count not known yet", noPeers: false, alone: false });

console.log("status.test.mjs: ok");
