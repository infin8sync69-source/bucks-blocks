// Shared application state and polling. Everything shown comes from the node's
// REST API. The node rate-limits to 300 requests/minute per client, so polling
// is deliberately slow (10 s), pauses while the tab is hidden, and fetches the
// block list only when the tip changes.
import { get, ApiError, onSessionExpired } from "./api.js";

const POLL_MS = 10000;
const ECONOMY_MS = 60000;

export const state = {
  conn: "connecting", // connecting | ok | offline
  lastOk: 0,
  rateLimitedUntil: 0,
  authChecked: false,
  authenticated: false,
  sessionExpired: false,
  walletsEnabled: true,
  walletError: "",
  cookieFile: "",
  info: null,
  mining: null,
  hashSamples: [],
  economy: null,
  economyError: "",
  blocks: null,
  blocksError: "",
  wallets: null, // null = not loaded yet
  walletsError: "",
  selected: "",
};

const subs = new Set();
export function subscribe(fn) { subs.add(fn); return () => subs.delete(fn); }
export function emit() { subs.forEach((fn) => { try { fn(state); } catch (e) { console.error(e); } }); }

try { state.selected = sessionStorage.getItem("bucks.selected") || ""; } catch { /* storage unavailable */ }
export function selectWallet(addr) {
  state.selected = addr;
  try { sessionStorage.setItem("bucks.selected", addr); } catch { /* ignore */ }
  emit();
}
export function selectedWallet() {
  const list = state.wallets || [];
  return list.find((w) => w.address === state.selected) || list[0] || null;
}
export const network = () => (state.info ? state.info.network : "");

function noteFailure(e) {
  if (e instanceof ApiError) {
    if (e.offline) state.conn = "offline";
    if (e.status === 429) state.rateLimitedUntil = Date.now() + Math.max(5, e.retryAfter || 30) * 1000;
  } else state.conn = "offline";
}
function noteOk() { state.conn = "ok"; state.lastOk = Date.now(); }

export async function loadAuth() {
  try {
    const a = await get("/api/auth/status");
    state.authenticated = !!a.authenticated;
    state.walletsEnabled = a.walletsEnabled !== false;
    state.walletError = a.walletError || "";
    state.cookieFile = a.cookieFile || "";
    state.authChecked = true;
    noteOk();
  } catch (e) {
    noteFailure(e);
    state.authChecked = true;
  }
  emit();
}

export async function loadInfo() {
  try {
    const prev = state.info;
    state.info = await get("/api/blockchain/info");
    state.node = await get("/api/status").catch(() => state.node);
    noteOk();
    if (!state.blocks || !prev || prev.tipHash !== state.info.tipHash) await loadBlocks(false);
  } catch (e) { noteFailure(e); }
}

export async function loadBlocks(doEmit = true) {
  try {
    const r = await get("/api/blocks/recent");
    state.blocks = (r.blocks || []).slice().sort((a, b) => b.height - a.height);
    state.blocksError = "";
  } catch (e) { state.blocksError = e.message; noteFailure(e); }
  if (doEmit) emit();
}

export async function loadMining(doEmit = true) {
  try {
    state.mining = await get("/api/mining/status");
    state.hashSamples.push({ t: Date.now(), v: Number(state.mining.hashRate) || 0 });
    if (state.hashSamples.length > 60) state.hashSamples.shift();
    noteOk();
  } catch (e) { noteFailure(e); }
  if (doEmit) emit();
}

export async function loadEconomy(doEmit = true) {
  try {
    state.economy = await get("/api/economy/status");
    state.economyError = "";
  } catch (e) { state.economyError = e.message; noteFailure(e); }
  if (doEmit) emit();
}

export async function loadWallets(doEmit = true) {
  if (!state.authenticated) return;
  try {
    const r = await get("/api/wallets");
    state.wallets = r.wallets || [];
    state.walletsError = "";
    if (state.selected && !state.wallets.some((w) => w.address === state.selected)) state.selected = "";
    noteOk();
  } catch (e) {
    if (e.status === 503) { state.walletsEnabled = false; state.walletError = state.walletError || e.message; state.wallets = []; }
    state.walletsError = e.message;
    noteFailure(e);
  }
  if (doEmit) emit();
}

let tickBusy = false;
let lastEconomy = 0;
export async function refresh() {
  if (tickBusy || Date.now() < state.rateLimitedUntil) return;
  tickBusy = true;
  try {
    const jobs = [loadInfo(), loadMining(false)];
    if (state.authenticated) jobs.push(loadWallets(false));
    if (Date.now() - lastEconomy > ECONOMY_MS) { lastEconomy = Date.now(); jobs.push(loadEconomy(false)); }
    await Promise.all(jobs);
  } finally { tickBusy = false; }
  emit();
}

let timer = null;
function schedule() {
  clearInterval(timer);
  timer = setInterval(() => { if (!document.hidden) refresh(); }, POLL_MS);
}

export function startPolling() {
  schedule();
  document.addEventListener("visibilitychange", () => {
    if (!document.hidden) { refresh(); schedule(); }
  });
  window.addEventListener("online", refresh);
  onSessionExpired(() => {
    if (!state.authenticated) return;
    state.authenticated = false;
    state.sessionExpired = true;
    state.wallets = null;
    emit();
  });
}

export function resetAfterLogin() {
  state.authenticated = true;
  state.sessionExpired = false;
  state.wallets = null;
  return Promise.all([loadAuth(), loadWallets(false)]).then(() => { state.authenticated = true; emit(); });
}
export function resetAfterLogout() {
  state.authenticated = false;
  state.sessionExpired = false;
  state.wallets = null;
  state.selected = "";
  emit();
}
