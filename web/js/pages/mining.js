import { h, icon, card, kv, stat, pageHead, keyedRender, skeletonRows, errorState, notice, busy, toast } from "../ui.js";
import { post, ApiError } from "../api.js";
import { state, loadMining, loadInfo, loadBlocks, loadWallets, emit } from "../store.js";
import { fmtHashRate, fmtInt, fmtDifficulty, COINBASE_MATURITY } from "../util.js";

function sparkline(samples) {
  const W = 300, H = 64;
  const pts = samples.slice(-40);
  if (pts.length < 2) return h("p", { class: "muted", style: "margin:0;font-size:.88rem" }, "Collecting samples. One is taken every 10 seconds while this tab is open.");
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
  const body = h("div", { class: "stack" });
  const feedback = h("div", { "aria-live": "polite" });
  root.append(pageHead("Mining", "Control this node's built-in miner."), body);
  const draw = keyedRender(body);
  let pendingAction = false;
  let mineResult = null;

  async function toggle(start, btn) {
    pendingAction = true;
    busy(btn, true, start ? "Starting…" : "Stopping…");
    feedback.replaceChildren();
    try {
      await post(start ? "/api/mining/start" : "/api/mining/stop", {});
      toast(start ? "Mining started" : "Mining stopped", "ok");
    } catch (e) {
      feedback.replaceChildren(notice("error", e instanceof ApiError && e.offline ? "Cannot reach the node." : e.message,
        /wallet/i.test(e.message || "") ? [" ", h("a", { href: "#/wallet" }, "Open the wallet page")] : null));
    }
    pendingAction = false;
    await loadMining(false);
    view.update(state, true);
  }

  async function mineOne(btn) {
    pendingAction = true;
    busy(btn, true, "Mining a block…");
    feedback.replaceChildren();
    mineResult = null;
    try {
      const r = await post("/api/mining/mine", {}, { timeout: 90000 });
      mineResult = r;
      toast(`Mined block #${r.block.height}`, "ok");
      await Promise.all([loadInfo(), loadBlocks(false), loadWallets(false), loadMining(false)]);
    } catch (e) {
      feedback.replaceChildren(notice("error", e instanceof ApiError && e.offline ? "The node did not answer. It may still be mining; check the Blocks page." : e.message));
    }
    pendingAction = false;
    view.update(state, true);
  }

  function build(s) {
    const m = s.mining;
    if (!m) {
      if (s.conn === "offline") return errorState("The node did not respond.", () => loadMining(), "Node offline");
      return skeletonRows(3);
    }
    const noWallet = s.wallets && s.wallets.length === 0;
    const toggleBtn = h("button", { type: "button", class: `btn lg ${m.mining ? "danger" : "primary"}`, disabled: pendingAction || (!m.mining && noWallet) },
      icon(m.mining ? "stop" : "play", { size: 18 }), m.mining ? "Stop mining" : "Start mining");
    toggleBtn.addEventListener("click", () => toggle(!m.mining, toggleBtn));
    const oneBtn = h("button", { type: "button", class: "btn", disabled: pendingAction || noWallet || m.mining }, icon("mining", { size: 16 }), "Mine one block");
    oneBtn.addEventListener("click", () => mineOne(oneBtn));
    return h("div", { class: "stack" },
      s.info && s.info.syncing && notice("warn", "This node is still syncing, so it cannot mine until it catches up."),
      h("div", { class: "grid cols-4" },
        stat("Status", m.mining ? "Mining" : "Idle", { tone: m.mining ? "ok" : undefined, sub: m.mining ? "Searching for blocks" : "Miner is stopped" }),
        stat("Hash rate", m.mining ? fmtHashRate(m.hashRate) : "—", { sub: "Measured by this node" }),
        stat("Blocks found", fmtInt(m.blocksFound), { sub: "Since the node started" }),
        stat("Difficulty", s.info ? fmtDifficulty(s.info.difficulty) : "—", { sub: "Current network target" })),
      h("div", { class: "grid cols-2" },
        card("Controls", h("div", { class: "stack" },
          noWallet && notice("warn", h("span", null, "Mining needs a wallet to pay the rewards to. ", h("a", { href: "#/wallet" }, "Create or restore one first."))),
          h("div", { class: "row actions" }, toggleBtn, oneBtn),
          feedback,
          mineResult && notice("ok", `Mined block #${fmtInt(mineResult.block.height)} for ${mineResult.minerAddress}.`),
          h("p", { class: "muted", style: "margin:0;font-size:.88rem" }, `Rewards go to the miner address and can be spent after ${COINBASE_MATURITY} further blocks. "Mine one block" searches for a single block (up to a minute) and is disabled while continuous mining runs.`))),
        card("Miner", h("div", { class: "stack" },
          kv([["Reward address", m.minerAddress ? h("span", { class: "mono wrap" }, m.minerAddress) : h("span", { class: "muted" }, "not set until mining starts")]]),
          sparkline(s.hashSamples)))));
  }

  const view = {
    update(s, force) {
      draw([force && Math.random(), s.conn, s.mining && { ...s.mining, hashRate: Math.round(s.mining.hashRate) }, s.info && [s.info.difficulty, s.info.syncing], s.wallets && s.wallets.length, s.hashSamples.length, pendingAction, mineResult && mineResult.block.height], () => build(s));
    },
    destroy() {},
  };
  return view;
}
