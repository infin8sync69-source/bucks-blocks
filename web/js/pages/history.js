import { h, icon, card, copyButton, pageHead, skeletonRows, errorState, emptyState, notice, field } from "../ui.js";
import { get, ApiError } from "../api.js";
import { fmtBucks, fmtInt, timeAgo, fmtDate, shortHash, shortAddr, describeActivity } from "../util.js";
import { state, selectWallet, selectedWallet } from "../store.js";

export function mount(root) {
  const pick = h("div");
  const mineBody = h("div");
  const netBody = h("div");
  const refreshBtn = h("button", { type: "button", class: "btn", onclick: () => load(true) }, icon("refresh", { size: 16 }), "Refresh");
  root.append(h("div", { class: "stack" },
    pageHead("History", "Payments your wallet sent and received, and recent activity on the chain.", refreshBtn),
    pick,
    card("Your activity", mineBody),
    card("Recent network transactions", netBody)));

  let mine = { loading: true }; // { loading, error, data, address }
  let net = { loading: true };
  let seen = ""; // tip, mempool size and wallet the lists were loaded for
  let gone = false;
  let seq = 0;

  async function load(manual) {
    const my = ++seq;
    const w = selectedWallet();
    if (manual) { mine = { ...mine, loading: true }; net = { ...net, loading: true }; paint(); }
    const [a, b] = await Promise.allSettled([
      w ? get(`/api/wallets/${encodeURIComponent(w.address)}/history?limit=100`) : Promise.resolve(null),
      get("/api/transactions?limit=25"),
    ]);
    if (gone || my !== seq) return;
    if (a.status === "fulfilled") mine = { data: a.value, none: !w };
    else mine = { error: a.reason };
    if (b.status === "fulfilled") net = { data: (b.value.transactions || []).slice().reverse() };
    else net = { error: b.reason };
    paint();
  }

  function paint() {
    paintMine();
    paintNet();
  }

  // History follows the wallet selected on the Wallet page; with several wallets it can be switched here.
  let pickSig = "";
  function paintPick() {
    const list = state.wallets || [];
    const cur = selectedWallet();
    const sig = JSON.stringify([list.map((w) => [w.address, w.balance]), cur && cur.address]);
    if (sig === pickSig) return;
    pickSig = sig;
    if (list.length < 2) return void pick.replaceChildren();
    const sel = h("select", { id: "h-wallet", onchange: (e) => selectWallet(e.target.value) },
      list.map((w) => h("option", { value: w.address, selected: cur && cur.address === w.address }, `${shortAddr(w.address)}  ·  ${fmtBucks(w.balance, { trim: true })} BUCKS`)));
    pick.replaceChildren(h("div", { class: "card" }, field({ id: "h-wallet", label: "Wallet", input: sel })));
  }

  function paintMine() {
    if (mine.loading && !mine.data) return void mineBody.replaceChildren(skeletonRows(3));
    if (mine.none || (state.wallets && !state.wallets.length)) return void mineBody.replaceChildren(emptyState("No wallet yet", "Create or restore a wallet to see what it sends and receives.", h("a", { class: "btn primary", href: "#/wallet" }, "Open wallet")));
    if (mine.error) return void mineBody.replaceChildren(errorState(errMsg(mine.error), () => load(true), "Could not load your activity"));
    const d = mine.data;
    const rows = (d.activity || []).map((t) => describeActivity(t));
    mineBody.replaceChildren(h("div", { class: "stack" },
      h("div", { class: "sub" }, "Wallet ", h("span", { class: "mono wrap" }, d.address), ` · spendable now ${fmtBucks(d.spendable, { trim: true })} BUCKS`),
      rows.length
        ? h("div", { class: "list" }, rows.map((r, i) => {
            const t = d.activity[i];
            return h("div", { class: "list-row tx-row" },
              h("div", { style: "min-width:0" },
                h("div", null, h("strong", null, r.label), " ", h("span", { class: `pill ${r.tone}` }, r.status)),
                h("div", { class: "sub" },
                  h("span", { class: "mono", title: t.txid }, shortHash(t.txid, 12, 8)),
                  t.height != null && [" · block ", h("a", { href: `#/blocks/${t.height}` }, `#${fmtInt(t.height)}`)],
                  t.time ? [" · ", h("span", { title: fmtDate(t.time) }, timeAgo(t.time))] : null),
                t.to && h("div", { class: "sub" }, "To ", h("span", { class: "mono wrap" }, t.to)),
                t.fee != null && h("div", { class: "sub" }, `Network fee ${fmtBucks(t.fee, { trim: true })} BUCKS`)),
              h("div", { class: "num" }, h("strong", null, `${r.sign}${fmtBucks(t.amount, { trim: true })}`), h("div", { class: "sub" }, "BUCKS")),
              copyButton(t.txid, "Copy", { what: "transaction id", compact: true }));
          }))
        : emptyState("Nothing yet", "Payments to and from this wallet, and its mining rewards, will appear here."),
      notice("info", d.complete
        ? "This is all of this wallet's activity since the first block."
        : `Shown: activity in the newest ${fmtInt(d.scannedBlocks)} blocks (#${fmtInt(d.scannedFrom)} to #${fmtInt(d.tipHeight)})${rows.length >= 100 ? ", newest 100 entries" : ""}. The node keeps no address index yet, so older activity is not listed; the balance counts all of it.`)));
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

  return {
    update(s) {
      // Reload when a block arrives, the mempool changes or another wallet is selected.
      const w = selectedWallet();
      const key = JSON.stringify([s.info && s.info.tipHash, s.info && s.info.mempoolSize, w && w.address, !!s.wallets]);
      if (key !== seen && s.wallets) { seen = key; load(false); }
      paintPick();
    },
    destroy() { gone = true; },
  };
}
