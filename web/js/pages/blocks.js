import { h, icon, card, kv, copyButton, pageHead, keyedRender, skeletonRows, emptyState, errorState, spinner, notice } from "../ui.js";
import { get, ApiError } from "../api.js";
import { fmtInt, fmtBits, timeAgo, fmtDate, isoUtc, isHex64 } from "../util.js";
import { state } from "../store.js";

function row(b) {
  return h("a", { class: "list-row", href: `#/blocks/${b.height}` },
    h("span", { class: "h" }, `#${fmtInt(b.height)}`),
    h("span", { class: "mono", style: "overflow:hidden;text-overflow:ellipsis" }, b.hash),
    h("span", { class: "num sub hide-sm" }, `${b.transactionCount} tx`),
    h("span", { class: "num sub", title: fmtDate(b.timestamp) }, timeAgo(b.timestamp)));
}

export function mount(root, { params }) {
  return params[0] ? detail(root, params[0]) : list(root);
}

// ---------- list ----------
function list(root) {
  const input = h("input", { id: "block-q", type: "search", inputmode: "search", autocomplete: "off", spellcheck: "false", placeholder: "Height or block hash", "aria-describedby": "block-q-err" });
  const err = h("div", { class: "field-error", id: "block-q-err", role: "alert" });
  const form = h("form", { class: "card", novalidate: true },
    h("div", { class: "field", style: "margin:0" }, h("label", { for: "block-q" }, "Find a block"),
      h("div", { class: "row", style: "flex-wrap:nowrap" }, input, h("button", { class: "btn primary", type: "submit" }, "Open")), err));
  form.addEventListener("submit", (e) => {
    e.preventDefault();
    const q = input.value.trim();
    err.textContent = "";
    if (/^\d+$/.test(q)) location.hash = `#/blocks/${q}`;
    else if (isHex64(q)) location.hash = `#/blocks/${q.toLowerCase()}`;
    else err.textContent = "Enter a block height (digits) or a 64-character block hash.";
  });
  const body = h("div");
  const more = h("div", { class: "row", style: "margin-top:12px;justify-content:center" });
  root.append(pageHead("Blocks", "The most recent blocks on this node's chain."), h("div", { class: "stack" }, form, card(null, h("div", null, body, more))));
  const draw = keyedRender(body);
  let older = []; // extra blocks loaded by "Load older"
  let loadingMore = false;
  let moreError = "";

  async function loadOlder(s) {
    const all = (s.blocks || []).concat(older);
    const lowest = all.length ? all[all.length - 1].height : 0;
    if (lowest <= 0) return;
    loadingMore = true; moreError = ""; paintMore(s);
    const heights = [];
    for (let k = 1; k <= 10 && lowest - k >= 0; k++) heights.push(lowest - k);
    try {
      const res = await Promise.all(heights.map((n) => get(`/api/blocks/height/${n}`).then((r) => r.block)));
      older = older.concat(res);
    } catch (e) { moreError = e.message; }
    loadingMore = false;
    view.update(state);
  }
  function paintMore(s) {
    const all = (s.blocks || []).concat(older);
    const lowest = all.length ? all[all.length - 1].height : 0;
    more.replaceChildren(...(lowest > 0 && s.blocks ? [
      moreError && h("span", { class: "field-error", style: "display:block" }, moreError),
      h("button", { class: "btn", type: "button", disabled: loadingMore, onclick: () => loadOlder(state) }, loadingMore ? [h("span", { class: "spin" }), "Loading…"] : "Load 10 older blocks"),
    ] : []));
  }
  const view = {
    update(s) {
      draw([s.blocks && s.blocks.map((b) => b.hash), s.blocksError, older.map((b) => b.hash), Math.floor(Date.now() / 30000)], () => {
        if (s.blocksError && !s.blocks) return errorState(s.blocksError, () => location.reload(), "Could not load blocks");
        if (!s.blocks) return skeletonRows(5);
        if (!s.blocks.length) return emptyState("No blocks yet");
        return h("div", { class: "list" }, [
          h("div", { class: "list-row head" }, h("span", null, "Height"), h("span", null, "Hash"), h("span", { class: "num hide-sm" }, "Txs"), h("span", { class: "num" }, "Age")),
          ...s.blocks.concat(older).map(row)]);
      });
      paintMore(s);
    },
  };
  return view;
}

// ---------- detail ----------
function detail(root, key) {
  const isHeight = /^\d+$/.test(key);
  const isHash = isHex64(key);
  const body = h("div", { class: "stack" }, spinner("Loading block…"));
  root.append(body);
  let gone = false;

  async function load() {
    body.replaceChildren(spinner("Loading block…"));
    if (!isHeight && !isHash) {
      body.replaceChildren(pageHead("Block not found"), errorState("That is not a block height or a 64-character hash.", null, "Invalid block reference"), backLink());
      return;
    }
    try {
      const r = await get(isHeight ? `/api/blocks/height/${key}` : `/api/blocks/hash/${key}`);
      if (gone) return;
      show(r.block);
    } catch (e) {
      if (gone) return;
      const nf = e instanceof ApiError && e.status === 404;
      body.replaceChildren(pageHead(nf ? "Block not found" : "Block unavailable"),
        errorState(nf ? "This node has no block with that height or hash. It may not have synced that far yet." : e.message, nf ? null : load, nf ? "Not found" : "Could not load block"), backLink());
    }
  }
  const backLink = () => h("div", null, h("a", { class: "btn", href: "#/blocks" }, icon("chevl", { size: 16 }), "All blocks"));

  function show(b) {
    const tip = state.info ? state.info.height : null;
    const zero = /^0+$/.test(b.prevHash);
    body.replaceChildren(
      pageHead(`Block #${fmtInt(b.height)}`, `${b.transactionCount} transaction${b.transactionCount === 1 ? "" : "s"} · ${timeAgo(b.timestamp)}`,
        h("a", { class: "btn", href: `#/blocks/${b.height - 1}`, "aria-disabled": b.height === 0, tabindex: b.height === 0 ? "-1" : null, style: b.height === 0 ? "pointer-events:none;opacity:.5" : null }, icon("chevl", { size: 16 }), "Previous"),
        h("a", { class: "btn", href: `#/blocks/${b.height + 1}`, style: tip !== null && b.height >= tip ? "pointer-events:none;opacity:.5" : null, "aria-disabled": tip !== null && b.height >= tip }, "Next", icon("chevr", { size: 16 })),
        h("a", { class: "btn ghost", href: "#/blocks" }, "All blocks")),
      card(null, kv([
        ["Hash", [h("span", { class: "mono wrap" }, b.hash), copyButton(b.hash, "Copy", { what: "block hash", compact: true })]],
        ["Previous block", zero ? h("span", { class: "muted" }, "none (genesis)") : [h("a", { class: "mono wrap", href: `#/blocks/${b.prevHash}` }, b.prevHash), copyButton(b.prevHash, "Copy", { what: "previous hash", compact: true })]],
        ["Merkle root", [h("span", { class: "mono wrap" }, b.merkleRoot), copyButton(b.merkleRoot, "Copy", { what: "merkle root", compact: true })]],
        ["Time", [fmtDate(b.timestamp), h("span", { class: "muted" }, isoUtc(b.timestamp))]],
        ["Difficulty bits", h("span", { class: "mono" }, fmtBits(b.difficulty))],
        ["Nonce", h("span", { class: "mono" }, String(b.nonce))],
        ["Transactions", String(b.transactionCount)],
      ])),
      h("p", { class: "muted", style: "font-size:.85rem" }, "The node's block API returns header fields and a transaction count; individual transactions are not listed yet."));
    const h1 = body.querySelector("h1");
    document.title = `Block #${b.height} · Bucks`;
    void h1;
  }

  load();
  return { destroy() { gone = true; }, update() { /* static once loaded */ } };
}
