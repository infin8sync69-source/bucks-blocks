import { h, icon, copyButton, pageHead, skeletonRows, errorState, notice, field, busy, netBadge } from "../ui.js";
import { get, post, ApiError } from "../api.js";
import { state, loadWallets, selectWallet, selectedWallet, emit } from "../store.js";
import { fmtBucks, fmtInt, parseBucks, amountProblem, validateAddress, MIN_FEE_UNITS, DUST_LIMIT_UNITS, COINBASE_MATURITY, NETWORKS, shortAddr } from "../util.js";

export function mount(root) {
  const holder = h("div", { class: "stack" });
  root.append(holder);
  let flow = null; // form/review/result, built once the prerequisites are met
  let shown = ""; // what holder currently displays
  let walletSig = "";

  function prerequisites(s) {
    if (!s.walletsEnabled) return errorState(s.walletError || "The node started without its wallet subsystem.", null, "Wallets are unavailable on this node");
    if (s.wallets === null) {
      if (s.walletsError) return errorState(s.walletsError, () => loadWallets().then(emit), "Could not load wallets");
      return skeletonRows(3);
    }
    if (!s.wallets.length) return h("div", { class: "state empty" }, h("div", null, h("strong", null, "You need a wallet to send"), h("p", null, "This node has no wallet yet.")), h("a", { class: "btn primary", href: "#/wallet" }, "Create or restore a wallet"));
    if (!s.info) return skeletonRows(2);
    return null;
  }

  function update(s) {
    const blocked = prerequisites(s);
    if (blocked) {
      flow = null; walletSig = "";
      const key = JSON.stringify(["blocked", s.walletsEnabled, s.walletsError, s.wallets === null, s.wallets && s.wallets.length, !!s.info]);
      if (key !== shown) { shown = key; holder.replaceChildren(pageHead("Send", "Transfer BUCKS to another address."), blocked); }
      return;
    }
    const sig = JSON.stringify([s.info.network, s.wallets.map((w) => w.address)]);
    if (!flow || sig !== walletSig) {
      walletSig = sig;
      flow = buildFlow(s);
      shown = "flow:" + sig;
      holder.replaceChildren(flow.el);
    }
    flow.refresh(s);
  }

  function buildFlow(s0) {
    const net = s0.info.network;
    const netName = NETWORKS[net] ? NETWORKS[net].label : net;
    let step = "form"; // form | review | done
    let touchedTo = false, touchedAmt = false;
    let sentInFlight = false;

    const from = h("select", { id: "s-from" });
    const to = h("input", { id: "s-to", type: "text", autocomplete: "off", autocapitalize: "off", spellcheck: "false", "aria-describedby": "s-to-err", placeholder: "Recipient address" });
    const amt = h("input", { id: "s-amt", type: "text", inputmode: "decimal", autocomplete: "off", placeholder: "0.00000000", "aria-describedby": "s-amt-hint s-amt-err" });
    const toField = field({ id: "s-to", label: "Recipient address", input: to });
    const amtInput = h("div", { class: "input-affix" }, amt, h("span", { class: "affix" }, "BUCKS"));
    const balHint = h("span");
    const amtField = field({ id: "s-amt", label: "Amount", input: amtInput });
    amtField.querySelector("label").after(amtInput);
    amtField.insertBefore(h("div", { class: "hint", id: "s-amt-hint" }, balHint), amtField.querySelector(".field-error"));
    const fromField = field({ id: "s-from", label: "From wallet", input: from });
    const reviewBtn = h("button", { type: "submit", class: "btn primary lg" }, "Review payment", icon("arrow", { size: 16 }));

    function fillFrom(s) {
      const cur = selectedWallet();
      from.replaceChildren(...s.wallets.map((w) => h("option", { value: w.address, selected: cur && cur.address === w.address }, optionText(w))));
      fromField.hidden = s.wallets.length < 2;
    }
    const wallet = (s) => s.wallets.find((w) => w.address === from.value) || s.wallets[0];
    // Spendable, not the balance: immature rewards and coins locked by a pending payment cannot be sent (ONB-07).
    const hintText = () => {
      const w = wallet(state);
      const rest = w.spendable != null && BigInt(w.balance) > BigInt(w.spendable) ? ` of the ${fmtBucks(w.balance, { trim: true })} BUCKS balance (the rest is maturing rewards or in a pending payment)` : "";
      return `Spendable now: ${fmtBucks(w.spendable ?? w.balance, { trim: true })} BUCKS${rest}. Up to 8 decimal places.`;
    };
    const optionText = (w) => `${shortAddr(w.address)}  ·  ${fmtBucks(w.spendable ?? w.balance, { trim: true })} spendable`;

    function setErr(f, input, msg) {
      f.classList.toggle("invalid", !!msg);
      f.querySelector(".field-error").textContent = msg;
      if (msg) input.setAttribute("aria-invalid", "true"); else input.removeAttribute("aria-invalid");
    }
    function check(showAll) {
      const result = { ok: true };
      const v = validateAddress(to.value, net);
      setErr(toField, to, (showAll || touchedTo) && !v.ok && !(v.empty && !showAll) ? v.reason : "");
      if (!v.ok) result.ok = false;
      const a = parseBucks(amt.value);
      let amtErr = "";
      if (!a.ok) { result.ok = false; if (amt.value.trim() || showAll) amtErr = a.error; }
      else {
        const w = wallet(state);
        const dust = state.info && state.info.dustLimit != null ? state.info.dustLimit : DUST_LIMIT_UNITS;
        const problem = amountProblem(a.units, { balance: w.balance, spendable: w.spendable, dust });
        if (problem) { amtErr = problem; result.ok = false; }
        result.units = a.units;
      }
      setErr(amtField, amt, showAll || touchedAmt ? amtErr : "");
      return result;
    }

    to.addEventListener("input", () => { if (touchedTo) check(false); });
    to.addEventListener("blur", () => { if (to.value.trim()) { touchedTo = true; check(false); } });
    amt.addEventListener("input", () => { if (touchedAmt) check(false); });
    amt.addEventListener("blur", () => { if (amt.value.trim()) { touchedAmt = true; check(false); } });
    from.addEventListener("change", () => { selectWallet(from.value); balHint.textContent = hintText(); check(false); });

    const formErr = h("div", { "aria-live": "assertive" });
    const form = h("form", { class: "card", novalidate: true },
      h("div", { class: "stack" }, fromField, toField, amtField,
        h("div", { class: "notice info" }, icon("info", { size: 18 }), h("div", null, h("strong", null, "Network fee: "), `set by the node from the size of the payment, at least ${fmtBucks(MIN_FEE_UNITS, { trim: true })} BUCKS. You see the exact fee before you confirm.`)),
        formErr,
        h("div", { class: "row actions" }, reviewBtn)));

    form.addEventListener("submit", (e) => {
      e.preventDefault();
      touchedTo = touchedAmt = true;
      const r = check(true);
      if (!r.ok) { (form.querySelector(".invalid input") || to).focus(); return; }
      startReview(r.units);
    });

    // The node builds the payment once without sending it, so the review shows the fee it will really
    // pay (ONB-03). Confirm sends that fee as maxFee: if coin selection changed meanwhile, the node refuses.
    let estimating = false;
    async function startReview(units) {
      if (estimating) return;
      estimating = true;
      formErr.replaceChildren();
      busy(reviewBtn, true, "Working out the fee…");
      const w = wallet(state);
      const dest = to.value.trim();
      try {
        const est = await post("/api/transactions/estimate", { from: w.address, to: dest, amount: Number(units) });
        toReview(units, w, dest, est);
      } catch (ex) {
        step = "form"; show();
        if (ex instanceof ApiError && ex.status === 400 && /insufficient|no spendable|dust/i.test(ex.message)) {
          setErr(amtField, amt, explain(ex));
          amt.focus();
        } else {
          formErr.replaceChildren(notice("error", explain(ex, false)));
        }
      } finally {
        estimating = false;
        reviewBtn.disabled = false;
        reviewBtn.replaceChildren("Review payment", icon("arrow", { size: 16 }));
      }
    }

    // ----- review -----
    const reviewBox = h("div");
    function toReview(units, w, dest, est) {
      step = "review";
      const fee = BigInt(est.fee);
      const back = h("button", { type: "button", class: "btn", onclick: () => { step = "form"; show(); amt.focus(); } }, icon("chevl", { size: 16 }), "Edit");
      const confirm = h("button", { type: "button", class: "btn primary lg" }, icon("send", { size: 18 }), "Confirm and send");
      const err = h("div", { "aria-live": "assertive" });
      confirm.addEventListener("click", () => send(units, w, dest, fee, confirm, back, err));
      reviewBox.replaceChildren(h("section", { class: "card" }, h("div", { class: "stack" },
        h("div", { class: "row between" }, h("h2", null, "Review and confirm"), netBadge(net)),
        net !== "main" && notice("warn", `This is ${netName.toLowerCase()}. The coins have no real value.`),
        h("div", { class: "review" },
          h("div", null, h("span", { class: "k" }, "From"), h("span", { class: "mono wrap" }, w.address)),
          h("div", null, h("span", { class: "k" }, "To"), h("span", { class: "mono wrap" }, dest)),
          h("div", null, h("span", { class: "k" }, "Amount"), h("span", { class: "big" }, `${fmtBucks(units)} BUCKS`)),
          h("div", null, h("span", { class: "k" }, "Network fee"), h("span", null, `${fmtBucks(fee)} BUCKS`,
            fee > MIN_FEE_UNITS ? h("span", { class: "sub", style: "display:block" }, `Higher than the minimum: this payment combines ${est.inputCount} coins, which makes it larger.`) : null)),
          h("div", null, h("span", { class: "k" }, "Total"), h("span", null, h("strong", null, `${fmtBucks(units + fee)} BUCKS`)))),
        h("p", { class: "muted", style: "margin:0;font-size:.88rem" }, "Transactions cannot be reversed. Check the first and last characters of the recipient address."),
        err,
        h("div", { class: "row actions" }, confirm, back))));
      show();
      confirm.focus();
    }

    async function send(units, w, dest, fee, confirm, back, err) {
      if (sentInFlight) return;
      sentInFlight = true;
      err.replaceChildren();
      busy(confirm, true, "Sending…");
      back.disabled = true;
      try {
        const r = await post("/api/transactions/send", { from: w.address, to: dest, amount: Number(units), maxFee: Number(fee) }, { timeout: 30000 });
        step = "done";
        loadWallets(true);
        showDone(r.transaction, units, dest, w.address);
        to.value = ""; amt.value = ""; touchedTo = touchedAmt = false;
        check(false);
      } catch (ex) {
        confirm.disabled = false;
        confirm.replaceChildren(icon("send", { size: 18 }), "Confirm and send");
        back.disabled = false;
        if (ex instanceof ApiError && ex.status === 409) {
          // Nothing was sent: the fee went up since the review. Show the new one.
          err.replaceChildren(notice("warn", h("span", null, "The network fee changed since you reviewed this payment, so nothing was sent. "),
            h("button", { type: "button", class: "btn sm", onclick: () => startReview(units) }, "Review the new fee")));
        } else {
          err.replaceChildren(notice("error", explain(ex)));
        }
      } finally { sentInFlight = false; }
    }

    function explain(ex, sending = true) {
      if (ex instanceof ApiError) {
        if (ex.offline && !sending) return "Cannot reach the node. Nothing was sent.";
        // History now lists this wallet's pending payments, so it can answer whether this one went out (ONB-05).
        if (ex.offline) return "The node did not answer, so this payment may or may not have been sent. Do not send it again yet: when the node is reachable, open History. A payment that went out is listed there as pending or confirmed.";
        const m = ex.message || "";
        if (/insufficient|no spendable/i.test(m)) return `${m}. Newly mined rewards can be spent only after ${COINBASE_MATURITY} further blocks, and coins in a pending payment are locked until it is mined.`;
        if (/address|base58|checksum|version/i.test(m)) return `The node rejected the recipient address: ${m}`;
        if (ex.status === 401) return "Your session has expired. Sign in again; nothing was sent.";
        if (ex.status === 429) return "The node is receiving too many requests. Wait a moment and try again.";
        return m;
      }
      return String(ex.message || ex);
    }

    // ----- done -----
    const doneBox = h("div");
    let watch = null; // { txid, from, el, key }: the sent payment, followed until it confirms (ONB-05)
    function showDone(tx, units, dest, fromAddress) {
      const txid = tx && tx.hash;
      const statusEl = h("div", { role: "status" });
      watch = txid ? { txid, from: fromAddress, el: statusEl, key: "" } : null;
      paintStatus({ status: "pending" });
      doneBox.replaceChildren(h("section", { class: "card" }, h("div", { class: "stack" },
        h("div", { class: "row between" }, h("h2", { tabindex: "-1" }, "Payment submitted"), netBadge(net)),
        statusEl,
        h("div", { class: "review" },
          h("div", null, h("span", { class: "k" }, "Amount"), h("span", null, `${fmtBucks(units)} BUCKS`)),
          h("div", null, h("span", { class: "k" }, "To"), h("span", { class: "mono wrap" }, dest)),
          tx && tx.fee != null && h("div", null, h("span", { class: "k" }, "Network fee"), h("span", null, `${fmtBucks(tx.fee)} BUCKS`)),
          h("div", null, h("span", { class: "k" }, "Transaction ID"), h("span", { class: "row" }, h("span", { class: "mono wrap" }, txid || "unknown"), txid && copyButton(txid, "Copy", { what: "transaction id", compact: true })))),
        h("div", { class: "row actions" },
          h("button", { type: "button", class: "btn primary", onclick: () => { step = "form"; watch = null; show(); to.focus(); } }, "Send another"),
          h("a", { class: "btn", href: "#/history" }, "View history"),
          h("a", { class: "btn ghost", href: "#/wallet" }, "Back to wallet")))));
      show();
      const head = doneBox.querySelector("h2");
      if (head) head.focus({ preventScroll: true });
    }

    function paintStatus(entry) {
      if (!watch) return;
      const conf = entry && entry.confirmations;
      watch.el.replaceChildren(!entry
        ? notice("warn", "This payment is not in the wallet's recent activity. If it does not show up in History after a few blocks, the node dropped it and the coins are free again.")
        : entry.status === "pending"
          ? notice("info", h("strong", null, "Pending. "), "It is in the node's mempool and is confirmed when a block includes it. This page updates by itself.")
          : notice("ok", h("strong", null, "Confirmed. "), `In block #${fmtInt(entry.height)}, ${fmtInt(conf)} confirmation${conf === 1 ? "" : "s"}.`));
    }

    // Looks the payment up again whenever a block arrives or the mempool changes.
    async function followPayment(s) {
      if (!watch || step !== "done" || !s.info) return;
      const key = `${s.info.tipHash}/${s.info.mempoolSize}`;
      if (key === watch.key) return;
      watch.key = key;
      const w = watch;
      try {
        const r = await get(`/api/wallets/${encodeURIComponent(w.from)}/history?limit=50&depth=200`);
        if (w === watch) paintStatus((r.activity || []).find((a) => a.txid === w.txid) || null);
      } catch { if (w === watch) w.key = ""; }
    }

    const syncWarn = h("div");
    const el = h("div", { class: "stack" }, pageHead("Send", `Transfer BUCKS on ${netName.toLowerCase()}.`), syncWarn, form, reviewBox, doneBox);
    function show() {
      form.hidden = step !== "form";
      reviewBox.hidden = step !== "review";
      doneBox.hidden = step !== "done";
    }
    show();

    return {
      el,
      refresh(s) {
        if (from.options.length !== s.wallets.length) fillFrom(s);
        else [...from.options].forEach((o) => { const w = s.wallets.find((x) => x.address === o.value); if (w) o.textContent = optionText(w); });
        balHint.textContent = hintText();
        syncWarn.replaceChildren(...(s.info && s.info.syncing ? [notice("warn", "This node is still syncing. Balances may be out of date and payments may be rejected until it catches up.")] : []));
        followPayment(s);
      },
    };
  }

  return { update, destroy() {} };
}
