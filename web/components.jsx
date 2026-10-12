/* ============================================================
   BUCKS — shared UI components
   ============================================================ */

/* ---------- brand mark: the Bucks coin ---------- */
function BucksMark({ size = 28, glow = false }) {
  const id = React.useId();
  return (
    <svg width={size} height={size} viewBox="0 0 48 48" aria-label="Bucks"
         style={{ filter: glow ? "drop-shadow(0 0 10px var(--gold-glow))" : "none" }}>
      <defs>
        <linearGradient id={`g${id}`} x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="var(--gold)" />
          <stop offset="1" stopColor="var(--gold-deep)" />
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
    gold:    { background: "linear-gradient(180deg, var(--gold), var(--gold-deep))", color: "#1c1404", borderColor: "var(--gold-deep)", boxShadow: "0 6px 20px -8px var(--gold-glow)" },
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
    gold:    { bg: "rgba(232,176,75,.12)", fg: "var(--gold)", bd: "rgba(232,176,75,.32)" },
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
  BucksMark, Button, Card, Eyebrow, Stat, Chip, LiveDot, Field, Segmented, Slider, Sparkline, Ring,
});
