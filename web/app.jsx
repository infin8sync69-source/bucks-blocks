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
        <div style={{ width: 34, height: 34, borderRadius: 99, background: "linear-gradient(135deg, var(--accent), var(--gold-deep))", display: "grid", placeItems: "center", color: "#1a1206", fontWeight: 700, fontFamily: "var(--font-display)", fontSize: 14, flexShrink: 0 }}>
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
          <Icons.vault size={14} style={{ color: "var(--gold)" }} />
          <span className="mono" style={{ fontSize: 12.5, fontWeight: 600, color: "var(--gold)" }}>{fmt.usd(window.GOLD_USD_PER_G)}</span>
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
