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
          Proof-of-work with<br /><span style={{ color: "var(--gold)" }}>a gold reserve.</span>
        </h1>
        <p style={{ fontSize: 16, lineHeight: 1.55, color: "var(--ink-2)", marginTop: 18, maxWidth: 420 }}>
          Mine BUX and watch the reserve the vault signers record on-chain. The figures here are what they reported; this node does not audit physical gold.
        </p>
        <div style={{ display: "flex", gap: 22, marginTop: 30 }}>
          {[[rs.perBuxMg === null ? "—" : `${rs.perBuxMg.toFixed(4)} mg`, "reported reserve / BUX"],
            [coverageLabel(rs.coverage), `of the ${TARGET_PEG_MG_PER_BUX} mg peg`],
            [fmt.bux(rs.supplyBux), "BUX circulating"]].map(([a, b]) => (
            <div key={b}>
              <div className="mono" style={{ fontSize: 21, fontWeight: 600, color: "var(--gold)" }}>{a}</div>
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
