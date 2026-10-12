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
                    {p.vault && <Icons.vault size={15} style={{ color: "var(--gold)" }} />}
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
            <Icons.vault size={16} style={{ color: "var(--gold)" }} />
            <span style={{ fontSize: 13, fontWeight: 600 }}>Backing</span>
          </div>
          <BackingBar />
          <div style={{ fontSize: 11.5, color: "var(--ink-3)", marginTop: 10, lineHeight: 1.45 }}>Reported reserve per BUX: <span className="mono" style={{ color: "var(--gold)" }}>{window.PEG_MG_PER_BUX.toFixed(4)} mg</span> (as recorded on-chain by the vault signers).</div>
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
