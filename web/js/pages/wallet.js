import { h, icon, card, append, kv, copyButton, pageHead, keyedRender, skeletonRows, errorState, notice, field, busy, toast } from "../ui.js";
import { get, post, ApiError } from "../api.js";
import { state, loadWallets, selectWallet, selectedWallet, emit } from "../store.js";
import { fmtBucks, COINBASE_MATURITY, NETWORKS } from "../util.js";
import { toSvg } from "../qr.js";

// The recovery phrase lives only in this module variable while the user writes
// it down. It is never put in localStorage, sessionStorage, the URL or the DOM
// after the confirm step, and is dropped as soon as the step is completed.
let restored = null; // { from, words, address }: the Arabic form of a phrase restored in another language
let pending = null; // { words: string[], direction, address, persisted, saveError }

window.addEventListener("beforeunload", (e) => {
  if (pending) { e.preventDefault(); e.returnValue = ""; }
});

export function mount(root) {
  let mode = pending ? "phrase" : "main"; // main | create | restore | phrase | done
  const holder = h("div", { class: "stack" });
  root.append(holder);
  let langs = null; // cached /api/wallets/languages
  const draw = keyedRender(holder);

  // The form views write into holder directly, so the main view's cache is stale after any switch (ONB-06).
  function go(m) { mode = m; lastSig = null; draw.reset(); render(state); }
  let lastSig = null;

  function render(s) {
    const sig = [mode, s.walletsEnabled, s.walletsError, s.wallets, s.selected, s.info && s.info.network];
    if (mode === "create" || mode === "restore" || mode === "phrase") {
      // forms keep their own state; build them once per mode switch
      if (JSON.stringify(sig.slice(0, 1)) === lastSig) return;
      lastSig = JSON.stringify(sig.slice(0, 1));
      holder.replaceChildren(mode === "create" ? createView() : mode === "restore" ? restoreView() : phraseView());
      focusHeading();
      return;
    }
    lastSig = null;
    draw(sig, () => (mode === "done" ? doneView() : mode === "restored" ? restoredView() : mainView(s)));
  }
  const focusHeading = () => { const el = holder.querySelector("h1"); if (el) el.focus({ preventScroll: true }); };

  // ---------- main ----------
  function mainView(s) {
    if (!s.walletsEnabled) {
      return h("div", { class: "stack" }, pageHead("Wallet"), errorState(s.walletError || "The node started without its wallet subsystem.", null, "Wallets are unavailable on this node"));
    }
    if (s.wallets === null) {
      if (s.walletsError) return h("div", { class: "stack" }, pageHead("Wallet"), errorState(s.walletsError, () => loadWallets().then(emit), "Could not load wallets"));
      return h("div", { class: "stack" }, pageHead("Wallet"), skeletonRows(3));
    }
    if (!s.wallets.length) return emptyWallet();
    const w = selectedWallet();
    const net = s.info && s.info.network;
    return h("div", { class: "stack" },
      pageHead("Wallet", "Your balance and receiving address.",
        h("button", { type: "button", class: "btn", onclick: () => go("create") }, icon("plus", { size: 16 }), "New wallet"),
        h("button", { type: "button", class: "btn ghost", onclick: () => go("restore") }, "Restore")),
      h("div", { class: "grid cols-main" },
        h("div", { class: "stack" },
          card("Balance", h("div", { class: "stack" },
            h("div", { class: "balance", "aria-label": `Balance ${fmtBucks(w.balance)} BUCKS` }, fmtBucks(w.balance), h("small", null, "BUCKS")),
            balanceParts(w),
            h("div", { class: "row actions" }, h("a", { class: "btn primary", href: "#/send" }, icon("send", { size: 16 }), "Send"), h("a", { class: "btn", href: "#/history" }, icon("history", { size: 16 }), "History"))), { cls: "accent" }),
          s.wallets.length > 1 && card("Wallets on this node", h("div", { class: "list" }, s.wallets.map((x) =>
            h("button", { type: "button", class: "list-row tx-row btn ghost", style: "text-align:left;justify-content:flex-start;min-height:56px", "aria-pressed": x.address === w.address, onclick: () => selectWallet(x.address) },
              h("span", { class: "mono wrap" }, x.address, x.address === w.address && h("span", { class: "pill ok", style: "margin-left:8px" }, "selected")),
              h("span", { class: "num" }, `${fmtBucks(x.balance, { trim: true })}`),
              h("span", { class: "sub hide-sm" }, "BUCKS"))))),
          notice("info", "Your keys are kept by this node. Back up your recovery phrase; the node cannot show it again.")),
        card("Receive", receive(w.address, net))));
  }

  // What of the balance can be spent now (ONB-07). Older nodes send only "balance".
  function balanceParts(w) {
    if (w.spendable == null) return h("p", { class: "muted", style: "margin:0;font-size:.88rem" }, `Includes newly mined rewards, which can be spent only after ${COINBASE_MATURITY} further blocks.`);
    const amount = (v) => `${fmtBucks(v, { trim: true })} BUCKS`;
    return kv([
      ["Spendable now", h("strong", null, amount(w.spendable))],
      BigInt(w.immature || 0) > 0n && ["Maturing rewards", h("span", null, amount(w.immature), h("span", { class: "sub", style: "display:block" }, `Mining rewards can be spent ${COINBASE_MATURITY} blocks after they were mined.`))],
      BigInt(w.locked || 0) > 0n && ["In pending payments", h("span", null, amount(w.locked), h("span", { class: "sub", style: "display:block" }, "Coins spent by a payment that is not in a block yet. Its change comes back when it is."))],
      BigInt(w.incoming || 0) > 0n && ["Incoming", h("span", null, amount(w.incoming), h("span", { class: "sub", style: "display:block" }, "Not confirmed yet; not part of the balance until it is in a block."))],
    ]);
  }

  function receive(address, net) {
    const qrWrap = h("div", { class: "qr", role: "img", "aria-label": `QR code of the address ${address}` });
    try {
      const doc = new DOMParser().parseFromString(toSvg(address), "image/svg+xml");
      qrWrap.append(document.importNode(doc.documentElement, true));
    } catch (e) {
      qrWrap.replaceWith(notice("error", "Could not draw the QR code. Use the address below."));
    }
    return h("div", { class: "stack", style: "justify-items:center;text-align:center" },
      qrWrap,
      h("div", { style: "width:100%;text-align:left" },
        h("div", { class: "label", style: "margin-bottom:6px" }, `Your ${net ? (NETWORKS[net] ? NETWORKS[net].label.toLowerCase() : net) : ""} address`),
        h("div", { class: "addr", id: "my-address" }, address)),
      h("div", { class: "row" }, copyButton(address, "Copy address", { what: "address" })),
      h("p", { class: "muted", style: "font-size:.85rem;margin:0" }, "Only send BUCKS from the same network. Addresses from other networks will not work."));
  }

  function emptyWallet() {
    return h("div", { class: "stack" },
      pageHead("Wallet", "A wallet holds the keys that control your coins."),
      card(null, h("div", { class: "state empty", style: "border:0;padding:8px" },
        icon("wallet", { size: 36 }),
        h("div", null, h("strong", { style: "font-size:1.15rem" }, "This node has no wallet yet"),
          h("p", null, "Create a new wallet, or restore one from its recovery phrase. You need a wallet to receive coins, send them, or mine.")),
        h("div", { class: "row actions" },
          h("button", { type: "button", class: "btn primary lg", onclick: () => go("create") }, icon("plus", { size: 18 }), "Create a new wallet"),
          h("button", { type: "button", class: "btn lg", onclick: () => go("restore") }, "Restore from phrase")))));
  }

  const devNotice = () => notice("warn", h("strong", null, "Development word list. "), "This node uses a placeholder word list, not the final release list. A phrase made now may not restore a wallet once the release list ships. Use it only for testing.");

  const draftNotice = () => notice("warn", h("strong", null, "Draft word lists. "), "The Arabic and English word lists have not been reviewed yet. They are fine for testing, but do not rely on a phrase made now for coins that matter until the final lists ship.");

  // ---------- create ----------
  function createView() {
    const err = h("div");
    const devWarn = h("div");
    const lang = h("select", { id: "w-lang" }, h("option", { value: "" }, "Default"));
    const langField = field({ id: "w-lang", label: "Save my recovery phrase as", hint: "Both versions restore the same wallet. Write down only one.", input: lang });
    let words = 13;
    const seg = h("div", { class: "seg", role: "radiogroup", "aria-label": "Phrase length" },
      [[13, "13 words"], [25, "25 words"]].map(([n, label]) => h("label", null,
        h("input", { type: "radio", name: "words", value: n, checked: n === 13, onchange: () => { words = n; } }), h("span", null, label))));
    if (!langs) {
      get("/api/wallets/languages").then((r) => { langs = r; fillLangs(); }).catch(() => { langField.hidden = true; });
    } else fillLangs();
    function fillLangs() {
      // Users choose between exactly two ways to keep their phrase: Arabic or English
      // The placeholder development list is never offered.
      const offered = ((langs && langs.languages) || []).filter((l) => l.status !== "dev");
      const arabic = offered.find((l) => l.normalizer === "arabic");
      const english = offered.find((l) => l.normalizer === "latin");
      const choices = [arabic && { lang: arabic.lang, label: "Arabic", status: arabic.status },
                       english && { lang: english.lang, label: "English", status: english.status }].filter(Boolean);
      if (choices.length < 2) {
        // Only the placeholder list exists on this node: keep the old behaviour (default list, with its warning).
        const def = langs && langs.languages && langs.languages.find((l) => l.lang === langs.default);
        devWarn.replaceChildren(...(def && def.status === "dev" ? [devNotice()] : []));
        langField.hidden = true;
        return;
      }
      devWarn.replaceChildren(...(choices.some((c) => c.status === "draft") ? [draftNotice()] : []));
      lang.replaceChildren(...choices.map((c, i) => h("option", { value: c.lang, selected: i === 0 }, c.label)));
      langField.hidden = false;
    }
    const submit = h("button", { type: "submit", class: "btn primary lg" }, "Create wallet");
    const form = h("form", { class: "card", novalidate: true },
      h("div", { class: "stack" },
        devWarn,
        notice("warn", h("strong", null, "You will see a recovery phrase once. "), "Anyone with it controls your coins and nobody, including this node, can recover it for you. Have pen and paper ready and make sure nobody is watching your screen."),
        h("div", { class: "field" }, h("div", { class: "label" }, "Phrase length"), seg, h("div", { class: "hint" }, "13 words is the standard. 25 words gives extra strength.")),
        langField, err,
        h("div", { class: "row actions" }, submit, h("button", { type: "button", class: "btn ghost", onclick: () => go("main") }, "Cancel"))));
    form.addEventListener("submit", async (e) => {
      e.preventDefault();
      err.replaceChildren();
      busy(submit, true, "Creating…");
      try {
        const body = { words };
        if (!langField.hidden && lang.value) {
          body.language = lang.value;
          body.includeCanonical = true;   // for English: also return the Arabic words (ignored for Arabic)
        }
        const r = await post("/api/wallets/create", body);
        const used = langs && langs.languages && langs.languages.find((l) => l.lang === r.language);
        pending = { dev: !!(used && used.status === "dev"), draft: !!(used && used.status === "draft"), words: String(r.mnemonic).trim().split(/\s+/), direction: r.direction === "rtl" ? "rtl" : "ltr", canonical: r.canonicalMnemonic ? String(r.canonicalMnemonic).trim().split(/\s+/) : null, address: r.wallet && r.wallet.address, persisted: r.persisted !== false, saveError: r.error || "" };
        r.mnemonic = null; r.canonicalMnemonic = null;
        await loadWallets(false);
        if (pending && pending.address) selectWallet(pending.address);
        go("phrase");
      } catch (ex) {
        err.replaceChildren(notice("error", createError(ex)));
        submit.disabled = false; submit.textContent = "Create wallet";
      }
    });
    return h("div", { class: "stack" }, pageHead("Create a wallet", "A new key pair is generated on the node."), form);
  }

  function createError(ex) {
    if (ex instanceof ApiError && ex.offline) return "Cannot reach the node. Nothing was created; try again when it is back.";
    if (ex instanceof ApiError && ex.status === 503) return `Wallets are unavailable on this node: ${ex.message}`;
    return ex.message;
  }

  // ---------- phrase: show once, then confirm ----------
  function phraseView() {
    if (!pending) { setTimeout(() => go("main"), 0); return h("div"); }
    const p = pending;
    let revealed = false;
    let step = 1; // 1 write down, 2 verify
    const wrap = h("div", { class: "stack" });
    const idx = pickIndexes(p.words.length);

    function paint() {
      wrap.replaceChildren();
      append(wrap, [
        h("div", { class: "steps", "aria-label": "Steps" },
          h("span", { class: `pill${step === 1 ? " current" : ""}` }, "1  Write it down"), h("span", { class: `pill${step === 2 ? " current" : ""}` }, "2  Check it")),
        !p.persisted && notice("error", h("strong", null, "The wallet could not be saved to disk. "), p.saveError || "It exists in memory only and will be lost when the node stops."),
        p.dev && devNotice(),
        p.draft && draftNotice(),
        step === 1 ? step1() : step2()]);
    }
    function step1() {
      const list = h("ol", { class: `words${revealed ? "" : " hidden"}`, dir: p.direction, lang: p.direction === "rtl" ? "ar" : null, "aria-label": "Recovery phrase" },
        p.words.map((w, i) => h("li", null, h("span", { class: "n" }, `${i + 1}.`), h("span", null, w))));
      const ack = h("input", { type: "checkbox", id: "ack" });
      const next = h("button", { type: "button", class: "btn primary lg", disabled: true, onclick: () => { step = 2; paint(); focusHeading(); } }, "Continue");
      ack.addEventListener("change", () => { next.disabled = !ack.checked; });
      return card(null, h("div", { class: "stack" },
        notice("warn", h("strong", null, `Write these ${p.words.length} words on paper, in order. `), "This is the only time they are shown. Do not screenshot them, email them or store them in a notes app."),
        list,
        p.canonical && h("div", { class: "stack" },
          h("p", { class: "muted", style: "margin:0" }, "The same phrase in the base Arabic list. Either version restores this wallet; you only need to keep one."),
          h("ol", { class: `words${revealed ? "" : " hidden"}`, dir: "rtl", lang: "ar", "aria-label": "Recovery phrase in the base Arabic list" },
            p.canonical.map((w, i) => h("li", null, h("span", { class: "n" }, `${i + 1}.`), h("span", null, w))))),
        h("div", { class: "row" },
          h("button", { type: "button", class: "btn", "aria-pressed": revealed, onclick: () => { revealed = !revealed; paint(); } }, icon(revealed ? "eyeoff" : "eye", { size: 16 }), revealed ? "Hide words" : "Reveal words")),
        h("label", { class: "check", for: "ack" }, ack, h("span", null, "I have written down all the words in order and stored the paper somewhere safe.")),
        h("div", { class: "row actions" }, next)));
    }
    function step2() {
      const inputs = idx.map((i, k) => h("input", { id: `chk-${k}`, type: "text", autocomplete: "off", autocapitalize: "off", spellcheck: "false", dir: p.direction }));
      const err = h("div", { role: "alert" });
      const form = h("form", { novalidate: true, class: "stack" },
        h("p", { style: "margin:0" }, "Type the words below from your paper to prove you wrote them down correctly."),
        idx.map((i, k) => field({ id: `chk-${k}`, label: `Word number ${i + 1}`, input: inputs[k] })),
        err,
        h("div", { class: "row actions" },
          h("button", { type: "submit", class: "btn primary lg" }, "Confirm and finish"),
          h("button", { type: "button", class: "btn ghost", onclick: () => { step = 1; paint(); } }, "Back to the words")));
      form.addEventListener("submit", (e) => {
        e.preventDefault();
        const ok = idx.every((i, k) => inputs[k].value.trim().normalize("NFC").toLowerCase() === p.words[i].normalize("NFC").toLowerCase());
        if (!ok) { err.replaceChildren(notice("error", "Those words do not match. Look at your paper (or go back to the words) and try again.")); return; }
        pending = null; // drop the phrase from memory
        go("done");
      });
      return card("Check your backup", form);
    }
    paint();
    return h("div", { class: "stack" }, pageHead("Your recovery phrase", p.address ? `For wallet ${p.address}` : undefined), wrap);
  }

  function pickIndexes(n) {
    const a = new Set();
    const buf = new Uint32Array(8);
    crypto.getRandomValues(buf);
    for (const v of buf) { if (a.size < 2) a.add(v % n); }
    while (a.size < 2) a.add((a.size + 1) % n);
    return [...a].sort((x, y) => x - y);
  }

  function restoredView() {
    if (!restored) { setTimeout(() => go("main"), 0); return h("div"); }
    const r = restored;
    return h("div", { class: "stack" }, pageHead("Wallet restored", r.address ? `Wallet ${r.address}` : undefined),
      card(null, h("div", { class: "stack" },
        notice("ok", `The phrase you entered was recognised as ${r.from}. This is the same phrase in the base Arabic list, for checking.`),
        h("ol", { class: "words", dir: "rtl", lang: "ar", "aria-label": "Recovery phrase in the base Arabic list" },
          r.words.map((w, i) => h("li", null, h("span", { class: "n" }, `${i + 1}.`), h("span", null, w)))),
        h("div", { class: "row actions" }, h("button", { class: "btn primary", type: "button", onclick: () => { restored = null; go("main"); } }, "Done")))));
  }

  function doneView() {
    return h("div", { class: "stack" }, pageHead("Wallet ready"),
      card(null, h("div", { class: "stack" },
        notice("ok", "Your wallet was created and your backup is confirmed. The phrase has been cleared from this page."),
        h("div", { class: "row actions" }, h("button", { class: "btn primary", type: "button", onclick: () => go("main") }, "Go to my wallet")))));
  }

  // ---------- restore ----------
  function restoreView() {
    const ta = h("textarea", { id: "r-phrase", rows: 4, autocomplete: "off", autocapitalize: "off", autocorrect: "off", spellcheck: "false", "aria-describedby": "r-phrase-hint r-phrase-err" });
    const pass = h("input", { id: "r-pass", type: "password", autocomplete: "off" });
    const err = h("div");
    const submit = h("button", { type: "submit", class: "btn primary lg" }, "Restore wallet");
    const phraseField = field({ id: "r-phrase", label: "Recovery phrase", hint: "Type or paste the words separated by spaces. They are sent only to your own node.", input: ta });
    const form = h("form", { class: "card", novalidate: true },
      h("div", { class: "stack" }, phraseField,
        h("details", null, h("summary", { style: "cursor:pointer;margin-bottom:10px" }, "Advanced: passphrase"), field({ id: "r-pass", label: "Passphrase (optional)", hint: "Leave empty unless you set a passphrase when you created this wallet.", input: pass })),
        err,
        h("div", { class: "row actions" }, submit, h("button", { type: "button", class: "btn ghost", onclick: () => go("main") }, "Cancel"))));
    form.addEventListener("submit", async (e) => {
      e.preventDefault();
      err.replaceChildren();
      const phrase = ta.value.trim().replace(/\s+/g, " ");
      if (!phrase) { err.replaceChildren(notice("error", "Enter your recovery phrase.")); ta.focus(); return; }
      busy(submit, true, "Restoring…");
      try {
        const r = await post("/api/wallets/restore", { mnemonic: phrase, passphrase: pass.value, includeCanonical: true });
        ta.value = ""; pass.value = "";
        await loadWallets(false);
        if (r.wallet) selectWallet(r.wallet.address);
        toast(r.existing ? "That wallet was already on this node." : "Wallet restored.", "ok");
        if (r.persisted === false) toast("Warning: the wallet could not be saved to disk.", "error", 8000);
        if (r.canonicalMnemonic && r.detectedLanguage && r.detectedLanguage !== r.canonicalLanguage) {
          restored = { from: r.detectedLanguage, words: String(r.canonicalMnemonic).trim().split(/\s+/), address: r.wallet && r.wallet.address };
          r.canonicalMnemonic = null;
          go("restored");
          return;
        }
        go("main");
      } catch (ex) {
        err.replaceChildren(notice("error", ex instanceof ApiError && ex.offline ? "Cannot reach the node." : ex.status === 400 ? `The node rejected the phrase: ${ex.message}` : ex.message));
        submit.disabled = false; submit.textContent = "Restore wallet";
      }
    });
    return h("div", { class: "stack" }, pageHead("Restore a wallet", "Rebuild a wallet on this node from its recovery phrase."), form);
  }

  render(state);
  return { update: render, destroy() {} };
}
