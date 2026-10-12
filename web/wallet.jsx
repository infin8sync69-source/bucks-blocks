/* ============================================================
   BUCKS — Wallet & withdraw
   ============================================================ */

function txIcon(type) {
  if (type === "withdraw" || type === "send") return { Ic: Icons.arrowUp, col: "var(--ink-2)", bg: "var(--surface-3)" };
  if (type === "mint") return { Ic: Icons.vault, col: "var(--gold)", bg: "rgba(232,176,75,.12)" };
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
              <Icons.vault size={16} style={{ color: "var(--gold)" }} /><Eyebrow color="var(--gold)">Your gold</Eyebrow>
            </div>
            <div style={{ display: "flex", alignItems: "baseline", gap: 7, marginTop: 12 }}>
              <span className="mono tnum" style={{ fontSize: 32, fontWeight: 600, color: "var(--gold)", lineHeight: 1 }}>{gold.toFixed(4)}</span>
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
            <span className="mono" style={{ fontSize: 14, fontWeight: 600, color: "var(--gold)" }}>{window.PEG_MG_PER_BUX.toFixed(4)} mg Au</span>
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
