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
          <stop offset="0" stopColor="#f6d889" /><stop offset="0.5" stopColor="var(--gold)" /><stop offset="1" stopColor="var(--gold-deep)" />
        </linearGradient>
        <linearGradient id="coinEdge" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="var(--gold-deep)" /><stop offset="1" stopColor="#7a560f" />
        </linearGradient>
        <linearGradient id="coinDim" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#2a2620" /><stop offset="1" stopColor="#1c1a16" />
        </linearGradient>
        <radialGradient id="vaultGlow" cx="0.5" cy="0.42" r="0.6">
          <stop offset="0" stopColor="var(--gold)" stopOpacity="0.16" /><stop offset="1" stopColor="var(--gold)" stopOpacity="0" />
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
              style={{ filter: justMinted ? "drop-shadow(0 0 12px var(--gold-glow))" : "none", transition: "filter .4s" }} />
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
            <Icons.vault size={16} style={{ color: "var(--gold)" }} />
            <Eyebrow color="var(--gold)">The Gold Vault</Eyebrow>
          </div>
          <div style={{ fontSize: 13, color: "var(--ink-2)", maxWidth: 320, lineHeight: 1.5 }}>
            Reserve as reported on-chain by the vault signers (3-of-5), set against the circulating supply. The node records these figures; it does not audit the metal.
          </div>

          <div style={{ marginTop: 22, display: "grid", gridTemplateColumns: "1fr 1fr", gap: "20px 28px" }}>
            <div>
              <Eyebrow>Reported reserve</Eyebrow>
              <div style={{ display: "flex", alignItems: "baseline", gap: 6, marginTop: 9 }}>
                <span className="mono tnum" style={{ fontSize: 34, fontWeight: 600, color: "var(--gold)", lineHeight: 1 }}>{grams.toFixed(2)}</span>
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
                <Icons.shield size={17} style={{ color: "var(--gold)" }} />
                <span style={{ fontSize: 13, color: "var(--ink-2)" }}>Reported reserve per BUX</span>
              </div>
              <span className="mono" style={{ fontSize: 14, fontWeight: 600, color: "var(--gold)" }}>
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
  const tone = rs.coverage === null ? "var(--ink-3)" : rs.coverage >= 1 ? "var(--gold)" : "var(--warn)";
  return (
    <div title={`Reported reserve ${rs.reserveG.toFixed(2)} g against ${TARGET_PEG_MG_PER_BUX} mg/BUX for ${fmt.bux(rs.supplyBux)} BUX`}>
      <div style={{ display: "flex", alignItems: "center", gap: 12 }}>
        <div style={{ flex: 1, height: 7, borderRadius: 99, background: "var(--surface-hi)", overflow: "hidden" }}>
          <div style={{ width: `${pct}%`, height: "100%", borderRadius: 99, transition: "width .6s ease",
            background: "linear-gradient(90deg, var(--gold-deep), var(--gold))",
            backgroundSize: "200% 100%", animation: pct > 0 ? "shimmer 3.5s linear infinite" : "none" }} />
        </div>
        <span className="mono" style={{ fontSize: 13, fontWeight: 600, color: tone }}>{coverageLabel(rs.coverage)}</span>
      </div>
      <div style={{ fontSize: 10.5, color: "var(--ink-3)", marginTop: 5 }}>reported reserve · {TARGET_PEG_MG_PER_BUX} mg/BUX peg</div>
    </div>
  );
}

Object.assign(window, { CoinStack, GoldVault, BackingBar });
