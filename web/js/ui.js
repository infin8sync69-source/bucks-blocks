// Tiny DOM helpers, inline SVG icons, toasts, clipboard, shared widgets.
import { NETWORKS } from "./util.js";

const SVG_NS = "http://www.w3.org/2000/svg";

/** h("div", {class:"x", onclick: fn, "aria-label":"y"}, child, "text", [nested]) */
export function h(tag, props, ...kids) {
  const el = document.createElement(tag);
  if (props) {
    for (const [k, v] of Object.entries(props)) {
      if (v === undefined || v === null || v === false) continue;
      if (k === "class") el.className = v;
      else if (k === "dataset") Object.assign(el.dataset, v);
      else if (k === "style") el.style.cssText = v; // CSSOM, allowed under style-src 'self' (a style attribute is not)
      else if (k.startsWith("on") && typeof v === "function") el.addEventListener(k.slice(2).toLowerCase(), v);
      else if (k === "value" || k === "checked" || k === "disabled" || k === "selected") el[k] = v;
      else el.setAttribute(k, v === true ? "" : String(v));
    }
  }
  append(el, kids);
  return el;
}

export function append(el, kids) {
  for (const k of kids.flat(Infinity)) {
    if (k === null || k === undefined || k === false) continue;
    el.append(k instanceof Node ? k : document.createTextNode(String(k)));
  }
  return el;
}

const ICONS = {
  overview: "M3 13h8V3H3v10zm0 8h8v-6H3v6zm10 0h8V11h-8v10zm0-18v6h8V3h-8z",
  blocks: "M12 2 3 7v10l9 5 9-5V7l-9-5zm0 2.2 6.4 3.5L12 11.2 5.6 7.7 12 4.2zM5 9.4l6 3.3v6.6l-6-3.3V9.4zm8 9.9v-6.6l6-3.3v6.6l-6 3.3z",
  wallet: "M3 7a2 2 0 0 1 2-2h13v3h1a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7zm2 0v1h11V7H5zm12 6a1.3 1.3 0 1 0 0 2.6 1.3 1.3 0 0 0 0-2.6z",
  send: "M3.4 20.4 21 12 3.4 3.6 3.3 10l12.6 2-12.6 2z",
  history: "M13 3a9 9 0 1 0 9 9h-2a7 7 0 1 1-2.05-4.95L15 9h7V2l-2.64 2.64A8.96 8.96 0 0 0 13 3zm-1 5v5l4.3 2.5.8-1.4-3.6-2.1V8H12z",
  mining: "M14.7 3.3 13 5l1.8 1.8L9 12.6l-1.4-1.4-2.1 2.1 5.2 5.2 2.1-2.1-1.4-1.4 5.8-5.8L18.8 11l1.7-1.7-5.8-6zM4 20h8v2H4z",
  gold: "M12 2 4 8l2 12h12l2-12-8-6zm0 2.5L17.2 8.5 16.4 14H7.6l-.8-5.5L12 4.5zM8 16h8l.4 2H7.6L8 16z",
  copy: "M16 1H4a2 2 0 0 0-2 2v14h2V3h12V1zm3 4H8a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h11a2 2 0 0 0 2-2V7a2 2 0 0 0-2-2zm0 16H8V7h11v14z",
  check: "M9 16.2 4.8 12l-1.4 1.4L9 19 21 7l-1.4-1.4L9 16.2z",
  alert: "M12 2 1 21h22L12 2zm1 15h-2v-2h2v2zm0-4h-2V9h2v4z",
  info: "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zm1 15h-2v-6h2v6zm0-8h-2V7h2v2z",
  lock: "M18 8h-1V6a5 5 0 0 0-10 0v2H6a2 2 0 0 0-2 2v10a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V10a2 2 0 0 0-2-2zM9 6a3 3 0 0 1 6 0v2H9V6z",
  logout: "M10 17v-3H3v-4h7V7l5 5-5 5zm9-14H12v2h7v14h-7v2h7a2 2 0 0 0 2-2V5a2 2 0 0 0-2-2z",
  refresh: "M17.65 6.35A7.96 7.96 0 0 0 12 4a8 8 0 1 0 7.74 10h-2.08A6 6 0 1 1 12 6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z",
  plus: "M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z",
  eye: "M12 5C7 5 2.7 8.1 1 12.5 2.7 16.9 7 20 12 20s9.3-3.1 11-7.5C21.3 8.1 17 5 12 5zm0 12.5a5 5 0 1 1 0-10 5 5 0 0 1 0 10zm0-8a3 3 0 1 0 0 6 3 3 0 0 0 0-6z",
  eyeoff: "M12 7a5 5 0 0 1 5 5c0 .65-.13 1.26-.36 1.83l2.92 2.92A11.8 11.8 0 0 0 23 12.5C21.3 8.1 17 5 12 5c-1.4 0-2.74.25-3.98.7l2.16 2.16C10.74 7.13 11.35 7 12 7zM2 4.27l2.28 2.28A11.8 11.8 0 0 0 1 12.5C2.7 16.9 7 20 12 20c1.55 0 3.03-.3 4.38-.84l.42.42L19.73 22 21 20.73 3.27 3 2 4.27zM7.53 9.8l1.55 1.55A3 3 0 0 0 9 12a3 3 0 0 0 3 3c.22 0 .44-.03.65-.08l1.55 1.55c-.67.33-1.41.53-2.2.53a5 5 0 0 1-5-5c0-.79.2-1.53.53-2.2z",
  arrow: "M12 4l-1.41 1.41L16.17 11H4v2h12.17l-5.58 5.59L12 20l8-8-8-8z",
  chevl: "M15.41 7.41 14 6l-6 6 6 6 1.41-1.41L10.83 12z",
  chevr: "M10 6 8.59 7.41 13.17 12l-4.58 4.59L10 18l6-6z",
  down: "M7 10l5 5 5-5z",
  stop: "M6 6h12v12H6z",
  play: "M8 5v14l11-7z",
  shield: "M12 1 3 5v6c0 5.55 3.84 10.74 9 12 5.16-1.26 9-6.45 9-12V5l-9-4z",
  link: "M3.9 12a3.1 3.1 0 0 1 3.1-3.1h4V7H7a5 5 0 0 0 0 10h4v-1.9H7A3.1 3.1 0 0 1 3.9 12zM8 13h8v-2H8v2zm9-6h-4v1.9h4a3.1 3.1 0 0 1 0 6.2h-4V17h4a5 5 0 0 0 0-10z",
};

export function icon(name, { size = 18, cls = "" } = {}) {
  const svg = document.createElementNS(SVG_NS, "svg");
  svg.setAttribute("viewBox", "0 0 24 24");
  svg.setAttribute("width", size);
  svg.setAttribute("height", size);
  svg.setAttribute("aria-hidden", "true");
  svg.setAttribute("focusable", "false");
  svg.setAttribute("class", `icon ${cls}`.trim());
  const p = document.createElementNS(SVG_NS, "path");
  p.setAttribute("d", ICONS[name] || ICONS.info);
  p.setAttribute("fill", "currentColor");
  svg.append(p);
  return svg;
}

// ---- toasts (aria-live) ----
export function toast(message, kind = "info", ms = 3500) {
  const host = document.getElementById("toasts");
  if (!host) return;
  const t = h("div", { class: `toast ${kind}`, role: kind === "error" ? "alert" : "status" }, icon(kind === "error" ? "alert" : kind === "ok" ? "check" : "info"), h("span", null, message));
  host.append(t);
  setTimeout(() => t.remove(), ms);
}

// ---- clipboard ----
export async function copyText(text) {
  try {
    if (navigator.clipboard && window.isSecureContext) {
      await navigator.clipboard.writeText(text);
      return true;
    }
  } catch { /* fall through */ }
  try {
    const ta = h("textarea", { "aria-hidden": "true", tabindex: "-1", readonly: "", style: "position:fixed;left:-9999px;top:0" });
    ta.value = text;
    document.body.append(ta);
    ta.select();
    const ok = document.execCommand("copy");
    ta.remove();
    return ok;
  } catch { return false; }
}

/** A button that copies `text` and flips to "Copied" for a moment. */
export function copyButton(text, label = "Copy", { what = "value", compact = false } = {}) {
  const lab = h("span", null, compact ? "" : label);
  const btn = h("button", { type: "button", class: `btn ghost sm${compact ? " iconly" : ""}`, "aria-label": `Copy ${what}` }, icon("copy", { size: 15 }), lab);
  let timer;
  btn.addEventListener("click", async () => {
    const value = typeof text === "function" ? text() : text;
    const ok = await copyText(value);
    clearTimeout(timer);
    btn.replaceChildren(icon(ok ? "check" : "alert", { size: 15 }), h("span", null, compact ? "" : ok ? "Copied" : "Copy failed"));
    btn.classList.toggle("copied", ok);
    toast(ok ? `Copied ${what}` : "Could not copy. Select the text and copy it manually.", ok ? "ok" : "error", 2000);
    timer = setTimeout(() => {
      btn.replaceChildren(icon("copy", { size: 15 }), h("span", null, compact ? "" : label));
      btn.classList.remove("copied");
    }, 1800);
  });
  return btn;
}

// ---- shared widgets ----
export function netBadge(network) {
  const n = NETWORKS[network];
  return h("span", { class: `net-badge ${n ? n.cls : "net-unknown"}`, title: "Network this node is running on" }, n ? n.label : "UNKNOWN NETWORK");
}

export function card(title, body, { actions, cls = "" } = {}) {
  return h("section", { class: `card ${cls}` },
    (title || actions) && h("header", { class: "card-head" }, title && h("h2", null, title), actions && h("div", { class: "card-actions" }, actions)),
    body);
}

export function stat(label, value, { sub, mono = false, tone } = {}) {
  return h("div", { class: `stat${tone ? ` tone-${tone}` : ""}` },
    h("div", { class: "stat-label" }, label),
    h("div", { class: `stat-value${mono ? " mono" : ""}` }, value),
    sub && h("div", { class: "stat-sub" }, sub));
}

export function kv(rows) {
  return h("dl", { class: "kv" }, rows.filter(Boolean).flatMap(([k, v, opts = {}]) => [
    h("dt", null, k),
    h("dd", { class: opts.mono ? "mono wrap" : "" }, v),
  ]));
}

export function spinner(label = "Loading…") {
  return h("div", { class: "state loading", role: "status" }, h("span", { class: "spin", "aria-hidden": "true" }), h("span", null, label));
}

export function skeletonRows(n = 3) {
  return h("div", { class: "skeleton-list", "aria-hidden": "true" }, Array.from({ length: n }, () => h("div", { class: "skeleton" })));
}

export function errorState(message, onRetry, title = "Something went wrong") {
  return h("div", { class: "state error", role: "alert" },
    icon("alert", { size: 22 }),
    h("div", null, h("strong", null, title), h("p", null, message)),
    onRetry && h("button", { type: "button", class: "btn", onclick: onRetry }, icon("refresh", { size: 16 }), "Try again"));
}

export function emptyState(title, text, ...actions) {
  return h("div", { class: "state empty" },
    h("div", null, h("strong", null, title), text && h("p", null, text)),
    actions.length ? h("div", { class: "row" }, actions) : null);
}

export function notice(kind, ...kids) {
  return h("div", { class: `notice ${kind}`, role: kind === "error" ? "alert" : undefined },
    icon(kind === "error" || kind === "warn" ? "alert" : kind === "ok" ? "check" : "info", { size: 18 }),
    h("div", null, kids));
}

export function hashLink(hash, { head = 10, tail = 8 } = {}) {
  return h("a", { class: "mono", href: `#/blocks/${hash}`, title: hash }, hash.length > head + tail + 1 ? `${hash.slice(0, head)}…${hash.slice(-tail)}` : hash);
}

/** A form field with a real <label>, optional hint and error line. */
export function field({ id, label, hint, error, input }) {
  return h("div", { class: `field${error ? " invalid" : ""}` },
    h("label", { for: id }, label),
    input,
    hint && h("div", { class: "hint", id: `${id}-hint` }, hint),
    h("div", { class: "field-error", id: `${id}-err`, role: "alert" }, error || ""));
}

export function pageHead(title, subtitle, ...actions) {
  return h("div", { class: "page-head" },
    h("div", null, h("h1", { tabindex: "-1" }, title), subtitle && h("p", null, subtitle)),
    actions.length ? h("div", { class: "row" }, actions) : null);
}

/** Replace children of `el` only when `sig` changed (keeps text selection while polling). */
export function keyedRender(el) {
  let last = null;
  return (sig, build) => {
    const s = JSON.stringify(sig);
    if (s === last) return;
    last = s;
    el.replaceChildren(build());
  };
}

export function busy(btn, on, label) {
  btn.disabled = on;
  if (on) {
    btn.dataset.label = btn.textContent;
    btn.replaceChildren(h("span", { class: "spin", "aria-hidden": "true" }), label || "Working…");
  }
}
