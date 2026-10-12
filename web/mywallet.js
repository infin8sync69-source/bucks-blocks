const API = '/api';
const POLL_MS = 5000;

let state = {
  wallets: [],
  activeIndex: 0,
  address: '',
  publicKey: '',
  balance: 0,
  goldRate: 0,
  tempMnemonic: '',   // only while the creation screens are showing
  transactions: [],
  mining: { active: false, hashRate: 0, blocksFound: 0, minerAddress: '' },
  authenticated: false,
  cookieFile: ''
};

// ── Bootstrap ──

document.addEventListener('DOMContentLoaded', async () => {
  bindEvents();
  if (await checkAuth()) {
    await enterWallet();
  } else {
    showScreen('screen-login');
  }
});

// After login: existing wallet -> wallet screen, otherwise onboarding
async function enterWallet() {
  const hasWallet = await checkExistingWallet();
  if (hasWallet) {
    showScreen('screen-wallet');
    refreshAll();
    startPolling();
  } else {
    showScreen('screen-onboarding');
  }
}

async function checkExistingWallet() {
  try {
    const res = await api('/wallets');
    const data = await res.json();
    if (data.wallets && data.wallets.length > 0) {
      state.address = data.wallets[0].address;
      state.balance = data.wallets[0].balance;
      await loadPrimaryDetails();
      return true;
    }
  } catch (e) { console.warn('No existing wallet:', e); }
  return false;
}

async function loadPrimaryDetails() {
  try {
    const res = await api('/wallets/primary');
    const data = await res.json();
    if (data.success) {
      state.address = data.address;
      state.publicKey = data.publicKey || '';
      state.balance = data.balance;
    }
  } catch (e) { console.warn('Primary details error:', e); }
}

// ── API helper ──
// Sends the HttpOnly session cookie (same-origin only) and the X-Bucks-CSRF
// header the node requires on cookie-authenticated state changes. A 401
// means the session is gone: back to the login screen. No token or secret
// is ever written to localStorage/sessionStorage.

function api(path, opts = {}) {
  const method = (opts.method || 'GET').toUpperCase();
  const headers = Object.assign({}, opts.headers || {});
  if (method !== 'GET' && method !== 'HEAD') headers['X-Bucks-CSRF'] = '1';
  if (opts.body !== undefined && !headers['Content-Type']) headers['Content-Type'] = 'application/json';
  return fetch(API + path, Object.assign({}, opts, { method, headers, credentials: 'same-origin' })).then((res) => {
    if (res.status === 401 && state.authenticated) {
      state.authenticated = false;
      stopPolling();
      showScreen('screen-login');
      showToast('Session expired', 'Please enter the API token again', 'warning');
    }
    return res;
  });
}

// ── Auth ──

async function checkAuth() {
  try {
    const res = await api('/auth/status');
    const data = await res.json();
    state.authenticated = !!data.authenticated;
    state.cookieFile = data.cookieFile || '';
    const pathEl = document.getElementById('login-cookie-path');
    if (pathEl && state.cookieFile) pathEl.textContent = state.cookieFile;
    if (data.walletsEnabled === false) {
      showToast('Wallet disabled', data.walletError || 'The wallet file could not be read', 'error');
    }
    return state.authenticated;
  } catch (e) {
    showToast('Error', 'Node not reachable', 'error');
    return false;
  }
}

async function login() {
  const input = document.getElementById('input-token');
  const token = input.value.trim();
  const errEl = document.getElementById('login-error');
  if (!token) return;
  errEl.textContent = '';
  try {
    const res = await api('/auth/login', { method: 'POST', body: JSON.stringify({ token }) });
    input.value = '';
    if (res.ok) {
      state.authenticated = true;
      await checkAuth();
      await enterWallet();
    } else {
      errEl.textContent = 'That token was not accepted.';
    }
  } catch (e) {
    errEl.textContent = 'Node not reachable.';
  }
}

async function logout() {
  try { await api('/auth/logout', { method: 'POST' }); } catch (e) { /* session may already be gone */ }
  state.authenticated = false;
  stopPolling();
  showScreen('screen-login');
}

// ── Polling ──

let pollTimer = null;
function startPolling() {
  if (pollTimer) return;
  pollTimer = setInterval(refreshAll, POLL_MS);
}

function stopPolling() {
  if (pollTimer) clearInterval(pollTimer);
  pollTimer = null;
}

async function refreshAll() {
  await Promise.all([refreshBalance(), refreshMining(), refreshTransactions()]);
  updateWalletUI();
}

async function refreshBalance() {
  try {
    const [walletRes, ecoRes] = await Promise.all([
      api('/wallets/primary'),
      api('/economy/status')
    ]);
    const wd = await walletRes.json();
    const ed = await ecoRes.json();
    if (wd.success) {
      state.balance = wd.balance;
      state.address = wd.address;
      state.publicKey = wd.publicKey || state.publicKey;
      if (wd.transactions) state.transactions = wd.transactions;
    }
    if (ed.success) {
      // Reported reserve per BUX (grams); 0 until the vault signers record one
      state.goldRate = ed.price_per_buck || 0;
    }
  } catch (e) { console.warn('Balance refresh error:', e); }
}

async function refreshMining() {
  try {
    const res = await api('/mining/status');
    const data = await res.json();
    state.mining = {
      active: data.mining,
      hashRate: data.hashRate || 0,
      blocksFound: data.blocksFound || 0,
      minerAddress: data.minerAddress || ''
    };
  } catch (e) { console.warn('Mining refresh error:', e); }
}

async function refreshTransactions() {
  // Transactions come from /wallets/primary response
}

// ── UI Updates ──

function updateWalletUI() {
  const bucks = state.balance / 100000000;

  const balEl = document.getElementById('wallet-balance');
  if (balEl) balEl.textContent = formatBucks(bucks);

  const goldEl = document.getElementById('wallet-gold');
  if (goldEl) {
    const goldVal = bucks * state.goldRate;
    goldEl.textContent = (goldVal >= 1
      ? `≈ ${goldVal.toFixed(3)} g gold`
      : `≈ ${(goldVal * 1000).toFixed(3)} mg gold`) + ' (reported reserve)';
  }

  const addrShort = document.getElementById('addr-short');
  if (addrShort && state.address) {
    addrShort.textContent = state.address.slice(0, 8) + '...' + state.address.slice(-6);
  }

  const receiveAddr = document.getElementById('receive-address');
  if (receiveAddr) receiveAddr.textContent = state.address;

  // Network status
  const dot = document.getElementById('network-dot');
  const label = document.getElementById('network-label');
  if (dot && label) {
    dot.classList.remove('offline');
    label.textContent = 'Connected';
  }

  // Settings
  const sa = document.getElementById('settings-address');
  if (sa) sa.textContent = state.address;
  const sp = document.getElementById('settings-pubkey');
  if (sp) sp.textContent = state.publicKey;

  // Mining in settings
  const ms = document.getElementById('settings-mining-status');
  if (ms) {
    ms.textContent = state.mining.active ? 'Active' : 'Stopped';
    ms.style.color = state.mining.active ? 'var(--success)' : 'var(--danger)';
  }
  const hr = document.getElementById('settings-hash-rate');
  if (hr) {
    const h = state.mining.hashRate;
    hr.textContent = h > 1000 ? `${(h / 1000).toFixed(1)} KH/s` : `${h} H/s`;
  }
  const bf = document.getElementById('settings-blocks-found');
  if (bf) bf.textContent = state.mining.blocksFound;
  const ma = document.getElementById('settings-miner-addr');
  if (ma) ma.textContent = state.mining.minerAddress || '--';

  // Send form: populate wallet selector
  const sendFrom = document.getElementById('send-from');
  if (sendFrom && sendFrom.options.length === 0 && state.address) {
    const opt = document.createElement('option');
    opt.value = state.address;
    opt.textContent = state.address.slice(0, 12) + '... (' + formatBucks(bucks) + ' BUCKS)';
    sendFrom.appendChild(opt);
  }

  // Gold equiv on send
  const sendAmt = document.getElementById('send-amount');
  const sendGold = document.getElementById('send-gold-equiv');
  if (sendAmt && sendGold) {
    const amt = parseFloat(sendAmt.value) || 0;
    const gv = amt * state.goldRate;
    sendGold.textContent = amt > 0
      ? `≈ ${gv >= 1 ? gv.toFixed(3) + ' g' : (gv * 1000).toFixed(3) + ' mg'} gold (reported reserve)`
      : '';
  }

  renderTransactions();
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

function renderTransactions() {
  const recentEl = document.getElementById('recent-tx-list');
  const fullEl = document.getElementById('full-tx-list');
  if (!state.transactions.length) return;

  const sorted = [...state.transactions].sort((a, b) => b.blockHeight - a.blockHeight);

  const renderTx = (tx) => {
    const isMining = tx.type === 'mining_reward';
    const bucks = tx.amount / 100000000;
    return `
      <div class="tx-item">
        <div class="tx-icon ${isMining ? 'receive' : escapeHtml(tx.type)}">
          ${isMining ? '&#9935;' : tx.type === 'receive' ? '&#8595;' : '&#8593;'}
        </div>
        <div class="tx-details">
          <div class="tx-type">${isMining ? 'Mining Reward' : tx.type === 'receive' ? 'Received' : 'Sent'}</div>
          <div class="tx-address">${escapeHtml(String(tx.hash).slice(0, 16))}...</div>
        </div>
        <div class="tx-amount">
          <div class="tx-amount-value ${isMining ? 'receive' : escapeHtml(tx.type)}">+${formatBucks(bucks)}</div>
          <div class="tx-status">Block #${Number(tx.blockHeight)}</div>
        </div>
      </div>`;
  };

  if (recentEl) recentEl.innerHTML = sorted.slice(0, 5).map(renderTx).join('');
  if (fullEl) fullEl.innerHTML = sorted.map(renderTx).join('');
}

function formatBucks(val) {
  return val.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 8 });
}

// ── Screen / View Navigation ──

function showScreen(id) {
  document.querySelectorAll('.onboarding-screen, .password-screen, .seedphrase-screen, .wallet-screen').forEach(el => {
    el.classList.remove('active');
  });
  const el = document.getElementById(id);
  if (el) el.classList.add('active');
  if (id === 'screen-login') {
    const input = document.getElementById('input-token');
    if (input) input.focus();
  }
}

function showView(id) {
  document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
  const v = document.getElementById(id);
  if (v) v.classList.add('active');
  updateNavActive(id);
}

function updateNavActive(viewId) {
  const map = { 'view-home': 'nav-home', 'view-send': 'nav-send', 'view-receive': 'nav-receive', 'view-settings': 'nav-settings', 'view-history': 'nav-home' };
  document.querySelectorAll('[id^="nav-"]').forEach(btn => {
    btn.style.color = 'var(--text-tertiary)';
    btn.style.fontWeight = '500';
  });
  const active = document.getElementById(map[viewId] || 'nav-home');
  if (active) {
    active.style.color = 'var(--primary)';
    active.style.fontWeight = '600';
  }
}

// ── Toast ──

function showToast(title, message, type = 'success') {
  const container = document.getElementById('toast-container');
  const icons = { success: '&#10003;', error: '&#10007;', warning: '&#9888;' };
  const toast = document.createElement('div');
  toast.className = `toast ${type}`;
  toast.innerHTML = `
    <div class="toast-icon">${icons[type]}</div>
    <div class="toast-content">
      <div class="toast-title">${escapeHtml(title)}</div>
      <div class="toast-message">${escapeHtml(message)}</div>
    </div>`;
  container.appendChild(toast);
  setTimeout(() => toast.remove(), 4000);
}

// ── Event Bindings ──

function bindEvents() {
  // Login
  const tokenInput = document.getElementById('input-token');
  tokenInput.oninput = () => { document.getElementById('btn-login').disabled = !tokenInput.value.trim(); };
  tokenInput.onkeydown = (e) => { if (e.key === 'Enter' && tokenInput.value.trim()) login(); };
  document.getElementById('toggle-token').onclick = () => {
    tokenInput.type = tokenInput.type === 'password' ? 'text' : 'password';
  };
  document.getElementById('btn-login').onclick = login;

  // Onboarding
  document.getElementById('btn-create').onclick = createWalletNow;
  document.getElementById('btn-restore-show').onclick = () => showScreen('screen-restore');

  // Seed phrase screen (no "back": the wallet already exists on the node)
  document.getElementById('reveal-overlay').onclick = revealSeedPhrase;
  document.getElementById('btn-seed-continue').onclick = () => showScreen('screen-confirm-seed');

  // Confirm seed
  document.getElementById('btn-back-confirm').onclick = () => showScreen('screen-seedphrase');
  document.getElementById('btn-confirm-finish').onclick = finishWalletCreation;

  // Restore
  document.getElementById('btn-back-restore').onclick = () => showScreen('screen-onboarding');
  document.getElementById('btn-restore-confirm').onclick = restoreWallet;

  // Wallet views
  document.getElementById('btn-go-send').onclick = () => showView('view-send');
  document.getElementById('btn-go-receive').onclick = () => showView('view-receive');
  document.getElementById('btn-go-history').onclick = () => showView('view-history');
  document.getElementById('link-all-tx').onclick = () => showView('view-history');
  document.getElementById('btn-settings').onclick = () => showView('view-settings');

  // Back buttons
  document.getElementById('btn-back-send').onclick = () => showView('view-home');
  document.getElementById('btn-back-receive').onclick = () => showView('view-home');
  document.getElementById('btn-back-history').onclick = () => showView('view-home');
  document.getElementById('btn-back-settings').onclick = () => showView('view-home');

  // Nav
  document.getElementById('nav-home').onclick = () => showView('view-home');
  document.getElementById('nav-send').onclick = () => showView('view-send');
  document.getElementById('nav-receive').onclick = () => showView('view-receive');
  document.getElementById('nav-settings').onclick = () => showView('view-settings');

  // Actions
  document.getElementById('btn-copy-addr').onclick = copyAddress;
  document.getElementById('btn-copy-receive').onclick = copyAddress;
  document.getElementById('btn-send-confirm').onclick = sendTransaction;
  document.getElementById('btn-refresh-session').onclick = () => location.reload();
  document.getElementById('btn-logout').onclick = logout;
  document.getElementById('send-amount').oninput = () => updateWalletUI();
}

// ── Wallet Creation Flow ──
// POST /wallets/create makes the wallet on the node and returns the phrase
// exactly once; it is rendered, confirmed, then dropped from memory.

async function createWalletNow() {
  const btn = document.getElementById('btn-create');
  btn.disabled = true;
  try {
    const res = await api('/wallets/create', { method: 'POST', body: JSON.stringify({}) });
    const data = await res.json();
    if (res.ok && data.success && data.mnemonic) {
      state.tempMnemonic = data.mnemonic;
      state.address = data.wallet.address;
      state.publicKey = data.wallet.publicKey || '';
      if (data.persisted === false) showToast('Warning', data.error || 'Wallet was not saved to disk', 'error');
      renderSeedPhrase(data.mnemonic, data.direction || 'ltr');
      showScreen('screen-seedphrase');
    } else {
      showToast('Error', data.error || 'Failed to create wallet', 'error');
    }
  } catch (e) {
    showToast('Error', 'Server connection failed', 'error');
  } finally {
    btn.disabled = false;
  }
}

function renderSeedPhrase(mnemonic, direction) {
  const words = mnemonic.trim().split(/\s+/);
  const grid = document.getElementById('seedphrase-grid');
  const rtl = direction === 'rtl';
  grid.dir = rtl ? 'rtl' : 'ltr';
  grid.lang = rtl ? 'ar' : '';
  grid.style.textAlign = rtl ? 'right' : 'left';
  grid.innerHTML = words.map((w, i) =>
    `<div class="seedphrase-word" style="${rtl ? 'font-family: inherit; font-size: 17px;' : ''}"><span style="opacity:0.5; font-size:11px; direction:ltr; unicode-bidi:isolate">${i + 1}.</span> <span style="unicode-bidi:isolate">${escapeHtml(w)}</span></div>`
  ).join('');
  const count = document.getElementById('seed-word-count');
  if (count) count.textContent = String(words.length);
  const container = document.getElementById('seedphrase-container');
  container.classList.add('blurred');
  document.getElementById('reveal-overlay').classList.remove('hidden');
  document.getElementById('btn-seed-continue').disabled = true;

  // Setup confirmation screen
  setupConfirmation(words, rtl);
}

function revealSeedPhrase() {
  const container = document.getElementById('seedphrase-container');
  const overlay = document.getElementById('reveal-overlay');
  container.classList.remove('blurred');
  overlay.classList.add('hidden');
  document.getElementById('btn-seed-continue').disabled = false;
}

function setupConfirmation(words, rtl) {
  const selectionEl = document.getElementById('confirm-selection');
  const poolEl = document.getElementById('word-pool');
  selectionEl.dir = rtl ? 'rtl' : 'ltr';
  poolEl.dir = rtl ? 'rtl' : 'ltr';
  const shuffled = [...words].sort(() => Math.random() - 0.5);
  const selected = [];

  function render() {
    selectionEl.innerHTML = selected.map((w, i) =>
      `<div class="selected-word" data-idx="${i}">${i + 1}. ${escapeHtml(w)}</div>`
    ).join('');

    poolEl.innerHTML = shuffled.map((w, i) => {
      const used = selected.includes(w) && selected.filter(s => s === w).length >= shuffled.filter(s => s === w).length;
      return `<div class="pool-word ${used ? 'used' : ''}" data-i="${i}">${escapeHtml(w)}</div>`;
    }).join('');

    poolEl.querySelectorAll('.pool-word:not(.used)').forEach(el => {
      el.onclick = () => {
        selected.push(shuffled[parseInt(el.dataset.i, 10)]);
        render();
      };
    });

    selectionEl.querySelectorAll('.selected-word').forEach(el => {
      el.onclick = () => {
        selected.splice(parseInt(el.dataset.idx, 10), 1);
        render();
      };
    });

    const btn = document.getElementById('btn-confirm-finish');
    btn.disabled = !(selected.length === words.length && selected.every((w, i) => w === words[i]));
  }

  render();
}

// The wallet was created in createWalletNow(); this only forgets the phrase.
function finishWalletCreation() {
  state.tempMnemonic = '';
  document.getElementById('seedphrase-grid').innerHTML = '';
  document.getElementById('confirm-selection').innerHTML = '';
  document.getElementById('word-pool').innerHTML = '';
  showScreen('screen-wallet');
  showToast('Wallet Created', 'Your wallet is ready to use');
  refreshAll();
  startPolling();
}

async function restoreWallet() {
  const input = document.getElementById('input-restore-mnemonic');
  const mnemonic = input.value.trim();
  if (!mnemonic) {
    showToast('Error', 'Please enter your recovery phrase', 'error');
    return;
  }
  try {
    const res = await api('/wallets/restore', {
      method: 'POST',
      body: JSON.stringify({ mnemonic, passphrase: '' })
    });
    const data = await res.json();
    if (res.ok && data.success) {
      input.value = '';
      state.address = data.wallet.address;
      state.publicKey = data.wallet.publicKey || '';
      showScreen('screen-wallet');
      showToast('Wallet Restored', data.existing ? 'That wallet was already on this node' : 'Welcome back!');
      refreshAll();
      startPolling();
    } else {
      showToast('Error', data.error || 'Restoration failed', 'error');
    }
  } catch (e) {
    showToast('Error', 'Server connection failed', 'error');
  }
}

// ── Send Transaction ──

async function sendTransaction() {
  const to = document.getElementById('send-to').value.trim();
  const amount = parseFloat(document.getElementById('send-amount').value);
  if (!to || isNaN(amount) || amount <= 0) {
    showToast('Invalid', 'Please enter a valid address and amount', 'warning');
    return;
  }
  try {
    const res = await api('/transactions/send', {
      method: 'POST',
      body: JSON.stringify({
        from: state.address,
        to,
        amount: Math.floor(amount * 100000000)
      })
    });
    const data = await res.json();
    if (res.ok && data.success) {
      showToast('Sent', 'Transaction broadcasted successfully');
      document.getElementById('send-to').value = '';
      document.getElementById('send-amount').value = '';
      showView('view-home');
      refreshAll();
    } else {
      showToast('Failed', data.error || 'Transaction rejected', 'error');
    }
  } catch (e) {
    showToast('Error', 'Network error', 'error');
  }
}

// ── Copy Address ──

function copyAddress() {
  if (!state.address) return;
  navigator.clipboard.writeText(state.address).then(() => {
    showToast('Copied', 'Address copied to clipboard');
  }).catch(() => {
    const textarea = document.createElement('textarea');
    textarea.value = state.address;
    document.body.appendChild(textarea);
    textarea.select();
    document.execCommand('copy');
    textarea.remove();
    showToast('Copied', 'Address copied to clipboard');
  });
}
