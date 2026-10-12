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
            <div style={{ width: 30, height: 30, borderRadius: 8, background: "var(--surface-3)", display: "grid", placeItems: "center", color: "var(--gold)", flexShrink: 0 }}><Icons.coin size={15} /></div>
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
        <Kpi label="≈ Gold (reported reserve)" value={balGold.toFixed(3)} unit="g Au" accent="var(--gold)" icon={Icons.vault} />
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
              <span className="mono" style={{ fontWeight: 600, color: "var(--gold)" }}>{fmt.usd(GOLD_USD_PER_G)}/g</span>
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
