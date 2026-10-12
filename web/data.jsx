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
