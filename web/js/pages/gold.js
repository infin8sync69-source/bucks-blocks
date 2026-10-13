import { h, icon, card, kv, pageHead, keyedRender, skeletonRows, notice } from "../ui.js";
import { fmtBucks } from "../util.js";
import { loadEconomy } from "../store.js";

export function mount(root) {
  const body = h("div", { class: "stack" });
  root.append(pageHead("Gold reserve", "Status of the gold reserve module on this network."), body);
  const draw = keyedRender(body);
  loadEconomy();
  return {
    update(s) {
      draw([s.economy && [s.economy.supply, s.economy.gold_reserve], s.economyError], () => {
        const e = s.economy;
        const active = !!(e && Number(e.gold_reserve) > 0);
        return h("div", { class: "stack" },
          card(null, h("div", { class: "gold-hero" },
            h("div", { class: "coin" }, icon("gold", { size: 30 })),
            h("div", null, h("span", { class: "pill warn" }, active ? "Reported by node, unaudited" : "Not yet active")),
            h("h2", { style: "font-size:1.35rem;margin:14px 0 8px" }, active ? "The node reports a gold reserve figure" : "The gold reserve is not part of this launch"),
            h("p", { class: "muted", style: "max-width:560px;margin:0 auto" },
              active
                ? "This number is read from markers recorded on the chain by the vault signers. The node cannot verify physical gold, so treat it as a claim, not proof."
                : "This network does not yet record or verify any gold reserve. There is no coverage figure, no gold price and no way to redeem BUCKS for gold. Nothing on this page should be read as backing."))),
          h("div", { class: "grid cols-2" },
            card("What is live today", kv([
              ["Circulating supply", e ? `${fmtBucks(e.supply, { trim: true })} BUCKS` : s.economyError ? "unavailable" : "…"],
              ["Gold reserve module", h("span", { class: "pill" }, "Not active")],
              ["Reserve figure", active ? `${e.gold_reserve} g (reported)` : "none recorded"],
            ])),
            card("What this means", h("div", { class: "stack" },
              h("p", { style: "margin:0" }, "BUCKS are produced by proof-of-work mining. They are not claims on gold and are not redeemable for gold."),
              h("p", { style: "margin:0" }, "If a reserve is introduced later, this page will show what the chain records, labelled as reported by the vault signers and not audited by the node.")))),
          s.economyError && notice("warn", `Could not read the economy status: ${s.economyError}`),
          !e && !s.economyError && skeletonRows(1));
      });
    },
    destroy() {},
  };
}
