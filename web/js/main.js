// Application shell: login gate, banners, navigation, routing, polling.
import { h, icon, netBadge, errorState, spinner, notice, busy, toast } from "./ui.js";
import { post, ApiError } from "./api.js";
import { state, subscribe, startPolling, refresh, loadAuth, resetAfterLogin, resetAfterLogout, network, hasUnsaved } from "./store.js";
import { NETWORKS, timeAgo, fmtInt, syncStatus } from "./util.js";

const PAGES = [
  { id: "overview", label: "Overview", short: "Home", icon: "overview", load: () => import("./pages/overview.js") },
  { id: "blocks", label: "Blocks", short: "Blocks", icon: "blocks", load: () => import("./pages/blocks.js") },
  { id: "wallet", label: "Wallet", short: "Wallet", icon: "wallet", load: () => import("./pages/wallet.js") },
  { id: "send", label: "Send", short: "Send", icon: "send", load: () => import("./pages/send.js") },
  { id: "history", label: "History", short: "History", icon: "history", load: () => import("./pages/history.js") },
  { id: "mining", label: "Mining", short: "Mine", icon: "mining", load: () => import("./pages/mining.js") },
  { id: "gold", label: "Gold reserve", short: "Gold", icon: "gold", load: () => import("./pages/gold.js") },
];

const app = document.getElementById("app");
let mode = ""; // "login" | "shell" | ""
let shell = null; // element refs
let current = null; // { id, view }
let routeToken = 0;
let justSignedIn = false; // the first page after a login takes the focus (ONB-18)

// ---------- routing ----------
function parseRoute() {
  const parts = location.hash.replace(/^#\/?/, "").split("/").filter(Boolean);
  const id = PAGES.some((p) => p.id === parts[0]) ? parts[0] : "overview";
  return { id, params: parts.slice(1).map(decodeURIComponent) };
}

async function renderRoute(userInitiated) {
  if (mode !== "shell") return;
  const { id, params } = parseRoute();
  const page = PAGES.find((p) => p.id === id);
  const token = ++routeToken;
  shell.nav.querySelectorAll("a").forEach((a) => {
    if (a.dataset.page === id) a.setAttribute("aria-current", "page"); else a.removeAttribute("aria-current");
  });
  if (current && current.view && current.view.destroy) current.view.destroy();
  current = null;
  shell.main.replaceChildren(spinner("Loading…"));
  let mod;
  try {
    mod = await page.load();
  } catch (e) {
    console.error(e);
    if (token === routeToken) shell.main.replaceChildren(errorState("This page could not be loaded. Reload the console and try again.", () => location.reload(), "Page failed to load"));
    return;
  }
  if (token !== routeToken) return;
  const root = h("div", { class: "page" });
  shell.main.replaceChildren(root);
  const view = mod.mount(root, { params, state }) || {};
  current = { id, view };
  view.update && view.update(state);
  setTitle(page.label);
  if (userInitiated) {
    const h1 = root.querySelector("h1");
    if (h1) h1.focus({ preventScroll: true });
    window.scrollTo(0, 0);
  }
}

function setTitle(label) {
  const net = state.info && NETWORKS[network()];
  document.title = `${label || ""}${label ? " · " : ""}Bucks${net ? ` ${net.label}` : ""}`;
}

// ---------- shell ----------
function buildShell() {
  const nav = h("nav", { class: "nav", "aria-label": "Main" }, PAGES.map((p) =>
    h("a", { href: `#/${p.id}`, dataset: { page: p.id } }, icon(p.icon, { size: 20 }), h("span", null, window.innerWidth < 900 ? p.short : p.label))));
  const banners = h("div", { class: "banners", id: "banners", role: "region", "aria-label": "Node status" });
  const netSlot = h("span", { id: "net-slot" });
  const conn = h("span", { class: "conn", id: "conn", role: "status" });
  const logout = h("button", { type: "button", class: "btn ghost sm", onclick: doLogout, "aria-label": "Sign out" }, icon("logout", { size: 16 }), h("span", { class: "conn-text" }, "Sign out"));
  const main = h("main", { class: "main", id: "main", tabindex: "-1" });
  const el = h("div", { class: "shell" },
    h("aside", { class: "side" },
      h("a", { class: "brand", href: "#/overview", "aria-label": "Bucks overview" }, h("span", { class: "brand-mark", "aria-hidden": "true" }, "B"), "Bucks"),
      nav,
      h("div", { class: "side-foot" }, "Node console")),
    h("header", { class: "top" },
      h("a", { class: "brand brand-inline", href: "#/overview", "aria-label": "Bucks overview" }, h("span", { class: "brand-mark", "aria-hidden": "true" }, "B")),
      netSlot,
      h("span", { class: "spacer" }),
      conn,
      logout),
    main);
  app.replaceChildren(banners, el);
  shell = { nav, banners, netSlot, conn, main };
  // The nav labels switch between long and short at the breakpoint.
  const mq = window.matchMedia("(max-width: 899px)");
  const relabel = () => nav.querySelectorAll("a").forEach((a, i) => { a.lastChild.textContent = mq.matches ? PAGES[i].short : PAGES[i].label; });
  mq.addEventListener("change", relabel);
  relabel();
}

let lastChrome = "";
function updateChrome() {
  if (!shell) return;
  const net = network();
  const sync = syncStatus(state.info, state.node);
  const sig = JSON.stringify([state.conn, net, state.lastOk && Math.floor(state.lastOk / 10000), state.info && state.info.height, sync && [sync.label, sync.alone], state.rateLimitedUntil > Date.now(), notBackedUp().length]);
  if (sig === lastChrome) return;
  lastChrome = sig;

  shell.netSlot.replaceChildren(net ? netBadge(net) : "");
  // "Synced" only with peers: a node alone mines and shows a chain nobody else has (ONB-04).
  const dotCls = state.conn === "ok" ? (sync && sync.tone === "ok" ? "ok" : "warn") : state.conn === "offline" ? "bad" : "";
  const text = state.conn === "offline" ? "Node offline" : !sync ? "Connecting…" : `${sync.label} · #${fmtInt(state.info.height)}`;
  shell.conn.replaceChildren(h("span", { class: `dot ${dotCls}` }), h("span", { class: "conn-text" }, text));
  shell.conn.title = state.lastOk ? `Last response ${timeAgo(state.lastOk / 1000)}` : "No response yet";

  const items = [];
  if (state.conn === "offline") {
    items.push(h("div", { class: "banner offline", role: "alert" }, icon("alert", { size: 18 }),
      h("span", null, `Cannot reach the node${state.lastOk ? `. Showing data from ${timeAgo(state.lastOk / 1000)}` : ""}. Retrying every 10 seconds.`),
      h("button", { type: "button", class: "btn sm", onclick: () => refresh() }, "Retry now")));
  }
  if (state.rateLimitedUntil > Date.now()) {
    items.push(h("div", { class: "banner offline", role: "alert" }, icon("alert", { size: 18 }), h("span", null, "The node is rate-limiting this browser. Updates are paused for a moment.")));
  }
  if (state.conn === "ok" && sync && sync.alone) {
    items.push(h("div", { class: "banner net-warn" }, icon("alert", { size: 18 }),
      h("span", null, h("strong", null, "No peers connected. "), "This node is not in contact with the Bucks network: blocks it mines and payments it shows exist only here until it connects. Check the internet connection and that the P2P port is open, or start the node with --seed or --connect.")));
  }
  const unconfirmed = notBackedUp();
  if (unconfirmed.length) {
    items.push(h("div", { class: "banner net-warn" }, icon("alert", { size: 18 }),
      h("span", null, `${unconfirmed.length === 1 ? "A wallet on this node is" : `${unconfirmed.length} wallets on this node are`} not backed up: its recovery phrase was never confirmed. `,
        h("a", { href: "#/wallet" }, "Finish the backup"))));
  }
  if (net === "test") items.push(h("div", { class: "banner net-warn" }, icon("alert", { size: 18 }), h("span", null, "TESTNET. These coins have no value and the network can be reset at any time. Never reuse a real wallet phrase here.")));
  else if (net === "regtest") items.push(h("div", { class: "banner net-dev" }, icon("info", { size: 18 }), h("span", null, "REGTEST. A private development chain; blocks are cheap to mine and coins have no value.")));
  else if (net && net !== "main") items.push(h("div", { class: "banner net-warn" }, icon("alert", { size: 18 }), h("span", null, "Unknown network. Do not send funds.")));
  shell.banners.replaceChildren(...items);
  setTitle((PAGES.find((p) => p.id === parseRoute().id) || {}).label);
}

const notBackedUp = () => (state.wallets || []).filter((w) => w.backedUp === false);

async function doLogout() {
  if (hasUnsaved() && !window.confirm("You have not finished backing up your new wallet. Signing out clears its recovery phrase from this page for good, and the wallet stays marked as not backed up. Sign out anyway?")) return;
  try { await post("/api/auth/logout", {}); } catch { /* the cookie may already be gone */ }
  resetAfterLogout();
}

// ---------- login ----------
function buildLogin() {
  const errBox = h("div", { id: "login-err", role: "alert", "aria-live": "assertive" });
  const input = h("input", { id: "token", name: "token", type: "password", autocomplete: "off", autocapitalize: "off", spellcheck: "false", required: true, "aria-describedby": "token-hint login-err" });
  const submit = h("button", { type: "submit", class: "btn primary lg block" }, icon("lock", { size: 18 }), "Unlock console");
  const status = h("div", { id: "login-status" });
  const form = h("form", { class: "card login-card", novalidate: true },
    h("div", { class: "row", style: "margin-bottom:14px" }, h("span", { class: "brand-mark", "aria-hidden": "true" }, "B"), h("strong", { style: "font-size:1.2rem" }, "Bucks node console"), h("span", { id: "login-net" })),
    h("h1", { tabindex: "-1", style: "font-size:1.4rem;margin-bottom:6px" }, "Sign in to your node"),
    h("p", { class: "muted", id: "token-hint" }, "Paste the node's API token. It is the value in the file passed with --api-token-file, or the .cookie file in the node's data directory. It is exchanged for a session cookie and never stored in this browser."),
    status,
    h("div", { class: "field", style: "margin-top:12px" }, h("label", { for: "token" }, "API token"), input),
    tokenHelp(),
    errBox,
    h("div", { style: "margin-top:14px" }, submit));
  form.addEventListener("submit", async (e) => {
    e.preventDefault();
    const token = input.value.trim();
    errBox.replaceChildren();
    if (!token) { errBox.replaceChildren(notice("error", "Enter the API token.")); input.focus(); return; }
    busy(submit, true, "Checking…");
    try {
      await post("/api/auth/login", { token });
      input.value = "";
      justSignedIn = true;
      await resetAfterLogin();
      refresh();
    } catch (err) {
      let msg = err.message;
      if (err instanceof ApiError) {
        if (err.status === 401) msg = "That token was not accepted. Check that you copied the whole value, with no extra characters. If the node was restarted since you copied it, read the token again: it changes at every start.";
        else if (err.status === 429) msg = `Too many attempts. Wait ${err.retryAfter || 60} seconds and try again.`;
        else if (err.offline) msg = "Cannot reach the node. Is it running with --server, and are you on the right address and port?";
      }
      errBox.replaceChildren(notice("error", msg));
      input.focus();
    } finally {
      submit.disabled = false;
      submit.replaceChildren(icon("lock", { size: 18 }), "Unlock console");
    }
  });
  const el = h("main", { class: "login", id: "main", tabindex: "-1" }, form);
  app.replaceChildren(h("div", { class: "banners", id: "banners", role: "region", "aria-label": "Node status" }), el);
  shell = null;
  current = null;
  return { status };
}

// Where the token is, and that the generated one changes at every start (ONB-15). The exact path depends on
// --datadir and the network; the node prints it at start.
function tokenHelp() {
  return h("details", { class: "token-help" },
    h("summary", null, "Where do I find the token?"),
    h("div", { class: "stack", style: "gap:8px;margin-top:8px;font-size:.9rem" },
      h("p", { style: "margin:0" }, "On the computer that runs the node, with the default data directory on mainnet:"),
      h("code", { class: "addr", style: "user-select:all" }, "cat ~/.bucks/.cookie"),
      h("p", { style: "margin:0" }, "The node prints the exact file when it starts, in the line \"API token file: ...\". If the node was started with --datadir, the file is in that directory."),
      h("p", { style: "margin:0" }, h("strong", null, "A new token is written every time the node starts, "), "so after a restart or an upgrade sign in with the new value. To keep one token, start the node with --api-token-file <file>.")));
}

let loginRefs = null;
function updateLogin() {
  const status = loginRefs.status;
  const net = network();
  const slot = document.getElementById("login-net");
  if (slot) slot.replaceChildren(net ? netBadge(net) : "");
  const items = [];
  if (state.sessionExpired) items.push(notice("warn", "Your session expired or was ended. Sign in again to continue. If the node was restarted, it has a new token: read it again from its .cookie file (unless the node uses --api-token-file)."));
  if (state.conn === "offline") items.push(notice("error", "Cannot reach the node. Retrying every 10 seconds."));
  else if (state.walletsEnabled === false && state.walletError) items.push(notice("warn", `Wallet subsystem unavailable: ${state.walletError}`));
  status.replaceChildren(...items);
  const b = document.getElementById("banners");
  if (b) {
    const msg = net === "test" ? "TESTNET. Coins have no value." : net === "regtest" ? "REGTEST. Private development chain." : "";
    b.replaceChildren(...(msg ? [h("div", { class: `banner ${net === "test" ? "net-warn" : "net-dev"}` }, icon("alert", { size: 18 }), h("span", null, msg))] : []));
  }
}

// ---------- state changes ----------
function onState() {
  if (!state.authChecked) return;
  if (!state.authenticated) {
    if (mode !== "login") {
      const signedOut = mode === "shell";
      if (current && current.view && current.view.destroy) current.view.destroy();
      loginRefs = buildLogin();
      mode = "login";
      lastChrome = "";
      setTitle("Sign in");
      // After a sign-out or an expired session the focus would fall to <body> with the removed shell (ONB-18).
      const t = document.querySelector(".login h1");
      if (t && (state.sessionExpired || signedOut)) t.focus();
    }
    updateLogin();
    return;
  }
  if (mode !== "shell") {
    buildShell();
    mode = "shell";
    lastChrome = "";
    updateChrome();
    renderRoute(justSignedIn);
    justSignedIn = false;
    return;
  }
  updateChrome();
  if (current && current.view && current.view.update) current.view.update(state);
}

// ---------- boot ----------
async function boot() {
  app.replaceChildren(h("div", { class: "login" }, spinner("Connecting to the node…")));
  try {
    // An earlier console version registered a caching service worker; remove it.
    if ("serviceWorker" in navigator) {
      const regs = await navigator.serviceWorker.getRegistrations();
      await Promise.all(regs.map((r) => r.unregister()));
    }
  } catch { /* not important */ }
  subscribe(onState);
  window.addEventListener("hashchange", () => { if (location.hash !== "#main") renderRoute(true); });
  const skip = document.querySelector(".skip");
  if (skip) skip.addEventListener("click", (e) => { e.preventDefault(); const m = document.getElementById("main"); if (m) m.focus(); });
  await loadAuth();
  if (state.conn === "offline" && !state.authenticated) {
    // Show the login form with an offline notice rather than a blank page.
    state.authChecked = true;
  }
  await refresh();
  startPolling();
  onState();
  if (state.conn === "offline") {
    // keep trying quickly until the node answers
    const t = setInterval(async () => {
      if (state.conn === "ok" && state.authChecked) { clearInterval(t); }
      if (!state.authenticated) await loadAuth();
    }, 5000);
  }
}

window.addEventListener("error", (e) => console.error("Unhandled error:", e.message));
boot().catch((e) => {
  console.error(e);
  app.replaceChildren(h("div", { class: "login" }, errorState("The console failed to start. Reload the page.", () => location.reload(), "Startup error")));
});
