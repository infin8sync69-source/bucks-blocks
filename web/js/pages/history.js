import { h, icon, card, copyButton, pageHead, skeletonRows, errorState, emptyState, notice } from "../ui.js";
import { get, ApiError } from "../api.js";
import { fmtBucks, fmtInt, timeAgo, fmtDate, shortHash, COINBASE_MATURITY } from "../util.js";
import { state } from "../store.js";

export function mount(root) {
  const mineBody = h("div");
  const netBody = h("div");
  const refreshBtn = h("button", { type: "button", class: "btn", onclick: () => load(true) }, icon("refresh", { size: 16 }), "Refresh");
  root.append(h("div", { class: "stack" },
    pageHead("History", "Coins your wallet received and recent activity on the chain.", refreshBtn),
    card("Received coins", mineBody),
    card("Recent network transactions", netBody)));

  let mine = { loading: true }; // { loading, error, data, address }
  let net = { loading: true };
  let lastTip = null;
  let gone = false;
  let seq = 0;

  async function load(manual) {
    const my = ++seq;
    if (manual) { mine = { ...mine, loading: true }; net = { ...net, loading: true }; paint(); }
    const [a, b] = await Promise.allSettled([get("/api/wallets/primary"), get("/api/transactions?limit=25")]);
    if (gone || my !== seq) return;
    if (a.status === "fulfilled") mine = { data: a.value };
    else mine = { error: a.reason, none: a.reason instanceof ApiError && a.reason.status === 404 };
    if (b.status === "fulfilled") net = { data: (b.value.transactions || []).slice().reverse() };
    else net = { error: b.reason };
    paint();
  }

  function paint() {
    paintMine();
    paintNet();
  }

  function paintMine() {
    if (mine.loading && !mine.data) return void mineBody.replaceChildren(skeletonRows(3));
    if (mine.none) return void mineBody.replaceChildren(emptyState("No wallet yet", "Create or restore a wallet to see the coins it has received.", h("a", { class: "btn primary", href: "#/wallet" }, "Open wallet")));
    if (mine.error) return void mineBody.replaceChildren(errorState(errMsg(mine.error), () => load(true), "Could not load your coins"));
    const d = mine.data;
    const tip = state.info ? state.info.height : null;
    const txs = (d.transactions || []).slice().sort((a, b) => b.blockHeight - a.blockHeight);
    mineBody.replaceChildren(h("div", { class: "stack" },
      h("div", { class: "sub" }, "Wallet ", h("span", { class: "mono wrap" }, d.address), (state.wallets && state.wallets.length > 1) ? " (the primary wallet; other wallets are not listed)" : ""),
      txs.length
        ? h("div", { class: "list" }, txs.map((t) => {
            const conf = tip !== null && t.blockHeight != null ? tip - t.blockHeight + 1 : null;
            const reward = t.type === "mining_reward";
            const maturing = reward && conf !== null && conf < COINBASE_MATURITY;
            return h("div", { class: "list-row tx-row" },
              h("div", { style: "min-width:0" },
                h("div", null, h("strong", null, reward ? "Mining reward" : "Received"), " ", h("span", { class: `pill ${maturing ? "warn" : "ok"}` }, maturing ? `Matures in ${COINBASE_MATURITY - conf} blocks` : conf === null ? "" : "Spendable")),
                h("div", { class: "sub" }, h("span", { class: "mono", title: t.hash }, shortHash(t.hash, 12, 8)), " · block ", h("a", { href: `#/blocks/${t.blockHeight}` }, `#${fmtInt(t.blockHeight)}`), conf !== null ? ` · ${fmtInt(conf)} confirmations` : "")),
              h("div", { class: "num" }, h("strong", null, `+${fmtBucks(t.amount, { trim: true })}`), h("div", { class: "sub" }, "BUCKS")),
              copyButton(t.hash, "Copy", { what: "transaction id", compact: true }));
          }))
        : emptyState("Nothing received yet", "Coins sent to this wallet, and mining rewards, will appear here."),
      notice("info", `This list shows ${txs.length < Number(d.utxoCount) ? `${txs.length} of the ${fmtInt(d.utxoCount)} coins` : "the coins"} your wallet currently holds, as returned by the node. Payments you sent, and coins you have already spent, are not listed because the node does not offer a per-wallet transaction history yet.`),
      state.info && state.info.mempoolSize > 0 && h("div", { class: "sub" }, `${fmtInt(state.info.mempoolSize)} transaction${state.info.mempoolSize === 1 ? "" : "s"} waiting in the node's mempool (not wallet-specific).`)));
  }

  function paintNet() {
    if (net.loading && !net.data) return void netBody.replaceChildren(skeletonRows(4));
    if (net.error) return void netBody.replaceChildren(errorState(errMsg(net.error), () => load(true), "Could not load transactions"));
    const txs = net.data;
    netBody.replaceChildren(txs.length
      ? h("div", { class: "list" }, txs.map((t) => h("div", { class: "list-row tx-row" },
          h("div", { style: "min-width:0" }, h("div", { class: "mono", title: t.hash }, shortHash(t.hash, 14, 10)), h("div", { class: "sub" }, `${t.inputCount} in · ${t.outputCount} out`)),
          h("div", { class: "num" }, h("strong", null, fmtBucks(t.totalAmount, { trim: true })), h("div", { class: "sub" }, "BUCKS out")),
          h("div", { class: "num sub", title: fmtDate(t.timestamp) }, timeAgo(t.timestamp)))))
      : emptyState("No transactions yet"));
  }

  const errMsg = (e) => (e instanceof ApiError && e.offline ? "Cannot reach the node." : e.message);

  load(false);
  return {
    update(s) {
      const tip = s.info && s.info.tipHash;
      if (lastTip !== null && tip && tip !== lastTip) load(false);
      else if (lastTip === null || !mine.data) paintMine();
      lastTip = tip || lastTip;
    },
    destroy() { gone = true; },
  };
}
