import { h, icon, card, stat, kv, copyButton, pageHead, keyedRender, skeletonRows, emptyState, errorState, notice, hashLink } from "../ui.js";
import { fmtInt, fmtBucks, fmtDifficulty, fmtHashRate, fmtBits, timeAgo, fmtDate, NETWORKS } from "../util.js";
import { loadBlocks, loadInfo, emit } from "../store.js";

export function mount(root) {
  const head = pageHead("Overview", "Live status of the node you are connected to.");
  const body = h("div", { class: "stack" });
  root.append(head, body);
  const draw = keyedRender(body);
  let tickTimer;

  function view(s) {
    if (!s.info) {
      if (s.conn === "offline") return errorState("The node did not respond. This page will fill in as soon as it does.", () => loadInfo().then(emit), "Node offline");
      return h("div", { class: "grid cols-4" }, [1, 2, 3, 4].map(() => h("div", { class: "stat" }, skeletonRows(1))));
    }
    const i = s.info;
    const m = s.mining;
    const last = s.blocks && s.blocks[0];
    const syncing = !!i.syncing;
    const tipChainWork = "0x" + (String(i.chainWork || "").replace(/^0+/, "") || "0");
    return h("div", { class: "stack" },
      i.forkAlert && notice("error", h("strong", null, "Fork alert. "), "The node has seen a competing chain with more work that it did not adopt automatically. Do not trust incoming payments until you have investigated."),
      h("div", { class: "grid cols-4" },
        stat("Block height", fmtInt(i.height), { sub: last ? `Last block ${timeAgo(last.timestamp)}` : undefined }),
        stat("Sync", syncing ? "Syncing" : "Synced", { tone: syncing ? "warn" : "ok", sub: syncing ? "Initial block download in progress" : "Following the chain tip" }),
        stat("Difficulty", fmtDifficulty(i.difficulty), { sub: `bits ${fmtBits(i.bits)}` }),
        stat("Mempool", fmtInt(i.mempoolSize), { sub: i.mempoolSize === 1 ? "transaction waiting" : "transactions waiting" })),
      h("div", { class: "grid cols-main" },
        card("Chain tip", kv([
          ["Network", `${NETWORKS[i.network] ? NETWORKS[i.network].label : i.network} (${i.network})`],
          ["Tip hash", [h("a", { class: "mono wrap", href: `#/blocks/${i.tipHash}` }, i.tipHash), copyButton(i.tipHash, "Copy", { what: "tip hash", compact: true })], { mono: false }],
          ["Chain work", [h("span", { class: "mono wrap" }, tipChainWork), copyButton(i.chainWork, "Copy", { what: "chain work", compact: true })]],
          ["Circulating supply", `${fmtBucks(i.circulatingSupply, { trim: true })} BUCKS`],
          ["Unspent outputs", fmtInt(i.utxoCount)],
        ])),
        card("This node's miner", m ? kv([
          ["Status", h("span", { class: `pill ${m.mining ? "ok" : ""}` }, m.mining ? "Mining" : "Idle")],
          ["Hash rate", m.mining ? fmtHashRate(m.hashRate) : "—"],
          ["Blocks found", fmtInt(m.blocksFound)],
          ["Reward address", m.minerAddress ? h("span", { class: "mono wrap" }, m.minerAddress) : h("span", { class: "muted" }, "none set")],
        ]) : skeletonRows(2), { actions: h("a", { class: "btn sm", href: "#/mining" }, "Mining") })),
      card("Latest blocks", latest(s), { actions: h("a", { class: "btn sm", href: "#/blocks" }, "All blocks", icon("arrow", { size: 14 })) }),
      s.node && card("This node", kv([
        ["Version", s.node.version || "unknown"],
        ["Peers", fmtInt(s.node.peerCount || 0)],
        ["Uptime", fmtUptime(s.node.uptimeSeconds || 0)],
      ])));
  }

  function fmtUptime(sec) {
    const d = Math.floor(sec / 86400), hh = Math.floor((sec % 86400) / 3600), mm = Math.floor((sec % 3600) / 60);
    return d ? `${d}d ${hh}h` : hh ? `${hh}h ${mm}m` : `${mm}m ${sec % 60}s`;
  }

  function latest(s) {
    if (s.blocksError && !s.blocks) return errorState(s.blocksError, () => loadBlocks(), "Could not load blocks");
    if (!s.blocks) return skeletonRows(4);
    if (!s.blocks.length) return emptyState("No blocks yet", "The chain has no blocks to show.");
    return h("div", { class: "list" }, s.blocks.slice(0, 6).map((b) =>
      h("a", { class: "list-row", href: `#/blocks/${b.height}` },
        h("span", { class: "h" }, `#${fmtInt(b.height)}`),
        h("span", { class: "mono", style: "overflow:hidden;text-overflow:ellipsis" }, b.hash.slice(0, 18) + "…"),
        h("span", { class: "num sub hide-sm" }, `${b.transactionCount} tx`),
        h("span", { class: "num sub", title: fmtDate(b.timestamp) }, timeAgo(b.timestamp)))));
  }

  return {
    update(s) {
      const tick = Math.floor(Date.now() / 30000); // relative times refresh at most twice a minute
      draw([s.conn, s.info, s.mining && { ...s.mining, hashRate: Math.round(s.mining.hashRate) }, s.blocks && s.blocks.map((b) => b.hash), s.blocksError, tick], () => view(s));
    },
    destroy() { clearInterval(tickTimer); },
  };
}
