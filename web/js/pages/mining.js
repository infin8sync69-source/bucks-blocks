import { h, icon, card, kv, stat, pageHead, keyedRender, skeletonRows, errorState, notice, busy, toast, field } from "../ui.js";
import { post, ApiError } from "../api.js";
import { state, loadMining, loadInfo, loadBlocks, loadWallets, selectWallet, selectedWallet } from "../store.js";
import { fmtHashRate, fmtInt, fmtDifficulty, fmtBucks, shortAddr, COINBASE_MATURITY } from "../util.js";

const BUSY_POLL_MS = 3000; // while the miner runs, so the hash rate and a one-block search stay current

/** Seconds -> "about 2 h 5 min". Average times only: a block can come much sooner or later. */
export function fmtDuration(seconds) {
  const s = Number(seconds);
  if (seconds === null || seconds === undefined || !Number.isFinite(s) || s < 0) return "—";
  if (s < 1) return "under a second";
  if (s < 90) return `${Math.round(s)} s`;
  if (s < 90 * 60) return `${Math.round(s / 60)} min`;
  if (s < 36 * 3600) {
    const hrs = Math.floor(s / 3600), min = Math.round((s % 3600) / 60);
    return min ? `${hrs} h ${min} min` : `${hrs} h`;
  }
  if (s < 365 * 86400) return `${Math.round(s / 86400)} days`;
  return `${(s / (365.25 * 86400)).toFixed(1)} years`;
}

/**
 * Average time to a block for this node: from the live hash rate while it mines, else from
 * the last rate measured in this browser session. { seconds, basis } or null.
 */
export function expectedBlockTime(m, samples = []) {
  if (!m || !(Number(m.expectedHashesPerBlock) > 0)) return null;
  if (m.busy && Number(m.hashRate) > 0) return { seconds: m.expectedHashesPerBlock / m.hashRate, basis: "live" };
  const last = [...samples].reverse().find((p) => Number(p.v) > 0);
  return last ? { seconds: m.expectedHashesPerBlock / last.v, basis: "last" } : null;
}

/** Body for start / mine-one-block: the chosen wallet, unless the operator fixed the payout. */
export function payoutBody(m, address) {
  if (!m || m.fixedPayoutAddress || !address) return {};
  return { minerAddress: address };
}

/** Why the node cannot mine now, in words for the page (ONB-14: not the time-based "syncing"). */
export function blockedText(reason) {
  if (!reason) return "";
  if (reason === "node is syncing") return "This node is still downloading blocks that other nodes announced, so it cannot mine yet. Mining starts by itself once it has caught up.";
  return `This node cannot mine right now: ${reason}.`;
}

function sparkline(samples) {
  const W = 300, H = 64;
  const pts = samples.slice(-40);
  if (pts.length < 2) return h("p", { class: "muted", style: "margin:0;font-size:.88rem" }, "Collecting samples while this tab is open: every 10 seconds, every 3 while mining.");
  const max = Math.max(...pts.map((p) => p.v), 1);
  const d = pts.map((p, i) => `${i ? "L" : "M"}${(i / (pts.length - 1)) * W} ${H - 4 - (p.v / max) * (H - 8)}`).join(" ");
  const ns = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(ns, "svg");
  svg.setAttribute("viewBox", `0 0 ${W} ${H}`);
  svg.setAttribute("class", "sparkline");
  svg.setAttribute("preserveAspectRatio", "none");
  svg.setAttribute("role", "img");
  svg.setAttribute("aria-label", `Hash rate over the last ${pts.length} samples, peak ${fmtHashRate(max)}`);
  const path = document.createElementNS(ns, "path");
  path.setAttribute("d", d);
  path.setAttribute("fill", "none");
  path.setAttribute("stroke", "var(--accent)");
  path.setAttribute("stroke-width", "2");
  path.setAttribute("vector-effect", "non-scaling-stroke");
  svg.append(path);
  return h("div", null, svg, h("div", { class: "sub" }, `Samples from this browser session, peak ${fmtHashRate(max)}`));
}

export function mount(root) {
  const top = h("div", { class: "stack" });
  const controls = h("div", { class: "stack" });
  const miner = h("div", { class: "stack" });
  const feedback = h("div", { "aria-live": "polite" });
  const body = h("div", { class: "stack" }, top, h("div", { class: "grid cols-2" }, card("Controls", controls), card("Miner", miner)));
  root.append(pageHead("Mining", "Control this node's built-in miner."), body);
  const drawTop = keyedRender(top);
  const drawControls = keyedRender(controls);
  const drawMiner = keyedRender(miner);
  let pendingAction = false;
  let mineResult = null;   // a block found by "Mine one block" from this page
  let watchedJob = 0;      // the one-block search this page started
  let poller = null;

  // The reward goes to the wallet chosen here (the selected wallet by default, ONB-12).
  const rewardSelect = h("select", { id: "m-reward" });
  rewardSelect.addEventListener("change", () => selectWallet(rewardSelect.value));
  const rewardField = field({ id: "m-reward", label: "Rewards go to", input: rewardSelect });
  const payoutAddress = () => (rewardSelect.value || (selectedWallet() || {}).address || "");

  function syncPolling(m) {
    const want = !!(m && (m.busy || (watchedJob && m.job && m.job.id === watchedJob && m.job.state === "running")));
    if (want && !poller) poller = setInterval(() => { if (!document.hidden) loadMining(); }, BUSY_POLL_MS);
    if (!want && poller) { clearInterval(poller); poller = null; }
  }

  async function afterBlock() {
    await Promise.all([loadInfo(), loadBlocks(false), loadWallets(false)]);
  }

  function noteJob(m) {
    const job = m && m.job;
    if (!job || job.id !== watchedJob || job.state === "running") return;
    watchedJob = 0;
    if (job.state === "found" && job.block) {
      mineResult = { block: job.block, minerAddress: job.minerAddress };
      toast(`Mined block #${job.block.height}`, "ok");
      afterBlock();
    } else if (job.state === "failed") {
      feedback.replaceChildren(notice("error", job.error || "Mining failed."));
    }
  }

  async function toggle(start, btn) {
    pendingAction = true;
    busy(btn, true, start ? "Starting…" : "Stopping…");
    feedback.replaceChildren();
    try {
      await post(start ? "/api/mining/start" : "/api/mining/stop", start ? payoutBody(state.mining, payoutAddress()) : {});
      toast(start ? "Mining started" : "Mining stopped", "ok");
    } catch (e) {
      feedback.replaceChildren(notice("error", e instanceof ApiError && e.offline ? "Cannot reach the node." : e.message,
        /wallet/i.test(e.message || "") ? [" ", h("a", { href: "#/wallet" }, "Open the wallet page")] : null));
    }
    pendingAction = false;
    await loadMining(false);
    view.update(state, true);
  }

  // Starts a background search for one block. The node answers at once with the block on fast
  // test networks, otherwise with 202 and keeps searching until it finds one or is stopped (ML-4).
  async function mineOne(btn) {
    pendingAction = true;
    busy(btn, true, "Starting…");
    feedback.replaceChildren();
    mineResult = null;
    try {
      const r = await post("/api/mining/mine", payoutBody(state.mining, payoutAddress()), { timeout: 30000 });
      if (r.pending) {
        watchedJob = r.job ? r.job.id : 0;
        toast("Searching for a block in the background", "ok");
      } else {
        mineResult = r;
        toast(`Mined block #${r.block.height}`, "ok");
        await afterBlock();
      }
    } catch (e) {
      feedback.replaceChildren(notice("error", e instanceof ApiError && e.offline ? "The node did not answer. It may still be mining; check the Blocks page." : e.message));
    }
    pendingAction = false;
    await loadMining(false);
    view.update(state, true);
  }

  function buildTop(s, m, eta) {
    const noWallet = s.wallets && s.wallets.length === 0;
    const searching = m.busy && !m.mining;
    return h("div", { class: "stack" },
      m.blockedReason && notice("warn", blockedText(m.blockedReason)),
      noWallet && !m.fixedPayoutAddress && notice("warn", h("span", null, "Mining needs a wallet to pay the rewards to. ", h("a", { href: "#/wallet" }, "Create or restore one first."))),
      h("div", { class: "grid cols-4" },
        stat("Status", m.mining ? "Mining" : searching ? "Searching" : "Idle", {
          tone: m.busy ? "ok" : undefined,
          sub: m.mining ? "Continuously, every block it can" : searching ? "For one block, until it is found" : "Miner is stopped",
        }),
        stat("Hash rate", m.busy ? (m.hashRate > 0 ? fmtHashRate(m.hashRate) : "Measuring…") : "—", { sub: `${fmtInt(m.threads)} ${m.threads === 1 ? "thread" : "threads"} on this node` }),
        stat("Time to a block", eta ? `about ${fmtDuration(eta.seconds)}` : "—", {
          sub: eta ? (eta.basis === "live" ? "On average, at this hash rate" : "On average, at the last measured rate") : "Start mining to measure",
        }),
        stat("Difficulty", s.info ? fmtDifficulty(s.info.difficulty) : "—", { sub: "Current network target" })));
  }

  function buildControls(s, m, eta) {
    const noWallet = s.wallets && s.wallets.length === 0;
    const canPay = !!m.fixedPayoutAddress || !noWallet;
    const job = m.job;
    const searching = m.busy && !m.mining;
    const toggleBtn = h("button", { type: "button", class: `btn lg ${m.busy ? "danger" : "primary"}`, disabled: pendingAction || (!m.busy && !canPay) },
      icon(m.busy ? "stop" : "play", { size: 18 }), m.busy ? (searching ? "Stop searching" : "Stop mining") : "Start mining");
    toggleBtn.addEventListener("click", () => toggle(!m.busy, toggleBtn));
    const oneBtn = h("button", { type: "button", class: "btn", disabled: pendingAction || !canPay || m.busy }, icon("mining", { size: 16 }), "Mine one block");
    oneBtn.addEventListener("click", () => mineOne(oneBtn));

    let reward;
    if (m.fixedPayoutAddress) {
      reward = kv([["Rewards go to", h("span", { class: "mono wrap" }, m.fixedPayoutAddress)]]);
    } else if (s.wallets && s.wallets.length) {
      const cur = m.busy && m.minerAddress ? m.minerAddress : payoutAddress();
      rewardSelect.replaceChildren(...s.wallets.map((w) => h("option", { value: w.address, selected: w.address === cur }, `${shortAddr(w.address)}  ·  ${fmtBucks(w.balance, { trim: true })}`)));
      rewardSelect.disabled = m.busy || pendingAction;   // the payout is fixed while the miner runs
      reward = rewardField;
    }
    const etaText = eta ? `about ${fmtDuration(eta.seconds)}` : "";
    // The last one-block search that found a block, also when it ended while this page was closed.
    const found = mineResult || (job && job.state === "found" && job.block ? { block: job.block, minerAddress: job.minerAddress } : null);
    return h("div", { class: "stack" },
      reward,
      h("div", { class: "row actions" }, toggleBtn, oneBtn),
      feedback,
      searching && job && job.state === "running" && notice("info",
        `Searching for one block since ${new Date(job.startedAt * 1000).toLocaleTimeString()}, paying ${shortAddr(job.minerAddress)}. `,
        etaText ? `On average that takes ${etaText} here; it can be much sooner or later. ` : "",
        "You can leave this page; the search keeps going until a block is found or you stop it."),
      !m.busy && found && notice("ok", `Mined block #${fmtInt(found.block.height)} for ${found.minerAddress}.`),
      h("p", { class: "muted", style: "margin:0;font-size:.88rem" },
        `Rewards can be spent after ${COINBASE_MATURITY} further blocks. "Mine one block" searches in the background until it finds one block; continuous mining keeps finding blocks until you stop it. `,
        m.fixedPayoutAddress ? "The node operator fixed the reward address with --mine-address." : ""));
  }

  function buildMiner(s, m) {
    return h("div", { class: "stack" },
      kv([
        ["Reward address", m.minerAddress ? h("span", { class: "mono wrap" }, m.minerAddress) : h("span", { class: "muted" }, "not set until mining starts")],
        ["Blocks found", `${fmtInt(m.blocksFound)} since the node started`],
      ]),
      sparkline(s.hashSamples));
  }

  const view = {
    update(s, force) {
      const m = s.mining;
      if (!m) {
        drawTop(["loading", s.conn], () => (s.conn === "offline" ? errorState("The node did not respond.", () => loadMining(), "Node offline") : skeletonRows(3)));
        drawControls(["loading"], () => skeletonRows(2));
        drawMiner(["loading"], () => skeletonRows(2));
        return;
      }
      noteJob(m);
      syncPolling(m);
      const eta = expectedBlockTime(m, s.hashSamples);
      const etaKey = eta && [eta.basis, Math.round(Math.log10(eta.seconds) * 20)];
      const job = m.job && [m.job.id, m.job.state];
      const wallets = s.wallets && s.wallets.map((w) => [w.address, w.balance]);
      drawTop([force && Math.random(), s.conn, m.mining, m.busy, Math.round(m.hashRate), m.threads, m.blockedReason, m.fixedPayoutAddress, etaKey, s.info && s.info.difficulty, wallets && wallets.length], () => buildTop(s, m, eta));
      drawControls([force && Math.random(), m.mining, m.busy, m.fixedPayoutAddress, m.minerAddress, job, wallets, s.selected, pendingAction, mineResult && mineResult.block.height, etaKey], () => buildControls(s, m, eta));
      drawMiner([m.minerAddress, m.threads, m.blocksFound, s.hashSamples.length], () => buildMiner(s, m));
    },
    destroy() { if (poller) clearInterval(poller); poller = null; },
  };
  return view;
}
