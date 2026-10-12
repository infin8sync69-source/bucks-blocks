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
