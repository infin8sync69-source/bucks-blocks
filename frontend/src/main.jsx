import React from 'react';
import ReactDOM from 'react-dom/client';
import './index.css';

window.React = React;
window.ReactDOM = ReactDOM;

// --- tweaks-panel.jsx ---
// @ds-adherence-ignore -- omelette starter scaffold (raw elements/hex/px by design)

/* BEGIN USAGE */
// tweaks-panel.jsx
// Reusable Tweaks shell + form-control helpers.
// Exports (to window): useTweaks, TweaksPanel, TweakSection, TweakRow, TweakSlider,
//   TweakToggle, TweakRadio, TweakSelect, TweakText, TweakNumber, TweakColor, TweakButton.
//
// Owns the host protocol (listens for __activate_edit_mode / __deactivate_edit_mode,
// posts __edit_mode_available / __edit_mode_set_keys / __edit_mode_dismissed) so
// individual prototypes don't re-roll it. Ships a consistent set of controls so you
// don't hand-draw <input type="range">, segmented radios, steppers, etc.
//
// Usage (in an HTML file that loads React + Babel):
//
//   const TWEAK_DEFAULTS = /*EDITMODE-BEGIN*/{
//     "primaryColor": "#D97757",
//     "palette": ["#D97757", "#29261b", "#f6f4ef"],
//     "fontSize": 16,
//     "density": "regular",
//     "dark": false
//   }/*EDITMODE-END*/;
//
//   function App() {
//     const [t, setTweak] = useTweaks(TWEAK_DEFAULTS);
//     return (
//       <div style={{ fontSize: t.fontSize, color: t.primaryColor }}>
//         Hello
//         <TweaksPanel>
//           <TweakSection label="Typography" />
//           <TweakSlider label="Font size" value={t.fontSize} min={10} max={32} unit="px"
//                        onChange={(v) => setTweak('fontSize', v)} />
//           <TweakRadio  label="Density" value={t.density}
//                        options={['compact', 'regular', 'comfy']}
//                        onChange={(v) => setTweak('density', v)} />
//           <TweakSection label="Theme" />
//           <TweakColor  label="Primary" value={t.primaryColor}
//                        options={['#D97757', '#2A6FDB', '#1F8A5B', '#7A5AE0']}
//                        onChange={(v) => setTweak('primaryColor', v)} />
//           <TweakColor  label="Palette" value={t.palette}
//                        options={[['#D97757', '#29261b', '#f6f4ef'],
//                                  ['#475569', '#0f172a', '#f1f5f9']]}
//                        onChange={(v) => setTweak('palette', v)} />
//           <TweakToggle label="Dark mode" value={t.dark}
//                        onChange={(v) => setTweak('dark', v)} />
//         </TweaksPanel>
//       </div>
//     );
//   }
//
// TweakRadio is the segmented control for 2–3 short options (auto-falls-back to
// TweakSelect past ~16/~10 chars per label); reach for TweakSelect directly when
// options are many or long. For color tweaks always curate 3-4 options rather than
// a free picker; an option can also be a whole 2–5 color palette (the stored value
// is the array). The Tweak* controls are a floor, not a ceiling — build custom
// controls inside the panel if a tweak calls for UI they don't cover.
/* END USAGE */
// ─────────────────────────────────────────────────────────────────────────────

const __TWEAKS_STYLE = `
  .twk-panel{position:fixed;right:16px;bottom:16px;z-index:2147483646;width:280px;
    max-height:calc(100vh - 32px);display:flex;flex-direction:column;
    transform:scale(var(--dc-inv-zoom,1));transform-origin:bottom right;
    background:rgba(250,249,247,.78);color:#29261b;
    -webkit-backdrop-filter:blur(24px) saturate(160%);backdrop-filter:blur(24px) saturate(160%);
    border:.5px solid rgba(255,255,255,.6);border-radius:14px;
    box-shadow:0 1px 0 rgba(255,255,255,.5) inset,0 12px 40px rgba(0,0,0,.18);
    font:11.5px/1.4 ui-sans-serif,system-ui,-apple-system,sans-serif;overflow:hidden}
  .twk-hd{display:flex;align-items:center;justify-content:space-between;
    padding:10px 8px 10px 14px;cursor:move;user-select:none}
  .twk-hd b{font-size:12px;font-weight:600;letter-spacing:.01em}
  .twk-x{appearance:none;border:0;background:transparent;color:rgba(41,38,27,.55);
    width:22px;height:22px;border-radius:6px;cursor:default;font-size:13px;line-height:1}
  .twk-x:hover{background:rgba(0,0,0,.06);color:#29261b}
  .twk-body{padding:2px 14px 14px;display:flex;flex-direction:column;gap:10px;
    overflow-y:auto;overflow-x:hidden;min-height:0;
    scrollbar-width:thin;scrollbar-color:rgba(0,0,0,.15) transparent}
  .twk-body::-webkit-scrollbar{width:8px}
  .twk-body::-webkit-scrollbar-track{background:transparent;margin:2px}
  .twk-body::-webkit-scrollbar-thumb{background:rgba(0,0,0,.15);border-radius:4px;
    border:2px solid transparent;background-clip:content-box}
  .twk-body::-webkit-scrollbar-thumb:hover{background:rgba(0,0,0,.25);
    border:2px solid transparent;background-clip:content-box}
  .twk-row{display:flex;flex-direction:column;gap:5px}
  .twk-row-h{flex-direction:row;align-items:center;justify-content:space-between;gap:10px}
  .twk-lbl{display:flex;justify-content:space-between;align-items:baseline;
    color:rgba(41,38,27,.72)}
  .twk-lbl>span:first-child{font-weight:500}
  .twk-val{color:rgba(41,38,27,.5);font-variant-numeric:tabular-nums}

  .twk-sect{font-size:10px;font-weight:600;letter-spacing:.06em;text-transform:uppercase;
    color:rgba(41,38,27,.45);padding:10px 0 0}
  .twk-sect:first-child{padding-top:0}

  .twk-field{appearance:none;box-sizing:border-box;width:100%;min-width:0;height:26px;padding:0 8px;
    border:.5px solid rgba(0,0,0,.1);border-radius:7px;
    background:rgba(255,255,255,.6);color:inherit;font:inherit;outline:none}
  .twk-field:focus{border-color:rgba(0,0,0,.25);background:rgba(255,255,255,.85)}
  select.twk-field{padding-right:22px;
    background-image:url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='10' height='6' viewBox='0 0 10 6'><path fill='rgba(0,0,0,.5)' d='M0 0h10L5 6z'/></svg>");
    background-repeat:no-repeat;background-position:right 8px center}

  .twk-slider{appearance:none;-webkit-appearance:none;width:100%;height:4px;margin:6px 0;
    border-radius:999px;background:rgba(0,0,0,.12);outline:none}
  .twk-slider::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;
    width:14px;height:14px;border-radius:50%;background:#fff;
    border:.5px solid rgba(0,0,0,.12);box-shadow:0 1px 3px rgba(0,0,0,.2);cursor:default}
  .twk-slider::-moz-range-thumb{width:14px;height:14px;border-radius:50%;
    background:#fff;border:.5px solid rgba(0,0,0,.12);box-shadow:0 1px 3px rgba(0,0,0,.2);cursor:default}

  .twk-seg{position:relative;display:flex;padding:2px;border-radius:8px;
    background:rgba(0,0,0,.06);user-select:none}
  .twk-seg-thumb{position:absolute;top:2px;bottom:2px;border-radius:6px;
    background:rgba(255,255,255,.9);box-shadow:0 1px 2px rgba(0,0,0,.12);
    transition:left .15s cubic-bezier(.3,.7,.4,1),width .15s}
  .twk-seg.dragging .twk-seg-thumb{transition:none}
  .twk-seg button{appearance:none;position:relative;z-index:1;flex:1;border:0;
    background:transparent;color:inherit;font:inherit;font-weight:500;min-height:22px;
    border-radius:6px;cursor:default;padding:4px 6px;line-height:1.2;
    overflow-wrap:anywhere}

  .twk-toggle{position:relative;width:32px;height:18px;border:0;border-radius:999px;
    background:rgba(0,0,0,.15);transition:background .15s;cursor:default;padding:0}
  .twk-toggle[data-on="1"]{background:#34c759}
  .twk-toggle i{position:absolute;top:2px;left:2px;width:14px;height:14px;border-radius:50%;
    background:#fff;box-shadow:0 1px 2px rgba(0,0,0,.25);transition:transform .15s}
  .twk-toggle[data-on="1"] i{transform:translateX(14px)}

  .twk-num{display:flex;align-items:center;box-sizing:border-box;min-width:0;height:26px;padding:0 0 0 8px;
    border:.5px solid rgba(0,0,0,.1);border-radius:7px;background:rgba(255,255,255,.6)}
  .twk-num-lbl{font-weight:500;color:rgba(41,38,27,.6);cursor:ew-resize;
    user-select:none;padding-right:8px}
  .twk-num input{flex:1;min-width:0;height:100%;border:0;background:transparent;
    font:inherit;font-variant-numeric:tabular-nums;text-align:right;padding:0 8px 0 0;
    outline:none;color:inherit;-moz-appearance:textfield}
  .twk-num input::-webkit-inner-spin-button,.twk-num input::-webkit-outer-spin-button{
    -webkit-appearance:none;margin:0}
  .twk-num-unit{padding-right:8px;color:rgba(41,38,27,.45)}

  .twk-btn{appearance:none;height:26px;padding:0 12px;border:0;border-radius:7px;
    background:rgba(0,0,0,.78);color:#fff;font:inherit;font-weight:500;cursor:default}
  .twk-btn:hover{background:rgba(0,0,0,.88)}
  .twk-btn.secondary{background:rgba(0,0,0,.06);color:inherit}
  .twk-btn.secondary:hover{background:rgba(0,0,0,.1)}

  .twk-swatch{appearance:none;-webkit-appearance:none;width:56px;height:22px;
    border:.5px solid rgba(0,0,0,.1);border-radius:6px;padding:0;cursor:default;
    background:transparent;flex-shrink:0}
  .twk-swatch::-webkit-color-swatch-wrapper{padding:0}
  .twk-swatch::-webkit-color-swatch{border:0;border-radius:5.5px}
  .twk-swatch::-moz-color-swatch{border:0;border-radius:5.5px}

  .twk-chips{display:flex;gap:6px}
  .twk-chip{position:relative;appearance:none;flex:1;min-width:0;height:46px;
    padding:0;border:0;border-radius:6px;overflow:hidden;cursor:default;
    box-shadow:0 0 0 .5px rgba(0,0,0,.12),0 1px 2px rgba(0,0,0,.06);
    transition:transform .12s cubic-bezier(.3,.7,.4,1),box-shadow .12s}
  .twk-chip:hover{transform:translateY(-1px);
    box-shadow:0 0 0 .5px rgba(0,0,0,.18),0 4px 10px rgba(0,0,0,.12)}
  .twk-chip[data-on="1"]{box-shadow:0 0 0 1.5px rgba(0,0,0,.85),
    0 2px 6px rgba(0,0,0,.15)}
  .twk-chip>span{position:absolute;top:0;bottom:0;right:0;width:34%;
    display:flex;flex-direction:column;box-shadow:-1px 0 0 rgba(0,0,0,.1)}
  .twk-chip>span>i{flex:1;box-shadow:0 -1px 0 rgba(0,0,0,.1)}
  .twk-chip>span>i:first-child{box-shadow:none}
  .twk-chip svg{position:absolute;top:6px;left:6px;width:13px;height:13px;
    filter:drop-shadow(0 1px 1px rgba(0,0,0,.3))}
`;

// ── useTweaks ───────────────────────────────────────────────────────────────
// Single source of truth for tweak values. setTweak persists via the host
// (__edit_mode_set_keys → host rewrites the EDITMODE block on disk).
function useTweaks(defaults) {
  const [values, setValues] = React.useState(defaults);
  // Accepts either setTweak('key', value) or setTweak({ key: value, ... }) so a
  // useState-style call doesn't write a "[object Object]" key into the persisted
  // JSON block.
  const setTweak = React.useCallback((keyOrEdits, val) => {
    const edits = typeof keyOrEdits === 'object' && keyOrEdits !== null
      ? keyOrEdits : { [keyOrEdits]: val };
    setValues((prev) => ({ ...prev, ...edits }));
    window.parent.postMessage({ type: '__edit_mode_set_keys', edits }, '*');
    // Same-window signal so in-page listeners (deck-stage rail thumbnails)
    // can react — the parent message only reaches the host, not peers.
    window.dispatchEvent(new CustomEvent('tweakchange', { detail: edits }));
  }, []);
  return [values, setTweak];
}

// ── TweaksPanel ─────────────────────────────────────────────────────────────
// Floating shell. Registers the protocol listener BEFORE announcing
// availability — if the announce ran first, the host's activate could land
// before our handler exists and the toolbar toggle would silently no-op.
// The close button posts __edit_mode_dismissed so the host's toolbar toggle
// flips off in lockstep; the host echoes __deactivate_edit_mode back which
// is what actually hides the panel.
function TweaksPanel({ title = 'Tweaks', children }) {
  const [open, setOpen] = React.useState(false);
  const dragRef = React.useRef(null);
  const offsetRef = React.useRef({ x: 16, y: 16 });
  const PAD = 16;

  const clampToViewport = React.useCallback(() => {
    const panel = dragRef.current;
    if (!panel) return;
    const w = panel.offsetWidth, h = panel.offsetHeight;
    const maxRight = Math.max(PAD, window.innerWidth - w - PAD);
    const maxBottom = Math.max(PAD, window.innerHeight - h - PAD);
    offsetRef.current = {
      x: Math.min(maxRight, Math.max(PAD, offsetRef.current.x)),
      y: Math.min(maxBottom, Math.max(PAD, offsetRef.current.y)),
    };
    panel.style.right = offsetRef.current.x + 'px';
    panel.style.bottom = offsetRef.current.y + 'px';
  }, []);

  React.useEffect(() => {
    if (!open) return;
    clampToViewport();
    if (typeof ResizeObserver === 'undefined') {
      window.addEventListener('resize', clampToViewport);
      return () => window.removeEventListener('resize', clampToViewport);
    }
    const ro = new ResizeObserver(clampToViewport);
    ro.observe(document.documentElement);
    return () => ro.disconnect();
  }, [open, clampToViewport]);

  React.useEffect(() => {
    const onMsg = (e) => {
      const t = e?.data?.type;
      if (t === '__activate_edit_mode') setOpen(true);
      else if (t === '__deactivate_edit_mode') setOpen(false);
    };
    window.addEventListener('message', onMsg);
    window.parent.postMessage({ type: '__edit_mode_available' }, '*');
    return () => window.removeEventListener('message', onMsg);
  }, []);

  const dismiss = () => {
    setOpen(false);
    window.parent.postMessage({ type: '__edit_mode_dismissed' }, '*');
  };

  const onDragStart = (e) => {
    const panel = dragRef.current;
    if (!panel) return;
    const r = panel.getBoundingClientRect();
    const sx = e.clientX, sy = e.clientY;
    const startRight = window.innerWidth - r.right;
    const startBottom = window.innerHeight - r.bottom;
    const move = (ev) => {
      offsetRef.current = {
        x: startRight - (ev.clientX - sx),
        y: startBottom - (ev.clientY - sy),
      };
      clampToViewport();
    };
    const up = () => {
      window.removeEventListener('mousemove', move);
      window.removeEventListener('mouseup', up);
    };
    window.addEventListener('mousemove', move);
    window.addEventListener('mouseup', up);
  };

  if (!open) return null;
  return (
    <>
      <style>{__TWEAKS_STYLE}</style>
      <div ref={dragRef} className="twk-panel" data-omelette-chrome=""
           style={{ right: offsetRef.current.x, bottom: offsetRef.current.y }}>
        <div className="twk-hd" onMouseDown={onDragStart}>
          <b>{title}</b>
          <button className="twk-x" aria-label="Close tweaks"
                  onMouseDown={(e) => e.stopPropagation()}
                  onClick={dismiss}>✕</button>
        </div>
        <div className="twk-body">
          {children}
        </div>
      </div>
    </>
  );
}

// ── Layout helpers ──────────────────────────────────────────────────────────

function TweakSection({ label, children }) {
  return (
    <>
      <div className="twk-sect">{label}</div>
      {children}
    </>
  );
}

function TweakRow({ label, value, children, inline = false }) {
  return (
    <div className={inline ? 'twk-row twk-row-h' : 'twk-row'}>
      <div className="twk-lbl">
        <span>{label}</span>
        {value != null && <span className="twk-val">{value}</span>}
      </div>
      {children}
    </div>
  );
}

// ── Controls ────────────────────────────────────────────────────────────────

function TweakSlider({ label, value, min = 0, max = 100, step = 1, unit = '', onChange }) {
  return (
    <TweakRow label={label} value={`${value}${unit}`}>
      <input type="range" className="twk-slider" min={min} max={max} step={step}
             value={value} onChange={(e) => onChange(Number(e.target.value))} />
    </TweakRow>
  );
}

function TweakToggle({ label, value, onChange }) {
  return (
    <div className="twk-row twk-row-h">
      <div className="twk-lbl"><span>{label}</span></div>
      <button type="button" className="twk-toggle" data-on={value ? '1' : '0'}
              role="switch" aria-checked={!!value}
              onClick={() => onChange(!value)}><i /></button>
    </div>
  );
}

function TweakRadio({ label, value, options, onChange }) {
  const trackRef = React.useRef(null);
  const [dragging, setDragging] = React.useState(false);
  // The active value is read by pointer-move handlers attached for the lifetime
  // of a drag — ref it so a stale closure doesn't fire onChange for every move.
  const valueRef = React.useRef(value);
  valueRef.current = value;

  // Segments wrap mid-word once per-segment width runs out. The track is
  // ~248px (280 panel − 28 body pad − 4 seg pad), each button loses 12px
  // to its own padding, and 11.5px system-ui averages ~6.3px/char — so 2
  // options fit ~16 chars each, 3 fit ~10. Past that (or >3 options), fall
  // back to a dropdown rather than wrap.
  const labelLen = (o) => String(typeof o === 'object' ? o.label : o).length;
  const maxLen = options.reduce((m, o) => Math.max(m, labelLen(o)), 0);
  const fitsAsSegments = maxLen <= ({ 2: 16, 3: 10 }[options.length] ?? 0);
  if (!fitsAsSegments) {
    // <select> emits strings — map back to the original option value so the
    // fallback stays type-preserving (numbers, booleans) like the segment path.
    const resolve = (s) => {
      const m = options.find((o) => String(typeof o === 'object' ? o.value : o) === s);
      return m === undefined ? s : typeof m === 'object' ? m.value : m;
    };
    return <TweakSelect label={label} value={value} options={options}
                        onChange={(s) => onChange(resolve(s))} />;
  }
  const opts = options.map((o) => (typeof o === 'object' ? o : { value: o, label: o }));
  const idx = Math.max(0, opts.findIndex((o) => o.value === value));
  const n = opts.length;

  const segAt = (clientX) => {
    const r = trackRef.current.getBoundingClientRect();
    const inner = r.width - 4;
    const i = Math.floor(((clientX - r.left - 2) / inner) * n);
    return opts[Math.max(0, Math.min(n - 1, i))].value;
  };

  const onPointerDown = (e) => {
    setDragging(true);
    const v0 = segAt(e.clientX);
    if (v0 !== valueRef.current) onChange(v0);
    const move = (ev) => {
      if (!trackRef.current) return;
      const v = segAt(ev.clientX);
      if (v !== valueRef.current) onChange(v);
    };
    const up = () => {
      setDragging(false);
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };

  return (
    <TweakRow label={label}>
      <div ref={trackRef} role="radiogroup" onPointerDown={onPointerDown}
           className={dragging ? 'twk-seg dragging' : 'twk-seg'}>
        <div className="twk-seg-thumb"
             style={{ left: `calc(2px + ${idx} * (100% - 4px) / ${n})`,
                      width: `calc((100% - 4px) / ${n})` }} />
        {opts.map((o) => (
          <button key={o.value} type="button" role="radio" aria-checked={o.value === value}>
            {o.label}
          </button>
        ))}
      </div>
    </TweakRow>
  );
}

function TweakSelect({ label, value, options, onChange }) {
  return (
    <TweakRow label={label}>
      <select className="twk-field" value={value} onChange={(e) => onChange(e.target.value)}>
        {options.map((o) => {
          const v = typeof o === 'object' ? o.value : o;
          const l = typeof o === 'object' ? o.label : o;
          return <option key={v} value={v}>{l}</option>;
        })}
      </select>
    </TweakRow>
  );
}

function TweakText({ label, value, placeholder, onChange }) {
  return (
    <TweakRow label={label}>
      <input className="twk-field" type="text" value={value} placeholder={placeholder}
             onChange={(e) => onChange(e.target.value)} />
    </TweakRow>
  );
}

function TweakNumber({ label, value, min, max, step = 1, unit = '', onChange }) {
  const clamp = (n) => {
    if (min != null && n < min) return min;
    if (max != null && n > max) return max;
    return n;
  };
  const startRef = React.useRef({ x: 0, val: 0 });
  const onScrubStart = (e) => {
    e.preventDefault();
    startRef.current = { x: e.clientX, val: value };
    const decimals = (String(step).split('.')[1] || '').length;
    const move = (ev) => {
      const dx = ev.clientX - startRef.current.x;
      const raw = startRef.current.val + dx * step;
      const snapped = Math.round(raw / step) * step;
      onChange(clamp(Number(snapped.toFixed(decimals))));
    };
    const up = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };
  return (
    <div className="twk-num">
      <span className="twk-num-lbl" onPointerDown={onScrubStart}>{label}</span>
      <input type="number" value={value} min={min} max={max} step={step}
             onChange={(e) => onChange(clamp(Number(e.target.value)))} />
      {unit && <span className="twk-num-unit">{unit}</span>}
    </div>
  );
}

// Relative-luminance contrast pick — checkmarks drawn over a swatch need to
// read on both #111 and #fafafa without per-option configuration. Hex input
// only (#rgb / #rrggbb); named or rgb()/hsl() colors fall through to "light".
function __twkIsLight(hex) {
  const h = String(hex).replace('#', '');
  const x = h.length === 3 ? h.replace(/./g, (c) => c + c) : h.padEnd(6, '0');
  const n = parseInt(x.slice(0, 6), 16);
  if (Number.isNaN(n)) return true;
  const r = (n >> 16) & 255, g = (n >> 8) & 255, b = n & 255;
  return r * 299 + g * 587 + b * 114 > 148000;
}

const __TwkCheck = ({ light }) => (
  <svg viewBox="0 0 14 14" aria-hidden="true">
    <path d="M3 7.2 5.8 10 11 4.2" fill="none" strokeWidth="2.2"
          strokeLinecap="round" strokeLinejoin="round"
          stroke={light ? 'rgba(0,0,0,.78)' : '#fff'} />
  </svg>
);

// TweakColor — curated color/palette picker. Each option is either a single
// hex string or an array of 1-5 hex strings; the card adapts — a lone color
// renders solid, a palette renders colors[0] as the hero (left ~2/3) with the
// rest stacked in a sharp column on the right. onChange emits the
// option in the shape it was passed (string stays string, array stays array).
// Without options it falls back to the native color input for back-compat.
function TweakColor({ label, value, options, onChange }) {
  if (!options || !options.length) {
    return (
      <div className="twk-row twk-row-h">
        <div className="twk-lbl"><span>{label}</span></div>
        <input type="color" className="twk-swatch" value={value}
               onChange={(e) => onChange(e.target.value)} />
      </div>
    );
  }
  // Native <input type=color> emits lowercase hex per the HTML spec, so
  // compare case-insensitively. String() guards JSON.stringify(undefined),
  // which returns the primitive undefined (no .toLowerCase).
  const key = (o) => String(JSON.stringify(o)).toLowerCase();
  const cur = key(value);
  return (
    <TweakRow label={label}>
      <div className="twk-chips" role="radiogroup">
        {options.map((o, i) => {
          const colors = Array.isArray(o) ? o : [o];
          const [hero, ...rest] = colors;
          const sup = rest.slice(0, 4);
          const on = key(o) === cur;
          return (
            <button key={i} type="button" className="twk-chip" role="radio"
                    aria-checked={on} data-on={on ? '1' : '0'}
                    aria-label={colors.join(', ')} title={colors.join(' · ')}
                    style={{ background: hero }}
                    onClick={() => onChange(o)}>
              {sup.length > 0 && (
                <span>
                  {sup.map((c, j) => <i key={j} style={{ background: c }} />)}
                </span>
              )}
              {on && <__TwkCheck light={__twkIsLight(hero)} />}
            </button>
          );
        })}
      </div>
    </TweakRow>
  );
}

function TweakButton({ label, onClick, secondary = false }) {
  return (
    <button type="button" className={secondary ? 'twk-btn secondary' : 'twk-btn'}
            onClick={onClick}>{label}</button>
  );
}

Object.assign(window, {
  useTweaks, TweaksPanel, TweakSection, TweakRow,
  TweakSlider, TweakToggle, TweakRadio, TweakSelect,
  TweakText, TweakNumber, TweakColor, TweakButton,
});


// --- data.jsx ---
/* ============================================================
   BUCKS — data layer: economics, catalog, formatters, icons
   Exposes everything on window for cross-file use and integrates
   directly with the C++ backend APIs.
   ============================================================ */

const API_BASE = '/api';

/* ---------- economics (the peg) ---------- */
const GOLD_USD_PER_G = 84.5; // spot, $/gram

// Dynamic getters to bridge global constants to the C++ backend state
window.GOLD_USD_PER_G = GOLD_USD_PER_G;


// Reported reserve per BUX (grams), straight from /api/economy/status.
// 0 until the vault signers have recorded a reserve: there is no made-up
// fallback peg (API-18).
Object.defineProperty(window, 'PEG_G_PER_BUX', {
  get() {
    const state = window.__bucks_state;
    if (state && state.economy && state.economy.pricePerBuck) {
      return state.economy.pricePerBuck;
    }
    return 0;
  },
  configurable: true
});

Object.defineProperty(window, 'PEG_MG_PER_BUX', {
  get() {
    return window.PEG_G_PER_BUX * 1000;
  },
  configurable: true
});

Object.defineProperty(window, 'BUX_USD', {
  get() {
    return window.PEG_G_PER_BUX * window.GOLD_USD_PER_G;
  },
  configurable: true
});

/* ---------- rig catalog (ASIC) ---------- */
const RIGS = [
  {
    id: "s21pro", name: "Vault S21 Pro", tier: "Flagship",
    th: 235, watts: 3510, temp: 62, fan: 78,
    daily: 1486, price: 4290, blurb: "Hydro-cooled flagship. Best BUX/day on the network.",
  },
  {
    id: "s21", name: "Vault S21", tier: "Performance",
    th: 188, watts: 3010, temp: 58, fan: 71,
    daily: 1188, price: 3120, blurb: "The workhorse. Balanced efficiency and output.",
  },
  {
    id: "s19k", name: "Vault S19k", tier: "Efficiency",
    th: 120, watts: 2760, temp: 64, fan: 83,
    daily: 758, price: 1640, blurb: "Low entry cost. Ideal for a first rig.",
  },
];

/* ---------- mining pools ---------- */
const POOLS = [
  { id: "aurum",   name: "Aurum Pool",   fee: 1.0, latency: 12, share: 48, vault: true,
    blurb: "Vault-operated. Direct gold settlement, lowest variance." },
  { id: "bullion", name: "Bullion Pool", fee: 1.5, latency: 28, share: 34, vault: false,
    blurb: "Large public pool. Steady payouts, slightly higher fee." },
  { id: "solo",    name: "Solo · Vault", fee: 0.5, latency: 9,  share: 18, vault: true,
    blurb: "Mine direct to the vault. High variance, max reward." },
];

const PAYOUT_FREQ = [
  { id: "epoch", label: "Every epoch", sub: "~4 hrs" },
  { id: "daily", label: "Daily",       sub: "00:00 UTC" },
  { id: "thresh", label: "On threshold", sub: "at 5,000 BUX" },
];

/* ---------- formatters ---------- */
const fmt = {
  bux(n, dp = 0) {
    return Number(n).toLocaleString("en-US", { minimumFractionDigits: dp, maximumFractionDigits: dp });
  },
  usd(n) {
    const v = Number(n);
    if (Math.abs(v) < 1) return "$" + v.toFixed(4);
    return v.toLocaleString("en-US", { style: "currency", currency: "USD", maximumFractionDigits: 2 });
  },
  gold(grams) {
    if (grams >= 1) return grams.toFixed(3) + " g";
    return (grams * 1000).toFixed(2) + " mg";
  },
  signed(n) { return (n >= 0 ? "+" : "−") + fmt.bux(Math.abs(n)); },
};

/* ---------- tiny SVG icon set (stroke, 1.6) ---------- */
function Icon({ d, size = 18, fill = "none", stroke = "currentColor", sw = 1.6, children, vb = 24, style }) {
  return (
    <svg width={size} height={size} viewBox={`0 0 ${vb} ${vb}`} fill={fill} stroke={stroke}
         strokeWidth={sw} strokeLinecap="round" strokeLinejoin="round" style={style} aria-hidden="true">
      {d ? <path d={d} /> : children}
    </svg>
  );
}
const Icons = {
  gauge:   (p) => <Icon {...p}><path d="M12 13a3 3 0 1 0 3 3"/><path d="M3.5 16a8.5 8.5 0 1 1 17 0"/><path d="m14.2 13.8 3.3-3.8"/></Icon>,
  vault:   (p) => <Icon {...p}><rect x="3" y="4" width="18" height="16" rx="2"/><circle cx="12" cy="12" r="4"/><path d="M12 9v.01M15 12h.01M12 15v.01M9 12h.01"/></Icon>,
  wallet:  (p) => <Icon {...p}><path d="M3 7a2 2 0 0 1 2-2h12a2 2 0 0 1 2 2v0H5a2 2 0 0 0-2 2v0"/><path d="M3 9a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><circle cx="16.5" cy="13" r="1.3" fill="currentColor" stroke="none"/></Icon>,
  sliders: (p) => <Icon {...p}><path d="M4 6h10M18 6h2M4 12h2M10 12h10M4 18h8M16 18h4"/><circle cx="16" cy="6" r="2"/><circle cx="8" cy="12" r="2"/><circle cx="14" cy="18" r="2"/></Icon>,
  bolt:    (p) => <Icon {...p}><path d="M13 3 5 13h6l-1 8 8-10h-6z"/></Icon>,
  plus:    (p) => <Icon {...p}><path d="M12 5v14M5 12h14"/></Icon>,
  check:   (p) => <Icon {...p}><path d="m5 12 5 5 9-11"/></Icon>,
  arrowR:  (p) => <Icon {...p}><path d="M5 12h14M13 6l6 6-6 6"/></Icon>,
  arrowUp: (p) => <Icon {...p}><path d="M12 19V5M6 11l6-6 6 6"/></Icon>,
  chevD:   (p) => <Icon {...p}><path d="m6 9 6 6 6-6"/></Icon>,
  chevR:   (p) => <Icon {...p}><path d="m9 6 6 6-6 6"/></Icon>,
  activity:(p) => <Icon {...p}><path d="M3 12h4l3 8 4-16 3 8h4"/></Icon>,
  coin:    (p) => <Icon {...p}><ellipse cx="12" cy="6.5" rx="7" ry="3"/><path d="M5 6.5v11c0 1.7 3.1 3 7 3s7-1.3 7-3v-11"/><path d="M5 12c0 1.7 3.1 3 7 3s7-1.3 7-3"/></Icon>,
  temp:    (p) => <Icon {...p}><path d="M10 13.5V5a2 2 0 1 1 4 0v8.5a4 4 0 1 1-4 0z"/></Icon>,
  fan:     (p) => <Icon {...p}><circle cx="12" cy="12" r="2"/><path d="M12 10c0-3 .5-6 2-6s2 2 1 4-3 2-3 2M14 12c3 0 6 .5 6 2s-2 2-4 1-2-3-2-3M12 14c0 3-.5 6-2 6s-2-2-1-4 3-2 3-2M10 12c-3 0-6-.5-6-2s2-2 4-1 2 3 2 3"/></Icon>,
  shield:  (p) => <Icon {...p}><path d="M12 3 5 6v5c0 4.5 3 8 7 10 4-2 7-5.5 7-10V6z"/><path d="m9 12 2 2 4-4"/></Icon>,
  lock:    (p) => <Icon {...p}><rect x="5" y="11" width="14" height="9" rx="2"/><path d="M8 11V8a4 4 0 1 1 8 0v3"/></Icon>,
  user:    (p) => <Icon {...p}><circle cx="12" cy="8" r="3.5"/><path d="M5 20a7 7 0 0 1 14 0"/></Icon>,
  mail:    (p) => <Icon {...p}><rect x="3" y="5" width="18" height="14" rx="2"/><path d="m4 7 8 6 8-6"/></Icon>,
  cpu:     (p) => <Icon {...p}><rect x="6" y="6" width="12" height="12" rx="2"/><rect x="9.5" y="9.5" width="5" height="5" rx="1"/><path d="M9 3v3M15 3v3M9 18v3M15 18v3M3 9h3M3 15h3M18 9h3M18 15h3"/></Icon>,
  logout:  (p) => <Icon {...p}><path d="M14 8V6a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2v-2M10 12h10M17 9l3 3-3 3"/></Icon>,
  spark:   (p) => <Icon {...p}><path d="M12 3v4M12 17v4M3 12h4M17 12h4M6 6l2.5 2.5M15.5 15.5 18 18M18 6l-2.5 2.5M8.5 15.5 6 18"/></Icon>,
  globe:   (p) => <Icon {...p}><circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c2.5 2.7 2.5 15.3 0 18M12 3c-2.5 2.7-2.5 15.3 0 18"/></Icon>,
  clock:   (p) => <Icon {...p}><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></Icon>,
  copy:    (p) => <Icon {...p}><rect x="9" y="9" width="11" height="11" rx="2"/><path d="M5 15V6a2 2 0 0 1 2-2h9"/></Icon>,
  external:(p) => <Icon {...p}><path d="M14 5h5v5M19 5l-8 8M12 5H7a2 2 0 0 0-2 2v10a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2v-5"/></Icon>,
};

/* ---------- React Context Data Layer (Real Backend Integration) ---------- */
const DataContext = React.createContext();

// Every API call goes through here. The session cookie (HttpOnly, set by
// POST /api/auth/login) is sent same-origin only, state-changing calls carry
// the X-Bucks-CSRF header the node requires for cookie sessions, and no
// token or secret is ever written to localStorage/sessionStorage.
async function apiFetch(path, opts = {}) {
  const method = (opts.method || 'GET').toUpperCase();
  const headers = { ...(opts.headers || {}) };
  if (method !== 'GET' && method !== 'HEAD') headers['X-Bucks-CSRF'] = '1';
  if (opts.body !== undefined && !headers['Content-Type']) headers['Content-Type'] = 'application/json';
  return fetch(`${API_BASE}${path}`, { ...opts, method, headers, credentials: 'same-origin' });
}

function DataProvider({ children }) {
  const [auth, setAuth] = React.useState({
    checked: false, connected: true, authenticated: false,
    cookieFile: '', walletsEnabled: true, walletError: ''
  });
  const authRef = React.useRef(false);
  authRef.current = auth.authenticated;

  const [state, setState] = React.useState({
    connected: false,
    height: 0,
    difficulty: 0,
    utxoCount: 0,
    wallets: [],
    primaryWallet: null,
    economy: { supply: 0, goldReserve: 0, pricePerBuck: 0 },
    mining: { active: false, hashRate: 0, blocksFound: 0, minerAddress: '' },
    recentBlocks: [],
    transactions: [],
    toasts: []
  });

  const showToast = React.useCallback((title, message, type = 'success') => {
    const id = Date.now() + Math.random().toString(36).substr(2, 9);
    setState(prev => ({
      ...prev,
      toasts: [...prev.toasts, { id, title, message, type }]
    }));
    setTimeout(() => {
      setState(prev => ({
        ...prev,
        toasts: prev.toasts.filter(t => t.id !== id)
      }));
    }, 4000);
  }, []);

  const markLoggedOut = React.useCallback(() => {
    setAuth(a => ({ ...a, checked: true, authenticated: false }));
    setState(prev => ({ ...prev, primaryWallet: null }));
  }, []);

  // JSON helper; a 401 means the session is gone, so show the login screen.
  const apiJson = React.useCallback(async (path, opts) => {
    const res = await apiFetch(path, opts);
    if (res.status === 401) markLoggedOut();
    let data = {};
    try { data = await res.json(); } catch (err) { data = {}; }
    return { ok: res.ok, status: res.status, data };
  }, [markLoggedOut]);

  const safeJsonFetch = async (path) => {
    try {
      const response = await apiFetch(path);
      if (!response.ok) return {};
      const text = await response.text();
      try {
        return JSON.parse(text);
      } catch (err) {
        // If rate limited or invalid text, returns empty object
        return {};
      }
    } catch (e) {
      return {};
    }
  };

  const checkAuth = React.useCallback(async () => {
    try {
      const res = await apiFetch('/auth/status');
      if (!res.ok) throw new Error('status ' + res.status);
      const d = await res.json();
      setAuth({
        checked: true, connected: true, authenticated: !!d.authenticated,
        cookieFile: d.cookieFile || '', walletsEnabled: d.walletsEnabled !== false, walletError: d.walletError || ''
      });
      return !!d.authenticated;
    } catch (e) {
      setAuth(a => ({ ...a, checked: true, connected: false, authenticated: false }));
      return false;
    }
  }, []);

  const refreshData = React.useCallback(async () => {
    try {
      // Check status
      const statusRes = await apiFetch('/status').catch(() => null);
      if (!statusRes || !statusRes.ok) {
        setState(prev => ({ ...prev, connected: false }));
        setAuth(a => ({ ...a, connected: false }));
        return;
      }

      // Public data in parallel - optimized list to prevent rate limit
      const [infoRes, economyRes, miningRes] = await Promise.all([
        safeJsonFetch('/blockchain/info'),
        safeJsonFetch('/economy/status'),
        safeJsonFetch('/mining/status')
      ]);

      // Wallet data needs the session (never contains keys or the phrase)
      let primary = null;
      if (authRef.current) {
        const p = await apiFetch('/wallets/primary').catch(() => null);
        if (p && p.status === 401) markLoggedOut();
        else if (p && p.ok) {
          try { const d = await p.json(); if (d.success) primary = d; } catch (err) { primary = null; }
        }
      }

      const economy = {
        supply: economyRes.supply || 0,
        goldReserve: economyRes.gold_reserve || 0,
        pricePerBuck: economyRes.price_per_buck || 0
      };

      setState(prev => ({
        ...prev,
        connected: true,
        height: infoRes.height || 0,
        difficulty: infoRes.difficulty || 0,
        utxoCount: infoRes.utxoCount || 0,
        wallets: [], // unused in premium console
        primaryWallet: primary,
        economy,
        mining: {
          active: miningRes.mining || false,
          hashRate: miningRes.hashRate || 0,
          blocksFound: miningRes.blocksFound || 0,
          minerAddress: miningRes.minerAddress || ''
        }
      }));
      setAuth(a => (a.connected ? a : { ...a, connected: true }));
      // Only the public peg figures are mirrored on window (for the PEG getters)
      window.__bucks_state = { economy };
    } catch (e) {
      setState(prev => ({ ...prev, connected: false }));
    }
  }, [markLoggedOut]);

  // Find out whether we have a session, then poll (6 s is rate-limit safe)
  React.useEffect(() => {
    let cancelled = false;
    (async () => {
      await checkAuth();
      if (!cancelled) await refreshData();
    })();
    const interval = setInterval(refreshData, 6000);
    return () => { cancelled = true; clearInterval(interval); };
  }, [checkAuth, refreshData]);

  const login = async (token) => {
    try {
      const res = await apiFetch('/auth/login', { method: 'POST', body: JSON.stringify({ token }) });
      if (!res.ok) return false;
      const ok = await checkAuth();
      if (ok) await refreshData();
      return ok;
    } catch (e) {
      setAuth(a => ({ ...a, connected: false }));
      return false;
    }
  };

  const logout = async () => {
    try { await apiFetch('/auth/logout', { method: 'POST' }); } catch (e) { /* session may already be gone */ }
    markLoggedOut();
  };

  const copyToClipboard = React.useCallback((text, successMsg = 'Copied to clipboard') => {
    navigator.clipboard.writeText(text).then(() => {
      showToast('Copied', successMsg);
    }).catch(() => {
      // Fallback
      const t = document.createElement('textarea');
      t.value = text;
      document.body.appendChild(t);
      t.select();
      document.execCommand('copy');
      t.remove();
      showToast('Copied', successMsg);
    });
  }, [showToast]);

  const formatBucks = React.useCallback((sats) => {
    return (sats / 100000000).toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 8 });
  }, []);

  const toGoldEquivalent = React.useCallback((sats) => {
    const bucks = sats / 100000000;
    const goldVal = bucks * window.PEG_G_PER_BUX;
    return goldVal >= 1
      ? `${goldVal.toFixed(3)} g gold`
      : `${(goldVal * 1000).toFixed(3)} mg gold`;
  }, []);

  // Word lists the node can issue phrases in (index-aligned, see /api/wallets/languages)
  const getLanguages = async () => {
    const { ok, data } = await apiJson('/wallets/languages');
    return ok && data.success ? data : null;
  };

  // Creates the wallet on the node. The returned phrase is shown ONCE by the
  // caller and dropped; the node never returns it again.
  const createWallet = async ({ language = '', words = 13 } = {}) => {
    try {
      const { ok, data } = await apiJson('/wallets/create', {
        method: 'POST',
        body: JSON.stringify({ language, words })
      });
      if (ok && data.success) {
        if (data.persisted === false) showToast('Warning', data.error || 'Wallet was not saved to disk', 'error');
        await refreshData();
        return data;
      }
      showToast('Error', data.error || 'Failed to create wallet', 'error');
      return null;
    } catch (e) {
      showToast('Error', 'Connection failed', 'error');
      return null;
    }
  };

  const restoreWallet = async (mnemonic, passphrase = '') => {
    try {
      const { ok, data } = await apiJson('/wallets/restore', {
        method: 'POST',
        body: JSON.stringify({ mnemonic, passphrase })
      });
      if (ok && data.success) {
        showToast('Success', data.existing ? 'That wallet is already on this node' : 'Wallet restored successfully');
        await refreshData();
        return true;
      }
      showToast('Error', data.error || 'Restoration failed', 'error');
      return false;
    } catch (e) {
      showToast('Error', 'Connection failed', 'error');
      return false;
    }
  };

  const sendTransaction = async (to, amountBucks) => {
    if (!state.primaryWallet) {
      showToast('Error', 'No wallet setup', 'error');
      return false;
    }
    try {
      const amount = Math.floor(amountBucks * 100000000);
      const { ok, data } = await apiJson('/transactions/send', {
        method: 'POST',
        body: JSON.stringify({
          from: state.primaryWallet.address,
          to,
          amount
        })
      });
      if (ok && data.success) {
        showToast('Success', 'Transaction broadcasted successfully');
        await refreshData();
        return true;
      }
      showToast('Failed', data.error || 'Transaction rejected', 'error');
      return false;
    } catch (e) {
      showToast('Error', 'Network error', 'error');
      return false;
    }
  };

  const toggleMining = async () => {
    const endpoint = state.mining.active ? 'stop' : 'start';
    try {
      const { ok, data } = await apiJson(`/mining/${endpoint}`, { method: 'POST' });
      if (ok && data.success) {
        showToast('Success', `Mining ${state.mining.active ? 'stopped' : 'started'}`);
        await refreshData();
      } else {
        showToast('Error', data.error || 'Failed to toggle mining', 'error');
      }
    } catch (e) {
      showToast('Error', 'Network error', 'error');
    }
  };

  return (
    <DataContext.Provider value={{
      state,
      auth,
      login,
      logout,
      checkAuth,
      copyToClipboard,
      formatBucks,
      toGoldEquivalent,
      getLanguages,
      createWallet,
      restoreWallet,
      sendTransaction,
      toggleMining,
      refreshData,
      showToast
    }}>
      {children}
    </DataContext.Provider>
  );
}

// Global hooks setup
const useData = () => React.useContext(DataContext);

// Save context reference on window
Object.assign(window, {
  RIGS, POOLS, PAYOUT_FREQ,
  fmt, Icon, Icons,
  apiFetch, DataProvider, useData, DataContext
});


// --- components.jsx ---
/* ============================================================
   BUCKS — shared UI components
   ============================================================ */

/* ---------- brand mark: the Bucks coin ---------- */
function BucksMark({ size = 28, glow = false }) {
  const id = React.useId();
  return (
    <svg width={size} height={size} viewBox="0 0 48 48" aria-label="Bucks"
         style={{ filter: glow ? "drop-shadow(0 0 10px var(--action))" : "none" }}>
      <defs>
        <linearGradient id={`g${id}`} x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="var(--action)" />
          <stop offset="1" stopColor="var(--action)" />
        </linearGradient>
      </defs>
      <circle cx="24" cy="24" r="22" fill="none" stroke={`url(#g${id})`} strokeWidth="2.5" />
      <circle cx="24" cy="24" r="16.5" fill="none" stroke="var(--accent)" strokeWidth="1.4" strokeDasharray="2 3.4" opacity=".75" />
      <text x="24" y="31.5" textAnchor="middle" fontFamily="var(--font-display)" fontWeight="700"
            fontSize="22" fill={`url(#g${id})`}>B</text>
    </svg>
  );
}

/* ---------- buttons ---------- */
function Button({ children, variant = "primary", size = "md", icon: Ic, iconRight, full, disabled, onClick, style, type }) {
  const base = {
    display: "inline-flex", alignItems: "center", justifyContent: "center", gap: 8,
    fontFamily: "var(--font-ui)", fontWeight: 600, border: "1px solid transparent",
    borderRadius: "var(--r-md)", cursor: disabled ? "not-allowed" : "pointer",
    transition: "transform .12s ease, background .15s ease, border-color .15s ease, opacity .15s",
    whiteSpace: "nowrap", width: full ? "100%" : "auto", opacity: disabled ? .45 : 1,
    letterSpacing: ".01em",
  };
  const sizes = {
    sm: { padding: "7px 12px", fontSize: 13 },
    md: { padding: "11px 18px", fontSize: 14.5 },
    lg: { padding: "15px 26px", fontSize: 16 },
  };
  const variants = {
    primary: { background: "var(--accent)", color: "#1a1206", borderColor: "var(--accent)", boxShadow: "0 6px 20px -8px var(--accent-line)" },
    action:    { background: "linear-gradient(180deg, var(--action), var(--action))", color: "#1c1404", borderColor: "var(--action)", boxShadow: "0 6px 20px -8px var(--action)" },
    ghost:   { background: "var(--surface-2)", color: "var(--ink)", borderColor: "var(--line-strong)" },
    quiet:   { background: "transparent", color: "var(--ink-2)", borderColor: "transparent" },
    danger:  { background: "transparent", color: "#f06a5d", borderColor: "rgba(240,106,93,.4)" },
  };
  return (
    <button type={type || "button"} disabled={disabled} onClick={onClick}
      style={{ ...base, ...sizes[size], ...variants[variant], ...style }}
      onMouseDown={(e) => !disabled && (e.currentTarget.style.transform = "translateY(1px)")}
      onMouseUp={(e) => (e.currentTarget.style.transform = "none")}
      onMouseLeave={(e) => (e.currentTarget.style.transform = "none")}>
      {Ic && <Ic size={size === "lg" ? 19 : 17} />}
      {children}
      {iconRight && React.createElement(iconRight, { size: size === "lg" ? 19 : 17 })}
    </button>
  );
}

/* ---------- card ---------- */
function Card({ children, pad = 20, style, hover, onClick, glow }) {
  const [h, setH] = React.useState(false);
  return (
    <div onClick={onClick} onMouseEnter={() => setH(true)} onMouseLeave={() => setH(false)}
      style={{
        background: "linear-gradient(180deg, var(--surface-2), var(--surface-1))",
        border: `1px solid ${hover && h ? "var(--line-strong)" : "var(--line)"}`,
        borderRadius: "var(--r-lg)", padding: pad, position: "relative",
        boxShadow: glow ? "var(--shadow-2), inset 0 1px 0 rgba(255,255,255,.03)" : "var(--shadow-1)",
        transition: "border-color .18s ease, transform .18s ease",
        transform: hover && h ? "translateY(-2px)" : "none",
        cursor: onClick ? "pointer" : "default", ...style,
      }}>
      {children}
    </div>
  );
}

/* ---------- section label ---------- */
function Eyebrow({ children, color = "var(--ink-3)" }) {
  return <div style={{ font: "600 11px/1 var(--font-ui)", letterSpacing: ".16em", textTransform: "uppercase", color }}>{children}</div>;
}

/* ---------- stat block ---------- */
function Stat({ label, value, unit, sub, accent, mono = true }) {
  return (
    <div>
      <Eyebrow>{label}</Eyebrow>
      <div style={{ display: "flex", alignItems: "baseline", gap: 6, marginTop: 9 }}>
        <span className={mono ? "mono tnum" : ""} style={{ fontSize: 30, fontWeight: 600, color: accent || "var(--ink)", lineHeight: 1, fontFamily: mono ? "var(--font-mono)" : "var(--font-display)" }}>{value}</span>
        {unit && <span style={{ fontSize: 13, color: "var(--ink-3)", fontWeight: 600 }}>{unit}</span>}
      </div>
      {sub && <div style={{ marginTop: 7, fontSize: 12.5, color: "var(--ink-2)" }}>{sub}</div>}
    </div>
  );
}

/* ---------- pill / chip ---------- */
function Chip({ children, tone = "neutral", icon: Ic }) {
  const tones = {
    neutral: { bg: "var(--surface-3)", fg: "var(--ink-2)", bd: "var(--line)" },
    accent:  { bg: "var(--accent-soft)", fg: "var(--accent)", bd: "var(--accent-line)" },
    pos:     { bg: "var(--pos-soft)", fg: "var(--pos)", bd: "rgba(62,207,142,.3)" },
    action:    { bg: "rgba(232,176,75,.12)", fg: "var(--action)", bd: "rgba(232,176,75,.32)" },
  };
  const t = tones[tone];
  return (
    <span style={{ display: "inline-flex", alignItems: "center", gap: 5, padding: "4px 10px",
      borderRadius: 99, background: t.bg, color: t.fg, border: `1px solid ${t.bd}`,
      font: "600 11.5px/1 var(--font-ui)", letterSpacing: ".02em" }}>
      {Ic && <Ic size={13} />}{children}
    </span>
  );
}

/* ---------- live dot ---------- */
function LiveDot({ color = "var(--pos)" }) {
  return (
    <span style={{ position: "relative", width: 8, height: 8, display: "inline-block" }}>
      <span style={{ position: "absolute", inset: 0, borderRadius: 99, background: color }} />
      <span style={{ position: "absolute", inset: -3, borderRadius: 99, background: color, opacity: .4, animation: "pulse-soft 1.8s infinite" }} />
    </span>
  );
}

/* ---------- field ---------- */
function Field({ label, hint, icon: Ic, type = "text", value, onChange, placeholder, suffix, autoComplete }) {
  const [f, setF] = React.useState(false);
  return (
    <label style={{ display: "block" }}>
      {label && <div style={{ font: "600 12.5px var(--font-ui)", color: "var(--ink-2)", marginBottom: 8 }}>{label}</div>}
      <div style={{ display: "flex", alignItems: "center", gap: 10, padding: "0 14px", height: 48,
        background: "var(--surface-1)", border: `1px solid ${f ? "var(--accent-line)" : "var(--line-strong)"}`,
        borderRadius: "var(--r-md)", transition: "border-color .15s", boxShadow: f ? "0 0 0 3px var(--accent-soft)" : "none" }}>
        {Ic && <Ic size={17} style={{ color: f ? "var(--accent)" : "var(--ink-3)", flexShrink: 0 }} />}
        <input type={type} value={value} placeholder={placeholder} autoComplete={autoComplete}
          onFocus={() => setF(true)} onBlur={() => setF(false)}
          onChange={(e) => onChange && onChange(e.target.value)}
          style={{ flex: 1, background: "none", border: "none", outline: "none", color: "var(--ink)",
            fontSize: 15, fontFamily: type === "email" ? "var(--font-mono)" : "var(--font-ui)" }} />
        {suffix && <span style={{ color: "var(--ink-3)", fontSize: 13 }} className="mono">{suffix}</span>}
      </div>
      {hint && <div style={{ marginTop: 7, fontSize: 12, color: "var(--ink-3)" }}>{hint}</div>}
    </label>
  );
}

/* ---------- segmented control ---------- */
function Segmented({ options, value, onChange }) {
  return (
    <div style={{ display: "inline-flex", padding: 4, gap: 4, background: "var(--surface-1)",
      border: "1px solid var(--line)", borderRadius: "var(--r-md)" }}>
      {options.map((o) => {
        const active = o.id === value;
        return (
          <button key={o.id} onClick={() => onChange(o.id)}
            style={{ padding: "8px 14px", borderRadius: 8, border: "none",
              background: active ? "var(--surface-hi)" : "transparent",
              color: active ? "var(--ink)" : "var(--ink-3)", font: "600 13px var(--font-ui)",
              boxShadow: active ? "var(--shadow-1)" : "none", transition: "all .15s",
              display: "flex", flexDirection: "column", alignItems: "flex-start", gap: 2 }}>
            {o.label}
            {o.sub && <span style={{ fontSize: 10.5, color: "var(--ink-3)", fontWeight: 500 }}>{o.sub}</span>}
          </button>
        );
      })}
    </div>
  );
}

/* ---------- slider ---------- */
function Slider({ value, min, max, step = 1, onChange, accent = "var(--accent)" }) {
  const pct = ((value - min) / (max - min)) * 100;
  return (
    <input type="range" min={min} max={max} step={step} value={value}
      onChange={(e) => onChange(Number(e.target.value))}
      style={{
        width: "100%", height: 6, appearance: "none", WebkitAppearance: "none", borderRadius: 99, outline: "none",
        background: `linear-gradient(90deg, ${accent} ${pct}%, var(--surface-hi) ${pct}%)`, cursor: "pointer",
      }}
      className="bux-slider" />
  );
}

/* ---------- sparkline ---------- */
function Sparkline({ data, w = 240, h = 56, color = "var(--accent)", fill = true, strokeW = 2 }) {
  const id = React.useId();
  if (!data || data.length < 2) return <svg width={w} height={h} />;
  const min = Math.min(...data), max = Math.max(...data), range = max - min || 1;
  const pts = data.map((v, i) => [(i / (data.length - 1)) * w, h - ((v - min) / range) * (h - 8) - 4]);
  const line = pts.map((p, i) => `${i ? "L" : "M"}${p[0].toFixed(1)} ${p[1].toFixed(1)}`).join(" ");
  const area = `${line} L${w} ${h} L0 ${h} Z`;
  return (
    <svg width={w} height={h} style={{ display: "block", overflow: "visible" }}>
      <defs>
        <linearGradient id={`sp${id}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor={color} stopOpacity=".26" />
          <stop offset="1" stopColor={color} stopOpacity="0" />
        </linearGradient>
      </defs>
      {fill && <path d={area} fill={`url(#sp${id})`} />}
      <path d={line} fill="none" stroke={color} strokeWidth={strokeW} strokeLinejoin="round" strokeLinecap="round" />
      <circle cx={pts[pts.length - 1][0]} cy={pts[pts.length - 1][1]} r="3" fill={color} />
    </svg>
  );
}

/* ---------- progress ring ---------- */
function Ring({ pct, size = 64, sw = 6, color = "var(--accent)", track = "var(--surface-hi)", children }) {
  const r = (size - sw) / 2, c = 2 * Math.PI * r;
  return (
    <div style={{ position: "relative", width: size, height: size }}>
      <svg width={size} height={size} style={{ transform: "rotate(-90deg)" }}>
        <circle cx={size/2} cy={size/2} r={r} fill="none" stroke={track} strokeWidth={sw} />
        <circle cx={size/2} cy={size/2} r={r} fill="none" stroke={color} strokeWidth={sw}
          strokeDasharray={c} strokeDashoffset={c * (1 - pct / 100)} strokeLinecap="round"
          style={{ transition: "stroke-dashoffset .6s cubic-bezier(.4,0,.2,1)" }} />
      </svg>
      <div style={{ position: "absolute", inset: 0, display: "grid", placeItems: "center" }}>{children}</div>
    </div>
  );
}

Object.assign(window, {
  BucksMark, Button, Card, Eyebrow, Stat, Chip, LiveDot, Field, 


// --- auth.jsx ---
/* ============================================================
   BUCKS — node login, one-time recovery phrase display,
   reported-reserve maths
   ============================================================ */

/* The peg the vault advertises (UI constant, not consensus). Coverage is the
   reported on-chain reserve divided by what this peg would require. */
const TARGET_PEG_MG_PER_BUX = 0.0675;

function reserveStats(economy) {
  const supplyBux = (economy && economy.supply ? economy.supply : 0) / 1e8;
  const reserveG = economy && economy.goldReserve ? economy.goldReserve : 0;
  const perBuxMg = supplyBux > 0 ? (reserveG * 1000) / supplyBux : null;
  const coverage = perBuxMg === null ? null : perBuxMg / TARGET_PEG_MG_PER_BUX;
  return { supplyBux, reserveG, perBuxMg, coverage };
}

function coverageLabel(coverage) {
  return coverage === null ? "n/a" : `${Math.round(coverage * 100)}%`;
}

/* Asks for the API token the node wrote to <datadir>/.cookie and exchanges
   it for an HttpOnly session cookie. The token itself is never kept. */
function LoginScreen() {
  const { auth, login } = useData();
  const [token, setToken] = React.useState("");
  const [busy, setBusy] = React.useState(false);
  const [error, setError] = React.useState("");

  const submit = async (e) => {
    e.preventDefault();
    const t = token.trim();
    if (!t) return;
    setBusy(true);
    setError("");
    const ok = await login(t);
    setBusy(false);
    setToken("");
    if (!ok) setError(auth.connected ? "That token was not accepted." : "Node not reachable.");
  };

  return (
    <div style={{ height: "100%", display: "grid", placeItems: "center", padding: 24, overflow: "auto" }}>
      <form onSubmit={submit} style={{ width: "100%", maxWidth: 440 }}>
        <Card pad={28} glow>
          <div style={{ display: "flex", alignItems: "center", gap: 11, marginBottom: 18 }}>
            <BucksMark size={30} glow />
            <span style={{ fontFamily: "var(--font-display)", fontWeight: 700, fontSize: 18 }}>Bucks</span>
          </div>
          <Eyebrow color="var(--accent)">Node access</Eyebrow>
          <h2 style={{ font: "600 24px/1.1 var(--font-display)", letterSpacing: "-.02em", margin: "10px 0 8px" }}>Enter the API token</h2>
          <p style={{ color: "var(--ink-2)", fontSize: 13.5, lineHeight: 1.55, margin: "0 0 18px" }}>
            The node generated it at start and wrote it to{" "}
            <code className="mono" style={{ color: "var(--ink)", wordBreak: "break-all" }}>{auth.cookieFile || "<datadir>/.cookie"}</code>
            {" "}(or it is the value you passed with <code className="mono">--api-token-file</code>). It is not stored in this browser; the session cookie expires after 12 hours.
          </p>
          <Field label="API token" icon={Icons.lock} type="password" autoComplete="off" value={token} onChange={setToken} placeholder="paste the token" />
          {error && <div style={{ color: "#f06a5d", fontSize: 13, marginTop: 10 }}>{error}</div>}
          {!auth.connected && <div style={{ color: "var(--warn)", fontSize: 13, marginTop: 10 }}>Node not reachable. Is it running with <code className="mono">--server</code>?</div>}
          <div style={{ marginTop: 20 }}>
            <Button type="submit" size="lg" full disabled={busy || !token.trim()} iconRight={Icons.arrowR}>{busy ? "Checking…" : "Unlock console"}</Button>
          </div>
        </Card>
      </form>
    </div>
  );
}

/* Numbered word grid. `direction` comes from the node ("rtl" for the
   Arabic lists): the grid, the reading order and the alignment follow it,
   while the index numbers stay left-to-right. */
function MnemonicGrid({ phrase, direction = "ltr", revealed, onReveal }) {
  const words = (phrase || "").trim().split(/\s+/).filter(Boolean);
  const rtl = direction === "rtl";
  return (
    <div style={{ position: "relative" }}>
      <div dir={rtl ? "rtl" : "ltr"} lang={rtl ? "ar" : undefined}
        style={{ display: "grid", gridTemplateColumns: "repeat(3, 1fr)", gap: 10,
          filter: revealed ? "none" : "blur(6px)", userSelect: revealed ? "text" : "none",
          transition: "filter 0.3s ease", background: "var(--surface-1)", border: "1px solid var(--line)",
          padding: 16, borderRadius: "var(--r-md)", textAlign: rtl ? "right" : "left" }}>
        {words.map((w, idx) => (
          <div key={idx} style={{ display: "flex", gap: 8, alignItems: "baseline",
            fontSize: rtl ? 17 : 13.5, fontFamily: rtl ? "var(--font-ui)" : "var(--font-mono)", lineHeight: 1.5 }}>
            <span className="mono" style={{ color: "var(--ink-3)", fontSize: 12, direction: "ltr", unicodeBidi: "isolate", minWidth: 22 }}>{idx + 1}.</span>
            <span style={{ unicodeBidi: "isolate" }}>{w}</span>
          </div>
        ))}
      </div>
      {!revealed && (
        <div onClick={onReveal} style={{
          position: "absolute", inset: 0, display: "flex", flexDirection: "column", alignItems: "center",
          justifyContent: "center", backgroundColor: "rgba(16,16,22,0.8)", cursor: "pointer", borderRadius: "var(--r-md)" }}>
          <Icons.lock size={20} style={{ color: "var(--accent)", marginBottom: 6 }} />
          <div style={{ fontWeight: 600, fontSize: 13 }}>Click to reveal the phrase</div>
          <div style={{ fontSize: 12, color: "var(--ink-3)", marginTop: 4 }}>Make sure nobody is watching</div>
        </div>
      )}
    </div>
  );
}

function BackupWarning({ wordCount }) {
  return (
    <div style={{ display: "flex", gap: 10, padding: "12px 14px", borderRadius: "var(--r-md)",
      background: "rgba(232,176,75,.08)", border: "1px solid rgba(232,176,75,.3)", fontSize: 13, lineHeight: 1.5, color: "var(--ink-2)" }}>
      <Icons.shield size={18} style={{ color: "var(--warn)", flexShrink: 0, marginTop: 2 }} />
      <div>
        <strong style={{ color: "var(--ink)" }}>Write these {wordCount} words down on paper, in order.</strong>{" "}
        They are shown only once: the node does not store them and cannot show them again.
        Anyone who has the words controls the funds; nobody can recover them for you.
      </div>
    </div>
  );
}

Object.assign(window, { TARGET_PEG_MG_PER_BUX, reserveStats, coverageLabel, LoginScreen, MnemonicGrid, BackupWarning });


Segmented, Slider, Sparkline, Ring,
});


// --- vault.jsx ---
/* ============================================================
   BUCKS — Gold Vault signature visualization (the peg)
   Every figure comes from /api/economy/status: the reserve is what the
   vault signers recorded on-chain ("reported"), never a constant.
   ============================================================ */

/* A stack of minted gold coins that grows with the vault.
   `level` 0..1 controls how full the stack is. */
function CoinStack({ level = 0.7, pulse = 0 }) {
  const COINS = 9;
  const lit = Math.round(level * COINS);
  return (
    <svg viewBox="0 0 180 220" width="100%" height="100%" style={{ display: "block" }}>
      <defs>
        <linearGradient id="coinFace" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#f6d889" /><stop offset="0.5" stopColor="var(--action)" /><stop offset="1" stopColor="var(--action)" />
        </linearGradient>
        <linearGradient id="coinEdge" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="var(--action)" /><stop offset="1" stopColor="#7a560f" />
        </linearGradient>
        <linearGradient id="coinDim" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#2a2620" /><stop offset="1" stopColor="#1c1a16" />
        </linearGradient>
        <radialGradient id="vaultGlow" cx="0.5" cy="0.42" r="0.6">
          <stop offset="0" stopColor="var(--action)" stopOpacity="0.16" /><stop offset="1" stopColor="var(--action)" stopOpacity="0" />
        </radialGradient>
      </defs>
      <ellipse cx="90" cy="110" rx="90" ry="100" fill="url(#vaultGlow)" />
      {/* coins from bottom up */}
      {Array.from({ length: COINS }).map((_, i) => {
        const y = 188 - i * 18;
        const isLit = i < lit;
        const justMinted = pulse && i === lit - 1;
        return (
          <g key={i} style={{ transition: "opacity .5s", opacity: 1 }}>
            {/* coin body */}
            <ellipse cx="90" cy={y + 9} rx="56" ry="14" fill={isLit ? "url(#coinEdge)" : "#15130f"} />
            <rect x="34" y={y} width="112" height="9" fill={isLit ? "url(#coinEdge)" : "#15130f"} />
            <ellipse cx="90" cy={y} rx="56" ry="14" fill={isLit ? "url(#coinFace)" : "url(#coinDim)"}
              stroke={isLit ? "#fbe7ad" : "#2a2620"} strokeWidth="1"
              style={{ filter: justMinted ? "drop-shadow(0 0 12px var(--action))" : "none", transition: "filter .4s" }} />
            {isLit && <ellipse cx="90" cy={y} rx="44" ry="10" fill="none" stroke="#fbe7ad" strokeOpacity="0.5" strokeWidth="0.8" />}
            {isLit && <text x="90" y={y + 4} textAnchor="middle" fontFamily="var(--font-display)" fontWeight="700" fontSize="13" fill="#7a560f">B</text>}
          </g>
        );
      })}
    </svg>
  );
}

/* Full vault panel for dashboard / vault page */
function GoldVault({ net, pulse }) {
  const { state: backendState } = useData();
  const rs = reserveStats(backendState.economy);
  const grams = rs.reserveG;
  const supplyBux = rs.supplyBux;

  const ozt = grams / 31.1035;
  const vaultUsd = grams * window.GOLD_USD_PER_G;
  const level = rs.coverage === null ? 0 : Math.min(1, rs.coverage); // visual fullness = coverage
  return (
    <Card pad={0} glow style={{ overflow: "hidden", height: "100%" }}>
      <div style={{ display: "grid", gridTemplateColumns: "1fr 168px", minHeight: 320, height: "100%" }}>
        {/* left: numbers */}
        <div style={{ padding: 24, display: "flex", flexDirection: "column" }}>
          <div style={{ display: "flex", alignItems: "center", gap: 8, marginBottom: 4 }}>
            <Icons.vault size={16} style={{ color: "var(--action)" }} />
            <Eyebrow color="var(--action)">The Gold Vault</Eyebrow>
          </div>
          <div style={{ fontSize: 13, color: "var(--ink-2)", maxWidth: 320, lineHeight: 1.5 }}>
            Reserve as reported on-chain by the vault signers (3-of-5), set against the circulating supply. The node records these figures; it does not audit the metal.
          </div>

          <div style={{ marginTop: 22, display: "grid", gridTemplateColumns: "1fr 1fr", gap: "20px 28px" }}>
            <div>
              <Eyebrow>Reported reserve</Eyebrow>
              <div style={{ display: "flex", alignItems: "baseline", gap: 6, marginTop: 9 }}>
                <span className="mono tnum" style={{ fontSize: 34, fontWeight: 600, color: "var(--action)", lineHeight: 1 }}>{grams.toFixed(2)}</span>
                <span style={{ fontSize: 13, color: "var(--ink-3)", fontWeight: 600 }}>g Au</span>
              </div>
              <div style={{ marginTop: 6, fontSize: 12, color: "var(--ink-3)" }} className="mono">{ozt.toFixed(2)} ozt · {fmt.usd(vaultUsd)}</div>
            </div>
            <div>
              <Eyebrow>BUX circulating</Eyebrow>
              <div style={{ display: "flex", alignItems: "baseline", gap: 6, marginTop: 9 }}>
                <span className="mono tnum" style={{ fontSize: 34, fontWeight: 600, lineHeight: 1 }}>{(supplyBux/1e6).toFixed(3)}</span>
                <span style={{ fontSize: 13, color: "var(--ink-3)", fontWeight: 600 }}>M BUX</span>
              </div>
              <div style={{ marginTop: 6, fontSize: 12, color: "var(--ink-3)" }} className="mono">{fmt.bux(supplyBux)} circulating</div>
            </div>
          </div>

          <div style={{ marginTop: "auto", paddingTop: 22 }}>
            <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", padding: "12px 16px",
              background: "rgba(232,176,75,.07)", border: "1px solid rgba(232,176,75,.22)", borderRadius: "var(--r-md)" }}>
              <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
                <Icons.shield size={17} style={{ color: "var(--action)" }} />
                <span style={{ fontSize: 13, color: "var(--ink-2)" }}>Reported reserve per BUX</span>
              </div>
              <span className="mono" style={{ fontSize: 14, fontWeight: 600, color: "var(--action)" }}>
                {rs.perBuxMg === null ? "n/a (no supply)" : `${rs.perBuxMg.toFixed(4)} mg Au`}
              </span>
            </div>
          </div>
        </div>
        {/* right: coin stack */}
        <div style={{ borderLeft: "1px solid var(--line)", background: "radial-gradient(circle at 50% 30%, rgba(232,176,75,.05), transparent 70%)", padding: "14px 10px", position: "relative" }}>
          <CoinStack level={level} pulse={pulse} />
          <div style={{ position: "absolute", bottom: 12, left: 0, right: 0, textAlign: "center", fontSize: 10.5, letterSpacing: ".14em", textTransform: "uppercase", color: "var(--ink-faint)" }}>
            Coverage · {coverageLabel(rs.coverage)}
          </div>
        </div>
      </div>
    </Card>
  );
}

/* Compact backing meter: reported reserve against the advertised peg */
function BackingBar() {
  const { state: backendState } = useData();
  const rs = reserveStats(backendState.economy);
  const pct = rs.coverage === null ? 0 : Math.max(0, Math.min(100, rs.coverage * 100));
  const tone = rs.coverage === null ? "var(--ink-3)" : rs.coverage >= 1 ? "var(--action)" : "var(--warn)";
  return (
    <div title={`Reported reserve ${rs.reserveG.toFixed(2)} g against ${TARGET_PEG_MG_PER_BUX} mg/BUX for ${fmt.bux(rs.supplyBux)} BUX`}>
      <div style={{ display: "flex", alignItems: "center", gap: 12 }}>
        <div style={{ flex: 1, height: 7, borderRadius: 99, background: "var(--surface-hi)", overflow: "hidden" }}>
          <div style={{ width: `${pct}%`, height: "100%", borderRadius: 99, transition: "width .6s ease",
            background: "linear-gradient(90deg, var(--action), var(--action))",
            backgroundSize: "200% 100%", animation: pct > 0 ? "shimmer 3.5s linear infinite" : "none" }} />
        </div>
        <span className="mono" style={{ fontSize: 13, fontWeight: 600, color: tone }}>{coverageLabel(rs.coverage)}</span>
      </div>
      <div style={{ fontSize: 10.5, color: "var(--ink-3)", marginTop: 5 }}>reported reserve · {TARGET_PEG_MG_PER_BUX} mg/BUX peg</div>
    </div>
  );
}

Object.assign(window, { CoinStack, GoldVault, BackingBar });


// --- onboarding.jsx ---
/* ============================================================
   BUCKS — Onboarding / account creation
   ============================================================ */

function BrandPanel() {
  const { state } = useData();
  const rs = reserveStats(state.economy);
  return (
    <div style={{ position: "relative", overflow: "hidden",
      background: "radial-gradient(120% 90% at 18% 0%, #1a1206 0%, #0c0b10 46%, #08080b 100%)",
      borderRight: "1px solid var(--line)", padding: "48px 44px", display: "flex", flexDirection: "column" }}>
      {/* faint grid */}
      <div style={{ position: "absolute", inset: 0, opacity: .5,
        backgroundImage: "linear-gradient(var(--line) 1px, transparent 1px), linear-gradient(90deg, var(--line) 1px, transparent 1px)",
        backgroundSize: "44px 44px", maskImage: "radial-gradient(80% 70% at 30% 30%, #000, transparent)" }} />
      <div style={{ position: "relative", display: "flex", alignItems: "center", gap: 11 }}>
        <BucksMark size={34} glow />
        <span style={{ fontFamily: "var(--font-display)", fontWeight: 700, fontSize: 20, letterSpacing: "-.01em" }}>Bucks</span>
      </div>

      <div style={{ position: "relative", marginTop: "auto" }}>
        <div style={{ width: 168, height: 200, margin: "0 0 32px" }}>
          <CoinStack level={rs.coverage === null ? 0.3 : Math.min(1, rs.coverage)} />
        </div>
        <h1 style={{ font: "600 42px/1.04 var(--font-display)", letterSpacing: "-.025em", margin: 0, textWrap: "balance" }}>
          Proof-of-work with<br /><span style={{ color: "var(--action)" }}>a gold reserve.</span>
        </h1>
        <p style={{ fontSize: 16, lineHeight: 1.55, color: "var(--ink-2)", marginTop: 18, maxWidth: 420 }}>
          Mine BUX and watch the reserve the vault signers record on-chain. The figures here are what they reported; this node does not audit physical gold.
        </p>
        <div style={{ display: "flex", gap: 22, marginTop: 30 }}>
          {[[rs.perBuxMg === null ? "—" : `${rs.perBuxMg.toFixed(4)} mg`, "reported reserve / BUX"],
            [coverageLabel(rs.coverage), `of the ${TARGET_PEG_MG_PER_BUX} mg peg`],
            [fmt.bux(rs.supplyBux), "BUX circulating"]].map(([a, b]) => (
            <div key={b}>
              <div className="mono" style={{ fontSize: 21, fontWeight: 600, color: "var(--action)" }}>{a}</div>
              <div style={{ fontSize: 12, color: "var(--ink-3)", marginTop: 3 }}>{b}</div>
            </div>
          ))}
        </div>
      </div>

      <div style={{ position: "relative", marginTop: 36, display: "flex", alignItems: "center", gap: 8, fontSize: 12, color: "var(--ink-faint)" }}>
        <Icons.lock size={13} /> Reserve figures as reported on-chain by the vault signers (3-of-5)
      </div>
    </div>
  );
}

function StepDots({ step, total }) {
  return (
    <div style={{ display: "flex", gap: 7 }}>
      {Array.from({ length: total }).map((_, i) => (
        <div key={i} style={{ height: 4, borderRadius: 99, width: i === step ? 28 : 16,
          background: i <= step ? "var(--accent)" : "var(--surface-hi)", transition: "all .3s" }} />
      ))}
    </div>
  );
}

function Onboarding({ onDone, user, setUser }) {
  const { auth, createWallet, restoreWallet, getLanguages } = useData();

  // Steps:
  // 0: Welcome (Create or Restore)
  // 1: Profile (name, email) + phrase language / length
  // 2: Recovery phrase reveal (shown ONCE; the node does not keep it)
  // 3: Restore wallet form
  // 4: Finished Success screen
  const [step, setStep] = React.useState(0);
  const [created, setCreated] = React.useState(null); // { mnemonic, direction, wordCount, language }
  const [revealMnemonic, setRevealMnemonic] = React.useState(false);
  const [backedUp, setBackedUp] = React.useState(false);
  const [busy, setBusy] = React.useState(false);
  const [languages, setLanguages] = React.useState(null);
  const [language, setLanguage] = React.useState("");
  const [words, setWords] = React.useState(13);

  // Restore state
  const [restorePhrase, setRestorePhrase] = React.useState("");

  const canAccount = user.name.trim().length > 0;

  React.useEffect(() => {
    if (step !== 1 || languages !== null) return;
    getLanguages().then((d) => {
      if (!d) { setLanguages([]); return; }
      setLanguages(d.languages || []);
      setLanguage(d.default || "");
    });
  }, [step, languages]);

  // Create the wallet on the node and show its phrase once
  const handleCreateWallet = async () => {
    setBusy(true);
    const data = await createWallet({ language, words });
    setBusy(false);
    if (data && data.mnemonic) {
      setCreated({ mnemonic: data.mnemonic, direction: data.direction || "ltr", wordCount: data.wordCount || 13, language: data.language });
      setRevealMnemonic(false);
      setBackedUp(false);
      setStep(2);
    }
  };

  // The phrase leaves memory here; the node never returns it again
  const handleFinalize = () => {
    setCreated(null);
    localStorage.setItem('bucks_user', JSON.stringify({
      name: user.name,
      email: user.email,
      balance: 0,
      pending: 0,
      lifetime: 0
    }));
    setStep(4);
  };

  // Restore wallet from user's recovery phrase
  const handleRestoreSubmit = async (e) => {
    e.preventDefault();
    if (!restorePhrase.trim()) return;
    setBusy(true);
    const success = await restoreWallet(restorePhrase.trim());
    setBusy(false);
    if (success) {
      setRestorePhrase("");
      const restoredUser = {
        name: user.name.trim() || "Miner Node",
        email: user.email.trim() || "",
        balance: 0,
        pending: 0,
        lifetime: 0
      };
      setUser(restoredUser);
      localStorage.setItem('bucks_user', JSON.stringify(restoredUser));
      setStep(4);
    }
  };

  const selectStyle = { width: "100%", height: 44, padding: "0 12px", background: "var(--surface-1)", color: "var(--ink)",
    border: "1px solid var(--line-strong)", borderRadius: "var(--r-md)", font: "500 14px var(--font-ui)" };

  return (
    <div style={{ height: "100%", display: "grid", gridTemplateColumns: "minmax(420px, 0.92fr) 1fr" }}>
      <BrandPanel />
      <div style={{ display: "grid", placeItems: "center", padding: 40, overflow: "auto" }}>
        <div style={{ width: "100%", maxWidth: 440 }} key={step}>
          <div style={{ animation: "rise .4s ease forwards" }}>

            {step === 0 && (
              <>
                <Eyebrow color="var(--accent)">Welcome</Eyebrow>
                <h2 style={{ font: "600 30px/1.1 var(--font-display)", letterSpacing: "-.02em", margin: "12px 0 10px" }}>Start mining Bucks</h2>
                <p style={{ color: "var(--ink-2)", fontSize: 15, lineHeight: 1.55, margin: 0 }}>Set up this node's HD wallet in under a minute. Keys stay on the node; you keep the recovery phrase.</p>
                {!auth.walletsEnabled && (
                  <div style={{ marginTop: 18, padding: "12px 14px", borderRadius: "var(--r-md)", background: "rgba(240,106,93,.1)", border: "1px solid rgba(240,106,93,.35)", fontSize: 13, color: "var(--ink-2)" }}>
                    The node's wallet file could not be read, so wallet actions are disabled until it is fixed: <span className="mono">{auth.walletError}</span>
                  </div>
                )}
                <div style={{ marginTop: 28, display: "grid", gap: 11 }}>
                  <Button size="lg" full iconRight={Icons.arrowR} disabled={!auth.walletsEnabled} onClick={() => setStep(1)}>Create wallet</Button>
                  <Button size="lg" full variant="ghost" disabled={!auth.walletsEnabled} onClick={() => setStep(3)}>I already mine — restore wallet</Button>
                </div>
                <div style={{ marginTop: 26, display: "grid", gap: 14 }}>
                  {[[Icons.bolt, "Plug-and-earn rigs", "Configure ASICs, pool targets, and power efficiency."],
                    [Icons.vault, "Reported reserve", "The vault signers record the gold reserve on-chain; the console shows it as reported."],
                    [Icons.shield, "Self-custody", "Your 13-word phrase is the only backup. Write it down; nobody can recover it for you."]].map(([Ic, t, d]) => (
                    <div key={t} style={{ display: "flex", gap: 13, alignItems: "flex-start" }}>
                      <div style={{ width: 36, height: 36, borderRadius: 10, background: "var(--surface-2)", border: "1px solid var(--line)", display: "grid", placeItems: "center", flexShrink: 0, color: "var(--accent)" }}><Ic size={17} /></div>
                      <div>
                        <div style={{ fontSize: 14, fontWeight: 600 }}>{t}</div>
                        <div style={{ fontSize: 13, color: "var(--ink-3)", marginTop: 2 }}>{d}</div>
                      </div>
                    </div>
                  ))}
                </div>
              </>
            )}

            {step === 1 && (
              <>
                <StepDots step={0} total={3} />
                <h2 style={{ font: "600 27px/1.1 var(--font-display)", letterSpacing: "-.02em", margin: "20px 0 8px" }}>Create your profile</h2>
                <p style={{ color: "var(--ink-2)", fontSize: 14.5, margin: "0 0 24px" }}>A display name for this console (kept in this browser only). Access to the node is by API token, not by password.</p>
                <div style={{ display: "grid", gap: 16 }}>
                  <Field label="Display name" icon={Icons.user} placeholder="Satoshi Nakamoto" value={user.name} onChange={(v) => setUser({ ...user, name: v })} />
                  <Field label="Email (optional)" icon={Icons.mail} type="email" placeholder="you@domain.com" value={user.email} onChange={(v) => setUser({ ...user, email: v })} />
                  <div>
                    <div style={{ font: "600 12.5px var(--font-ui)", color: "var(--ink-2)", marginBottom: 8 }}>Recovery phrase language</div>
                    <select value={language} onChange={(e) => setLanguage(e.target.value)} style={selectStyle} disabled={!languages || languages.length === 0}>
                      {(languages || []).map((l) => (
                        <option key={l.lang} value={l.lang}>{l.lang}{l.status === "dev" ? " (development word list)" : ""}{l.direction === "rtl" ? " · right-to-left" : ""}</option>
                      ))}
                    </select>
                  </div>
                  <div>
                    <div style={{ font: "600 12.5px var(--font-ui)", color: "var(--ink-2)", marginBottom: 8 }}>Phrase length</div>
                    <Segmented options={[{ id: 13, label: "13 words", sub: "128-bit" }, { id: 25, label: "25 words", sub: "256-bit" }]} value={words} onChange={setWords} />
                  </div>
                </div>
                <div style={{ marginTop: 28, display: "grid", gridTemplateColumns: "1fr 1.8fr", gap: 10 }}>
                  <Button variant="ghost" onClick={() => setStep(0)}>Back</Button>
                  <Button size="lg" full disabled={!canAccount || busy} iconRight={Icons.arrowR} onClick={handleCreateWallet}>{busy ? "Creating…" : "Create wallet & show phrase"}</Button>
                </div>
              </>
            )}

            {step === 2 && created && (
              <>
                <StepDots step={1} total={3} />
                <h2 style={{ font: "600 27px/1.1 var(--font-display)", letterSpacing: "-.02em", margin: "20px 0 8px" }}>Secret Recovery Phrase</h2>
                <p style={{ color: "var(--ink-2)", fontSize: 14, lineHeight: 1.45, margin: "0 0 16px" }}>
                  Your wallet exists on the node now. These {created.wordCount} words are the <strong>only</strong> way to recover it.
                </p>

                <div style={{ marginBottom: 16 }}>
                  <BackupWarning wordCount={created.wordCount} />
                </div>

                <div style={{ marginBottom: 20 }}>
                  <MnemonicGrid phrase={created.mnemonic} direction={created.direction} revealed={revealMnemonic} onReveal={() => setRevealMnemonic(true)} />
                </div>

                <div style={{ display: 'flex', gap: 8, alignItems: 'center', margin: '20px 0' }}>
                  <input
                    type="checkbox"
                    id="backup-check"
                    checked={backedUp}
                    disabled={!revealMnemonic}
                    onChange={(e) => setBackedUp(e.target.checked)}
                    style={{ width: 18, height: 18, cursor: 'pointer', accentColor: 'var(--accent)' }}
                  />
                  <label htmlFor="backup-check" style={{ fontSize: 13.5, color: 'var(--ink-2)', cursor: 'pointer', userSelect: 'none' }}>
                    I have written down all {created.wordCount} words, in order, in a safe place.
                  </label>
                </div>

                <Button size="lg" full disabled={!backedUp} iconRight={Icons.arrowR} onClick={handleFinalize}>I wrote it down — continue</Button>
              </>
            )}

            {step === 3 && (
              <form onSubmit={handleRestoreSubmit}>
                <Eyebrow color="var(--accent)">Restore Node</Eyebrow>
                <h2 style={{ font: "600 27px/1.1 var(--font-display)", letterSpacing: "-.02em", margin: "20px 0 8px" }}>Restore Wallet</h2>
                <p style={{ color: "var(--ink-2)", fontSize: 14, margin: "0 0 20px" }}>
                  Enter your 13- or 25-word recovery phrase. The language is detected automatically and the checksum is verified before anything is derived.
                </p>

                <div style={{ display: "grid", gap: 16, marginBottom: 20 }}>
                  <Field label="Display name (optional)" icon={Icons.user} placeholder="Satoshi Nakamoto" value={user.name} onChange={(v) => setUser({ ...user, name: v })} />
                  <div>
                    <div style={{ font: "600 12.5px var(--font-ui)", color: "var(--ink-2)", marginBottom: 8 }}>Recovery Phrase</div>
                    <textarea
                      value={restorePhrase}
                      onChange={(e) => setRestorePhrase(e.target.value)}
                      placeholder="word1 word2 … word13"
                      rows="4"
                      required
                      dir="auto"
                      autoComplete="off"
                      spellCheck={false}
                      style={{
                        width: '100%',
                        background: 'var(--surface-1)',
                        border: '1px solid var(--line-strong)',
                        borderRadius: 'var(--r-md)',
                        padding: 12,
                        color: 'var(--ink)',
                        fontSize: 15,
                        fontFamily: 'var(--font-ui)',
                        resize: 'none',
                        outline: 'none',
                        boxShadow: 'inset 0 1px 2px rgba(0,0,0,.3)'
                      }}
                    />
                  </div>
                </div>

                <div style={{ display: "grid", gridTemplateColumns: "1fr 1.8fr", gap: 10 }}>
                  <Button variant="ghost" onClick={() => setStep(0)}>Back</Button>
                  <Button type="submit" disabled={!restorePhrase.trim() || busy} iconRight={Icons.arrowR}>{busy ? "Checking…" : "Restore Now"}</Button>
                </div>
              </form>
            )}

            {step === 4 && (
              <div style={{ textAlign: "center" }}>
                <div style={{ width: 86, height: 86, margin: "0 auto 8px", display: "grid", placeItems: "center", position: "relative" }}>
                  <Ring pct={100} size={86} sw={4} color="var(--pos)">
                    <Icons.check size={36} style={{ color: "var(--pos)" }} />
                  </Ring>
                </div>
                <h2 style={{ font: "600 28px/1.1 var(--font-display)", letterSpacing: "-.02em", margin: "16px 0 8px" }}>You're in, {user.name.split(" ")[0] || "miner"}.</h2>
                <p style={{ color: "var(--ink-2)", fontSize: 15, margin: "0 auto 28px", maxWidth: 320, lineHeight: 1.5 }}>Your wallet is active on the node. Now, let's configure your mining rig.</p>
                <Button size="lg" full iconRight={Icons.arrowR} onClick={onDone}>Set up my first rig</Button>
                <div style={{ marginTop: 14 }}>
                  <Button variant="quiet" size="sm" onClick={onDone}>Skip — take me to the dashboard</Button>
                </div>
              </div>
            )}

          </div>
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { Onboarding });


// --- configure.jsx ---
/* ============================================================
   BUCKS — Configure mining (rig · pool · power · payout)
   ============================================================ */

function deriveOutput(rig, powerPct) {
  const f = powerPct / 100;
  const th = rig.th * Math.pow(f, 0.88);
  const watts = Math.round(rig.watts * f);
  const daily = rig.daily * (th / rig.th);
  const elec = (watts / 1000) * 24 * 0.08; // $/day @ $0.08/kWh
  const gross = daily * window.BUX_USD;
  const goldDay = daily * window.PEG_G_PER_BUX * 1000; // mg/day
  return { th, watts, daily, elec, gross, net: gross - elec, goldDay, eff: th / (watts / 1000) };
}

function RigOption({ rig, selected, onSelect }) {
  return (
    <Card pad={18} hover onClick={onSelect}
      style={{ borderColor: selected ? "var(--accent-line)" : "var(--line)",
        boxShadow: selected ? "0 0 0 1px var(--accent-line), var(--shadow-2)" : "var(--shadow-1)",
        background: selected ? "linear-gradient(180deg, color-mix(in oklch, var(--accent) 7%, var(--surface-2)), var(--surface-1))" : undefined }}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start" }}>
        <div style={{ display: "flex", gap: 13 }}>
          <div style={{ width: 46, height: 46, borderRadius: 11, background: "var(--surface-3)", border: "1px solid var(--line)", display: "grid", placeItems: "center", color: selected ? "var(--accent)" : "var(--ink-2)" }}>
            <Icons.cpu size={23} />
          </div>
          <div>
            <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
              <span style={{ fontSize: 15.5, fontWeight: 600, fontFamily: "var(--font-display)" }}>{rig.name}</span>
              <Chip tone={rig.tier === "Flagship" ? "gold" : "neutral"}>{rig.tier}</Chip>
            </div>
            <div style={{ fontSize: 12.5, color: "var(--ink-3)", marginTop: 4, maxWidth: 280 }}>{rig.blurb}</div>
          </div>
        </div>
        <div style={{ width: 22, height: 22, borderRadius: 99, border: `2px solid ${selected ? "var(--accent)" : "var(--line-strong)"}`, display: "grid", placeItems: "center", flexShrink: 0, background: selected ? "var(--accent)" : "transparent", transition: "all .15s" }}>
          {selected && <Icons.check size={13} stroke="#1a1206" sw={2.4} />}
        </div>
      </div>
      <div style={{ display: "flex", gap: 26, marginTop: 16, paddingTop: 14, borderTop: "1px solid var(--line)" }}>
        {[[`${rig.th}`, "TH/s"], [`${(rig.watts/1000).toFixed(2)}`, "kW"], [`${fmt.bux(rig.daily)}`, "BUX/day"], [fmt.usd(rig.price), "hardware"]].map(([v, l]) => (
          <div key={l}>
            <div className="mono tnum" style={{ fontSize: 15, fontWeight: 600 }}>{v}</div>
            <div style={{ fontSize: 11, color: "var(--ink-3)", marginTop: 2 }}>{l}</div>
          </div>
        ))}
      </div>
    </Card>
  );
}

function Configure({ config, setConfig, onLaunch, initial }) {
  const { state: backendState, toggleMining, copyToClipboard } = useData();
  const [c, setC] = React.useState(config);
  const rig = RIGS.find((r) => r.id === c.rigId) || RIGS[0];
  const out = deriveOutput(rig, c.power);
  const pool = POOLS.find((p) => p.id === c.poolId) || POOLS[0];
  const set = (patch) => setC({ ...c, ...patch });
  
  const launch = async () => {
    // Save configuration locally
    setConfig(c);
    localStorage.setItem('bucks_rig_config', JSON.stringify(c));
    
    // If the miner isn't running on the server yet, trigger it!
    if (!backendState.mining.active) {
      await toggleMining();
    }
    onLaunch();
  };

  const powerLabel = c.power < 80 ? "Eco" : c.power < 100 ? "Balanced" : c.power <= 102 ? "Rated" : "Overclock";

  return (
    <div style={{ display: "grid", gridTemplateColumns: "1fr 340px", gap: 24, alignItems: "start", maxWidth: 1180, margin: "0 auto" }}>
      {/* left: choices */}
      <div style={{ display: "grid", gap: 26 }}>
        <div>
          <div style={{ display: "flex", alignItems: "center", gap: 10, marginBottom: 14 }}>
            <span style={{ width: 24, height: 24, borderRadius: 99, background: "var(--accent-soft)", color: "var(--accent)", display: "grid", placeItems: "center", font: "700 12px var(--font-mono)" }}>1</span>
            <h3 style={{ font: "600 17px var(--font-display)", margin: 0 }}>Choose your rig</h3>
          </div>
          <div style={{ display: "grid", gap: 12 }}>
            {RIGS.map((r) => <RigOption key={r.id} rig={r} selected={r.id === c.rigId} onSelect={() => set({ rigId: r.id })} />)}
          </div>
        </div>

        <div>
          <div style={{ display: "flex", alignItems: "center", gap: 10, marginBottom: 14 }}>
            <span style={{ width: 24, height: 24, borderRadius: 99, background: "var(--accent-soft)", color: "var(--accent)", display: "grid", placeItems: "center", font: "700 12px var(--font-mono)" }}>2</span>
            <h3 style={{ font: "600 17px var(--font-display)", margin: 0 }}>Point at a pool</h3>
          </div>
          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr 1fr", gap: 12 }}>
            {POOLS.map((p) => {
              const sel = p.id === c.poolId;
              return (
                <Card key={p.id} pad={16} hover onClick={() => set({ poolId: p.id })}
                  style={{ borderColor: sel ? "var(--accent-line)" : "var(--line)", boxShadow: sel ? "0 0 0 1px var(--accent-line)" : "var(--shadow-1)" }}>
                  <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
                    <span style={{ fontSize: 14, fontWeight: 600 }}>{p.name}</span>
                    {p.vault && <Icons.vault size={15} style={{ color: "var(--action)" }} />}
                  </div>
                  <div style={{ fontSize: 11.5, color: "var(--ink-3)", marginTop: 6, minHeight: 46, lineHeight: 1.45 }}>{p.blurb}</div>
                  <div style={{ display: "flex", gap: 14, marginTop: 8, paddingTop: 10, borderTop: "1px solid var(--line)" }}>
                    <div><div className="mono" style={{ fontSize: 13, fontWeight: 600 }}>{p.fee}%</div><div style={{ fontSize: 10, color: "var(--ink-3)" }}>fee</div></div>
                    <div><div className="mono" style={{ fontSize: 13, fontWeight: 600 }}>{p.latency}ms</div><div style={{ fontSize: 10, color: "var(--ink-3)" }}>latency</div></div>
                  </div>
                </Card>
              );
            })}
          </div>
        </div>

        <div>
          <div style={{ display: "flex", alignItems: "center", gap: 10, marginBottom: 14 }}>
            <span style={{ width: 24, height: 24, borderRadius: 99, background: "var(--accent-soft)", color: "var(--accent)", display: "grid", placeItems: "center", font: "700 12px var(--font-mono)" }}>3</span>
            <h3 style={{ font: "600 17px var(--font-display)", margin: 0 }}>Tune power</h3>
          </div>
          <Card pad={22}>
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "baseline", marginBottom: 18 }}>
              <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
                <Icons.bolt size={18} style={{ color: "var(--accent)" }} />
                <span style={{ fontSize: 14.5, fontWeight: 600 }}>Power target</span>
                <Chip tone={c.power > 102 ? "gold" : "accent"}>{powerLabel}</Chip>
              </div>
              <span className="mono tnum" style={{ fontSize: 22, fontWeight: 600 }}>{c.power}%</span>
            </div>
            <Slider value={c.power} min={60} max={110} step={1} onChange={(v) => set({ power: v })} />
            <div style={{ display: "flex", justifyContent: "space-between", marginTop: 9, fontSize: 11, color: "var(--ink-3)" }} className="mono">
              <span>60% · quiet</span><span>100% · rated</span><span>110% · max</span>
            </div>
            <div style={{ display: "grid", gridTemplateColumns: "repeat(3,1fr)", gap: 14, marginTop: 20, paddingTop: 18, borderTop: "1px solid var(--line)" }}>
              <div><Eyebrow>Hashrate</Eyebrow><div className="mono tnum" style={{ fontSize: 19, fontWeight: 600, marginTop: 6 }}>{out.th.toFixed(0)} <span style={{ fontSize: 12, color: "var(--ink-3)" }}>TH/s</span></div></div>
              <div><Eyebrow>Draw</Eyebrow><div className="mono tnum" style={{ fontSize: 19, fontWeight: 600, marginTop: 6 }}>{(out.watts/1000).toFixed(2)} <span style={{ fontSize: 12, color: "var(--ink-3)" }}>kW</span></div></div>
              <div><Eyebrow>Efficiency</Eyebrow><div className="mono tnum" style={{ fontSize: 19, fontWeight: 600, marginTop: 6 }}>{out.eff.toFixed(1)} <span style={{ fontSize: 12, color: "var(--ink-3)" }}>TH/kW</span></div></div>
            </div>
          </Card>
        </div>

        <div>
          <div style={{ display: "flex", alignItems: "center", gap: 10, marginBottom: 14 }}>
            <span style={{ width: 24, height: 24, borderRadius: 99, background: "var(--accent-soft)", color: "var(--accent)", display: "grid", placeItems: "center", font: "700 12px var(--font-mono)" }}>4</span>
            <h3 style={{ font: "600 17px var(--font-display)", margin: 0 }}>Payout & node info</h3>
          </div>
          <Card pad={22}>
            <div style={{ fontSize: 12.5, color: "var(--ink-2)", marginBottom: 10, fontWeight: 600 }}>Frequency</div>
            <Segmented options={PAYOUT_FREQ} value={c.payout} onChange={(v) => set({ payout: v })} />
            
            <div style={{ fontSize: 12.5, color: "var(--ink-2)", margin: "20px 0 10px", fontWeight: 600 }}>Node reward address</div>
            <div style={{ display: "flex", gap: 10, alignItems: "center", padding: "10px 14px", background: "var(--surface-1)", border: "1px solid var(--line-strong)", borderRadius: "var(--r-md)", minHeight: 48 }}>
              <Icons.wallet size={17} style={{ color: "var(--accent)", flexShrink: 0 }} />
              <span className="mono" style={{ flex: 1, fontSize: 13, overflow: "hidden", textOverflow: "ellipsis", color: "var(--ink-2)" }}>
                {backendState.mining.minerAddress || backendState.primaryWallet?.address || "No address loaded"}
              </span>
              {(backendState.mining.minerAddress || backendState.primaryWallet?.address) && (
                <button onClick={() => copyToClipboard(backendState.mining.minerAddress || backendState.primaryWallet.address)} style={{ background: "none", border: "none", color: "var(--ink-3)", cursor: "pointer" }} title="Copy Address">
                  <Icons.copy size={15} />
                </button>
              )}
            </div>
          </Card>
        </div>

        {/* Recovery phrase: shown once at creation, never served again (API-2) */}
        <div>
          <div style={{ display: "flex", alignItems: "center", gap: 10, marginBottom: 14 }}>
            <span style={{ width: 24, height: 24, borderRadius: 99, background: "var(--accent-soft)", color: "var(--accent)", display: "grid", placeItems: "center", font: "700 12px var(--font-mono)" }}>5</span>
            <h3 style={{ font: "600 17px var(--font-display)", margin: 0 }}>Wallet backup</h3>
          </div>
          <Card pad={22}>
            <div style={{ display: "flex", gap: 12, alignItems: "flex-start" }}>
              <Icons.shield size={18} style={{ color: "var(--warn)", flexShrink: 0, marginTop: 2 }} />
              <p style={{ fontSize: 13, color: "var(--ink-2)", lineHeight: 1.55, margin: 0 }}>
                Your recovery phrase was shown once, when the wallet was created. The node does not store it and this console cannot show it again.
                If you did not write it down, create a new wallet now and move your funds to it; the old phrase cannot be recovered by anyone.
              </p>
            </div>
          </Card>
        </div>
      </div>

      {/* right: live projection */}
      <div style={{ position: "sticky", top: 80, display: "grid", gap: 16 }}>
        <Card pad={22} glow>
          <Eyebrow color="var(--accent)">Projected output</Eyebrow>
          <div style={{ marginTop: 16 }}>
            <div style={{ display: "flex", alignItems: "baseline", gap: 7 }}>
              <span className="mono tnum" style={{ fontSize: 38, fontWeight: 600, color: "var(--accent)", lineHeight: 1 }}>{fmt.bux(out.daily)}</span>
              <span style={{ fontSize: 14, color: "var(--ink-3)", fontWeight: 600 }}>BUX / day</span>
            </div>
            <div style={{ fontSize: 12.5, color: "var(--ink-3)", marginTop: 7 }} className="mono">≈ {fmt.usd(out.gross)} · {out.goldDay.toFixed(2)} mg Au minted daily</div>
          </div>
          <div style={{ marginTop: 20, display: "grid", gap: 11 }}>
            {[["Gross mining", "+" + fmt.usd(out.gross), "var(--pos)"],
              ["Power cost", "−" + fmt.usd(out.elec), "var(--ink-2)"],
              [`Pool fee · ${pool.fee}%`, "−" + fmt.usd(out.gross * pool.fee/100), "var(--ink-2)"]].map(([l, v, col]) => (
              <div key={l} style={{ display: "flex", justifyContent: "space-between", fontSize: 13 }}>
                <span style={{ color: "var(--ink-2)" }}>{l}</span>
                <span className="mono" style={{ color: col, fontWeight: 600 }}>{v}</span>
              </div>
            ))}
            <div style={{ height: 1, background: "var(--line)", margin: "3px 0" }} />
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
              <span style={{ fontSize: 13.5, fontWeight: 600 }}>Net / day</span>
              <span className="mono" style={{ fontSize: 17, fontWeight: 700, color: "var(--pos)" }}>{fmt.usd(out.net - out.gross*pool.fee/100)}</span>
            </div>
            <div style={{ display: "flex", justifyContent: "space-between", fontSize: 12, color: "var(--ink-3)" }}>
              <span>Projected / 30 days</span>
              <span className="mono">{fmt.bux(out.daily * 30)} BUX</span>
            </div>
          </div>
        </Card>

        <Card pad={18}>
          <div style={{ display: "flex", gap: 10, alignItems: "center", marginBottom: 12 }}>
            <Icons.vault size={16} style={{ color: "var(--action)" }} />
            <span style={{ fontSize: 13, fontWeight: 600 }}>Backing</span>
          </div>
          <BackingBar />
          <div style={{ fontSize: 11.5, color: "var(--ink-3)", marginTop: 10, lineHeight: 1.45 }}>Reported reserve per BUX: <span className="mono" style={{ color: "var(--action)" }}>{window.PEG_MG_PER_BUX.toFixed(4)} mg</span> (as recorded on-chain by the vault signers).</div>
        </Card>

        {/* Controller card (Start/Stop) */}
        <Card pad={18}>
          <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
            <div>
              <div style={{ fontSize: 11.5, color: "var(--ink-3)", textTransform: "uppercase", fontWeight: 600 }}>Miner Status</div>
              <div style={{ display: "flex", alignItems: "center", gap: 6, fontSize: 14.5, fontWeight: 600, marginTop: 4, color: backendState.mining.active ? "var(--pos)" : "var(--neg)" }}>
                <LiveDot color={backendState.mining.active ? "var(--pos)" : "var(--neg)"} />
                {backendState.mining.active ? "Active & Mining" : "Stopped"}
              </div>
            </div>
            <Button variant={backendState.mining.active ? "ghost" : "primary"} onClick={toggleMining}>
              {backendState.mining.active ? "Stop Mining" : "Start Mining"}
            </Button>
          </div>
        </Card>

        <Button size="lg" variant="gold" full iconRight={Icons.arrowR} onClick={launch}>
          {initial ? "Launch rig & start mining" : "Save configuration"}
        </Button>
      </div>
    </div>
  );
}

Object.assign(window, { Configure, deriveOutput });


// --- dashboard.jsx ---
/* ============================================================
   BUCKS — Live mining dashboard
   ============================================================ */

function Kpi({ label, value, unit, delta, icon: Ic, accent, spark, sparkColor }) {
  return (
    <Card pad={18} glow>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start" }}>
        <Eyebrow>{label}</Eyebrow>
        {Ic && <Ic size={16} style={{ color: accent || "var(--ink-3)" }} />}
      </div>
      <div style={{ display: "flex", alignItems: "baseline", gap: 6, marginTop: 12 }}>
        <span className="mono tnum" style={{ fontSize: 27, fontWeight: 600, lineHeight: 1, color: accent || "var(--ink)" }}>{value}</span>
        {unit && <span style={{ fontSize: 12, color: "var(--ink-3)", fontWeight: 600 }}>{unit}</span>}
      </div>
      {delta && (
        <div style={{ marginTop: 9, display: "flex", alignItems: "center", gap: 5, fontSize: 12 }}>
          <Icons.arrowUp size={12} style={{ color: "var(--pos)" }} />
          <span className="mono" style={{ color: "var(--pos)", fontWeight: 600 }}>{delta}</span>
          <span style={{ color: "var(--ink-3)" }}>today</span>
        </div>
      )}
      {spark && <div style={{ marginTop: 8 }}><Sparkline data={spark} w={200} h={30} color={sparkColor || "var(--accent)"} strokeW={1.6} /></div>}
    </Card>
  );
}

function HashratePanel({ live }) {
  const { state: backendState } = useData();
  const cur = live.hashSeries[live.hashSeries.length - 1];
  const avg = live.hashSeries.reduce((a, b) => a + b, 0) / live.hashSeries.length;
  
  // Format real CPU/GPU hashrate vs projected ASIC
  const formattedRealHash = backendState.mining.hashRate > 1000 
    ? `${(backendState.mining.hashRate / 1000).toFixed(2)} KH/s` 
    : `${backendState.mining.hashRate} H/s`;

  return (
    <Card pad={22} glow style={{ height: "100%" }}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start" }}>
        <div>
          <div style={{ display: "flex", alignItems: "center", gap: 9 }}>
            <LiveDot color={backendState.mining.active ? "var(--pos)" : "var(--neg)"} />
            <Eyebrow color={backendState.mining.active ? "var(--pos)" : "var(--ink-3)"}>
              {backendState.mining.active ? "Live hashrate" : "Miner Stopped"}
            </Eyebrow>
          </div>
          <div style={{ display: "flex", alignItems: "baseline", gap: 8, marginTop: 12 }}>
            <span className="mono tnum" style={{ fontSize: 44, fontWeight: 600, lineHeight: 1, letterSpacing: "-.02em" }}>
              {cur.toFixed(1)}
            </span>
            <span style={{ fontSize: 16, color: "var(--ink-3)", fontWeight: 600 }}>TH/s</span>
          </div>
          <div style={{ fontSize: 12.5, color: "var(--ink-3)", marginTop: 8 }} className="mono">
            avg {avg.toFixed(1)} · node speed: {formattedRealHash} · {backendState.mining.blocksFound} blocks found
          </div>
        </div>
        <div style={{ display: "flex", gap: 8 }}>
          <Chip tone="accent" icon={Icons.cpu}>{live.rig.name}</Chip>
          <Chip tone="gold" icon={Icons.vault}>{live.pool.name}</Chip>
        </div>
      </div>
      <div style={{ marginTop: 18 }}>
        <Sparkline data={live.hashSeries} w={760} h={120} color="var(--accent)" strokeW={2.2} />
      </div>
      <div style={{ display: "grid", gridTemplateColumns: "repeat(4,1fr)", gap: 18, marginTop: 18, paddingTop: 18, borderTop: "1px solid var(--line)" }}>
        {[["Core temp", `${live.temp.toFixed(0)}°C`, Icons.temp, live.temp > 70 ? "var(--warn)" : "var(--ink)"],
          ["Fan", `${live.fan.toFixed(0)}%`, Icons.fan, "var(--ink)"],
          ["Power", `${(live.watts/1000).toFixed(2)} kW`, Icons.bolt, "var(--ink)"],
          ["Uptime", live.uptime, Icons.clock, "var(--ink)"]].map(([l, v, Ic, col]) => (
          <div key={l} style={{ display: "flex", gap: 11, alignItems: "center" }}>
            <div style={{ width: 34, height: 34, borderRadius: 9, background: "var(--surface-3)", display: "grid", placeItems: "center", color: "var(--ink-3)", flexShrink: 0 }}><Ic size={17} /></div>
            <div>
              <div style={{ fontSize: 10.5, color: "var(--ink-3)", letterSpacing: ".08em", textTransform: "uppercase", fontWeight: 600 }}>{l}</div>
              <div className="mono tnum" style={{ fontSize: 16, fontWeight: 600, color: col, marginTop: 2 }}>{v}</div>
            </div>
          </div>
        ))}
      </div>
    </Card>
  );
}

function RewardsFeed({ feed }) {
  const { state: backendState } = useData();
  return (
    <Card pad={0} style={{ height: "100%", display: "flex", flexDirection: "column" }}>
      <div style={{ padding: "18px 20px 14px", display: "flex", justifyContent: "space-between", alignItems: "center", borderBottom: "1px solid var(--line)" }}>
        <div style={{ display: "flex", alignItems: "center", gap: 9 }}>
          <Icons.activity size={16} style={{ color: "var(--accent)" }} />
          <span style={{ fontSize: 14, fontWeight: 600 }}>Reward stream</span>
        </div>
        <Chip tone={backendState.mining.active ? "pos" : "neutral"}>
          <LiveDot color={backendState.mining.active ? "var(--pos)" : "var(--neg)"} />
          {backendState.mining.active ? "earning" : "offline"}
        </Chip>
      </div>
      <div style={{ flex: 1, overflow: "auto", padding: "6px 8px" }}>
        {feed.map((r, i) => (
          <div key={r.key} style={{ display: "flex", alignItems: "center", gap: 12, padding: "11px 12px", borderRadius: 10,
            animation: i === 0 ? "rise .4s ease forwards" : "none",
            background: i === 0 ? "var(--accent-soft)" : "transparent" }}>
            <div style={{ width: 30, height: 30, borderRadius: 8, background: "var(--surface-3)", display: "grid", placeItems: "center", color: "var(--action)", flexShrink: 0 }}><Icons.coin size={15} /></div>
            <div style={{ flex: 1, minWidth: 0 }}>
              <div style={{ fontSize: 13, fontWeight: 600 }}>{r.label || "Share accepted"}</div>
              <div style={{ fontSize: 11, color: "var(--ink-3)" }} className="mono">{r.time} · {r.mg.toFixed(3)} mg Au</div>
            </div>
            <span className="mono" style={{ fontSize: 13.5, fontWeight: 600, color: "var(--pos)" }}>+{r.bux.toFixed(2)}</span>
          </div>
        ))}
      </div>
    </Card>
  );
}

function Dashboard({ live, net, user, pulse, go }) {
  const { state: backendState } = useData();

  // Compute live values from the C++ backend primary wallet balance
  const balance = backendState.primaryWallet ? backendState.primaryWallet.balance / 100000000 : 0;
  const balUsd = balance * window.BUX_USD;
  const balGold = balance * window.PEG_G_PER_BUX;

  return (
    <div style={{ display: "grid", gap: 20 }}>
      {/* KPI row */}
      <div style={{ display: "grid", gridTemplateColumns: "repeat(4,1fr)", gap: 16 }}>
        <Kpi label="Vault balance" value={fmt.bux(balance)} unit="BUX" accent="var(--ink)"
          icon={Icons.wallet} delta={live.todayBux > 0 ? "+" + fmt.bux(live.todayBux) : undefined} />
        <Kpi label="≈ Gold (reported reserve)" value={balGold.toFixed(3)} unit="g Au" accent="var(--action)" icon={Icons.vault} />
        <Kpi label="Today's earnings" value={"+" + fmt.bux(live.todayBux)} unit="BUX" accent="var(--pos)" icon={Icons.spark}
          spark={live.earnSeries} sparkColor="var(--pos)" />
        <Kpi label="Net value" value={fmt.usd(balUsd)} unit="" icon={Icons.activity}
          spark={live.hashSeries.slice(-20)} />
      </div>

      {/* main row */}
      <div style={{ display: "grid", gridTemplateColumns: "1fr 360px", gap: 20 }}>
        <HashratePanel live={live} />
        <RewardsFeed feed={live.feed} />
      </div>

      {/* vault + actions */}
      <div style={{ display: "grid", gridTemplateColumns: "1fr 340px", gap: 20, alignItems: "stretch" }}>
        <GoldVault net={net} pulse={pulse} />
        <Card pad={22} style={{ display: "flex", flexDirection: "column", gap: 14 }}>
          <Eyebrow>Quick actions</Eyebrow>
          <Button variant="gold" full size="lg" icon={Icons.arrowUp} onClick={() => go("wallet")}>Withdraw BUX</Button>
          <Button variant="ghost" full icon={Icons.sliders} onClick={() => go("configure")}>Adjust rig & power</Button>
          <Button variant="ghost" full icon={Icons.plus} onClick={() => go("configure")}>Add another rig</Button>
          <div style={{ marginTop: "auto", paddingTop: 16, borderTop: "1px solid var(--line)", display: "grid", gap: 12 }}>
            <div style={{ display: "flex", justifyContent: "space-between", fontSize: 12.5 }}>
              <span style={{ color: "var(--ink-3)" }}>Blockchain Height</span>
              <span className="mono" style={{ fontWeight: 600 }}>#{backendState.height}</span>
            </div>
            <div style={{ display: "flex", justifyContent: "space-between", fontSize: 12.5 }}>
              <span style={{ color: "var(--ink-3)" }}>Gold spot</span>
              <span className="mono" style={{ fontWeight: 600, color: "var(--action)" }}>{fmt.usd(GOLD_USD_PER_G)}/g</span>
            </div>
            <div style={{ display: "flex", justifyContent: "space-between", fontSize: 12.5 }}>
              <span style={{ color: "var(--ink-3)" }}>Consensus Difficulty</span>
              <span className="mono" style={{ fontWeight: 600 }}>{backendState.difficulty}</span>
            </div>
          </div>
        </Card>
      </div>
    </div>
  );
}

Object.assign(window, { Dashboard });


// --- wallet.jsx ---
/* ============================================================
   BUCKS — Wallet & withdraw
   ============================================================ */

function txIcon(type) {
  if (type === "withdraw" || type === "send") return { Ic: Icons.arrowUp, col: "var(--ink-2)", bg: "var(--surface-3)" };
  if (type === "mint") return { Ic: Icons.vault, col: "var(--action)", bg: "rgba(232,176,75,.12)" };
  if (type === "bonus") return { Ic: Icons.spark, col: "var(--accent)", bg: "var(--accent-soft)" };
  return { Ic: Icons.coin, col: "var(--pos)", bg: "var(--pos-soft)" };
}

function WithdrawModal({ balance, onClose, onConfirm }) {
  const { sendTransaction } = useData();
  const [step, setStep] = React.useState(0);
  const [amt, setAmt] = React.useState("");
  const [dest, setDest] = React.useState("");
  const [sending, setSending] = React.useState(false);
  const n = Number(amt) || 0;
  const fee = 0.0001; // real blockchain tx fee
  const recv = Math.max(0, n - fee);
  const valid = n > 0 && n <= balance && dest.trim().length > 10;

  // Steps: 0 amount -> 1 review & confirm -> 2 sent. The node signs with
  // the wallet key after the session + CSRF checks; there is no second
  // factor (the old 6-digit "2FA" prompt accepted any code).
  const handleFinalWithdraw = async () => {
    setSending(true);
    const success = await sendTransaction(dest.trim(), n);
    setSending(false);
    if (success) {
      setStep(2);
    } else {
      setStep(0); // bounce back to start if rejected
    }
  };

  return (
    <div onClick={onClose} style={{ position: "absolute", inset: 0, zIndex: 50, background: "rgba(5,5,8,.7)", backdropFilter: "blur(6px)", display: "grid", placeItems: "center", animation: "rise .25s ease forwards", padding: 24 }}>
      <div onClick={(e) => e.stopPropagation()} style={{ width: 460, maxWidth: "100%" }}>
        <Card pad={26} glow style={{ boxShadow: "var(--shadow-pop)" }}>
          {step < 2 && (
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 22 }}>
              <h3 style={{ font: "600 19px var(--font-display)", margin: 0 }}>Withdraw BUCKS</h3>
              <button onClick={onClose} style={{ background: "var(--surface-3)", border: "none", color: "var(--ink-2)", width: 30, height: 30, borderRadius: 8, fontSize: 16 }}>✕</button>
            </div>
          )}

          {step === 0 && (
            <>
              <div style={{ background: "var(--surface-1)", border: "1px solid var(--line-strong)", borderRadius: "var(--r-md)", padding: 18 }}>
                <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 8 }}>
                  <span style={{ fontSize: 12.5, color: "var(--ink-3)" }}>Amount</span>
                  <button onClick={() => setAmt(String(balance))} style={{ background: "var(--accent-soft)", border: "none", color: "var(--accent)", font: "600 11px var(--font-ui)", padding: "3px 9px", borderRadius: 6 }}>MAX</button>
                </div>
                <div style={{ display: "flex", alignItems: "baseline", gap: 8 }}>
                  <input autoFocus value={amt} onChange={(e) => setAmt(e.target.value.replace(/[^0-9.]/g, ""))} placeholder="0.00"
                    className="mono tnum" style={{ flex: 1, background: "none", border: "none", outline: "none", color: "var(--ink)", fontSize: 34, fontWeight: 600, width: "100%" }} />
                  <span style={{ fontSize: 16, color: "var(--ink-3)", fontWeight: 600 }}>BUX</span>
                </div>
                <div style={{ fontSize: 12, color: "var(--ink-3)", marginTop: 6 }} className="mono">≈ {fmt.usd(n * window.BUX_USD)} · {(n * window.PEG_G_PER_BUX * 1000).toFixed(2)} mg Au</div>
              </div>
              <div style={{ display: "flex", justifyContent: "space-between", fontSize: 12.5, color: "var(--ink-3)", margin: "10px 2px 18px" }}>
                <span>Available</span><span className="mono">{fmt.bux(balance, 4)} BUX</span>
              </div>
              <Field label="Recipient Address" icon={Icons.wallet} value={dest} onChange={setDest} placeholder="Address starting with..." />
              <div style={{ marginTop: 22 }}>
                <Button size="lg" full disabled={!valid} iconRight={Icons.arrowR} onClick={() => setStep(1)}>{n <= 0 ? "Enter an amount" : n > balance ? "Exceeds balance" : "Review withdrawal"}</Button>
              </div>
            </>
          )}

          {step === 1 && (
            <>
              <div style={{ display: "grid", gap: 1, background: "var(--line)", borderRadius: "var(--r-md)", overflow: "hidden", border: "1px solid var(--line)" }}>
                {[["Amount", `${fmt.bux(n, 4)} BUX`], ["Network fee", `${fee} BUX`], ["Gold equivalent", `${(recv * window.PEG_G_PER_BUX * 1000).toFixed(2)} mg Au`]].map(([l, v]) => (
                  <div key={l} style={{ display: "flex", justifyContent: "space-between", padding: "13px 16px", background: "var(--surface-2)", fontSize: 13.5 }}>
                    <span style={{ color: "var(--ink-2)" }}>{l}</span><span className="mono" style={{ fontWeight: 600 }}>{v}</span>
                  </div>
                ))}
                <div style={{ display: "flex", justifyContent: "space-between", padding: "15px 16px", background: "var(--surface-3)" }}>
                  <span style={{ fontWeight: 600 }}>Recipient gets</span><span className="mono" style={{ fontWeight: 700, color: "var(--accent)", fontSize: 15 }}>{fmt.bux(recv, 4)} BUX</span>
                </div>
              </div>
              <div style={{ display: "flex", gap: 9, alignItems: "center", margin: "16px 2px", fontSize: 12, color: "var(--ink-3)" }}>
                <Icons.wallet size={14} /><span className="mono" style={{ wordBreak: "break-all" }}>{dest}</span>
              </div>
              <div style={{ display: "grid", gridTemplateColumns: "1fr 1.6fr", gap: 10 }}>
                <Button size="lg" variant="ghost" disabled={sending} onClick={() => setStep(0)}>Back</Button>
                <Button size="lg" iconRight={Icons.arrowR} disabled={sending} onClick={handleFinalWithdraw}>{sending ? "Signing & sending…" : "Confirm & send"}</Button>
              </div>
            </>
          )}

          {step === 2 && (
            <div style={{ textAlign: "center", padding: "8px 0" }}>
              <div style={{ width: 80, height: 80, margin: "0 auto 18px" }}>
                <Ring pct={100} size={80} sw={4} color="var(--pos)"><Icons.check size={34} style={{ color: "var(--pos)" }} /></Ring>
              </div>
              <h3 style={{ font: "600 21px var(--font-display)", margin: "0 0 8px" }}>Withdrawal sent</h3>
              <p style={{ fontSize: 14, color: "var(--ink-2)", margin: "0 auto 6px", maxWidth: 300, lineHeight: 1.5 }}>
                <span className="mono" style={{ color: "var(--accent)", fontWeight: 600 }}>{fmt.bux(recv, 4)} BUX</span> has been successfully broadcast to the mempool.
              </p>
              <p style={{ fontSize: 12, color: "var(--ink-3)", margin: "0 0 24px" }} className="mono">Settles in the next block.</p>
              <Button size="lg" full onClick={() => { onConfirm(n); onClose(); }}>Done</Button>
            </div>
          )}
        </Card>
      </div>
    </div>
  );
}

function ReceiveModal({ address, onClose }) {
  const { copyToClipboard } = useData();
  return (
    <div onClick={onClose} style={{ position: "absolute", inset: 0, zIndex: 50, background: "rgba(5,5,8,.7)", backdropFilter: "blur(6px)", display: "grid", placeItems: "center", animation: "rise .25s ease forwards", padding: 24 }}>
      <div onClick={(e) => e.stopPropagation()} style={{ width: 440, maxWidth: "100%" }}>
        <Card pad={26} glow style={{ boxShadow: "var(--shadow-pop)", textAlign: "center" }}>
          <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 22 }}>
            <h3 style={{ font: "600 19px var(--font-display)", margin: 0 }}>Receive BUCKS</h3>
            <button onClick={onClose} style={{ background: "var(--surface-3)", border: "none", color: "var(--ink-2)", width: 30, height: 30, borderRadius: 8, fontSize: 16 }}>✕</button>
          </div>

          <div style={{ display: "inline-block", padding: 14, background: "#fff", borderRadius: 10, marginBottom: 20 }}>
            {/* Simple CSS-rendered mockup QR Code */}
            <div style={{ width: 140, height: 140, background: "#000", display: "grid", gridTemplateColumns: "repeat(4, 1fr)", gap: 4, padding: 6 }}>
              {Array.from({ length: 16 }).map((_, i) => (
                <div key={i} style={{ background: (i % 3 === 0 || i % 4 === 1) ? "#000" : "#fff" }} />
              ))}
            </div>
          </div>

          <p style={{ fontSize: 13, color: "var(--ink-3)", margin: "0 0 16px" }}>Scan this code or copy the address below to receive coins.</p>

          <div style={{
            display: "flex", gap: 10, alignItems: "center", padding: "10px 14px",
            background: "var(--surface-1)", border: "1px solid var(--line-strong)",
            borderRadius: "var(--r-md)", minHeight: 48, marginBottom: 20, textAlign: "left"
          }}>
            <span className="mono" style={{ flex: 1, fontSize: 12.5, wordBreak: "break-all", color: "var(--ink-2)" }}>{address}</span>
            <button onClick={() => copyToClipboard(address)} style={{ background: "none", border: "none", color: "var(--ink-3)", cursor: "pointer" }} title="Copy Address">
              <Icons.copy size={15} />
            </button>
          </div>

          <Button variant="ghost" full onClick={onClose}>Close</Button>
        </Card>
      </div>
    </div>
  );
}

function Wallet({ user, live }) {
  const { state: backendState, copyToClipboard, refreshData } = useData();
  const [showW, setShowW] = React.useState(false);
  const [showR, setShowR] = React.useState(false);

  // Compute balance
  const balance = backendState.primaryWallet ? backendState.primaryWallet.balance / 100000000 : 0;
  const gold = balance * window.PEG_G_PER_BUX;

  const handleWithdrawConfirm = (n) => {
    refreshData();
  };

  // Map real transaction logs to UI logs
  const getTxs = () => {
    if (!backendState.primaryWallet || !backendState.primaryWallet.transactions) return [];
    return backendState.primaryWallet.transactions.map((tx, idx) => {
      const isMining = tx.type === "mining_reward";
      return {
        id: tx.hash,
        type: isMining ? "mined" : tx.type === "receive" ? "receive" : "withdraw",
        label: isMining ? "Mining Reward" : tx.type === "receive" ? `Received from ${tx.hash.slice(0, 10)}...` : `Sent to ${tx.hash.slice(0, 10)}...`,
        t: `Block #${tx.blockHeight}`,
        status: "settled",
        bux: isMining || tx.type === "receive" ? tx.amount / 100000000 : -tx.amount / 100000000
      };
    });
  };

  const txs = getTxs();

  return (
    <div style={{ display: "grid", gridTemplateColumns: "1fr 380px", gap: 20, alignItems: "start", maxWidth: 1180, margin: "0 auto", position: "relative" }}>
      {/* left */}
      <div style={{ display: "grid", gap: 20 }}>
        {/* balance hero */}
        <Card pad={26} glow style={{ background: "radial-gradient(120% 130% at 100% 0%, rgba(247,147,26,.10), transparent 55%), linear-gradient(180deg, var(--surface-2), var(--surface-1))" }}>
          <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start" }}>
            <div>
              <Eyebrow>Total balance</Eyebrow>
              <div style={{ display: "flex", alignItems: "baseline", gap: 9, marginTop: 14 }}>
                <span className="mono tnum" style={{ fontSize: 46, fontWeight: 600, lineHeight: 1, letterSpacing: "-.02em" }}>{fmt.bux(balance, 4)}</span>
                <span style={{ fontSize: 18, color: "var(--ink-3)", fontWeight: 600 }}>BUX</span>
              </div>
              <div style={{ display: "flex", gap: 16, marginTop: 14 }}>
                <Chip tone="gold" icon={Icons.vault}>{gold.toFixed(4)} g Au</Chip>
                <Chip tone="neutral">≈ {fmt.usd(balance * window.BUX_USD)}</Chip>
              </div>
            </div>
            <div style={{ display: "flex", gap: 10 }}>
              <Button variant="gold" icon={Icons.arrowUp} onClick={() => setShowW(true)}>Withdraw</Button>
              <Button variant="ghost" icon={Icons.copy} onClick={() => setShowR(true)}>Receive</Button>
            </div>
          </div>
          <div style={{ display: "grid", gridTemplateColumns: "repeat(3,1fr)", gap: 16, marginTop: 24, paddingTop: 20, borderTop: "1px solid var(--line)" }}>
            {[["Available", fmt.bux(balance, 4), "BUX"], ["Pending mempool", "0.00", "BUX"], ["Lifetime mined", fmt.bux(balance, 4), "BUX"]].map(([l, v, u]) => (
              <div key={l}>
                <div style={{ fontSize: 11.5, color: "var(--ink-3)", letterSpacing: ".06em", textTransform: "uppercase", fontWeight: 600 }}>{l}</div>
                <div style={{ display: "flex", alignItems: "baseline", gap: 5, marginTop: 7 }}>
                  <span className="mono tnum" style={{ fontSize: 18, fontWeight: 600 }}>{v}</span>
                  <span style={{ fontSize: 11, color: "var(--ink-3)" }}>{u}</span>
                </div>
              </div>
            ))}
          </div>
        </Card>

        {/* history */}
        <Card pad={0}>
          <div style={{ padding: "18px 22px 14px", borderBottom: "1px solid var(--line)", display: "flex", justifyContent: "space-between", alignItems: "center" }}>
            <h3 style={{ font: "600 15px var(--font-display)", margin: 0 }}>Activity</h3>
            <Button variant="quiet" size="sm" iconRight={Icons.external} onClick={() => refreshData()}>Refresh logs</Button>
          </div>
          <div>
            {txs.length === 0 ? (
              <div style={{ padding: "30px 22px", textAlign: "center", color: "var(--ink-3)", fontSize: 13 }}>No recent transactions recorded.</div>
            ) : (
              txs.map((tx, i) => {
                const { Ic, col, bg } = txIcon(tx.type);
                return (
                  <div key={tx.id || i} style={{ display: "flex", alignItems: "center", gap: 14, padding: "15px 22px", borderBottom: i < txs.length-1 ? "1px solid var(--line)" : "none" }}>
                    <div style={{ width: 38, height: 38, borderRadius: 10, background: bg, display: "grid", placeItems: "center", color: col, flexShrink: 0 }}><Ic size={18} /></div>
                    <div style={{ flex: 1, minWidth: 0 }}>
                      <div style={{ fontSize: 14, fontWeight: 600, overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}>{tx.label}</div>
                      <div style={{ fontSize: 12, color: "var(--ink-3)", marginTop: 2 }}>{tx.t}</div>
                    </div>
                    {tx.status === "pending" && <Chip tone="accent">pending</Chip>}
                    <span className="mono tnum" style={{ fontSize: 14.5, fontWeight: 600, color: tx.bux < 0 ? "var(--ink-2)" : "var(--pos)" }}>{fmt.signed(tx.bux)}</span>
                  </div>
                );
              })
            )}
          </div>
        </Card>
      </div>

      {/* right: redeem gold */}
      <div style={{ display: "grid", gap: 20, position: "sticky", top: 80 }}>
        <Card pad={0} glow style={{ overflow: "hidden" }}>
          <div style={{ padding: 22, borderBottom: "1px solid var(--line)" }}>
            <div style={{ display: "flex", alignItems: "center", gap: 9, marginBottom: 4 }}>
              <Icons.vault size={16} style={{ color: "var(--action)" }} /><Eyebrow color="var(--action)">Your gold</Eyebrow>
            </div>
            <div style={{ display: "flex", alignItems: "baseline", gap: 7, marginTop: 12 }}>
              <span className="mono tnum" style={{ fontSize: 32, fontWeight: 600, color: "var(--action)", lineHeight: 1 }}>{gold.toFixed(4)}</span>
              <span style={{ fontSize: 13, color: "var(--ink-3)", fontWeight: 600 }}>g Au</span>
            </div>
            <div style={{ fontSize: 12, color: "var(--ink-3)", marginTop: 7 }} className="mono">{(gold/31.1035).toFixed(4)} ozt · {fmt.usd(gold * GOLD_USD_PER_G)}</div>
          </div>
          <div style={{ padding: 22 }}>
            <div style={{ fontSize: 12.5, color: "var(--ink-2)", lineHeight: 1.5, marginBottom: 16 }}>Gold equivalent at the reported reserve per BUX. Redemption is handled by the vault operator, not by this node.</div>
            <Button variant="ghost" full icon={Icons.shield} disabled={gold < 1}>{gold < 1 ? `Redeem at 1.000 g` : "Redeem physical gold"}</Button>
          </div>
        </Card>

        <Card pad={20}>
          <Eyebrow>The peg</Eyebrow>
          <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginTop: 14 }}>
            <span style={{ fontSize: 13, color: "var(--ink-2)" }}>1 BUX</span>
            <span className="mono" style={{ fontSize: 14, fontWeight: 600, color: "var(--action)" }}>{window.PEG_MG_PER_BUX.toFixed(4)} mg Au</span>
          </div>
          <div style={{ height: 1, background: "var(--line)", margin: "14px 0" }} />
          <BackingBar />
          <div style={{ fontSize: 11.5, color: "var(--ink-3)", marginTop: 12, lineHeight: 1.45 }}>Reserve figures are what the vault signers recorded on-chain (3-of-5). The node does not audit physical gold.</div>
        </Card>
      </div>

      {showW && <WithdrawModal balance={balance} onClose={() => setShowW(false)} onConfirm={handleWithdrawConfirm} />}
      {showR && <ReceiveModal address={backendState.primaryWallet?.address || "No address loaded"} onClose={() => setShowR(false)} />}
    </div>
  );
}

Object.assign(window, { Wallet });


// --- app.jsx ---
/* ============================================================
   BUCKS — app shell, live simulation, routing, tweaks
   ============================================================ */

const TWEAK_DEFAULTS = {
  "accent": "#f7931a",
  "ambientMotion": true
};

const NAV = [
  { id: "dashboard", label: "Dashboard", icon: Icons.gauge },
  { id: "configure", label: "Rig & power", icon: Icons.sliders },
  { id: "wallet", label: "Wallet", icon: Icons.wallet },
];

function Sidebar({ route, go, user }) {
  const { state: backendState, logout } = useData();
  const balance = backendState.primaryWallet ? backendState.primaryWallet.balance / 100000000 : 0;

  return (
    <aside style={{ width: 244, flexShrink: 0, borderRight: "1px solid var(--line)", background: "var(--bg-grad-2)", display: "flex", flexDirection: "column", padding: "22px 16px" }}>
      <div style={{ display: "flex", alignItems: "center", gap: 11, padding: "4px 8px 22px" }}>
        <BucksMark size={30} glow />
        <div>
          <div style={{ fontFamily: "var(--font-display)", fontWeight: 700, fontSize: 17, lineHeight: 1, letterSpacing: "-.01em" }}>Bucks</div>
          <div style={{ fontSize: 10.5, color: "var(--ink-3)", letterSpacing: ".1em", textTransform: "uppercase", marginTop: 3 }}>Mining Console</div>
        </div>
      </div>

      <nav style={{ display: "grid", gap: 4, marginTop: 6 }}>
        {NAV.map((n) => {
          const active = route === n.id;
          return (
            <button key={n.id} onClick={() => go(n.id)}
              style={{ display: "flex", alignItems: "center", gap: 12, padding: "11px 12px", borderRadius: 10, border: "none",
                background: active ? "var(--surface-3)" : "transparent", color: active ? "var(--ink)" : "var(--ink-3)",
                font: "600 14px var(--font-ui)", textAlign: "left", transition: "all .15s", position: "relative" }}
              onMouseEnter={(e) => { if (!active) e.currentTarget.style.color = "var(--ink-2)"; }}
              onMouseLeave={(e) => { if (!active) e.currentTarget.style.color = "var(--ink-3)"; }}>
              {active && <span style={{ position: "absolute", left: -16, top: "50%", transform: "translateY(-50%)", width: 3, height: 18, borderRadius: 99, background: "var(--accent)" }} />}
              <n.icon size={18} style={{ color: active ? "var(--accent)" : "inherit" }} />
              {n.label}
            </button>
          );
        })}
      </nav>

      <div style={{ marginTop: 22, padding: "16px", borderRadius: "var(--r-md)", background: "var(--surface-1)", border: "1px solid var(--line)" }}>
        <div style={{ display: "flex", alignItems: "center", gap: 8, marginBottom: 10 }}>
          <LiveDot color={backendState.mining.active ? "var(--pos)" : "var(--neg)"} />
          <span style={{ fontSize: 12, fontWeight: 600 }}>
            {backendState.mining.active ? "Rig mining" : "Rig offline"}
          </span>
        </div>
        <BackingBar />
      </div>

      <div style={{ marginTop: "auto", display: "flex", alignItems: "center", gap: 11, padding: "10px 8px" }}>
        <div style={{ width: 34, height: 34, borderRadius: 99, background: "linear-gradient(135deg, var(--accent), var(--action))", display: "grid", placeItems: "center", color: "#1a1206", fontWeight: 700, fontFamily: "var(--font-display)", fontSize: 14, flexShrink: 0 }}>
          {(user.name || "M").split(" ").map((s) => s[0]).slice(0, 2).join("")}
        </div>
        <div style={{ flex: 1, minWidth: 0 }}>
          <div style={{ fontSize: 13, fontWeight: 600, whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis" }}>{user.name || "Miner"}</div>
          <div style={{ fontSize: 11, color: "var(--ink-3)", whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis" }} className="mono">{user.email || "session · 12h"}</div>
        </div>
        <button onClick={logout} title="Log out of the node console" style={{ background: "none", border: "none", color: "var(--ink-3)", cursor: "pointer", padding: 6 }}>
          <Icons.logout size={17} />
        </button>
      </div>
    </aside>
  );
}

function Topbar({ route, net, balance }) {
  const titles = { dashboard: "Dashboard", configure: "Rig & power", wallet: "Wallet" };
  const subs = {
    dashboard: "Live mining overview",
    configure: "Tune your rig, pool and payouts",
    wallet: "Balance, gold and withdrawals",
  };
  return (
    <header style={{ display: "flex", justifyContent: "space-between", alignItems: "center", padding: "18px 28px", borderBottom: "1px solid var(--line)", background: "color-mix(in oklch, var(--bg) 80%, transparent)", backdropFilter: "blur(10px)", position: "sticky", top: 0, zIndex: 20 }}>
      <div>
        <h1 style={{ font: "600 21px var(--font-display)", margin: 0, letterSpacing: "-.01em" }}>{titles[route]}</h1>
        <div style={{ fontSize: 12.5, color: "var(--ink-3)", marginTop: 3 }}>{subs[route]}</div>
      </div>
      <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
        <div style={{ display: "flex", alignItems: "center", gap: 8, padding: "8px 14px", borderRadius: 99, background: "var(--surface-1)", border: "1px solid var(--line)" }}>
          <Icons.vault size={14} style={{ color: "var(--action)" }} />
          <span className="mono" style={{ fontSize: 12.5, fontWeight: 600, color: "var(--action)" }}>{fmt.usd(window.GOLD_USD_PER_G)}</span>
          <span style={{ fontSize: 11, color: "var(--ink-3)" }}>/g spot</span>
        </div>
        <div style={{ display: "flex", alignItems: "center", gap: 8, padding: "8px 14px", borderRadius: 99, background: "var(--surface-1)", border: "1px solid var(--line)" }}>
          <BucksMark size={16} />
          <span className="mono tnum" style={{ fontSize: 13, fontWeight: 600 }}>{fmt.bux(balance, 4)}</span>
          <span style={{ fontSize: 11, color: "var(--ink-3)" }}>BUX</span>
        </div>
      </div>
    </header>
  );
}

function MainAppShell() {
  const { state: backendState, auth } = useData();
  const [t, setTweak] = useTweaks(TWEAK_DEFAULTS);
  const [phase, setPhase] = React.useState("onboarding"); // onboarding | setup | app
  const [route, setRoute] = React.useState("dashboard");
  const [pulse, setPulse] = React.useState(0);

  // Load persistent user profile
  const [user, setUser] = React.useState(() => {
    const local = localStorage.getItem('bucks_user');
    return local ? JSON.parse(local) : { name: "", email: "", balance: 0, pending: 0, lifetime: 0 };
  });

  // Load persistent rig configuration
  const [config, setConfig] = React.useState(() => {
    const local = localStorage.getItem('bucks_rig_config');
    return local ? JSON.parse(local) : { rigId: "s21pro", power: 100, poolId: "aurum", payout: "epoch", wallet: "" };
  });

  // Compute stats based on configuration
  const rig = RIGS.find((r) => r.id === config.rigId) || RIGS[0];
  const pool = POOLS.find((p) => p.id === config.poolId) || POOLS[0];
  const out = deriveOutput(rig, config.power);

  // Track initial balance to compute session earnings
  const [initialBalance, setInitialBalance] = React.useState(null);
  React.useEffect(() => {
    if (backendState.primaryWallet && initialBalance === null) {
      setInitialBalance(backendState.primaryWallet.balance / 100000000);
    }
  }, [backendState.primaryWallet, initialBalance]);

  const sessionEarnings = backendState.primaryWallet 
    ? Math.max(0, (backendState.primaryWallet.balance / 100000000) - (initialBalance || 0)) 
    : 0;

  // Sync brand variables
  React.useEffect(() => { document.documentElement.style.setProperty("--accent", t.accent); }, [t.accent]);
  React.useEffect(() => { document.body.dataset.motion = t.ambientMotion ? "on" : "off"; }, [t.ambientMotion]);

  // Telemetry loop values
  const seedHash = () => Array.from({ length: 48 }, () => 0);
  const [live, setLive] = React.useState(() => ({
    hashSeries: seedHash(), 
    earnSeries: Array.from({ length: 24 }, () => 0),
    todayBux: 0, temp: 45, fan: 15, watts: 45, accepted: 0, rejected: 0,
    uptime: "0h 0m", rig, pool,
    feed: []
  }));

  // Auto-bypass onboarding if wallet already exists on the C++ backend
  React.useEffect(() => {
    if (backendState.primaryWallet && phase === "onboarding") {
      setPhase("app");
    }
  }, [backendState.primaryWallet, phase]);

  const ref = React.useRef({ out, rig, pool, backendState });
  ref.current = { out, rig, pool, backendState };

  // Live telemetry loop
  React.useEffect(() => {
    let tick = 0;
    let upMinutes = 0;
    const iv = setInterval(() => {
      tick++;
      const { out, rig, pool, backendState } = ref.current;
      const active = backendState.mining.active;

      if (active) {
        upMinutes += 2; // Simulate uptime fast-forward
      }

      setLive((p) => {
        // Target values based on miner state
        let targetTemp = active ? rig.temp + (Math.random() - 0.5) * 2 : 44 + Math.random() * 0.5;
        let targetFan = active ? rig.fan + (Math.random() - 0.5) * 1.5 : 15 + Math.random() * 0.2;
        let targetWatts = active ? out.watts : 45; // 45W stand-by
        let targetTh = active ? out.th : 0;

        const jitter = active ? (Math.random() - 0.5) * out.th * 0.04 : 0;
        const h = Math.max(0, targetTh + jitter);

        // Core temp damping
        const nextTemp = p.temp + (targetTemp - p.temp) * 0.08;
        // Fan damping
        const nextFan = p.fan + (targetFan - p.fan) * 0.08;
        // Draw damping
        const nextWatts = p.watts + (targetWatts - p.watts) * 0.15;

        // Uptime string
        const hVal = Math.floor(upMinutes / 60);
        const mVal = upMinutes % 60;
        const uptimeStr = `${hVal}h ${mVal}m`;

        return {
          ...p,
          hashSeries: [...p.hashSeries.slice(-47), h],
          earnSeries: [...p.earnSeries.slice(-23), sessionEarnings],
          todayBux: sessionEarnings,
          temp: nextTemp,
          fan: nextFan,
          watts: nextWatts,
          accepted: backendState.mining.blocksFound * 12 + Math.floor(tick / 5), // mock shares w/ real block anchors
          rejected: Math.floor(tick / 180),
          uptime: active ? uptimeStr : "0h 0m",
          rig, pool,
        };
      });

      if (tick % 4 === 0) { 
        setPulse(1); 
        setTimeout(() => setPulse(0), 600); 
      }
    }, 1300);
    return () => clearInterval(iv);
  }, [sessionEarnings]);

  // Push new rewards to feed whenever blocks are mined on backend
  const prevBlocksFound = React.useRef(0);
  React.useEffect(() => {
    if (backendState.mining.blocksFound > prevBlocksFound.current) {
      const addedBlocks = backendState.mining.blocksFound - prevBlocksFound.current;
      setLive((p) => {
        const newFeedItems = [];
        for (let i = 0; i < addedBlocks; i++) {
          newFeedItems.push({
            key: "block-" + Date.now() + "-" + i,
            time: "just now",
            label: "Block Mined (Consensus Reward)",
            bux: 50.00,
            mg: 50.00 * window.PEG_MG_PER_BUX
          });
        }
        return {
          ...p,
          feed: [...newFeedItems, ...p.feed].slice(0, 10)
        };
      });
      prevBlocksFound.current = backendState.mining.blocksFound;
    }
  }, [backendState.mining.blocksFound]);

  const go = (r) => { setRoute(r); window.scrollTo?.({ top: 0 }); };

  // Node access: every wallet/mining route needs the session cookie
  if (!auth.checked) {
    return <div style={{ height: "100%", display: "grid", placeItems: "center", color: "var(--ink-3)", fontSize: 14 }}>Connecting to the node…</div>;
  }
  if (!auth.authenticated) {
    return (
      <>
        <LoginScreen />
        <BucksTweaks t={t} setTweak={setTweak} />
      </>
    );
  }

  // Onboarding Phase
  if (phase === "onboarding") {
    return (
      <>
        <Onboarding user={user} setUser={setUser} onDone={() => setPhase("setup")} />
        <BucksTweaks t={t} setTweak={setTweak} />
      </>
    );
  }

  // First-rig Setup Phase
  if (phase === "setup") {
    return (
      <div style={{ height: "100%", display: "flex", flexDirection: "column" }}>
        <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", padding: "20px 32px", borderBottom: "1px solid var(--line)" }}>
          <div style={{ display: "flex", alignItems: "center", gap: 11 }}>
            <BucksMark size={28} glow />
            <span style={{ fontFamily: "var(--font-display)", fontWeight: 700, fontSize: 17 }}>Bucks</span>
            <Chip tone="neutral" icon={Icons.cpu}>First-rig setup</Chip>
          </div>
          <Button variant="quiet" size="sm" onClick={() => setPhase("app")}>Skip to dashboard</Button>
        </div>
        <div style={{ flex: 1, overflow: "auto", padding: "32px 28px 60px" }}>
          <div style={{ maxWidth: 1180, margin: "0 auto 26px" }}>
            <Eyebrow color="var(--accent)">Set up mining</Eyebrow>
            <h1 style={{ font: "600 30px/1.1 var(--font-display)", letterSpacing: "-.02em", margin: "10px 0 6px" }}>Configure your first rig</h1>
            <p style={{ color: "var(--ink-2)", fontSize: 15, margin: 0, maxWidth: 560 }}>Pick hardware, point it at a pool, and dial in power. We'll project your daily BUX and the gold it mints before you launch.</p>
          </div>
          <Configure config={config} setConfig={setConfig} onLaunch={() => setPhase("app")} initial />
        </div>
        <BucksTweaks t={t} setTweak={setTweak} />
      </div>
    );
  }

  // Main App Console Phase
  const netInfo = {
    totalBux: backendState.economy.supply / 100000000,
    networkHash: 184.6,
    difficulty: backendState.difficulty,
    blockHeight: backendState.height,
    goldPrice: window.GOLD_USD_PER_G
  };

  const balance = backendState.primaryWallet ? backendState.primaryWallet.balance / 100000000 : 0;

  return (
    <div style={{ display: "flex", height: "100%", overflow: "hidden" }}>
      <Sidebar route={route} go={go} user={user} />
      <div style={{ flex: 1, display: "flex", flexDirection: "column", overflow: "hidden" }}>
        <Topbar route={route} net={netInfo} balance={balance} />
        <main style={{ flex: 1, overflow: "auto", padding: "26px 28px 56px" }}>
          <div key={route} style={{ animation: "rise .35s ease forwards" }}>
            {route === "dashboard" && <Dashboard live={live} net={netInfo} user={user} pulse={pulse} go={go} />}
            {route === "configure" && (
              <Configure config={config} setConfig={setConfig} onLaunch={() => go("dashboard")} />
            )}
            {route === "wallet" && <Wallet user={user} live={live} />}
          </div>
        </main>
      </div>
      <BucksTweaks t={t} setTweak={setTweak} />
    </div>
  );
}

function BucksTweaks({ t, setTweak }) {
  return (
    <TweaksPanel title="Tweaks">
      <TweakSection label="Brand accent" />
      <TweakColor label="Accent" value={t.accent}
        options={["#f7931a", "#e8b04b", "#3a86ff", "#33c98f", "#b06bff"]}
        onChange={(v) => setTweak("accent", v)} />
      <TweakSection label="Motion" />
      <TweakToggle label="Ambient motion" value={t.ambientMotion} onChange={(v) => setTweak("ambientMotion", v)} />
    </TweaksPanel>
  );
}

function App() {
  return (
    <DataProvider>
      <MainAppShell />
    </DataProvider>
  );
}

const rootEl = document.getElementById("root");
ReactDOM.createRoot(rootEl).render(<App />);


